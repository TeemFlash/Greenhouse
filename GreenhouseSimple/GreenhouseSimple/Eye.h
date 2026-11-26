#pragma once
#include <stdint.h>
#include <math.h>
inline uint16_t rgb565(int r,int g,int b){return uint16_t(((r&248)<<8)|((g&252)<<3)|(b>>3));}
inline uint16_t mix565(uint16_t a,uint16_t b,int weight){
  int r=((a>>11)&31)*(256-weight)+((b>>11)&31)*weight;
  int g=((a>>5)&63)*(256-weight)+((b>>5)&63)*weight;
  int bl=(a&31)*(256-weight)+(b&31)*weight;
  return uint16_t(((r>>8)<<11)|((g>>8)<<5)|(bl>>8));
}
inline uint16_t irisColor(int d2){
  // Constant colors between rings, a one-pixel blend at each edge.
  const int radii[]={6,18,27,36,46,56,70};
  const uint16_t colors[]={rgb565(255,222,47),rgb565(255,78,15),rgb565(235,48,0),
    rgb565(216,36,2),rgb565(198,23,7),rgb565(191,0,17)};
  uint16_t glow=rgb565(49+(4900-(d2<4900?d2:4900))*30/4900,23,18);
  for(int i=0;i<7;i++){
    int r=radii[i],edge=r*r;
    uint16_t inner=i<6?colors[i]:glow;
    uint16_t outer=i<5?colors[i+1]:i==5?glow:rgb565(16,16,16);
    if(d2<edge-r)return inner;
    if(d2<edge+r)return mix565(inner,outer,(d2-edge+r)*256/(2*r));
  }
  return rgb565(16,16,16);
}
static uint16_t irisPalette[5042];
inline void prepareEyePalette(){
  static bool ready=false;
  if(ready)return;
  for(int i=0;i<5042;i++)irisPalette[i]=irisColor(i);
  ready=true;
}
inline uint16_t eyePixelFixed(int x,int y,int gazeX16,int gazeY16){
  int dx=x-120,dy=y-120,d2=dx*dx+dy*dy;
  if(d2>14400)return 0;
  if(d2>10609)return rgb565(91,91,91);
  int ex=dx*16-gazeX16,ey=dy*16-gazeY16;
  int e2=(ex*ex+ey*ey)>>8;
  uint16_t c=e2<5042?irisPalette[e2]:rgb565(16,16,16);
  if(d2>76*76&&d2<92*92&&dy<-69&&dx*dx<dy*dy/8)c=rgb565(169,169,169);
  if(d2>100*100&&d2<102*102&&dy<-29&&dy>-70&&dx*dx>70*70)c=rgb565(143,143,143);
  return c;
}
inline void eyeGaze(uint32_t elapsed,int &x16,int &y16){
  const int points[][2]={{0,0},{0,0},{17,-8},{-17,5},{8,15},{0,0},{0,0}};
  unsigned segment=elapsed/1100;
  if(segment>=6){x16=y16=0;return;}
  float t=(elapsed%1100)/1100.0f;
  float ease=t*t*t*(t*(6*t-15)+10); // Zero velocity and acceleration at stops.
  x16=lroundf(16*(points[segment][0]+(points[segment+1][0]-points[segment][0])*ease));
  y16=lroundf(16*(points[segment][1]+(points[segment+1][1]-points[segment][1])*ease));
}
