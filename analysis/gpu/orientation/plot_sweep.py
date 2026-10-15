"""Standalone vector plot of the requested 24 rotation conditions at 1080p."""
import csv
import sys
from pathlib import Path
from reportlab.graphics.shapes import Drawing, String, Line, Rect
from reportlab.graphics.charts.lineplots import LinePlot
from reportlab.graphics import renderSVG
from reportlab.lib.colors import HexColor
from reportlab.graphics.widgets.markers import makeMarker

root = Path(__file__).parent
run = sys.argv[1] if len(sys.argv)>1 else 'steady_lit_1080_q8'
with (root/'orientations.csv').open(newline='') as f:
    rows = [r for r in csv.DictReader(f) if r['Run']==run]
assert rows, run
d = Drawing(990, 850)
d.add(Rect(0,0,990,850,fillColor=HexColor('#ffffff'),strokeColor=None))
d.add(String(34,824,'GPU draw time across 24 separate axis rotations',fontName='Helvetica-Bold',fontSize=18))
d.add(String(34,805,f'{rows[0]["Height"]}p; 64 instances; 3 blocks; {rows[0]["Samples"]} samples per case; same meshoptimizer ordering policy',fontSize=10))
series = [('Source','1','#808080',[3,2]),('Heap3D','1','#0072B2',[3,2]),
          ('Source','4','#202020',None),('Heap3D','4','#D55E00',None)]
for i,(method,msaa,color,dash) in enumerate(series):
    x=34+i*235
    d.add(Line(x,787,x+22,787,strokeColor=HexColor(color),strokeWidth=1.5,strokeDashArray=dash))
    d.add(String(x+28,783,f'{method}, {msaa}x MSAA',fontSize=10))
models = ['ellipse2d','ellipsoid','suzanne','torus_skew']
for row,model in enumerate(models):
    y=615-row*172
    selected = [r for r in rows if r['Geometry']==model and r['Method'] in ['Source','Heap3D']]
    ymax = max(float(r['Draw_ms']) for r in selected)*1.1
    for axis,name in enumerate(['X','Y','Z']):
        x=53+axis*321
        plot = LinePlot()
        plot.x,plot.y,plot.width,plot.height=x,y,264,112
        plot.xValueAxis.valueMin,plot.xValueAxis.valueMax,plot.xValueAxis.valueStep=0,360,90
        plot.yValueAxis.valueMin,plot.yValueAxis.valueMax=0,ymax
        plot.yValueAxis.labelTextFormat='%.3f'
        plot.xValueAxis.labels.fontSize=8
        plot.yValueAxis.labels.fontSize=8
        plot.data = []
        for method,msaa,color,dash in series:
            points = sorted((int(r['View'])%8*45,float(r['Draw_ms'])) for r in selected
                            if r['Method']==method and r['MSAA']==msaa and int(r['View'])//8==axis)
            assert len(points)==8
            plot.data.append(points)
        for i,(_,_,color,dash) in enumerate(series):
            plot.lines[i].strokeColor=HexColor(color)
            plot.lines[i].strokeWidth=1.2
            plot.lines[i].strokeDashArray=dash
            plot.lines[i].symbol=makeMarker('FilledCircle')
            plot.lines[i].symbol.size=3
        d.add(plot)
        d.add(String(x,y+126,f'{model}: {name} rotation',fontName='Helvetica-Bold',fontSize=11))
        d.add(String(x,y-26,'Angle (degrees)',fontSize=9))
        d.add(String(x-44,y+112,'ms',fontSize=9))
d.add(String(34,28,'All view means shown, including flagged controls. Correlations use eligible unique poses; see unique_poses.csv.',fontSize=9))
d.add(String(34,13,'GPU draw excludes clear, resolve, UI, presentation, diagnostics and readback. Times are not application frame times.',fontSize=9))
renderSVG.drawToFile(d,str(root/'orientation_curves.svg'))
