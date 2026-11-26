// Board: NodeMCU 1.0 (ESP-12E Module), ESP8266 core 3.1.2.
// K1/P0 = UV, K2/P1 = warm light, P15 = active-HIGH fan relay.
// All four DRV8833 inputs remain LOW; pump is always disabled.
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include "SimpleControl.h"
#include "MenuInput.h"
#include "FastDisplay.h"
#include "SoilSensor.h"
#include "SoilAdc.h"
#include "SoilCalibration.h"
#include "CalibrationDefaults.h"
#include "TemperatureSensor.h"
#include "TemperatureHtu.h"

FastDisplay display(D8,D3,D4);
SimpleControl outputs;
MenuSelection selection;
TouchButtons buttons;
RelayTransition relayTransition;
SoilReading soil;
SoilAdc soilAdc;
SoilCalibration soilCalibration;
TemperatureReading temperature;
TemperatureHtu temperatureHtu;
// Keep the first part of the old full controller's 1024-byte settings intact.
const unsigned CALIBRATION_OFFSET=768;
const uint8_t PCF_ADDRESS=0x20;
const uint8_t DISPLAY_ROTATION=3; // 0..3 if physically rotated in housing.
const uint32_t EYE_DURATION_MS=6600;
bool showingEye=true,dirty=true;
uint32_t eyeStarted=0,lastFrame=0,lastIo=0;
uint32_t eyeFrames=0,eyeRenderUs=0,eyeMaxUs=0;
uint32_t screenFrames=0,screenRenderUs=0,screenMaxUs=0;
uint16_t lastPcfBits=0;
#include "Screen.h"

bool saveCalibration(const SoilCalibration &candidate) {
  if(EEPROM.length()<CALIBRATION_OFFSET+SoilCalibration::kStorageSize){
    Serial.println("CALIBRATION STORAGE UNAVAILABLE");return false;
  }
  uint8_t record[SoilCalibration::kStorageSize],previous[SoilCalibration::kStorageSize];
  candidate.encode(record);
  for(unsigned i=0;i<sizeof(record);i++){
    previous[i]=EEPROM.read(CALIBRATION_OFFSET+i);
    EEPROM.write(CALIBRATION_OFFSET+i,record[i]);
  }
  if(!EEPROM.commit()){
    for(unsigned i=0;i<sizeof(record);i++)EEPROM.write(CALIBRATION_OFFSET+i,previous[i]);
    Serial.println("CALIBRATION SAVE FAILED");
    return false;
  }
  soilCalibration=candidate;
  dirty=true;
  Serial.printf("CALIBRATION SAVED dry=%d wet=%d mask=%u\n",
    soilCalibration.dry,soilCalibration.wet,unsigned(soilCalibration.mask));
  return true;
}
void loadCalibration() {
  EEPROM.begin(1024);
  if(EEPROM.length()<CALIBRATION_OFFSET+SoilCalibration::kStorageSize){
    Serial.println("CALIBRATION STORAGE UNAVAILABLE");return;
  }
  uint8_t record[SoilCalibration::kStorageSize];
  for(unsigned i=0;i<sizeof(record);i++)record[i]=EEPROM.read(CALIBRATION_OFFSET+i);
  if(soilCalibration.decode(record)){
    Serial.printf("CALIBRATION LOADED dry=%d wet=%d mask=%u\n",
      soilCalibration.dry,soilCalibration.wet,unsigned(soilCalibration.mask));
    return;
  }
  // These fallback endpoints are real, archived measurements of this build.
  // Never substitute guessed endpoints when the user has not calibrated.
  SoilCalibration measured;
  if(measured.capture(false,kRecordedSoilDry)&&measured.capture(true,kRecordedSoilWet))
    saveCalibration(measured);
}
void captureCalibration(bool wetPoint) {
  if(selection.selected!=PAGE_SOIL||outputs.on[UV]||outputs.on[WARM]||outputs.on[FANS]){
    Serial.println("CALIBRATION BLOCKED: select SOIL and switch loads off");return;
  }
  if(!soil.fresh(millis())){Serial.println("CALIBRATION BLOCKED: no fresh sample");return;}
  SoilCalibration candidate=soilCalibration;
  if(!candidate.capture(wetPoint,soil.raw)){
    Serial.println("CALIBRATION BLOCKED: endpoints invalid or too close");return;
  }
  saveCalibration(candidate);
}
// On this assembled board GPIO0/DC follows GPIO4/SDA when released.
// Keeping DC push-pull HIGH prevents every I2C peripheral from acknowledging.
// Serialize display drawing and I2C: drive DC only while drawing, then release
// it with display CS inactive. No I2C callbacks run inside display operations.
void acquireDisplayControl() {
  digitalWrite(D3,HIGH);
  pinMode(D3,OUTPUT);
}
void releaseDisplayControl() {
  digitalWrite(D8,HIGH);
  pinMode(D3,INPUT_PULLUP);
}

void lostConnection() {
  if(outputs.ioReady){Serial.println("PCF LOST / OUTPUTS UNKNOWN");dirty=true;}
  outputs.link(false);
  buttons.reset();
}
bool writeOutputs(uint16_t mask) {
  Wire.beginTransmission(PCF_ADDRESS);Wire.write(uint8_t(mask));Wire.write(uint8_t(mask>>8));
  if(Wire.endTransmission()!=0){lostConnection();return false;}
  relayTransition.applied(mask,millis());return true;
}
void action(int key) {
  if(key==3){outputs.allOff();writeOutputs(kAllOffMask);Serial.println("ALL OFF REQUESTED");}
  else if(key==0)selection.previous();
  else if(key==1)selection.next();
  else if(key==2){
    int channel=selection.channel();
    if(channel<0){soilAdc.request();temperatureHtu.request();Serial.println("SENSOR SAMPLES REQUESTED");}
    else if(outputs.toggle(channel))Serial.printf("REQUEST UV=%d WARM=%d FANS=%d\n",outputs.on[0],outputs.on[1],outputs.on[2]);
    else Serial.println("COMMAND BLOCKED: PCF NOT CONNECTED");
  }
  Serial.printf("BUTTON %d SELECTED %d\n",key+1,selection.selected);
  showingEye=false;dirty=true;
}
void ioTick(uint32_t now) {
  // Slow retries on a missing device, normal touch polling every 25 ms.
  if(uint32_t(now-lastIo)<(outputs.ioReady?25U:500U))return;
  lastIo=now;
  uint16_t desired=relayTransition.next(outputs.mask(),now);
  if(!writeOutputs(desired))return;
  if(Wire.requestFrom(PCF_ADDRESS,uint8_t(2))!=2){lostConnection();writeOutputs(kAllOffMask);return;}
  uint16_t bits=Wire.read();bits|=uint16_t(Wire.read())<<8;
  lastPcfBits=bits;
  if(!outputs.ioReady){outputs.link(true);dirty=true;Serial.println("PCF READY / ALL OFF");}
  static uint8_t previousTouches=255;
  uint8_t touches=(bits>>8)&15;
  if(touches!=previousTouches){
    Serial.printf("TOUCH P10=%d P11=%d P12=%d P13=%d\n",bool(touches&1),bool(touches&2),bool(touches&4),bool(touches&8));
    previousTouches=touches;
  }
  int key=buttons.poll(bits,now);
  if(key>=0)action(key);
}
void reportEye(){
  Serial.printf("DIAG EYE frames=%lu avg_us=%lu max_us=%lu\n",
    (unsigned long)eyeFrames,(unsigned long)(eyeFrames?eyeRenderUs/eyeFrames:0),(unsigned long)eyeMaxUs);
}
void probeBus(int sda,int scl,uint32_t hz,const char *name){
  Wire.begin(sda,scl);Wire.setClock(hz);Wire.setClockStretchLimit(1500);
  delay(2);
  Serial.printf("DIAG BUS %s %luHz SDA=%d SCL=%d idle=%d/%d addresses:",
    name,(unsigned long)hz,sda,scl,digitalRead(sda),digitalRead(scl));
  int found=0,errors[6]={};
  // Address-only probes; no output data is written to a peripheral.
  for(uint8_t a=8;a<120;a++){
    Wire.beginTransmission(a);uint8_t error=Wire.endTransmission();
    errors[error<5?error:5]++;
    if(!error){Serial.printf(" 0x%02X",a);found++;}
    yield();
  }
  Serial.printf(" (%d found) nack=%d other=%d\n",found,errors[2],errors[1]+errors[3]+errors[4]+errors[5]);
}
void wiringDiagnostics(){
  if(outputs.on[0]||outputs.on[1]||outputs.on[2]){
    Serial.println("DIAG BUS BLOCKED: switch all loads off first");return;
  }
  probeBus(D2,D1,100000,"STANDARD");
  probeBus(D2,D1,10000,"STANDARD");
  probeBus(D1,D2,100000,"SWAPPED");
  probeBus(D1,D2,10000,"SWAPPED");
  Wire.begin(D2,D1);Wire.setClock(100000);Wire.setClockStretchLimit(1500);
  buttons.reset();
  Serial.println("DIAG BUS DONE / STANDARD RESTORED");
}
void serialTick() {
  if(!Serial.available())return;
  char c=Serial.read();
  if(c>='1'&&c<='3')action(c-'1');
  else if(c=='0'||c=='4')action(3);
  else if(c=='e'){showingEye=true;resetEye();eyeStarted=millis();lastFrame=0;eyeFrames=eyeRenderUs=eyeMaxUs=0;}
  else if(c=='x')wiringDiagnostics();
  else if(c=='d'||c=='w')captureCalibration(c=='w');
  else if(c=='?'){
    Serial.printf("DIAG SIMPLE uptime=%lu io=%d UV=%d WARM=%d FANS=%d PUMP=0 selected=%d\n",
      (unsigned long)millis(),outputs.ioReady,outputs.on[0],outputs.on[1],outputs.on[2],selection.selected);
    if(outputs.ioReady)
      Serial.printf("DIAG PCF requested=0x%04X readback=0x%04X P15=%d OLD_DRIVER=%d%d\n",
        outputs.mask(),lastPcfBits,bool(lastPcfBits&kFanRelayMask),
        bool(lastPcfBits&(1U<<4)),bool(lastPcfBits&(1U<<5)));
    const char *pageNames[]={"WARM","UV","SOIL","FANS"};
    Serial.printf("DIAG PAGE index=%d name=%s channel=%d\n",
      selection.selected,pageNames[selection.selected],selection.channel());
    Serial.printf("DIAG SOIL fresh=%d raw=%d mV=%u calibrated=%d percent=%d dry=%d wet=%d\n",
      soil.fresh(millis()),soil.raw,unsigned(soil.millivolts),soilCalibration.valid(),
      soilCalibration.percent(soil,millis()),soilCalibration.dry,soilCalibration.wet);
    Serial.printf("DIAG TEMPERATURE fresh=%d celsius=%.2f source=HTU21_AIR age_ms=%lu\n",
      temperature.fresh(millis()),double(temperature.celsius),
      (unsigned long)(millis()-temperature.sampledAt));
    Serial.printf("DIAG SCREEN frames=%lu avg_us=%lu max_us=%lu\n",
      (unsigned long)screenFrames,(unsigned long)(screenFrames?screenRenderUs/screenFrames:0),
      (unsigned long)screenMaxUs);
    reportEye();
    if(outputs.on[0]||outputs.on[1]||outputs.on[2])return;
    Serial.print("DIAG I2C addresses:");int count=0;
    for(uint8_t a=1;a<127;a++){Wire.beginTransmission(a);if(Wire.endTransmission()==0){Serial.printf(" 0x%02X",a);count++;}yield();}
    Serial.printf(" (%d found)\n",count);
  }
}
void setup() {
  Serial.begin(115200);
  WiFi.persistent(false);WiFi.mode(WIFI_OFF);
  // SD shares SPI; keep its select inactive even though this sketch does not use it.
  pinMode(D0,OUTPUT);digitalWrite(D0,HIGH);
  pinMode(D8,OUTPUT);digitalWrite(D8,HIGH);
  Wire.begin(D2,D1);Wire.setClock(100000);Wire.setClockStretchLimit(1500);
  outputs.allOff();writeOutputs(kAllOffMask);
  loadCalibration();
  display.begin(20000000);display.setRotation(DISPLAY_ROTATION);display.setTextWrap(false);
  display.fillScreen(0);releaseDisplayControl();eyeStarted=millis();
  Serial.println("BOOT SIMPLE MINIMAL / SOIL PERCENT + AIR CELSIUS / P15 HIGH=ON / ALL OFF");
}
void loop() {
  uint32_t now=millis();ioTick(now);
  // ADC transactions, just like PCF transactions, run only while LCD DC is
  // released. Conversion progresses asynchronously between loop iterations.
  if(soilAdc.tick(millis(),soil)&&selection.selected==PAGE_SOIL)dirty=true;
  if(temperatureHtu.tick(millis(),temperature)&&selection.selected==PAGE_SOIL)dirty=true;
  serialTick();
  now=millis();
  if(showingEye){
    uint32_t elapsed=now-eyeStarted;
    if(elapsed>=EYE_DURATION_MS){showingEye=false;dirty=true;reportEye();}
    else if(uint32_t(now-lastFrame)>=16){
      lastFrame=now;uint32_t began=micros();
      acquireDisplayControl();
      bool rendered=drawEye(elapsed);
      releaseDisplayControl(); // Also release when an unchanged frame is skipped.
      if(rendered){
        uint32_t duration=micros()-began;eyeFrames++;eyeRenderUs+=duration;
        if(duration>eyeMaxUs)eyeMaxUs=duration;
      }
    }
  }
  if(!showingEye&&dirty){
    uint32_t began=micros();
    acquireDisplayControl();drawDashboard();releaseDisplayControl();dirty=false;
    uint32_t duration=micros()-began;screenFrames++;screenRenderUs+=duration;
    if(duration>screenMaxUs)screenMaxUs=duration;
  }
  yield();
}
