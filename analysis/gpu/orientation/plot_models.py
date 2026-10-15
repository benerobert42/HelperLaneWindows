"""Vector plot of connectivity effects; pose ranges are not confidence intervals."""
import csv
from pathlib import Path
from reportlab.graphics.shapes import Drawing, String, Line, Rect, Circle
from reportlab.graphics import renderSVG
from reportlab.lib.colors import HexColor

root=Path(__file__).parent
with (root/'model_summary.csv').open(newline='') as f:
    rows=[r for r in csv.DictReader(f) if r['Method']!='Source']
models=sorted({r['Model'] for r in rows})
height=155+len(models)*32
d=Drawing(1060,height)
d.add(Rect(0,0,1060,height,fillColor=HexColor('#ffffff'),strokeColor=None))
d.add(String(20,height-28,'Connectivity impact across models at 1920 x 1080',fontSize=17,fontName='Helvetica-Bold'))
d.add(String(20,height-46,'24 axis rotations; 64 instances; 3 blocks; 128 samples/case; 256 warmup; same meshoptimizer policy',fontSize=9))
for x,text,color in [(20,'CDTFlip','#0072B2'),(170,'Heap3D','#D55E00')]:
    d.add(Circle(x+3,height-62,3,fillColor=HexColor(color),strokeColor=None))
    d.add(String(x+12,height-65,text,fontSize=10))
d.add(String(330,height-65,'Point: mean change. Line: range across eligible poses. Labels: pose counts.',fontSize=9))
for panel,msaa in enumerate(['1','4']):
    left=215+panel*414
    width=330
    def xpos(value): return left+(value+100)/130*width
    d.add(String(left,height-88,f'{msaa}x MSAA: GPU draw time change (%)',fontSize=11,fontName='Helvetica-Bold'))
    for tick in [-100,-80,-60,-40,-20,0,20]:
        x=xpos(tick)
        d.add(Line(x,55,x,height-97,strokeColor=HexColor('#888888' if tick==0 else '#dddddd'),strokeWidth=0.7))
        d.add(String(x-10,40,str(tick),fontSize=9))
    for i,model in enumerate(models):
        y=height-110-i*32
        if panel==0:
            label=Path(model[4:]).stem if model.startswith('obj:') else model
            d.add(String(20,y-3,label,fontSize=10))
        counts=[]
        for method,color,offset in [('CDTFlip','#0072B2',4),('Heap3D','#D55E00',-4)]:
            selected=[r for r in rows if (r['Model'],r['Method'],r['MSAA'])==(model,method,msaa)]
            if not selected:
                counts.append('-'); continue
            r=selected[0]; cy=y+offset
            lo,hi=map(float,[r['DrawChangeMin_pct'],r['DrawChangeMax_pct']])
            # Keep out-of-range whiskers visibly capped and disclose the full CSV.
            d.add(Line(xpos(max(-100,lo)),cy,xpos(min(30,hi)),cy,strokeColor=HexColor(color),strokeWidth=1))
            d.add(Circle(xpos(float(r['DrawChange_pct'])),cy,2.5,fillColor=HexColor(color),strokeColor=None))
            counts.append(r['EligibleUniquePoses'])
        d.add(String(left+width+8,y-3,'/'.join(counts),fontSize=8))
d.add(String(20,22,'Negative is faster. Ranges describe orientations, not uncertainty. Stable controls and valid meshes only; 22 unique poses possible.',fontSize=9))
d.add(String(20,8,'GPU draw excludes clear, resolve, UI and presentation. Full observations, control flags and any values beyond axis bounds stay in the CSV.',fontSize=9))
renderSVG.drawToFile(d,str(root/'model_effects.svg'))
