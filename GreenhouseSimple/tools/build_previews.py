from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
p=Path('/tmp/greenhouse-simple-preview')
out=Path(__file__).resolve().parents[1]/'preview'
out.mkdir(exist_ok=True)
font=ImageFont.truetype('/usr/share/fonts/TTF/DejaVuSans.ttf',16)
items=[
    ('warm_off','Тёплый свет · выключен'),('uv_off','УФ · выключен'),
    ('soil_uncalibrated','Почва · без калибровки'),('fans_off','Кулеры · выключены'),
    ('warm_on','Тёплый свет · включён'),('uv_on','УФ · включён'),
    ('soil_on','Датчики · пример значений'),('fans_on','Кулеры · включены'),
    ('warm_offline','Свет · нет связи'),('uv_offline','УФ · нет связи'),
    ('soil_offline','Датчики · нет данных'),('fans_offline','Кулеры · нет связи'),
]
im=Image.new('RGB',(1120,900),'#10141d');d=ImageDraw.Draw(im)
for n,(f,title) in enumerate(items):
 x=20+(n%4)*280;y=16+(n//4)*300
 im.paste(Image.open(p/(f+'.ppm')),(x,y));d.text((x,y+250),title,fill='#e5eaf3',font=font)
im.save(out/'interface_preview.png')
for f,_ in items:
 Image.open(p/(f+'.ppm')).save(out/(f+'.png'))
Image.open(p/'soil_stale.ppm').save(out/'soil_stale.png')
limits=[
    ('soil_cold','0 % · −40,0 °C'),('soil_hot','100 % · 125,0 °C'),
    ('soil_negative','50 % · −0,1 °C'),('soil_temperature_missing','Температура недоступна'),
]
im=Image.new('RGB',(1120,300),'#10141d');d=ImageDraw.Draw(im)
for i,(f,title) in enumerate(limits):
 x=20+i*280
 im.paste(Image.open(p/(f+'.ppm')),(x,16))
 d.text((x,266),title,fill='#e5eaf3',font=font)
 Image.open(p/(f+'.ppm')).save(out/(f+'.png'))
im.save(out/'sensor_limits_preview.png')
eyes=Image.new('RGB',(560,300),'#10141d');ed=ImageDraw.Draw(eyes)
for i,(name,title) in enumerate([('eye_0','Заставка'),('eye_2200','Взгляд вправо')]):
 eyes.paste(Image.open(p/(name+'.ppm')),(20+i*280,16))
 ed.text((20+i*280,266),title,fill='#e5eaf3',font=font)
eyes.save(out/'eye_preview.png')
frames=[Image.open(p/f'eye_{t}.ppm').convert('RGB') for t in range(0,6601,20)]
frames[0].save(out/'startup_eye.gif',save_all=True,append_images=frames[1:],duration=20,loop=0)
print('Preview images updated')
