from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
root=Path(__file__).resolve().parents[1]
labels = 'ON OFF 0123456789 .-—%°C'
chars=sorted(set(labels))
font=ImageFont.truetype('/usr/share/fonts/TTF/DejaVuSans.ttf',12)
rows=[]
for ch in chars:
    im=Image.new('1',(14,16));ImageDraw.Draw(im).text((0,-1),ch,font=font,fill=1)
    width=min(14,int(font.getlength(ch)+1))
    bits=[sum((1<<(15-x)) for x in range(14) if im.getpixel((x,y))) for y in range(16)]
    rows.append('{%d,%d,{%s}}'%(ord(ch),width,','.join(hex(v) for v in bits)))
(root/'GreenhouseSimple/Font.h').write_text('#pragma once\n#include <stdint.h>\nstruct Glyph {uint16_t code;uint8_t width;uint16_t rows[16];};\nconst Glyph glyphs[] PROGMEM={\n'+',\n'.join(rows)+'\n};\n')
print('Generated',len(chars),'glyphs')
