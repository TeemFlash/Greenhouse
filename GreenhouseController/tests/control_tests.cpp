#include <cassert>
#include <iostream>
#include "../GreenhouseController/Control.h"
static void fresh(Readings &r,uint32_t t){r.waterAt=r.soilAt=r.airAt=t;}
static Settings configured(){Settings s;s.calMask=15;s.soilDry=20000;s.soilWet=8000;s.waterEmpty=100;s.waterFull=20000;s.mode[0]=AUTO;return s;}
static Readings good(){Readings r;r.air=r.water=r.soil=true;r.waterPct=80;r.soilPct=20;r.temperature=26;r.humidity=60;return r;}
int main(){
  Settings s=configured();Readings r=good();Controller c;c.paused=false;
  fresh(r,100);c.tick(s,r,100,true,-1);assert(c.out[PUMP]);
  fresh(r,5100);c.tick(s,r,5100,true,-1);assert(!c.out[PUMP]&&c.soaking);
  fresh(r,65100);c.tick(s,r,65100,true,-1);assert(c.out[PUMP]&&c.pulses==2);
  r.waterPct=0;fresh(r,65200);c.tick(s,r,65200,true,-1);assert(!c.out[PUMP]&&!c.cycleActive);
  r.waterPct=80;fresh(r,65300);c.tick(s,r,65300,true,-1);assert(!c.out[PUMP]); // cooldown
  c=Controller{};c.paused=false;s.calMask=0;fresh(r,100);c.tick(s,r,100,true,-1);assert(!c.out[PUMP]);
  s=configured();c.tick(s,r,5000,true,-1);assert(!c.out[PUMP]); // stale
  c=Controller{};c.paused=false;fresh(r,100);c.tick(s,r,100,true,-1);assert(c.out[PUMP]);
  fresh(r,110);c.tick(s,r,110,false,-1);assert(c.paused&&c.ioFault&&!c.out[PUMP]);
  fresh(r,200);c.tick(s,r,200,true,-1);assert(!c.out[PUMP]); // latched
  c=Controller{};c.paused=false;s.cycles=1;fresh(r,100);c.tick(s,r,100,true,-1);
  fresh(r,5100);c.tick(s,r,5100,true,-1);fresh(r,65100);c.tick(s,r,65100,true,-1);
  assert(c.pumpFault&&!c.out[PUMP]);
  c=Controller{};c.paused=false;s=configured();s.maxPumpSec=s.pulseSec=5;
  fresh(r,100);c.tick(s,r,100,true,-1);fresh(r,5100);c.tick(s,r,5100,true,-1);
  fresh(r,65100);c.tick(s,r,65100,true,-1);assert(c.pumpFault&&!c.out[PUMP]);
  c=Controller{};c.paused=false;s=configured();fresh(r,100);c.tick(s,r,100,true,-1);
  fresh(r,5100);c.tick(s,r,5100,true,-1);r.soilPct=60;fresh(r,65100);c.tick(s,r,65100,true,-1);
  assert(!c.cycleActive&&!c.pumpFault&&!c.out[PUMP]);
  c=Controller{};c.paused=false;s.mode[PUMP]=OFF;c.setManual(PUMP,true,100);fresh(r,100);c.tick(s,r,100,true,-1);assert(c.out[PUMP]);
  fresh(r,5100);c.tick(s,r,5100,true,-1);assert(!c.out[PUMP]);
  c=Controller{};c.paused=false;s.mode[FAN]=AUTO;r.temperature=31;r.humidity=70;fresh(r,100);c.tick(s,r,100,true,-1);assert(c.out[FAN]);
  r.temperature=29;fresh(r,200);c.tick(s,r,200,true,-1);assert(c.out[FAN]);
  r.temperature=27;r.humidity=90;fresh(r,300);c.tick(s,r,300,true,-1);assert(c.out[FAN]);
  r.humidity=70;fresh(r,400);c.tick(s,r,400,true,-1);assert(!c.out[FAN]);
  r.air=false;s.fanFallback=1;c.tick(s,r,500,true,-1);assert(c.out[FAN]);
  s.mode[LAMP]=AUTO;s.lightStart[0]=1320;s.lightEnd[0]=360;c.tick(s,r,600,true,0);assert(c.out[LAMP]);
  c.tick(s,r,700,true,720);assert(!c.out[LAMP]);c.tick(s,r,800,true,-1);assert(!c.out[LAMP]);
  s.noTimeLight[0]=1;c.tick(s,r,900,true,-1);assert(c.out[LAMP]);
  c.setManual(LAMP,false,1000);c.tick(s,r,1100,true,-1);assert(!c.out[LAMP]);
  c.tick(s,r,1000+s.manualSec*1000,true,-1);assert(c.out[LAMP]);
  assert(!inSchedule(480,480,480));assert(inSchedule(480,480,600));assert(!inSchedule(600,480,600));
  assert(percent(14000,20000,8000)==50);assert(percent(500,100,200)==100);
  Settings invalid=s;invalid.soilLow=90;invalid.soilTarget=50;assert(!valid(invalid));
  invalid=s;invalid.pulseSec=30;invalid.maxPumpSec=5;assert(!valid(invalid));
  c=Controller{};c.paused=false;s=configured();uint32_t nearWrap=0xffffff00U;fresh(r,nearWrap);r.soilPct=10;r.waterPct=80;
  c.tick(s,r,nearWrap,true,-1);assert(c.out[PUMP]);fresh(r,nearWrap+5000);c.tick(s,r,nearWrap+5000,true,-1);assert(!c.out[PUMP]);
  c.stop();for(bool on:c.out)assert(!on);
  // A manual override expiring into AUTO must not extend a running pulse.
  c=Controller{};c.paused=false;s=configured();s.manualSec=5;s.pulseSec=10;s.maxPumpSec=20;
  c.setManual(PUMP,true,100);fresh(r,100);c.tick(s,r,100,true,-1);assert(c.out[PUMP]);
  fresh(r,5100);c.tick(s,r,5100,true,-1);assert(!c.out[PUMP]&&c.watered);
  // Invalid numeric values must never grant permission to irrigate.
  for(float bad : {NAN, INFINITY, -1.0f, 101.0f}) {
    s=configured();r=good();fresh(r,100);r.waterPct=bad;
    c=Controller{};c.paused=false;c.tick(s,r,100,true,0);assert(!c.out[PUMP]);
    c.setManual(PUMP,true,100);c.tick(s,r,100,true,0);assert(!c.out[PUMP]);
    r=good();fresh(r,100);r.soilPct=bad;
    c=Controller{};c.paused=false;c.tick(s,r,100,true,0);assert(!c.out[PUMP]);
  }
  // Manual watering needs water calibration but not soil calibration.
  s=configured();s.calMask=12;r=good();r.soil=false;fresh(r,100);
  c=Controller{};c.paused=false;c.setManual(PUMP,true,100);
  c.tick(s,r,100,true,0);assert(c.out[PUMP]);
  r.waterPct=0;fresh(r,200);c.tick(s,r,200,true,0);assert(!c.out[PUMP]);
  r.waterPct=80;fresh(r,300);c.tick(s,r,300,true,0);assert(!c.out[PUMP]);
  // Periodic ventilation, missing-air fallback, independent light schedules.
  s=Settings{};s.mode[FAN]=AUTO;s.ventEveryMin=10;s.ventSec=60;
  s.mode[LAMP]=s.mode[STRIP]=AUTO;s.lightStart[1]=1200;s.lightEnd[1]=480;
  r=good();c=Controller{};c.paused=false;fresh(r,100);
  c.tick(s,r,100,true,600);assert(c.out[FAN]&&c.out[LAMP]&&!c.out[STRIP]);
  fresh(r,60100);c.tick(s,r,60100,true,1300);assert(!c.out[FAN]&&!c.out[LAMP]&&c.out[STRIP]);
  fresh(r,600100);c.tick(s,r,600100,true,1300);assert(c.out[FAN]);
  r.temperature=NAN;s.fanFallback=0;c.tick(s,r,600101,true,0);assert(!c.out[FAN]);
  s.fanFallback=1;c.tick(s,r,600102,true,0);assert(c.out[FAN]);
  // All combinations preserve input pins and correct active-low relay polarity.
  for(int bits=0;bits<16;bits++){
    bool outputs[4];for(int i=0;i<4;i++)outputs[i]=bits&(1<<i);
    uint16_t mask=outputMask(outputs);
    assert((mask&0xFFC0)==0xFFC0);
    assert(bool(mask&1)==!outputs[LAMP]&&bool(mask&2)==!outputs[STRIP]);
    assert(bool(mask&4)==outputs[PUMP]&&!(mask&8));
    assert(bool(mask&16)==outputs[FAN]&&!(mask&32));
  }
  // Freshness boundaries, including uint32_t rollover.
  r=good();fresh(r,0xffffff00U);
  assert(freshWater(r,0xffffff00U+2999U));
  assert(!freshWater(r,0xffffff00U+3000U));
  assert(freshAir(r,0xffffff00U+4999U));
  assert(!freshAir(r,0xffffff00U+5000U));
  // Randomized interleaving of failures and readings; invariant applies to every tick.
  uint32_t seed=12345,now=0; c=Controller{};c.paused=false;s=configured();
  for(int i=0;i<20000;i++) {
    seed=seed*1664525U+1013904223U;now+=seed%1000+1;
    r=good();fresh(r,now);bool io=(seed%31)!=0;
    if(seed%7==0)r.waterPct=0;
    if(seed%11==0)r.water=false;
    if(seed%13==0)r.soil=false;
    if(seed%17==0)r.waterAt=now-4000;
    if(seed%19==0)c.paused=true;
    if(seed%23==0&&io){c.ioFault=false;c.paused=false;}
    c.tick(s,r,now,io,-1);
    if(c.out[PUMP])assert(io&&!c.paused&&!c.ioFault&&!c.pumpFault&&r.soil&&c.waterReady(s,r,now));
    if(c.paused||c.ioFault)for(bool on:c.out)assert(!on);
  }
  std::cout<<"PASS: calibration, stale readings, water cutoff, pulse/total/cycle limits, cooldown, IO latch, fan hysteresis, schedules, manual timeout, millis wrap\n";
}
