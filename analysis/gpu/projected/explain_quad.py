"""An analytic illustration, not a benchmark or simulated GPU result."""
from pathlib import Path
import math
from reportlab.graphics.shapes import Drawing, String, Line, Polygon, Circle
from reportlab.graphics import renderSVG
from reportlab.lib.colors import HexColor

root=Path(__file__).parent
world=[(0,0),(2,0),(3,1),(1,1)]
screen=[((x-2*y)/math.sqrt(5),.1*(2*x+y)/math.sqrt(5)) for x,y in world]
def length(a,b,points): return math.dist(points[a],points[b])
boundary=sum(length(i,(i+1)%4,world) for i in range(4))
screen_boundary=sum(length(i,(i+1)%4,screen) for i in range(4))
world_short=boundary+length(1,3,world)
world_long=boundary+length(0,2,world)
screen_short=screen_boundary+length(0,2,screen)
screen_long=screen_boundary+length(1,3,screen)
d=Drawing(1020,300)
d.add(Polygon([0,0,1020,0,1020,300,0,300],fillColor=HexColor('#ffffff'),strokeColor=None))
d.add(String(20,275,'One planar quad: world-shorter and screen-shorter can disagree',fontSize=17,fontName='Helvetica-Bold'))
d.add(String(20,254,'Same four vertices, two triangles, plane, silhouette and filled area in this view. Only the internal diagonal changes.',fontSize=10))
for panel,(a,b,title,color) in enumerate([(1,3,'World-shorter diagonal','#0072B2'),(0,2,'Screen-shorter diagonal','#D55E00')]):
    left=30+panel*500
    d.add(String(left,227,title,fontSize=14,fontName='Helvetica-Bold',fillColor=HexColor(color)))
    points=[(left+30+(x+.4472136)*260,110+y*260) for x,y in screen]
    d.add(Polygon([v for p in points for v in p],fillColor=HexColor('#f0f0f0'),strokeColor=HexColor('#333333'),strokeWidth=1.3))
    d.add(Line(*points[a],*points[b],strokeColor=HexColor(color),strokeWidth=2.5))
    for i,(x,y) in enumerate(points):
        d.add(Circle(x,y,2.4,fillColor=HexColor('#333333'),strokeColor=None))
        d.add(String(x+5,y-13,'ABCD'[i],fontSize=10))
    d.add(String(left,82,f'Diagonal: world {length(a,b,world):.3f}; projected Euclidean {length(a,b,screen):.3f}',fontSize=11))
    total_world=world_short if panel==0 else world_long
    total_screen=screen_long if panel==0 else screen_short
    d.add(String(left,64,f'Total unique edges: world {total_world:.3f}; projected {total_screen:.3f}',fontSize=11))
d.add(String(20,36,f'Choosing the orange diagonal reduces total projected length {100*(1-screen_short/screen_long):.1f}% but increases world length {100*(world_long/world_short-1):.1f}%.',fontSize=11))
d.add(String(20,17,'A more face-on camera reverses this preference. Shorter projected edges do not by themselves guarantee lower GPU time.',fontSize=10))
renderSVG.drawToFile(d,str(root/'quad_example.svg'))
print('Analytic quad diagram written.')
