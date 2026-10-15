"""Vector figures; percentage means are not confidence intervals."""
import csv
from pathlib import Path
from reportlab.graphics.shapes import Drawing,Rect,String,Line
from reportlab.graphics import renderSVG
from reportlab.lib.colors import Color,HexColor

root=Path(__file__).parent
with (root/'summary.csv').open(newline='') as f: rows=list(csv.DictReader(f))
models=['sphere','sphere_dense','jittered','ellipsoid','ellipsoid_small','blob','blob_small','saddle_branch',
        'clustered','planar','jittered_planar','folded','skew1','skew2','skew3','skew_fold','skew_saddle',
        'torus','torus_skew','cylinder','cylinder_skew','suzanne','bunny','teapot','ellipse2d',
        'obj:analysis/gpu/orientation/models/GraphExHeart.obj','obj:analysis/gpu/orientation/models/GraphExTangleCube.obj']
def label(g): return Path(g).stem if g.startswith('obj:') else g
def text(d,x,y,s,size=10,color='#253447'):
    d.add(String(x,y,s,fontName='Helvetica',fontSize=size,fillColor=HexColor(color)))
def col(v):
    if v<0:
        t=min(1,-v/40); return Color(.94*(1-t)+.13*t,.97*(1-t)+.54*t,.98*(1-t)+.55*t)
    t=min(1,v/10); return Color(.98,.97*(1-t)+.45*t,.94*(1-t)+.33*t)

methods=['CDT','CDTFlip','CDTFlip3D','Greedy','LMT','LMT3D','GP','Heap3D','HeapPlane','HeapScreen','HeapQuad',
         'HeapQuadSet','HeapQuadView','HeapQuadLag','HeapQuadWarm','HeapQuadWarmLag','HeapLoose','HeapWide',
         'Star3D','StarLoose','StarWide']
names=['CDT','Flip2D','Flip3D','Greedy','LMT','LMT3D','GP','Heap3D','Plane','MixL2','MixL1','SetL1','ViewL1','Lag',
       'Warm','W-Lag','Loose','Wide','Star','S-Loose','S-Wide']
screen={(r['Geometry'],r['Method'],int(r['MSAA'])):r for r in rows if r['Stage']=='screen'}
for msaa in [1,4]:
    width=1280; height=780; left=180; cell=50; row_height=21; top=680
    d=Drawing(width,height); d.add(Rect(0,0,width,height,fillColor=HexColor('#ffffff'),strokeColor=None))
    text(d,25,748,f'All methods: GPU draw change at {msaa}x MSAA',20)
    text(d,25,722,'1080p, 64 instances, 24 axis conditions. Screening: 2 blocks, 64 warmups, 32 samples.',11)
    text(d,25,704,'Strict sampled surface budget: 0.1%. Green is faster; orange is slower. Numbers are percent changes.',11)
    for j,name in enumerate(names): text(d,left+j*cell+3,top+9,name,8)
    for i,g in enumerate(models):
        y=top-(i+1)*row_height
        text(d,25,y+6,label(g),10)
        for j,method in enumerate(methods):
            r=screen[(g,method,msaa)]; n=int(r['StrictEligibleConditions']); x=left+j*cell
            if int(float(r['TriangulationStatus']))==2: value='FB'; color=HexColor('#e3e6ec')
            elif not n: value='geom'; color=HexColor('#e3e6ec')
            elif n<12: value=f'n={n}'; color=HexColor('#f0f1f5')
            else: v=float(r['DrawVsSourceStrict_pct']); value=f'{v:+.1f}'; color=col(v)
            d.add(Rect(x,y,cell-1,row_height-1,fillColor=color,strokeColor=None))
            text(d,x+6,y+6,value,9)
    text(d,25,68,'FB: source fallback. geom: no strict eligible geometry. n<12: insufficient stable view conditions.',10)
    text(d,25,50,'Control gate: SourceControl <=2% mean and <=3% worst block difference per pose. No raw samples removed.',10)
    text(d,25,32,'Loose/Wide and Star variants use 0.5%/2% budgets; only their strict eligible poses are colored here.',10)
    renderSVG.drawToFile(d,str(root/f'screen_matrix_{msaa}x.svg'))

confirmed={(r['Geometry'],r['Method'],int(r['MSAA'])):r for r in rows if r['Stage']=='confirm'}
if confirmed:
    d=Drawing(1100,950); d.add(Rect(0,0,1100,950,fillColor=HexColor('#ffffff'),strokeColor=None))
    text(d,25,915,'CDT, edge heap and neighborhood retriangulation',22)
    text(d,25,890,'Longer confirmation: 3 blocks, 256 warmups, 128 samples. 1080p, 64 instances, 24 axis conditions.',11)
    colors={'CDT':'#4879b3','Heap3D':'#16857c','Star3D':'#bb7933'}
    for k,(name,color) in enumerate(colors.items()):
        x=25+k*155; d.add(Rect(x,867,12,9,fillColor=HexColor(color),strokeColor=None)); text(d,x+19,866,name,11)
    for panel,msaa in enumerate([1,4]):
        x0=205+panel*445; span=375; zero=x0+span*.8
        text(d,x0,838,f'{msaa}x MSAA; draw change (%)',13)
        for tick in [-80,-60,-40,-20,0,20]:
            x=zero+tick*span/100
            d.add(Line(x,110,x,825,strokeColor=HexColor('#dce2e8'),strokeWidth=.5))
            text(d,x-9,95,str(tick),9)
        for i,g in enumerate(models):
            y=804-i*25
            if panel==0: text(d,25,y-1,label(g),10)
            for k,method in enumerate(colors):
                r=confirmed[(g,method,msaa)]; n=int(r['StrictEligibleConditions'])
                yy=y+6-k*6
                if n>=12:
                    v=float(r['DrawVsSourceStrict_pct']); x=zero+v*span/100
                    d.add(Line(zero,yy,x,yy,strokeColor=HexColor(colors[method]),strokeWidth=3))
                else: text(d,zero+3,yy-2,'x',7,colors[method])
    text(d,25,57,'x: fallback, failed strict geometry audit, or fewer than 12 stable conditions. Detailed counts are in the CSV.',10)
    text(d,25,37,'Means of matched pose ratios; bars are not confidence intervals. Negative values are faster.',10)
    renderSVG.drawToFile(d,str(root/'core_comparison.svg'))
print('SVG research figures written.')
