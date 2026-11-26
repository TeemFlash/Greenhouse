#pragma once
// ESP8266 bulk FIFO transfer: Adafruit's generic writePixels sends one word
// at a time on this core. This adapter keeps its address window / CS handling.
class FastDisplay : public Adafruit_GC9A01A {
 public:
  FastDisplay(int8_t cs,int8_t dc,int8_t rst):Adafruit_GC9A01A(cs,dc,rst){}
  void beginRegion(int x,int y,int w,int h){startWrite();setAddrWindow(x,y,w,h);}
  void pushRegion(uint16_t *pixels,unsigned count){
    for(unsigned i=0;i<count;i++)pixels[i]=__builtin_bswap16(pixels[i]);
    SPI.writeBytes(reinterpret_cast<const uint8_t*>(pixels),count*2);
  }
  void endRegion(){endWrite();}
};
