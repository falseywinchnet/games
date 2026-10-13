"""Original deterministic card artwork: the four painted backs, the card shadow and the app
icon. The faces are the Classic reprint of Goodall's 1902 pack (authoring/cards1902); the
painted faces this once drew are retired."""
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from pathlib import Path
import math
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'authoring' / 'conv'))
import conv  # Plan Paint's CONV: the icon's cards are shrunk in linear light, never ringing
ROOT = Path(__file__).resolve().parents[1] / 'assets'
ROOT.mkdir(exist_ok=True)
W, H = 360, 504
FONT = '/System/Library/Fonts/Supplemental/Georgia.ttf'
BOLD = '/System/Library/Fonts/Supplemental/Georgia Bold.ttf'
INK = '#20354b'
RED = '#b52e45'
GOLD = '#bd9850'
def suit(draw, s, x, y, size, color):
    r = size / 2
    if s == 1:
        draw.polygon([(x,y-r),(x+r*.78,y),(x,y+r),(x-r*.78,y)], fill=color)
    elif s == 2 or s == 3:
        points=[]
        for i in range(100):
            t=2*math.pi*i/100
            px=16*math.sin(t)**3/18
            py=(13*math.cos(t)-5*math.cos(2*t)-2*math.cos(3*t)-math.cos(4*t))/18
            if s==2: points.append((x+px*r,y-py*r))
            else: points.append((x+px*r,y+py*r*.84-r*.15))
        draw.polygon(points,fill=color)
        if s==3: draw.polygon([(x,y),(x-r*.27,y+r),(x+r*.27,y+r)],fill=color)
    else:
        for dx,dy in [(0,-.44),(-.40,.14),(.40,.14)]:
            cx,cy=x+dx*r,y+dy*r
            draw.ellipse((cx-r*.47,cy-r*.47,cx+r*.47,cy+r*.47),fill=color)
        draw.polygon([(x,y),(x-r*.26,y+r),(x+r*.26,y+r)],fill=color)
def base():
    im=Image.new('RGBA',(W,H),(0,0,0,0)); d=ImageDraw.Draw(im)
    d.rounded_rectangle((2,2,W-3,H-3),radius=20,fill='#fffcf3',outline='#c5c0ab',width=3)
    d.rounded_rectangle((8,8,W-9,H-9),radius=15,outline='#ffffff',width=2)
    return im
for k,color in enumerate(['#245681','#853549','#2c695d','#644c86']):
    im=base(); d=ImageDraw.Draw(im)
    d.rounded_rectangle((18,18,W-19,H-19),radius=12,fill=color,outline=GOLD,width=4)
    d.rounded_rectangle((29,29,W-30,H-30),radius=8,outline='#e4d7ad',width=2)
    for y in range(40,H-35,24):
        for x in range(40,W-35,24):
            d.polygon([(x,y-7),(x+6,y),(x,y+7),(x-6,y)],outline='#bdab79')
    d.ellipse((79,151,281,353),fill=color,outline=GOLD,width=5)
    d.ellipse((92,164,268,340),outline='#e4d7ad',width=2)
    suit(d,k,180,252,104,'#f4e2b6')
    im.save(ROOT / ('back_%d.png'%k))
print('Created four original backs.')

icon=Image.new('RGBA',(1024,1024),(0,0,0,0)); d=ImageDraw.Draw(icon)
d.rounded_rectangle((24,24,1000,1000),radius=210,fill='#174b3b',outline=GOLD,width=16)
d.rounded_rectangle((58,58,966,966),radius=180,outline='#65a080',width=8)
for s in range(4):
    card=conv.resize_srgb_image(Image.open(ROOT / ('classic_%02d.png'%(s*13))).convert('RGBA'),250,350)
    card=card.rotate((1.5-s)*12,resample=Image.Resampling.BICUBIC,expand=True)
    icon.alpha_composite(card,(int(120+s*155),int(310+abs(s-1.5)*25)))
icon.save(ROOT / 'Games.png')

shadow=Image.new('RGBA',(440,584),(0,0,0,0))
ImageDraw.Draw(shadow).rounded_rectangle((40,48,400,552),radius=20,fill=(0,12,9,100))
shadow=shadow.filter(ImageFilter.GaussianBlur(14))
shadow.save(ROOT / 'card_shadow.png')
