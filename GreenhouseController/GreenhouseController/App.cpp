#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <SD.h>
#include <time.h>
extern "C" {
#include <user_interface.h>
}
#include "Control.h"

Settings cfg;
Readings readings;
Controller control;
Adafruit_GC9A01A display(D8,D3,D4);
GFXcanvas1 canvas(240,240);
uint16_t pixels[960];
ESP8266WebServer server(80);
bool pcfOK=false, sdOK=false;
uint16_t applied=0xFFFF;
uint32_t lastIO=0,lastLog=0,lastSD=0;
char apName[32],wifiSsid[33]={},wifiPass[65]={},accessKey[17]={};
char notice[40]="STARTING";
struct Event {uint32_t uptime;time_t epoch;char name[40];};
Event events[12];
uint8_t eventHead=0,eventCount=0;
Event pending[16];uint8_t pendingHead=0,pendingCount=0;
uint32_t droppedEvents=0;
uint8_t page=0;
uint32_t pageAt=0;
bool menu=false,editing=false;
int menuItem=0;
Settings draft;
const int EXTRA_MENU=11;

void event(const char *name) {
  Event e{};e.uptime=millis();e.epoch=time(nullptr);strlcpy(e.name,name,sizeof(e.name));
  events[eventHead]=e;eventHead=(eventHead+1)%12;if(eventCount<12)eventCount++;
  if(pendingCount<16){pending[(pendingHead+pendingCount)%16]=e;pendingCount++;}else droppedEvents++;
  Serial.println(name);
}
uint32_t crc32(const uint8_t *p,size_t n) {
  uint32_t c=~0UL;while(n--){c^=*p++;for(int i=0;i<8;i++)c=(c>>1)^(0xEDB88320UL&-(c&1));}return ~c;
}
struct Saved {uint32_t magic;Settings settings;char ssid[33],pass[65],key[17];uint32_t crc;};
bool saveSettings() {
  readings.soilPct=soilCal(cfg)&&readings.soil?percent(readings.soilRaw,cfg.soilDry,cfg.soilWet):NAN;
  readings.waterPct=waterCal(cfg)&&readings.water?percent(readings.waterRaw,cfg.waterEmpty,cfg.waterFull):NAN;
  Saved s{};s.magic=0x47524832;s.settings=cfg;
  strlcpy(s.ssid,wifiSsid,sizeof(s.ssid));strlcpy(s.pass,wifiPass,sizeof(s.pass));strlcpy(s.key,accessKey,sizeof(s.key));
  s.crc=crc32(reinterpret_cast<uint8_t*>(&s),offsetof(Saved,crc));
  EEPROM.put(0,s);bool ok=EEPROM.commit();strlcpy(notice,ok?"SAVED":"SAVE FAILED",sizeof(notice));event(notice);return ok;
}
void loadSettings() {
  EEPROM.begin(1024);Saved s;EEPROM.get(0,s);
  if(s.magic==0x47524832 && s.crc==crc32(reinterpret_cast<uint8_t*>(&s),offsetof(Saved,crc)) && valid(s.settings)
    && memchr(s.ssid,0,sizeof(s.ssid)) && memchr(s.pass,0,sizeof(s.pass)) && s.key[16]==0 && strlen(s.key)==16) {
    cfg=s.settings;strlcpy(wifiSsid,s.ssid,sizeof(wifiSsid));strlcpy(wifiPass,s.pass,sizeof(wifiPass));strlcpy(accessKey,s.key,sizeof(accessKey));
  } else {
    snprintf(accessKey,sizeof(accessKey),"%08lx%08lx",(unsigned long)os_random(),(unsigned long)os_random());saveSettings();
  }
}
bool writePCF(uint16_t mask) {
  Wire.beginTransmission(0x20);Wire.write(uint8_t(mask));Wire.write(uint8_t(mask>>8));
  bool ok=Wire.endTransmission()==0;
  if(ok) {
    // Record confirmed I2C command transitions, not physical water flow.
    bool pumpWasOn=bool(applied&(1<<2))&&!(applied&(1<<3));
    bool pumpNowOn=bool(mask&(1<<2));
    static uint32_t pumpStarted=0;
    if(pumpNowOn&&!pumpWasOn){pumpStarted=millis();event("PUMP COMMAND START");}
    if(!pumpNowOn&&pumpWasOn){
      char msg[40];snprintf(msg,sizeof(msg),"PUMP COMMAND END %lu ms",(unsigned long)(millis()-pumpStarted));event(msg);
    }
    applied=mask;
  }
  return ok;
}
void ioTick(uint32_t now) {
  
  uint16_t mask=outputMask(control.out);
  if(mask==applied && pcfOK && uint32_t(now-lastIO)<20)return;
  lastIO=now;
  bool ok=writePCF(mask);
  if(!ok) {
    if(pcfOK)event("PCF LOST / OUTPUTS UNKNOWN");
    control.ioFault=true;control.stop();
  }
  pcfOK=ok;
}
bool writeReg(uint8_t addr,uint8_t reg,uint16_t value) {
  Wire.beginTransmission(addr);Wire.write(reg);Wire.write(value>>8);Wire.write(value&255);return Wire.endTransmission()==0;
}
bool readReg(uint8_t addr,uint8_t reg,uint16_t &v) {
  Wire.beginTransmission(addr);Wire.write(reg);if(Wire.endTransmission(false)!=0)return false;
  if(Wire.requestFrom(addr,uint8_t(2))!=2)return false;
  v=uint16_t(Wire.read())<<8;v|=Wire.read();return true;
}
uint8_t crc8(uint8_t a,uint8_t b) {
  uint8_t c=0;uint8_t d[2]={a,b};for(uint8_t v:d){c^=v;for(int i=0;i<8;i++)c=(c&128)?(c<<1)^0x31:c<<1;}return c;
}
// Independent non-blocking no-hold HTU21 and single-shot ADS1115 state machines.
void sensorsTick(uint32_t now) {
  static uint8_t adsState=0,htuState=0;
  static uint32_t adsAt=0,adsCycle=0,htuAt=0,htuCycle=0;
  static float temperature=NAN;
  if(adsState==0 && uint32_t(now-adsCycle)>=1000) {
    adsCycle=now;
    if(writeReg(0x48,1,0xC383)){adsState=1;adsAt=now;}
    else {readings.soil=false;readings.water=false;}
  }
  if(adsState && uint32_t(now-adsAt)>=10) {
    uint16_t status=0,raw=0;
    bool ok=readReg(0x48,1,status);
    if(ok && !(status&0x8000) && uint32_t(now-adsAt)<100)return;
    ok=ok&&(status&0x8000)&&((status&0x7FFF)==(adsState==1?0x4383:0x5383))&&readReg(0x48,0,raw)&&raw<=26400;
    if(adsState==1) {
      readings.soil=ok;if(ok){readings.soilRaw=raw;readings.soilAt=now;}
      if(writeReg(0x48,1,0xD383)){adsState=2;adsAt=now;}
      else {adsState=0;readings.water=false;}
    } else {readings.water=ok;if(ok){readings.waterRaw=raw;readings.waterAt=now;}adsState=0;}
    readings.soilPct=soilCal(cfg)&&readings.soil?percent(readings.soilRaw,cfg.soilDry,cfg.soilWet):NAN;
    readings.waterPct=waterCal(cfg)&&readings.water?percent(readings.waterRaw,cfg.waterEmpty,cfg.waterFull):NAN;
  }
  if(htuState==0 && uint32_t(now-htuCycle)>=1500) {
    htuCycle=now;Wire.beginTransmission(0x40);Wire.write(0xF3);
    if(Wire.endTransmission()==0){htuState=1;htuAt=now;}else readings.air=false;
  }
  if(htuState && uint32_t(now-htuAt)>=(htuState==1?60:25)) {
    bool ok=Wire.requestFrom(uint8_t(0x40),uint8_t(3))==3;
    uint16_t raw=0;
    if(ok){uint8_t a=Wire.read(),b=Wire.read(),c=Wire.read();ok=crc8(a,b)==c;raw=((a<<8)|b)&0xFFFC;}
    if(!ok){readings.air=false;htuState=0;return;}
    if(htuState==1) {
      temperature=-46.85f+175.72f*raw/65536.0f;
      Wire.beginTransmission(0x40);Wire.write(0xF5);
      if(Wire.endTransmission()==0){htuState=2;htuAt=now;}else {htuState=0;readings.air=false;}
    } else {
      float humidity=-6+125.0f*raw/65536.0f;
      readings.air=isfinite(temperature)&&temperature>=-40&&temperature<=125&&humidity>=-6&&humidity<=119;
      if(readings.air){readings.temperature=temperature;readings.humidity=constrain(humidity,0.0f,100.0f);readings.airAt=now;}
      htuState=0;
    }
  }
}
int clockMinute() {time_t t=time(nullptr);if(t<1700000000)return -1;tm local;localtime_r(&t,&local);return local.tm_hour*60+local.tm_min;}
void setClock() {configTime(cfg.timezoneMin*60,0,"pool.ntp.org","time.google.com");}
const char *problem() {
  if(!pcfOK||control.ioFault)return "PCF / OUTPUTS UNKNOWN";
  if(control.paused)return "PAUSED";
  if(control.pumpFault)return control.noResponse?"NO SOIL RESPONSE":"PUMP LIMIT REACHED";
  if(!readings.water||millis()-readings.waterAt>=3000)return "WATER SENSOR ERROR";
  if(!waterCal(cfg))return "CALIBRATE WATER";
  if(readings.waterPct<cfg.waterMin)return "LOW WATER";
  if(!readings.soil||millis()-readings.soilAt>=3000)return "SOIL SENSOR ERROR";
  if(!soilCal(cfg))return "CALIBRATE SOIL";
  if(!readings.air||millis()-readings.airAt>=5000)return "AIR SENSOR ERROR";
  if(!sdOK)return "SD NOT READY";
  if(clockMinute()<0)return "CLOCK NOT SET";
  return "READY";
}
bool resumeSystem() {
  if(!writePCF(0xFFC3)){pcfOK=false;return false;}
  pcfOK=true;applied=0xFFC3;control.stop();control.ioFault=false;control.paused=false;event("RESUMED");return true;
}
void stopSystem() {control.stop();pcfOK=writePCF(0xFFC3);if(pcfOK)applied=0xFFC3;event("STOP ALL");}
bool manualCommand(int c,bool on) {
  if(c<0||c>3||control.paused||control.ioFault||!pcfOK)return false;
  if(c==PUMP&&on) {
    if(control.out[PUMP]||control.pumpFault||!control.waterReady(cfg,readings,millis()))return false;
  }
  control.setManual(c,on,millis());event(on?"MANUAL ON":"MANUAL OFF");return true;
}
bool capture(int endpoint) {
  bool soil=endpoint<2;
  if(endpoint<0||endpoint>3 || (soil?(!readings.soil||millis()-readings.soilAt>=3000):(!readings.water||millis()-readings.waterAt>=3000)))return false;
  stopSystem();
  int *v[]={&cfg.soilDry,&cfg.soilWet,&cfg.waterEmpty,&cfg.waterFull};
  *v[endpoint]=soil?readings.soilRaw:readings.waterRaw;cfg.calMask|=1<<endpoint;
  bool ok=saveSettings();event("CALIBRATION CAPTURED");return ok;
}
void timestamp(char *buf,size_t size,time_t epoch,uint32_t uptime) {
  if(epoch>=1700000000){tm t;gmtime_r(&epoch,&t);strftime(buf,size,"%Y-%m-%dT%H:%M:%SZ",&t);}
  else snprintf(buf,size,"uptime:%lu",(unsigned long)(uptime/1000));
}
void sdTick(uint32_t now) {
  if(control.out[PUMP])return; // Never block pump shutoff with SD writes.
  if(!sdOK) {
    if(now-lastSD<30000)return;
    lastSD=now;digitalWrite(D8,HIGH);sdOK=SD.begin(D0);
    if(sdOK)event("SD READY");else return;
  }
  bool sample=uint32_t(now-lastLog)>=uint32_t(cfg.logSec)*1000;
  if(!sample&&!pendingCount)return;
  // Bounded files, two rotations per stream.
  const char *path=pendingCount?"/events.csv":"/data.csv";
  const char *old=pendingCount?"/events.old":"/data.old";
  File f=SD.open(path,FILE_READ);bool rotate=f&&f.size()>1024UL*1024;f.close();
  if(rotate){SD.remove(old);if(!SD.rename(path,old)){sdOK=false;return;}}
  f=SD.open(path,FILE_WRITE);if(!f){sdOK=false;event("SD WRITE ERROR");return;}
  bool ok=true;
  if(f.size()==0) ok=f.println(pendingCount?"time,uptime_ms,event":"time,uptime_ms,temp_C,air_pct,soil_pct,water_pct,soil_raw,water_raw,pump_cmd,fan_cmd,lamp_cmd,strip_cmd,pump_mode,fan_mode,lamp_mode,strip_mode,paused,status")>0;
  char stamp[32],line[300];
  if(pendingCount) {
    Event &e=pending[pendingHead];timestamp(stamp,sizeof(stamp),e.epoch,e.uptime);
    snprintf(line,sizeof(line),"%s,%lu,%s",stamp,(unsigned long)e.uptime,e.name);
  } else {
    timestamp(stamp,sizeof(stamp),time(nullptr),now);
    snprintf(line,sizeof(line),"%s,%lu,%.1f,%.1f,%.1f,%.1f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s",stamp,(unsigned long)now,
      freshAir(readings,now)?readings.temperature:NAN,freshAir(readings,now)?readings.humidity:NAN,
      freshSoil(readings,now)?readings.soilPct:NAN,freshWater(readings,now)?readings.waterPct:NAN,
      readings.soilRaw,readings.waterRaw,control.out[0],control.out[1],control.out[2],control.out[3],
      control.overrideMode[0]>=0?MANUAL:cfg.mode[0],control.overrideMode[1]>=0?MANUAL:cfg.mode[1],
      control.overrideMode[2]>=0?MANUAL:cfg.mode[2],control.overrideMode[3]>=0?MANUAL:cfg.mode[3],control.paused,problem());
  }
  size_t expected=strlen(line)+2;
  ok=(f.println(line)==expected)&&ok;f.flush();ok=ok&&!f.getWriteError();f.close();
  if(!ok){sdOK=false;event("SD WRITE ERROR");return;}
  if(pendingCount){pendingHead=(pendingHead+1)%16;pendingCount--;}else lastLog=now;
}

void setupWeb();
void drawUI(uint32_t now);
void buttonsTick(uint32_t now);
void serialDiagnostics() {
  if(!Serial.available())return;
  char command=Serial.read();
  if(command!='?')return;
  if(!control.paused){Serial.println("DIAG: pause system first");return;}
  Serial.printf("DIAG uptime=%lu paused=%d io=%d ioFault=%d sd=%d heap=%u\n",
    (unsigned long)millis(),control.paused,pcfOK,control.ioFault,sdOK,ESP.getFreeHeap());
  Serial.printf("DIAG soil=%d raw=%d water=%d raw=%d air=%d temp=%.1f humidity=%.1f\n",
    readings.soil,readings.soilRaw,readings.water,readings.waterRaw,
    readings.air,readings.temperature,readings.humidity);
  Serial.printf("DIAG bus idle SDA=%d SCL=%d; I2C addresses:",digitalRead(D2),digitalRead(D1));
  int found=0;
  for(uint8_t addr=1;addr<127;addr++){
    Wire.beginTransmission(addr);
    if(Wire.endTransmission()==0){Serial.printf(" 0x%02X",addr);found++;}
    yield();
  }
  Serial.printf(" (%d found)\n",found);
}
void appSetup() {
  Serial.begin(115200);
  pinMode(D0,OUTPUT);digitalWrite(D0,HIGH);
  pinMode(D8,OUTPUT);digitalWrite(D8,HIGH);
  Wire.begin(D2,D1);Wire.setClock(100000);Wire.setClockStretchLimit(1500);
  pcfOK=writePCF(0xFFC3);control.ioFault=!pcfOK;
  loadSettings();control.paused=!cfg.bootAuto;
  display.begin(20000000);display.setRotation(0);display.fillScreen(0x0842);
  canvas.setTextWrap(false);
  digitalWrite(D8,HIGH);sdOK=SD.begin(D0);lastSD=millis();
  snprintf(apName,sizeof(apName),"Greenhouse-%06lx",(unsigned long)ESP.getChipId());
  WiFi.persistent(false);WiFi.mode(WIFI_AP_STA);WiFi.softAP(apName,accessKey);
  if(wifiSsid[0])WiFi.begin(wifiSsid,wifiPass);
  setClock();setupWeb();server.begin();
  Serial.printf("AP: %s\nPassword / web admin key: %s\nOpen http://192.168.4.1\n",apName,accessKey);
  event("BOOT / MANUAL STATES CLEARED");strlcpy(notice,"READY",sizeof(notice));
}
void appLoop() {
  uint32_t now=millis();
  sensorsTick(now);
  control.tick(cfg,readings,millis(),pcfOK,clockMinute());
  ioTick(millis());buttonsTick(millis());
  serialDiagnostics();
  static uint8_t prevOut=0;uint8_t out=0;for(int i=0;i<4;i++)out|=control.out[i]<<i;
  if(out!=prevOut){char e[40];snprintf(e,sizeof(e),"COMMANDS P%d F%d L%d S%d",control.out[0],control.out[1],control.out[2],control.out[3]);event(e);prevOut=out;}
  static char prevProblem[40]={};const char *p=problem();
  if(strcmp(p,prevProblem)!=0){event(p);strlcpy(prevProblem,p,sizeof(prevProblem));}
  if(!control.out[PUMP])server.handleClient();
  // Control checked again after network service before display and storage.
  control.tick(cfg,readings,millis(),pcfOK,clockMinute());ioTick(millis());
  sdTick(millis());drawUI(millis());yield();
}
#include "Web.h"
#include "Display.h"
