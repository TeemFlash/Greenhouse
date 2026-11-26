#pragma once
const float PI_F=3.14159265f;
void centered(const char *s,int y,int size=1) {
  canvas.setTextSize(size);canvas.setTextColor(1);
  canvas.setCursor((240-int(strlen(s))*6*size)/2,y);canvas.print(s);
}
void radial(float a,float r1,float r2,int cx=120,int cy=120) {
  canvas.drawLine(cx+int(cosf(a)*r1),cy+int(sinf(a)*r1),cx+int(cosf(a)*r2),cy+int(sinf(a)*r2),1);
}
void icon(int type,float t) {
  const int x=120,y=80;
  if(type==0) {
    int sway=int(sinf(t*2)*3);canvas.drawLine(x,y+16,x+sway,y-14,1);
    canvas.fillTriangle(x+sway,y,x-22+sway,y-16,x-4+sway,y-16,1);
    canvas.fillTriangle(x+sway,y+5,x+22+sway,y-13,x+5+sway,y-13,1);
    canvas.drawFastHLine(x-16,y+17,32,1);
  } else if(type==1) {
    canvas.drawCircle(x,y,23,1);
    for(int dx=-20;dx<=20;dx++) {
      int bottom=int(sqrtf(400-dx*dx));int surface=int(4*sinf(dx*.2f+t*3));
      int top=max(-bottom,surface);if(top<=bottom)canvas.drawFastVLine(x+dx,y+top,bottom-top+1,1);
    }
  } else if(type==2) {
    canvas.drawCircle(x,y,24,1);
    float rotation=control.out[FAN]?t*3:0;
    for(int i=0;i<3;i++){float a=rotation+i*2*PI_F/3;canvas.fillTriangle(x,y,x+int(cosf(a)*20),y+int(sinf(a)*20),x+int(cosf(a+.8f)*15),y+int(sinf(a+.8f)*15),1);}
    canvas.fillCircle(x,y,4,0);canvas.drawCircle(x,y,4,1);
  } else {
    int r=10+int(2*sinf(t*2));canvas.drawCircle(x,y,r,1);
    for(int i=0;i<8;i++)radial(i*PI_F/4+t*.2f,r+5,r+10,x,y);
  }
}
void present(uint16_t accent) {
  const uint8_t *bits=canvas.getBuffer();
  for(int y=0;y<240;y+=4) {
    for(int dy=0;dy<4;dy++)for(int x=0;x<240;x++) {
      int row=y+dy;bool on=bits[row*30+x/8]&(0x80>>(x&7));
      uint16_t color=(row>=110&&row<155&&x>35&&x<205)?0xFFFF:accent;
      pixels[dy*240+x]=on?color:0x0842;
    }
    display.drawRGBBitmap(0,y,pixels,240,4);yield();
  }
}
void drawUI(uint32_t now) {
  static uint32_t last=0;
  if(now-last<100)return;last=now;
  if(!canvas.getBuffer())return;
  if(!menu&&now-pageAt>=uint32_t(cfg.pageSec)*1000){page=(page+1)%6;pageAt=now;}
  canvas.fillScreen(0);canvas.drawCircle(120,120,116,1);
  uint16_t colors[]={0x4F33,0x05FF,0xB5FF,0xFE68,0xFFFF,0x07FF};
  if(menu) {
    Field f[40];int n=fields(editing?draft:cfg,f);
    centered(editing?"EDIT / OK TO SAVE":"SETTINGS",32);
    char pos[32];snprintf(pos,sizeof(pos),"%d / %d",menuItem+1,n+EXTRA_MENU);centered(pos,52);
    if(menuItem<n) {
      centered(f[menuItem].label,85);
      char v[20];snprintf(v,sizeof(v),"%d",*f[menuItem].value);centered(v,112,3);
      snprintf(v,sizeof(v),"%d .. %d",f[menuItem].lo,f[menuItem].hi);centered(v,150);
    } else {
      const char *actions[]={"RESUME","STOP ALL","ACK PUMP FAULT","TOGGLE PUMP","TOGGLE FANS","TOGGLE LAMP","TOGGLE STRIP","CAPTURE DRY","CAPTURE WET","CAPTURE EMPTY","CAPTURE FULL"};
      centered(actions[menuItem-n],112);
    }
    char msg[27];strlcpy(msg,notice,sizeof(msg));centered(msg,172);
    centered("1:- 2:+ 3:OK 4:BACK",193);
  } else if(page==4) {
    centered("RECENT EVENTS",35);
    int count=min(int(eventCount),5);
    for(int i=0;i<count;i++) {
      const Event &e=events[(eventHead+11-i)%12];char line[27];strlcpy(line,e.name,sizeof(line));centered(line,75+i*22);
    }
    centered(sdOK?"SD: OK":"SD: ERROR",195);
  } else if(page==5) {
    centered("NETWORK",32);
    centered(apName,62);
    centered("AP: 192.168.4.1",83);
    centered("USER: admin",104);
    centered("KEY / AP PASSWORD",123);
    centered(accessKey,142);
    centered(WiFi.status()==WL_CONNECTED?WiFi.localIP().toString().c_str():"WIFI NOT CONNECTED",167);
    centered(clockMinute()<0?"CLOCK NOT SET":"CLOCK SYNCED",189);
  } else {
    const char *titles[]={"SOIL MOISTURE","WATER LEVEL","AIR CLIMATE","LIGHTING"};
    centered(titles[page],30);
    int channel=page==0||page==1?PUMP:page==2?FAN:LAMP;
    int mode=control.overrideMode[channel]>=0?MANUAL:cfg.mode[channel];
    centered(page==3?"LAMP / STRIP":mode==AUTO?"AUTO":mode==MANUAL?"MANUAL":"OFF",46);
    float v=NAN;
    if(page==0&&readings.soil&&now-readings.soilAt<3000)v=readings.soilPct;
    if(page==1&&readings.water&&now-readings.waterAt<3000)v=readings.waterPct;
    if(page==2&&readings.air&&now-readings.airAt<5000)v=readings.temperature;
    float amount=isfinite(v)?constrain(page==2?v/40:v/100,0.0f,1.0f):0;
    if(page==3)amount=(control.out[LAMP]+control.out[STRIP])/2.0f;
    static float gauge[4]={};gauge[page]+=(amount-gauge[page])*.25f;amount=gauge[page];
    for(int i=0;i<=60;i++)radial((135+270.0f*i/60)*PI_F/180,i<=int(amount*60)?103:110,112);
    icon(page,(now%60000)/1000.0f);
    char val[24];
    if(page==3)snprintf(val,sizeof(val),"%d / 2",int(control.out[LAMP])+int(control.out[STRIP]));
    else if(isfinite(v))snprintf(val,sizeof(val),page==2?"%.1f":"%.0f",v);
    else strlcpy(val,"--",sizeof(val));
    centered(val,113,page==2||page==3?3:4);
    if(page==2) {
      if(readings.air&&now-readings.airAt<5000)snprintf(val,sizeof(val),"C / RH %.0f%%",readings.humidity);else strlcpy(val,"AIR SENSOR ERROR",sizeof(val));
    } else strlcpy(val,page==3?"LIGHT CHANNELS ON":"PERCENT",sizeof(val));
    centered(val,150);
    char warning[28];strlcpy(warning,problem(),sizeof(warning));centered(warning,169);
    snprintf(val,sizeof(val),"P:%d F:%d L:%d S:%d",control.out[0],control.out[1],control.out[2],control.out[3]);centered(val,188);
  }
  if(!menu)for(int i=0;i<6;i++){if(i==page)canvas.fillCircle(85+i*14,210,3,1);else canvas.drawCircle(85+i*14,210,2,1);}
  present(control.paused?0xFD20:colors[page]);
}
void keyPress(int k,uint32_t now) {
  pageAt=now;
  if(!menu) {
    if(k==0)page=(page+5)%6;
    if(k==1)page=(page+1)%6;
    if(k==2){menu=true;menuItem=0;editing=false;}
    return;
  }
  Field f[40];int n=fields(editing?draft:cfg,f);
  if(k==3){if(editing)editing=false;else menu=false;return;}
  if(k==0||k==1) {
    if(editing) {*f[menuItem].value=constrain(*f[menuItem].value+(k==1?1:-1)*f[menuItem].step,f[menuItem].lo,f[menuItem].hi);}
    else menuItem=(menuItem+(k==1?1:-1)+n+EXTRA_MENU)%(n+EXTRA_MENU);
    return;
  }
  if(k!=2)return;
  if(menuItem<n) {
    if(!editing){draft=cfg;editing=true;return;}
    if(!valid(draft)){strlcpy(notice,"INVALID LIMITS",sizeof(notice));return;}
    if(draft.soilDry!=cfg.soilDry)draft.calMask|=1;
    if(draft.soilWet!=cfg.soilWet)draft.calMask|=2;
    if(draft.waterEmpty!=cfg.waterEmpty)draft.calMask|=4;
    if(draft.waterFull!=cfg.waterFull)draft.calMask|=8;
    stopSystem();cfg=draft;saveSettings();setClock();editing=false;
  } else {
    int a=menuItem-n;bool ok=true;
    if(a==0)ok=resumeSystem();
    else if(a==1)stopSystem();
    else if(a==2){control.acknowledge();event("PUMP FAULT ACK");}
    else if(a<=6)ok=manualCommand(a-3,!control.out[a-3]);
    else ok=capture(a-7);
    strlcpy(notice,ok?"DONE":"BLOCKED / CHECK",sizeof(notice));
  }
}
void buttonsTick(uint32_t now) {
  static uint32_t last=0,change[4]={},pressedAt[4]={};
  static bool raw[4]={},stable[4]={},armed[4]={},held[4]={},accepted[4]={};
  if(now-last<25)return;last=now;
  if(Wire.requestFrom(uint8_t(0x20),uint8_t(2))!=2) {
    if(pcfOK)event("PCF READ ERROR");pcfOK=false;control.ioFault=true;control.stop();return;
  }
  uint16_t bits=Wire.read();bits|=uint16_t(Wire.read())<<8;
  for(int i=0;i<4;i++) {
    bool active=bool(bits&(1<<(8+i)))==bool(cfg.touchHigh);
    if(!active)armed[i]=true;
    if(active!=raw[i]){raw[i]=active;change[i]=now;}
    if(raw[i]!=stable[i]&&now-change[i]>=50) {
      stable[i]=raw[i];
      if(stable[i]){pressedAt[i]=now;held[i]=false;accepted[i]=armed[i];}
      else if(accepted[i]&&!held[i])keyPress(i,now);
    }
    if(i==3&&armed[i]&&stable[i]&&!held[i]&&now-pressedAt[i]>=1500) {
      stopSystem();held[i]=true;menu=false;editing=false;strlcpy(notice,"STOP ALL",sizeof(notice));
    }
  }
}
