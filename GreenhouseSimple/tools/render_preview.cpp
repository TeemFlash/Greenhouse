#include <stdint.h>
#include <fstream>
#include <string>
#include <cmath>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <cstring>
#include <limits>
#define PROGMEM
#define pgm_read_word(p) (*(p))
#define pgm_read_byte(p) (*(p))
void yield(){}
uint32_t previewNow=10000;
uint32_t millis(){return previewNow;}
#include "../GreenhouseSimple/SimpleControl.h"
#include "../GreenhouseSimple/MenuInput.h"
#include "../GreenhouseSimple/SoilSensor.h"
#include "../GreenhouseSimple/SoilCalibration.h"
#include "../GreenhouseSimple/TemperatureSensor.h"
struct PreviewDisplay {
 uint16_t p[240*240]={};
 int regionX=0,regionY=0,regionWidth=0,regionHeight=0,regionOffset=0;
 unsigned regions=0;
 void beginRegion(int x,int y,int w,int h){
  assert(x>=0&&y>=0&&x+w<=240&&y+h<=240&&w>0&&h>0);
  regionX=x;regionY=y;regionWidth=w;regionHeight=h;regionOffset=0;regions++;
 }
 void pushRegion(uint16_t *data,unsigned count){
  for(unsigned i=0;i<count;i++,regionOffset++)drawPixel(regionX+regionOffset%regionWidth,regionY+regionOffset/regionWidth,data[i]);
 }
 void endRegion(){assert(regionOffset==regionWidth*regionHeight);}
 void drawPixel(int x,int y,uint16_t c){if(x>=0&&x<240&&y>=0&&y<240)p[y*240+x]=c;}
 void fillScreen(uint16_t c){std::fill(p,p+240*240,c);}
 void drawRGBBitmap(int x,int y,const uint16_t *data,int w,int h){for(int j=0;j<h;j++)for(int i=0;i<w;i++)drawPixel(x+i,y+j,data[j*w+i]);}
 void fillCircle(int x,int y,int r,uint16_t c){for(int j=-r;j<=r;j++)for(int i=-r;i<=r;i++)if(i*i+j*j<=r*r)drawPixel(x+i,y+j,c);}
 void drawCircle(int x,int y,int r,uint16_t c){for(int j=-r;j<=r;j++)for(int i=-r;i<=r;i++)if(std::abs(std::sqrt(float(i*i+j*j))-r)<.55)drawPixel(x+i,y+j,c);}
 bool inside(int xx,int yy,int w,int h,int r){int cx=std::max(r,std::min(w-r-1,xx)),cy=std::max(r,std::min(h-r-1,yy));return (xx-cx)*(xx-cx)+(yy-cy)*(yy-cy)<=r*r;}
 void fillRoundRect(int x,int y,int w,int h,int r,uint16_t c){for(int j=0;j<h;j++)for(int i=0;i<w;i++)if(inside(i,j,w,h,r))drawPixel(x+i,y+j,c);}
 void drawRoundRect(int x,int y,int w,int h,int r,uint16_t c){for(int j=0;j<h;j++)for(int i=0;i<w;i++)if(inside(i,j,w,h,r)&&(i==0||j==0||i==w-1||j==h-1||!inside(i-1,j-1,w-2,h-2,r-1)))drawPixel(x+i,y+j,c);}
 void save(const std::string &path){std::ofstream o(path,std::ios::binary);o<<"P6\n240 240\n255\n";for(uint16_t c:p){unsigned char b[]={static_cast<unsigned char>(((c>>11)&31)*255/31),static_cast<unsigned char>(((c>>5)&63)*255/63),static_cast<unsigned char>((c&31)*255/31)};o.write(reinterpret_cast<char*>(b),3);}}
} display;
SimpleControl outputs;
MenuSelection selection;
SoilReading soil;
SoilCalibration soilCalibration;
TemperatureReading temperature;
#include "../GreenhouseSimple/Screen.h"

static void assertDashboardRegion(bool partial) {
 assert(display.regionX==0&&display.regionWidth==240);
 assert(display.regionY==(partial?120:0));
 assert(display.regionHeight==(partial?80:240));
}

static void assertFullDashboardMatchesCurrent() {
 uint16_t expected[240*240];std::copy(display.p,display.p+240*240,expected);
 // Direct writes bypass the renderer; invalidate its cache explicitly.
 display.fillScreen(0xFFFF);resetDashboard();drawDashboard();
 assertDashboardRegion(false);
 assert(std::equal(display.p,display.p+240*240,expected));
}

static void exampleCalibration() {
 soilCalibration.clear();
 assert(soilCalibration.capture(false,22000));
 assert(soilCalibration.capture(true,6000));
}

static void exampleReadings(int raw=14000,float celsius=24.5f) {
 soil.valid=true;soil.raw=raw;soil.millivolts=uint16_t((raw+4)/8);soil.sampledAt=previewNow;
 temperature.valid=true;temperature.celsius=celsius;temperature.sampledAt=previewNow;
}

int main(int argc,char **argv){
 std::string dir=argc>1?argv[1]:"/tmp";
 // Check the actual incremental renderer against a complete reference frame.
 for(int t=0;t<=6600;t+=16){
  drawEye(t);int gx,gy;eyeGaze(t,gx,gy);
  for(int y=0;y<240;y++)for(int x=0;x<240;x++)
   assert(display.p[y*240+x]==eyePixelFixed(x,y,gx,gy));
 }
 unsigned regions=display.regions;drawEye(6600);assert(display.regions==regions);
 display.fillScreen(0x1234);resetEye();drawEye(0);
 for(int y=0;y<240;y++)for(int x=0;x<240;x++)assert(display.p[y*240+x]==eyePixelFixed(x,y,0,0));
 std::cout<<"PASS: incremental eye equals full reference, bounds, reset, stationary frames skipped\n";
 resetEye();
 for(int t=0;t<=6600;t+=20){drawEye(t);display.save(dir+"/eye_"+std::to_string(t)+".ppm");}
 outputs.link(true);
 exampleCalibration();
 const char *pageNames[]={"warm","uv","soil","fans"};
 for(int page=0;page<PAGE_COUNT;page++){
  selection.selected=page;outputs.allOff();
  exampleReadings();
  drawDashboard();display.save(dir+"/"+pageNames[page]+"_off.ppm");
  int channel=selection.channel();
  if(channel>=0)assert(outputs.toggle(channel));
  drawDashboard();display.save(dir+"/"+pageNames[page]+"_on.ppm");
  outputs.link(false);soil.valid=false;temperature.valid=false;
  drawDashboard();display.save(dir+"/"+pageNames[page]+"_offline.ppm");
  outputs.link(true);
 }
 // Sensor updates must overwrite old digits and not retain an old page/icon.
 selection.selected=PAGE_SOIL;exampleReadings();
 drawDashboard();assertDashboardRegion(false);
 const int counts[]={0,6000,8000,14000,21999,22000,26399,26400};
 for(int count:counts){
  soil.raw=count;soil.millivolts=uint16_t((count+4)/8);
  drawDashboard();
  assertDashboardRegion(true);
  assertFullDashboardMatchesCurrent();
 }
 // A new invalid/stale state changes the sprout above the number region too.
 soil.sampledAt=previewNow-2001;
 drawDashboard();display.save(dir+"/soil_stale.ppm");
 assertDashboardRegion(false);assertFullDashboardMatchesCurrent();
 drawDashboard();assertDashboardRegion(true);assertFullDashboardMatchesCurrent();
 soil.sampledAt=previewNow;soil.valid=true;
 drawDashboard();assertDashboardRegion(false);assertFullDashboardMatchesCurrent();
 soil.valid=false;
 drawDashboard();assertDashboardRegion(false);assertFullDashboardMatchesCurrent();
 // Percentages require actual calibration; keep temperature independent.
 soil.valid=true;soilCalibration.clear();
 drawDashboard();assertFullDashboardMatchesCurrent();
 DashboardView view=dashboardSnapshot();
 assert(std::strcmp(view.value,"-- %")==0&&std::strcmp(view.temperatureValue,"24.5 °C")==0);
 display.save(dir+"/soil_uncalibrated.ppm");
 exampleCalibration();exampleReadings();
 drawDashboard();assertDashboardRegion(false);assertFullDashboardMatchesCurrent();
 // Both extremes and negative values fit and survive partial updates.
 const int rawLimits[]={22000,6000,14000,14000};
 const float temperatureLimits[]={-40.0f,125.0f,-0.1f,0.0f};
 const char *percentageLabels[]={"0 %","100 %","50 %","50 %"};
 const char *temperatureLabels[]={"-40.0 °C","125.0 °C","-0.1 °C","0.0 °C"};
 const char *limitFiles[]={"soil_cold","soil_hot","soil_negative","soil_zero"};
 for(unsigned i=0;i<4;i++){
  exampleReadings(rawLimits[i],temperatureLimits[i]);
  drawDashboard();assertDashboardRegion(true);assertFullDashboardMatchesCurrent();
  view=dashboardSnapshot();
  assert(std::strcmp(view.value,percentageLabels[i])==0);
  assert(std::strcmp(view.temperatureValue,temperatureLabels[i])==0);
  display.save(dir+"/"+limitFiles[i]+".ppm");
 }
 temperature.sampledAt=previewNow-5001;
 drawDashboard();assertDashboardRegion(false);assertFullDashboardMatchesCurrent();
 assert(std::strcmp(dashboardSnapshot().temperatureValue,"-- °C")==0);
 display.save(dir+"/soil_temperature_missing.ppm");
 temperature.sampledAt=previewNow;
 drawDashboard();assertDashboardRegion(false);assertFullDashboardMatchesCurrent();
 const float badTemperatures[]={-40.1f,125.1f,std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity()};
 for(float invalid:badTemperatures){
  temperature.celsius=invalid;drawDashboard();assertFullDashboardMatchesCurrent();
  assert(std::strcmp(dashboardSnapshot().temperatureValue,"-- °C")==0);
 }
 exampleReadings();drawDashboard();assertDashboardRegion(false);
 // Navigation and replaying the eye must never leave stale page decorations.
 selection.selected=PAGE_WARM;drawDashboard();assertDashboardRegion(false);
 selection.selected=PAGE_SOIL;drawDashboard();assertDashboardRegion(false);
 uint16_t beforeEye[240*240];std::copy(display.p,display.p+240*240,beforeEye);
 resetEye();drawEye(2200);drawDashboard();assertDashboardRegion(false);
 assert(std::equal(display.p,display.p+240*240,beforeEye));
 assertFullDashboardMatchesCurrent();
 std::cout<<"PASS: four pages, independent OFF/ON/offline states, sensor redraw and stale data\n";
 std::cout<<"PASS: 240x80 incremental sensors equal full frame; freshness, navigation and eye replay invalidate cache\n";
 std::cout<<"PASS: calibrated 0/50/100%, missing calibration, temperature -40/125/negative/invalid/stale\n";
}
