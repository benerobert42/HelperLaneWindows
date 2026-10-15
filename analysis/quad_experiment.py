"""Illustrative one-sample software raster model, NOT measured GPU performance.

Pixel centers, 2x2 quads, no occlusion, no MSAA; every touched quad schedules
four lanes per triangle. Deterministic convex quads avoid edge ties. Hardware
raster precision, helper scheduling, and depth rejection can differ.
"""
import csv
import json
import math
import random
from pathlib import Path

HERE = Path(__file__).resolve().parent

def cross(a, b, c):
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])

def raster(tri):
    a, b, c = tri
    if cross(a,b,c)<0:
        b,c=c,b
    pixels=set()
    for y in range(math.floor(min(a[1],b[1],c[1])), math.ceil(max(a[1],b[1],c[1]))):
        for x in range(math.floor(min(a[0],b[0],c[0])), math.ceil(max(a[0],b[0],c[0]))):
            p=(x+0.5,y+0.5)
            if min(cross(a,b,p),cross(b,c,p),cross(c,a,p))>0:
                pixels.add((x,y))
    quads={(x//2,y//2) for x,y in pixels}
    return 4*len(quads)-len(pixels),len(pixels),len(quads)

def metrics(q, diagonal):
    tris=((0,1,2),(0,2,3)) if diagonal=='AC' else ((0,1,3),(1,2,3))
    parts=[raster(tuple(q[i] for i in tri)) for tri in tris]
    return tuple(sum(p[i] for p in parts) for i in range(3))

rng=random.Random(1707)
example=None
for _ in range(20000):
    q=[(rng.uniform(0,3),rng.uniform(0,3)),(rng.uniform(7,16),rng.uniform(0,3)),
       (rng.uniform(7,16),rng.uniform(7,16)),(rng.uniform(0,3),rng.uniform(7,16))]
    if min(cross(q[i],q[(i+1)%4],q[(i+2)%4]) for i in range(4))<=0:
        continue
    ac=math.dist(q[0],q[2]); bd=math.dist(q[1],q[3])
    m_ac=metrics(q,'AC'); m_bd=metrics(q,'BD')
    shorter='AC' if ac<bd else 'BD'
    mshort,mlong=(m_ac,m_bd) if shorter=='AC' else (m_bd,m_ac)
    if mshort[0]>=mlong[0]+8 and mshort[1]==mlong[1] and abs(ac-bd)>0.2:
        example={'vertices_xy':q,'AC_length':ac,'BD_length':bd,'shorter':shorter,
                 'AC_helpers':m_ac[0],'BD_helpers':m_bd[0], 'covered_pixels':m_ac[1]}
        break
assert example is not None
(HERE/'quad_counterexample.json').write_text(json.dumps(example,indent=2)+'\n')
with (HERE/'quad_phase_sweep.csv').open('w',newline='') as f:
    writer=csv.writer(f)
    writer.writerow(['PhaseX','PhaseY','AC_Length','BD_Length','AC_Helpers','BD_Helpers','CoveredPixels'])
    for iy in range(8):
        for ix in range(8):
            q=[(x+ix/4,y+iy/4) for x,y in example['vertices_xy']]
            ac=metrics(q,'AC');bd=metrics(q,'BD')
            assert ac[1]==bd[1], 'Triangulations must have identical pixel coverage.'
            writer.writerow([ix/4,iy/4,example['AC_length'],example['BD_length'],ac[0],bd[0],ac[1]])
print(json.dumps(example,indent=2))
