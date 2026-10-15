"""Scientific figures; raw screenshot pixels remain in the original 1080p PNGs."""
from pathlib import Path
import csv,json,base64,html
from reportlab.graphics.shapes import Drawing,Rect,String,Line
from reportlab.graphics import renderSVG
from reportlab.lib.colors import HexColor
root=Path(__file__).parent
with (root/'summary.csv').open(newline='') as f: rows=list(csv.DictReader(f))
models=json.loads((root/'models.json').read_text()); names=[m['Name'] for m in models]
def stem(r): return Path(r['Geometry'].removeprefix('obj:')).stem
lookup={(r['Stage'],stem(r),r['Method'],int(r['MSAA'])):r for r in rows}
def text(d,x,y,value,size=11,color='#263547'):
 d.add(String(x,y,str(value),fontName='Helvetica',fontSize=size,fillColor=HexColor(color)))
colors={'CDT':'#467bb8','Heap3D':'#16857c','HeapPlane':'#c28128','HeapQuadView':'#8650a0'}
d=Drawing(1400,590);d.add(Rect(0,0,1400,590,fillColor=HexColor('#ffffff'),strokeColor=None))
text(d,25,550,'Five new 3D geometries: connectivity and projected footprint',23)
text(d,25,526,'1080p raster lighting; fixed vertices/triangles within every comparison; negative is faster.',12)
for k,(method,color) in enumerate(colors.items()):
 x=25+190*k;d.add(Rect(x,501,13,9,fillColor=HexColor(color),strokeColor=None));text(d,x+20,500,method,11)
for panel,(stage,msaa,instances) in enumerate([('confirm',1,64),('confirm',4,64),('large',1,1),('large',4,1)]):
 left=185+panel*295;span=235;zero=left+span*.8
 text(d,left,465,f'{instances} object(s), {msaa}x MSAA',12)
 for tick in [-100,-60,-20,0,25]:
  x=zero+tick*span/125;d.add(Line(x,145,x,449,strokeColor=HexColor('#dce2e8'),strokeWidth=.6));text(d,x-9,125,tick,9)
 for i,name in enumerate(names):
  y=421-i*56
  if panel==0: text(d,25,y-8,name,12)
  for k,method in enumerate(colors):
   r=lookup.get((stage,name,method,msaa)); yy=y+8-k*7
   if r and int(r['StrictEligibleConditions'])>=12:
    v=float(r['DrawVsSourceStrict_pct']);x=zero+v*span/125
    d.add(Line(zero,yy,x,yy,strokeColor=HexColor(colors[method]),strokeWidth=4))
   elif r: text(d,zero+4,yy-2,'x',8,colors[method])
text(d,25,83,'GPU indexed-draw time: means of matched pose ratios, not complete application frame times or confidence intervals.',11)
text(d,25,61,'x: strict geometry/control audit failed or fewer than 12 eligible conditions. Projected method not tested in 1-object runs.',11)
text(d,25,39,'3 randomized blocks; 256 warmups, 128 samples/case. Different object counts are separate workloads, never count-reduction optimizations.',11)
renderSVG.drawToFile(d,str(root/'performance.svg'))

methods=['CDT','CDTFlip','Greedy','LMT','GP','Heap3D','HeapPlane','HeapQuadView','Star3D']
d=Drawing(1100,505); d.add(Rect(0,0,1100,505,fillColor=HexColor('#ffffff'),strokeColor=None))
text(d,25,465,'All screened methods: GPU draw change (%)',22)
text(d,25,440,'1080p, 64 instances, 24 axis conditions. Screening is preliminary; longer confirmation controls the conclusions.',11)
for panel,msaa in enumerate([1,4]):
 top=385-panel*160;text(d,25,top+18,f'{msaa}x MSAA',13)
 for k,method in enumerate(methods): text(d,180+k*98,top+18,method,9)
 for i,name in enumerate(names):
  y=top-25*(i+1);text(d,25,y+7,name,11)
  for k,method in enumerate(methods):
   r=lookup[('screen',name,method,msaa)];n=int(r['StrictEligibleConditions']);x=180+k*98
   value='FB' if int(float(r['TriangulationStatus']))==2 else 'audit' if not n else f'n={n}' if n<12 else f"{float(r['DrawVsSourceStrict_pct']):+.1f}"
   c='#e7eaf0' if n<12 else '#b9dcd3' if float(r['DrawVsSourceStrict_pct']) < -3 else '#f3d6c6' if float(r['DrawVsSourceStrict_pct']) > 3 else '#f0f2f5'
   d.add(Rect(x,y,95,23,fillColor=HexColor(c),strokeColor=None));text(d,x+25,y+7,value,11)
text(d,25,51,'FB: source fallback; audit: no eligible strict result; n<12: insufficient stable conditions. All raw observations remain recorded.',10)
text(d,25,31,'Strict sampled error <=0.1% of source bounds diagonal in both directions. Methods preserve every source vertex and the triangle count.',10)
renderSVG.drawToFile(d,str(root/'screen_matrix.svg'))

# Compose a contact sheet from the existing reviewed raster renderer, without
# synthesizing another lighting model or altering the original screenshot files.
svg=['<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="1440" height="700" viewBox="0 0 1440 700">',
     '<rect width="1440" height="700" fill="white"/>',
     '<text x="20" y="35" font-family="Arial" font-size="25">Five generated OBJ models — existing raster lit preview</text>']
for i,m in enumerate(models):
 path=root/'captures'/f"{m['Name']}.png"
 if not path.exists(): continue
 x=15+(i%3)*475;y=60+(i//3)*315
 data=base64.b64encode(path.read_bytes()).decode()
 svg += [f'<image x="{x}" y="{y}" width="460" height="259" xlink:href="data:image/png;base64,{data}"/>',
         f'<text x="{x}" y="{y+282}" font-family="Arial" font-size="17">{html.escape(m["Name"])} — {m["Triangles"]:,} triangles</text>']
svg += ['<text x="20" y="690" font-family="Arial" font-size="14">Originals: 1920×1080, 22.5° X / 11.25° Y; this contact sheet rescales them. Source meshes shown.</text>','</svg>']
(root/'geometry_gallery.svg').write_text('\n'.join(svg),encoding='utf-8')
print('Research figures written.')
