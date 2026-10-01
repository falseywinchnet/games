"""Original deterministic card artwork. No generated card faces or borrowed decks."""
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from pathlib import Path
import math
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
def court(s,rank):
    im=Image.new('RGBA',(240,350),(0,0,0,0)); d=ImageDraw.Draw(im)
    c=RED if s in (1,2) else INK
    d.rounded_rectangle((4,4,235,345),radius=8,outline=GOLD,width=3)
    # The same geometric portrait grammar and suit insignia across the deck.
    half=Image.new('RGBA',(240,175),(0,0,0,0)); q=ImageDraw.Draw(half)
    q.polygon([(28,175),(37,126),(81,100),(163,100),(207,133),(214,175)],fill=c,outline=GOLD,width=3)
    q.polygon([(49,126),(85,110),(167,175),(116,175)],fill=GOLD)
    q.polygon([(163,110),(195,130),(165,175),(143,158)],fill='#457988')
    q.ellipse((81,40,166,124),fill='#e4bb88',outline=INK,width=3)
    q.polygon([(80,80),(76,48),(101,31),(143,35),(169,60),(161,90),(151,57),(101,55),(94,87)],fill='#5b4638')
    q.line((113,75,120,75),fill=INK,width=3); q.line((139,75,146,75),fill=INK,width=3)
    q.line((129,76,126,91,132,93),fill='#a7765b',width=2)
    q.arc((115,91,144,110),0,150,fill='#9b4841',width=3)
    if rank==13: q.polygon([(103,102),(120,120),(150,103),(144,127),(119,139),(102,119)],fill='#735135')
    if rank==12:
        q.polygon([(88,54),(89,26),(104,43),(121,17),(139,40),(159,23),(157,54)],fill=GOLD,outline=INK)
        q.ellipse((167,95,176,108),fill=GOLD)
    elif rank==13: q.polygon([(82,48),(83,23),(102,36),(119,13),(138,34),(160,18),(164,48)],fill=GOLD,outline=INK)
    else:
        q.polygon([(75,46),(90,22),(143,25),(165,49)],fill=c,outline=GOLD)
        q.line((88,30,67,10),fill=GOLD,width=5)
    q.line((40,151,40,59),fill=GOLD,width=6)
    suit(q,s,40,48,30,c)
    suit(q,s,176,146,35,'#f5df9f')
    im.alpha_composite(half,(0,0)); im.alpha_composite(half.rotate(180),(0,175))
    return im
ranks=['','A','2','3','4','5','6','7','8','9','10','J','Q','K']
positions={1:[(0,0)],2:[(0,-1),(0,1)],3:[(0,-1),(0,0),(0,1)],4:[(-1,-1),(1,-1),(-1,1),(1,1)],5:[(-1,-1),(1,-1),(0,0),(-1,1),(1,1)],6:[(-1,-1),(1,-1),(-1,0),(1,0),(-1,1),(1,1)],7:[(-1,-1),(1,-1),(0,-.5),(-1,0),(1,0),(-1,1),(1,1)],8:[(-1,-1),(1,-1),(0,-.5),(-1,0),(1,0),(0,.5),(-1,1),(1,1)],9:[(-1,-1),(1,-1),(-1,-.33),(1,-.33),(0,0),(-1,.33),(1,.33),(-1,1),(1,1)],10:[(-1,-1),(1,-1),(0,-.66),(-1,-.33),(1,-.33),(-1,.33),(1,.33),(0,.66),(-1,1),(1,1)]}
for s in range(4):
    for rank in range(1,14):
        im=base(); d=ImageDraw.Draw(im); color=RED if s in (1,2) else INK
        corner=Image.new('RGBA',(62,105),(0,0,0,0)); cd=ImageDraw.Draw(corner)
        cd.text((31,1),ranks[rank],font=ImageFont.truetype(BOLD,43 if rank==10 else 49),anchor='mt',fill=color)
        suit(cd,s,31,79,35,color)
        im.alpha_composite(corner,(5,10)); im.alpha_composite(corner.rotate(180),(W-67,H-115))
        if rank<=10:
            for px,py in positions[rank]:
                pip=Image.new('RGBA',(110,110),(0,0,0,0))
                suit(ImageDraw.Draw(pip),s,55,55,95 if rank==1 else 57,color)
                if py>0: pip=pip.rotate(180)
                im.alpha_composite(pip,(int(W/2+px*64-55),int(H/2+py*151-55)))
        else: im.alpha_composite(court(s,rank),(60,77))
        im.save(ROOT / ('card_%02d.png'%(s*13+rank-1)))
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
print('Created 52 consistent HD card faces and four original backs.')

icon=Image.new('RGBA',(1024,1024),(0,0,0,0)); d=ImageDraw.Draw(icon)
d.rounded_rectangle((24,24,1000,1000),radius=210,fill='#174b3b',outline=GOLD,width=16)
d.rounded_rectangle((58,58,966,966),radius=180,outline='#65a080',width=8)
for s in range(4):
    card=Image.open(ROOT / ('card_%02d.png'%(s*13))).resize((250,350),Image.Resampling.LANCZOS)
    card=card.rotate((1.5-s)*12,resample=Image.Resampling.BICUBIC,expand=True)
    icon.alpha_composite(card,(int(120+s*155),int(310+abs(s-1.5)*25)))
icon.save(ROOT / 'Games.png')

shadow=Image.new('RGBA',(440,584),(0,0,0,0))
ImageDraw.Draw(shadow).rounded_rectangle((40,48,400,552),radius=20,fill=(0,12,9,100))
shadow=shadow.filter(ImageFilter.GaussianBlur(14))
shadow.save(ROOT / 'card_shadow.png')
