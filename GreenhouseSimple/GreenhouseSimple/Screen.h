#pragma once
#include <stdio.h>
#include "Eye.h"
#include "Font.h"

inline uint16_t nextCode(const char *&p) {
  uint8_t a=uint8_t(*p++);
  if(a<128)return a;
  if((a&0xE0)==0xC0&&*p){uint8_t b=uint8_t(*p++);return ((a&31)<<6)|(b&63);}
  if((a&0xF0)==0xE0&&p[0]&&p[1]){uint8_t b=uint8_t(*p++),c=uint8_t(*p++);return ((a&15)<<12)|((b&63)<<6)|(c&63);}
  return '?';
}
inline int glyphIndex(uint16_t code) {
  for(unsigned i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++)if(pgm_read_word(&glyphs[i].code)==code)return i;
  return -1;
}
inline int textWidth(const char *s) {
  int width=0;while(*s){int i=glyphIndex(nextCode(s));width+=i<0?7:pgm_read_byte(&glyphs[i].width);}return width;
}
static bool eyeDrawn=false;
static int previousGazeX=0,previousGazeY=0;
static bool dashboardDrawn=false,dashboardSoilFresh=false,dashboardTemperatureFresh=false;
static int dashboardPage=-1;
alignas(4) static uint16_t transferPixels[240*8];
void resetDashboard(){dashboardDrawn=false;}
void resetEye(){eyeDrawn=false;resetDashboard();}
bool drawEye(uint32_t elapsed) {
  resetDashboard();
  prepareEyePalette();
  int gx,gy;eyeGaze(elapsed,gx,gy);
  if(eyeDrawn&&gx==previousGazeX&&gy==previousGazeY)return false;
  int left=0,top=0,right=239,bottom=239;
  if(eyeDrawn){
    left=120+(gx<previousGazeX?gx:previousGazeX)/16-72;
    right=120+(gx>previousGazeX?gx:previousGazeX)/16+72;
    top=120+(gy<previousGazeY?gy:previousGazeY)/16-72;
    bottom=120+(gy>previousGazeY?gy:previousGazeY)/16+72;
  }
  int width=right-left+1;
  display.beginRegion(left,top,width,bottom-top+1);
  for(int y=top;y<=bottom;y+=8){
    int rows=bottom-y+1;if(rows>8)rows=8;
    for(int j=0;j<rows;j++)for(int x=left;x<=right;x++)
      transferPixels[j*width+x-left]=eyePixelFixed(x,y+j,gx,gy);
    display.pushRegion(transferPixels,width*rows);
  }
  // Keep one frame contiguous. loop() yields after it; frames take tens of
  // milliseconds, well below the watchdog interval, and Wi-Fi is disabled.
  display.endRegion();
  previousGazeX=gx;previousGazeY=gy;eyeDrawn=true;
  return true;
}

// The dashboard shares the eye's 3.75 KiB transfer buffer. Every strip is
// composed completely before transmission; there is no blank erase frame,
// allocation, or 115 KiB framebuffer when sensor readings update.
struct DashboardStrip {
  int top,rows;
  uint16_t *pixels;

  void pixel(int x,int y,uint16_t color) {
    if(x>=0&&x<240&&y>=top&&y<top+rows)pixels[(y-top)*240+x]=color;
  }
  uint16_t get(int x,int y) const {return pixels[(y-top)*240+x];}
  void rect(int x,int y,int w,int h,uint16_t color) {
    int left=x<0?0:x,right=x+w>240?240:x+w;
    int first=y<top?top:y,last=y+h>top+rows?top+rows:y+h;
    for(int yy=first;yy<last;yy++)for(int xx=left;xx<right;xx++)pixel(xx,yy,color);
  }
  void circle(int x,int y,int radius,uint16_t color,int inner=-1) {
    int first=y-radius<top?top:y-radius,last=y+radius>=top+rows?top+rows-1:y+radius;
    for(int yy=first;yy<=last;yy++)for(int xx=x-radius;xx<=x+radius;xx++){
      int d=(xx-x)*(xx-x)+(yy-y)*(yy-y);
      if(d<=radius*radius&&(inner<0||d>=inner*inner))pixel(xx,yy,color);
    }
  }
  void glow(int x,int y,int radius,uint16_t color,int strength) {
    int first=y-radius<top?top:y-radius,last=y+radius>=top+rows?top+rows-1:y+radius;
    for(int yy=first;yy<=last;yy++)for(int xx=x-radius;xx<=x+radius;xx++){
      int d=(xx-x)*(xx-x)+(yy-y)*(yy-y);
      if(xx>=0&&xx<240&&d<radius*radius){
        int weight=(radius*radius-d)*strength/(radius*radius);
        pixel(xx,yy,mix565(get(xx,yy),color,weight));
      }
    }
  }
  void line(int x0,int y0,int x1,int y1,uint16_t color,int thickness=1) {
    int dx=x1>x0?x1-x0:x0-x1,sx=x0<x1?1:-1;
    int dy=y1>y0?y0-y1:y1-y0,sy=y0<y1?1:-1,error=dx+dy;
    for(;;){
      if(thickness==1)pixel(x0,y0,color);
      else circle(x0,y0,thickness-1,color);
      if(x0==x1&&y0==y1)break;
      int twice=2*error;
      if(twice>=dy){error+=dy;x0+=sx;}
      if(twice<=dx){error+=dx;y0+=sy;}
    }
  }
  void text(const char *s,int x,int y,uint16_t color,int scale=1) {
    if(y+16*scale<=top||y>=top+rows)return;
    while(*s){
      int i=glyphIndex(nextCode(s));
      if(i<0){x+=7*scale;continue;}
      int width=pgm_read_byte(&glyphs[i].width);
      for(int yy=0;yy<16;yy++){
        if(y+(yy+1)*scale<=top||y+yy*scale>=top+rows)continue;
        uint16_t bits=pgm_read_word(&glyphs[i].rows[yy]);
        for(int xx=0;xx<width;xx++)if(bits&(0x8000>>xx))
          rect(x+xx*scale,y+yy*scale,scale,scale,color);
      }
      x+=width*scale;
    }
  }
  void centered(const char *s,int y,uint16_t color,int scale=1) {
    text(s,(240-textWidth(s)*scale)/2,y,color,scale);
  }
};

inline bool bulbInside(int x,int y) {
  int dx=x-120,dy=y-102;
  if(y<=117)return dx*dx+dy*dy<=30*30;
  return y<=135&&dx>=-(26-(y-117)*12/18)&&dx<=26-(y-117)*12/18;
}

void drawBulb(DashboardStrip &canvas,bool ultraviolet,bool on,uint16_t accent) {
  const uint16_t base=rgb565(12,17,25);
  canvas.glow(120,107,57,accent,on?65:13);
  for(int y=canvas.top;y<canvas.top+canvas.rows;y++)for(int x=87;x<=153;x++){
    if(!bulbInside(x,y))continue;
    bool edge=!bulbInside(x-2,y)||!bulbInside(x+2,y)||
              !bulbInside(x,y-2)||!bulbInside(x,y+2);
    uint16_t color;
    if(on){
      int weight=(y-72)*170/64;if(weight<0)weight=0;if(weight>170)weight=170;
      color=ultraviolet?mix565(rgb565(213,163,255),rgb565(121,39,231),weight):
                        mix565(rgb565(255,235,135),rgb565(255,170,36),weight);
      if(edge)color=accent;
    }else color=edge?mix565(base,accent,154):mix565(base,accent,12);
    canvas.pixel(x,y,color);
  }
  uint16_t detail=on?(ultraviolet?rgb565(83,26,137):rgb565(153,92,18)):
                     mix565(base,accent,140);
  if(ultraviolet){
    canvas.line(110,115,116,121,detail,2);
    canvas.line(116,121,123,115,detail,2);
    canvas.line(123,115,130,121,detail,2);
  }else{
    canvas.line(108,107,115,115,detail,2);
    canvas.line(115,115,120,108,detail,2);
    canvas.line(120,108,125,115,detail,2);
    canvas.line(125,115,132,107,detail,2);
    canvas.line(114,115,118,135,detail);
    canvas.line(126,115,122,135,detail);
  }
  canvas.line(106,139,134,139,on?accent:detail,2);
  canvas.line(108,145,132,145,on?accent:detail,2);
  canvas.line(114,150,126,150,on?accent:detail,2);
  if(on){
    canvas.line(120,59,120,65,accent,2);
    canvas.line(82,73,87,78,accent,2);
    canvas.line(153,78,158,73,accent,2);
    canvas.line(72,101,79,101,accent,2);
    canvas.line(161,101,168,101,accent,2);
    canvas.line(105,85,109,82,ultraviolet?rgb565(239,216,255):rgb565(255,251,220),2);
  }
}

inline bool fanBlade(int dx,int dy) {
  for(int rotation=0;rotation<4;rotation++){
    int u=dx-12,v=dy+21;
    if(dy<-1&&u*u*24*24+v*v*15*15<=15*15*24*24)return true;
    int old=dx;dx=-dy;dy=old;
  }
  return false;
}

void drawFan(DashboardStrip &canvas,bool on,uint16_t accent) {
  const uint16_t base=rgb565(12,17,25);
  canvas.glow(120,109,57,accent,on?42:8);
  canvas.circle(120,109,48,mix565(base,accent,on?68:34),47);
  for(int y=canvas.top;y<canvas.top+canvas.rows;y++)for(int x=74;x<=166;x++){
    int dx=x-120,dy=y-109;
    if(!fanBlade(dx,dy))continue;
    bool edge=!fanBlade(dx-1,dy)||!fanBlade(dx+1,dy)||
              !fanBlade(dx,dy-1)||!fanBlade(dx,dy+1);
    uint16_t color=on?mix565(rgb565(112,255,225),rgb565(19,148,135),(y-65)*2):
                       mix565(base,accent,edge?105:24);
    canvas.pixel(x,y,color);
  }
  canvas.circle(120,109,12,base);
  canvas.circle(120,109,11,on?accent:mix565(base,accent,110),9);
  canvas.circle(120,109,5,on?rgb565(199,255,244):mix565(base,accent,65));
}

void drawSprout(DashboardStrip &canvas,bool valid,uint16_t accent) {
  uint16_t leaf=valid?accent:rgb565(59,101,86);
  canvas.glow(120,88,42,accent,valid?25:7);
  canvas.line(120,110,120,84,leaf,2);
  canvas.line(120,98,105,86,leaf,2);
  canvas.line(120,90,133,78,leaf,2);
  for(int y=canvas.top;y<canvas.top+canvas.rows;y++)for(int x=91;x<=149;x++){
    int dx=x-105,dy=y-82,u=dx-dy,v=dx+dy;
    bool left=u*u*25+v*v*144<=144*25;
    dx=x-133;dy=y-76;u=dx+dy;v=dx-dy;
    bool right=u*u*25+v*v*144<=144*25;
    if(left||right)canvas.pixel(x,y,leaf);
  }
  canvas.line(103,112,137,112,mix565(rgb565(12,17,25),leaf,140),2);
  canvas.line(110,118,130,118,mix565(rgb565(12,17,25),leaf,80));
  uint16_t water=valid?rgb565(94,180,228):rgb565(54,81,100);
  for(int y=canvas.top;y<canvas.top+canvas.rows;y++)for(int x=149;x<=161;x++){
    int dx=x-155,dy=y-101;
    if((dy<=0&&dy>=-9&&dx*dx<=(dy+9)*(dy+9)/2)||
       (dy>0&&dx*dx+dy*dy<=36))canvas.pixel(x,y,water);
  }
}

struct DashboardView {
  int page;
  bool on,connected,soilFresh,temperatureFresh;
  uint16_t accent;
  char value[20],temperatureValue[24];
};

void drawThermometer(DashboardStrip &canvas,int x,int y,bool valid) {
  const uint16_t background=rgb565(9,14,22);
  const uint16_t color=valid?rgb565(155,197,229):rgb565(86,105,122);
  canvas.circle(x,y,3,color);
  canvas.rect(x-3,y,7,15,color);
  canvas.circle(x,y+15,5,color);
  canvas.circle(x,y,1,background);
  canvas.rect(x-1,y,3,12,background);
  canvas.line(x,y+7,x,y+15,color);
}

void dashboardStrip(DashboardStrip &canvas,const DashboardView &view) {
  const uint16_t ink=rgb565(232,237,242),muted=rgb565(123,138,155);
  const uint16_t background=rgb565(9,14,22);
  for(int y=canvas.top;y<canvas.top+canvas.rows;y++)for(int x=0;x<240;x++){
    int dx=x-120,dy=y-120,d=dx*dx+dy*dy;
    uint16_t color=0;
    if(d<=119*119){
      color=background;
      if(d>=115*115&&d<=117*117)color=rgb565(41,52,65);
      if(d>=115*115&&d<=117*117&&dy<-78&&dx>-62&&dx<62)
        color=mix565(background,view.accent,view.on?220:130);
      if(d<110*110&&dy<0)color=mix565(background,rgb565(25,34,45),(-dy)/4);
    }
    canvas.pixel(x,y,color);
  }
  canvas.line(26,113,20,120,muted,2);
  canvas.line(20,120,26,127,muted,2);
  canvas.line(214,113,220,120,muted,2);
  canvas.line(220,120,214,127,muted,2);

  if(view.page==PAGE_SOIL){
    drawSprout(canvas,view.soilFresh,view.accent);
    canvas.centered(view.value,124,view.soilFresh?ink:muted,2);
    const int groupWidth=textWidth(view.temperatureValue)*2+18;
    const int left=(240-groupWidth)/2;
    drawThermometer(canvas,left+5,170,view.temperatureFresh);
    canvas.text(view.temperatureValue,left+18,165,
                view.temperatureFresh?rgb565(176,208,231):muted,2);
  }else{
    if(view.page==PAGE_FANS)drawFan(canvas,view.on,view.accent);
    else drawBulb(canvas,view.page==PAGE_UV,view.on,view.accent);
    const char *state=!view.connected?"—":view.on?"ON":"OFF";
    canvas.centered(state,170,!view.connected?rgb565(246,161,111):view.on?view.accent:muted,2);
  }
  for(int i=0;i<PAGE_COUNT;i++){
    int x=96+i*16;
    if(i==view.page){
      canvas.circle(x,216,4,view.accent);
      canvas.circle(x,216,6,mix565(background,view.accent,55),5);
    }else canvas.circle(x,216,2,rgb565(64,77,94));
  }
}

DashboardView dashboardSnapshot() {
  DashboardView view{};
  view.page=selection.selected;
  if(view.page<0||view.page>=PAGE_COUNT)view.page=PAGE_WARM;
  view.connected=outputs.ioReady;
  int channel=selection.channel();
  view.on=view.connected&&channel>=0&&channel<3&&outputs.on[channel];
  const uint32_t now=millis();
  const int percent=soilCalibration.percent(soil,now);
  view.soilFresh=percent>=0&&percent<=100;
  view.temperatureFresh=temperature.fresh(now)&&isfinite(temperature.celsius)&&
                        temperature.celsius>=-40.0f&&temperature.celsius<=125.0f;
  const uint16_t accents[]={rgb565(255,202,70),rgb565(177,96,255),
                            rgb565(103,213,160),rgb565(68,228,202)};
  view.accent=accents[view.page];
  if(view.soilFresh){
    snprintf(view.value,sizeof(view.value),"%d %%",percent);
  }else{
    snprintf(view.value,sizeof(view.value),"-- %%");
  }
  if(view.temperatureFresh)
    snprintf(view.temperatureValue,sizeof(view.temperatureValue),"%.1f °C",double(temperature.celsius));
  else snprintf(view.temperatureValue,sizeof(view.temperatureValue),"-- °C");
  return view;
}

void drawDashboard() {
  const DashboardView view=dashboardSnapshot();
  const bool soilUpdate=dashboardDrawn&&dashboardPage==PAGE_SOIL&&
                        view.page==PAGE_SOIL&&dashboardSoilFresh==view.soilFresh&&
                        dashboardTemperatureFresh==view.temperatureFresh;
  const int top=soilUpdate?120:0,height=soilUpdate?80:240;
  display.beginRegion(0,top,240,height);
  for(int y=top;y<top+height;y+=8){
    DashboardStrip strip{y,8,transferPixels};
    dashboardStrip(strip,view);
    display.pushRegion(transferPixels,240*8);
  }
  display.endRegion();
  dashboardDrawn=true;
  dashboardPage=view.page;
  dashboardSoilFresh=view.soilFresh;
  dashboardTemperatureFresh=view.temperatureFresh;
}
