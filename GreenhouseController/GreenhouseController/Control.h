#pragma once
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum Mode : uint8_t { OFF, MANUAL, AUTO };
enum Channel : uint8_t { PUMP, FAN, LAMP, STRIP };
struct Settings {
  int mode[4] = {OFF, OFF, OFF, OFF};
  int soilLow=35, soilTarget=55, waterMin=15;
  int pulseSec=5, soakSec=60, cycles=3, maxPumpSec=20, intervalMin=60;
  int tempOn=30, tempOff=28, humOn=80, humOff=75;
  int ventEveryMin=0, ventSec=60, fanFallback=0;
  int lightStart[2]={480,480}, lightEnd[2]={1200,1200};
  int noTimeLight[2]={0,0};
  int manualSec=300, logSec=60, pageSec=8, timezoneMin=300, bootAuto=0;
  int soilDry=0, soilWet=0, waterEmpty=0, waterFull=0;
  int calMask=0, touchHigh=1;
};
struct Field {const char *key; const char *label; int *value; int lo,hi,step;};
inline int fields(Settings &s, Field *f) {
  int n=0;
#define GH_FIELD(key,label,var,lo,hi,step) f[n++]={key,label,&s.var,lo,hi,step}
  GH_FIELD("pumpMode","Pump mode",mode[0],0,2,1); GH_FIELD("fanMode","Fan mode",mode[1],0,2,1);
  GH_FIELD("lampMode","Lamp mode",mode[2],0,2,1); GH_FIELD("stripMode","Strip mode",mode[3],0,2,1);
  GH_FIELD("soilLow","Soil start %",soilLow,0,95,1); GH_FIELD("soilTarget","Soil target %",soilTarget,1,100,1);
  GH_FIELD("waterMin","Water min %",waterMin,1,90,1);
  GH_FIELD("pulseSec","Pump pulse sec",pulseSec,1,30,1); GH_FIELD("soakSec","Soak sec",soakSec,10,600,10);
  GH_FIELD("cycles","Max cycles",cycles,1,10,1); GH_FIELD("maxPumpSec","Pump total sec",maxPumpSec,1,120,1);
  GH_FIELD("intervalMin","Water gap min",intervalMin,1,1440,1);
  GH_FIELD("tempOn","Fan on C",tempOn,5,60,1); GH_FIELD("tempOff","Fan off C",tempOff,0,59,1);
  GH_FIELD("humOn","Fan on RH %",humOn,10,100,1); GH_FIELD("humOff","Fan off RH %",humOff,0,99,1);
  GH_FIELD("ventEveryMin","Vent gap min",ventEveryMin,0,1440,1); GH_FIELD("ventSec","Vent sec",ventSec,1,600,1);
  GH_FIELD("fanFallback","Fan fault on",fanFallback,0,1,1);
  GH_FIELD("lampStart","Lamp start min",lightStart[0],0,1439,15); GH_FIELD("lampEnd","Lamp end min",lightEnd[0],0,1439,15);
  GH_FIELD("stripStart","Strip start min",lightStart[1],0,1439,15); GH_FIELD("stripEnd","Strip end min",lightEnd[1],0,1439,15);
  GH_FIELD("lampFallback","Lamp no clock",noTimeLight[0],0,1,1); GH_FIELD("stripFallback","Strip no clock",noTimeLight[1],0,1,1);
  GH_FIELD("manualSec","Manual timeout",manualSec,5,3600,5); GH_FIELD("logSec","Log every sec",logSec,5,3600,5);
  GH_FIELD("pageSec","Page every sec",pageSec,3,60,1); GH_FIELD("timezoneMin","UTC offset min",timezoneMin,-720,840,15);
  GH_FIELD("bootAuto","Auto on boot",bootAuto,0,1,1);
  GH_FIELD("soilDry","Dry ADC",soilDry,0,26400,100); GH_FIELD("soilWet","Wet ADC",soilWet,0,26400,100);
  GH_FIELD("waterEmpty","Empty ADC",waterEmpty,0,26400,100); GH_FIELD("waterFull","Full ADC",waterFull,0,26400,100);
  GH_FIELD("touchHigh","Touch high",touchHigh,0,1,1);
#undef GH_FIELD
  return n;
}
inline bool valid(Settings &s) {
  Field f[40]; int n=fields(s,f);
  for(int i=0;i<n;i++) if(*f[i].value<f[i].lo || *f[i].value>f[i].hi) return false;
  return s.soilLow<s.soilTarget && s.tempOff<s.tempOn && s.humOff<s.humOn &&
    s.pulseSec<=s.maxPumpSec && (!s.ventEveryMin || s.ventSec<=s.ventEveryMin*60) && s.calMask>=0 && s.calMask<=15;
}
inline bool soilCal(const Settings &s) {return (s.calMask&3)==3 && abs(s.soilWet-s.soilDry)>=500;}
inline bool waterCal(const Settings &s) {return (s.calMask&12)==12 && abs(s.waterFull-s.waterEmpty)>=500;}
inline float percent(int raw,int zero,int full) {
  if(zero==full) return NAN;
  float v=100.0f*(raw-zero)/(full-zero); return v<0?0:(v>100?100:v);
}
inline bool inSchedule(int minute,int start,int end) {
  if(start==end) return false;
  return start<end ? minute>=start && minute<end : minute>=start || minute<end;
}
struct Readings {
  bool air=false, soil=false, water=false;
  float temperature=NAN, humidity=NAN, soilPct=NAN, waterPct=NAN;
  int soilRaw=0,waterRaw=0;
  uint32_t airAt=0, soilAt=0, waterAt=0;
};
inline bool freshSoil(const Readings &r,uint32_t now) {
  return r.soil&&uint32_t(now-r.soilAt)<3000&&isfinite(r.soilPct)&&r.soilPct>=0&&r.soilPct<=100;
}
inline bool freshWater(const Readings &r,uint32_t now) {
  return r.water&&uint32_t(now-r.waterAt)<3000&&isfinite(r.waterPct)&&r.waterPct>=0&&r.waterPct<=100;
}
inline bool freshAir(const Readings &r,uint32_t now) {
  return r.air&&uint32_t(now-r.airAt)<5000&&isfinite(r.temperature)&&isfinite(r.humidity)
    &&r.temperature>=-40&&r.temperature<=125&&r.humidity>=0&&r.humidity<=100;
}
inline uint16_t outputMask(const bool out[4]) {
  uint16_t mask=0xFFC3;
  if(out[LAMP])mask&=~1;
  if(out[STRIP])mask&=~2;
  if(out[PUMP])mask|=1<<2;
  if(out[FAN])mask|=1<<4;
  return mask;
}
struct Controller {
  bool paused=true, ioFault=false, pumpFault=false, noResponse=false, out[4]={};
  bool manual[4]={}, fanTemp=false, fanHum=false;
  int overrideMode[4]={-1,-1,-1,-1};
  uint32_t manualAt[4]={}, pulseAt=0, soakAt=0, lastWater=0;
  uint32_t usedMs=0, lastTick=0;
  bool watered=false, cycleActive=false, soaking=false;
  int pulses=0;
  float before=NAN;
  void stop() {paused=true; cancelWater(); for(int i=0;i<4;i++){out[i]=false;manual[i]=false;overrideMode[i]=-1;} }
  void cancelWater() {cycleActive=false;soaking=false;out[PUMP]=false;}
  void acknowledge() {pumpFault=false;noResponse=false;}
  void setManual(int c,bool on,uint32_t now) {
    if(c==PUMP) cancelWater();
    overrideMode[c]=MANUAL; manual[c]=on;manualAt[c]=now;
  }
  bool waterReady(const Settings &s,const Readings &r,uint32_t now) const {
    return waterCal(s)&&freshWater(r,now)&&r.waterPct>=s.waterMin;
  }
  void finish(uint32_t now) {cancelWater();lastWater=now;watered=true;}
  void tick(const Settings &s,const Readings &r,uint32_t now,bool ioOK,int minute) {
    uint32_t dt=now-lastTick;lastTick=now;
    if(cycleActive&&out[PUMP]) usedMs+=dt;
    if(!ioOK) {ioFault=true;stop();return;}
    if(paused||ioFault) {for(bool &o:out)o=false;return;}
    int m[4];
    for(int i=0;i<4;i++) {
      if(overrideMode[i]>=0 && uint32_t(now-manualAt[i])>=uint32_t(s.manualSec)*1000) {if(i==PUMP && manual[i])finish(now);overrideMode[i]=-1;manual[i]=false;}
      m[i]=overrideMode[i]>=0?overrideMode[i]:s.mode[i];
      out[i]=(m[i]==MANUAL && manual[i]);
    }
    bool wr=waterReady(s,r,now);
    bool sr=soilCal(s)&&freshSoil(r,now);
    if(m[PUMP]!=AUTO) {
      cycleActive=false;soaking=false;
      if(!wr||pumpFault) {out[PUMP]=false;manual[PUMP]=false;}
      if(out[PUMP]&&uint32_t(now-manualAt[PUMP])>=uint32_t(s.pulseSec)*1000) {
        out[PUMP]=false;manual[PUMP]=false;lastWater=now;watered=true;
      }
    } else {
      out[PUMP]=false;
      if(!wr||!sr||pumpFault) {if(cycleActive)finish(now);}
      else if(cycleActive) {
        if(soaking) {
          if(uint32_t(now-soakAt)>=uint32_t(s.soakSec)*1000) {
            if(r.soilPct>=s.soilTarget) finish(now);
            else if(pulses>=s.cycles||usedMs>=uint32_t(s.maxPumpSec)*1000) {pumpFault=true;noResponse=r.soilPct<before+2;finish(now);}
            else {soaking=false;pulseAt=now;pulses++;out[PUMP]=true;}
          }
        } else if(uint32_t(now-pulseAt)>=uint32_t(s.pulseSec)*1000||usedMs>=uint32_t(s.maxPumpSec)*1000) {
          soaking=true;soakAt=now;
        } else out[PUMP]=true;
      } else if(r.soilPct<s.soilLow&&(!watered||uint32_t(now-lastWater)>=uint32_t(s.intervalMin)*60000)) {
        cycleActive=true;soaking=false;pulses=1;usedMs=0;before=r.soilPct;pulseAt=now;out[PUMP]=true;
      }
    }
    if(m[FAN]==AUTO) {
      if(!freshAir(r,now)) {out[FAN]=s.fanFallback;fanTemp=fanHum=false;}
      else {
        if(r.temperature>=s.tempOn)fanTemp=true;else if(r.temperature<=s.tempOff)fanTemp=false;
        if(r.humidity>=s.humOn)fanHum=true;else if(r.humidity<=s.humOff)fanHum=false;
        uint32_t period=uint32_t(s.ventEveryMin)*60000;
        bool periodic=period && (now%period)<uint32_t(s.ventSec)*1000;
        out[FAN]=fanTemp||fanHum||periodic;
      }
    }
    for(int i=0;i<2;i++) if(m[i+2]==AUTO)
      out[i+2]=minute<0?s.noTimeLight[i]:inSchedule(minute,s.lightStart[i],s.lightEnd[i]);
  }
};
