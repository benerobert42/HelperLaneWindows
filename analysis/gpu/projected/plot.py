"""Additional GPU draw benefit over Heap3D, with per-view ranges (not CIs)."""
import csv
import sys
from pathlib import Path
from reportlab.graphics.shapes import Drawing, String, Line, Rect, Circle
from reportlab.graphics import renderSVG
from reportlab.lib.colors import HexColor

root=Path(__file__).parent
run=sys.argv[1] if len(sys.argv)>1 else 'projected_full_lit_1080_q8'
with (root/'summary.csv').open(newline='') as f:
    rows=[r for r in csv.DictReader(f) if r['Run']==run]
assert rows, run
models=sorted({r['Geometry'] for r in rows})
methods=[('HeapQuadSet','Fixed set, source seed','#0072B2'),('HeapQuadView','Current view, source seed','#777777'),
         ('HeapQuadWarm','Current view, Heap3D seed','#D55E00'),('HeapQuadWarmLag','Warm, 45 deg stale','#009E73')]
height=180+len(models)*85
d=Drawing(1070,height)
d.add(Rect(0,0,1070,height,fillColor=HexColor('#ffffff'),strokeColor=None))
d.add(String(20,height-28,'Does projected-edge optimization improve on Heap3D?',fontSize=17,fontName='Helvetica-Bold'))
d.add(String(20,height-46,'1920 x 1080; 64 instances; 24 axis conditions; matched stable controls and valid meshes',fontSize=10))
for i,(_,label,color) in enumerate(methods):
    x=20+i*246
    d.add(Circle(x+3,height-63,3,fillColor=HexColor(color),strokeColor=None))
    d.add(String(x+12,height-66,label,fontSize=10))
selected=[r for r in rows if r['Method'] in [m[0] for m in methods]]
lo=min(-10,min(float(r['DrawVsWorldMin_pct']) for r in selected)); hi=max(10,max(float(r['DrawVsWorldMax_pct']) for r in selected))
lo=10*int(lo//10); hi=10*int(-(-hi//10))
for panel,msaa in enumerate(['1','4']):
    left=200+panel*427; width=343
    def x(v): return left+(v-lo)/(hi-lo)*width
    d.add(String(left,height-93,f'{msaa}x MSAA: GPU draw change relative to Heap3D (%)',fontSize=10,fontName='Helvetica-Bold'))
    for tick in range(int(lo),int(hi)+1,10):
        d.add(Line(x(tick),65,x(tick),height-103,strokeColor=HexColor('#888888' if tick==0 else '#dddddd'),strokeWidth=.6))
        d.add(String(x(tick)-8,49,str(tick),fontSize=9))
    for i,model in enumerate(models):
        y=height-132-i*85
        if panel==0: d.add(String(20,y-4,model,fontSize=11))
        for j,(method,_,color) in enumerate(methods):
            rs=[r for r in rows if (r['Geometry'],r['Method'],r['MSAA'])==(model,method,msaa)]
            if not rs: continue
            r=rs[0]; cy=y+23-j*14
            d.add(Line(x(float(r['DrawVsWorldMin_pct'])),cy,x(float(r['DrawVsWorldMax_pct'])),cy,strokeColor=HexColor(color),strokeWidth=1))
            d.add(Circle(x(float(r['DrawVsWorld_pct'])),cy,2.8,fillColor=HexColor(color),strokeColor=None))
            d.add(String(left+width+8,cy-3,r['EligibleViewConditions']+'/24',fontSize=8))
d.add(String(20,28,'Negative is additional speedup. Points: means; lines: observed pose ranges, not confidence intervals.',fontSize=10))
d.add(String(20,12,'Pure full-run objectives use all 64 actual projections. GPU draw excludes clear, resolve, UI and preparation.',fontSize=9))
renderSVG.drawToFile(d,str(root/(run+'_effects.svg')))
