"""Five explicit fixed-count meshes; deliberately poor connectivity is labeled.

Panel assets have identical-vertex balanced controls on exactly the same surface.
Knot control removes an integer UV shear; ramp control only shortens cell diagonals.
No generated positions or triangle counts change between a source/control pair.
"""
from pathlib import Path
import math,json,hashlib
import numpy as np

root=Path(__file__).parent
assets=root.parents[2]/'Data/Models/ResearchCandidates'
assets.mkdir(parents=True,exist_ok=True)
pi=math.pi

class Panels:
 def __init__(self): self.vertices=[]; self.ids={}; self.source=[]; self.control=[]
 def vertex(self,p):
  key=tuple(round(float(c),12) for c in p)
  if key not in self.ids: self.ids[key]=len(self.vertices); self.vertices.append(key)
  return self.ids[key]
 def panel(self,points,steps):
  p=np.array(points,float); n=len(p)
  normal=np.cross(p[1]-p[0],p[2]-p[0]); normal/=np.linalg.norm(normal)
  assert max(abs((p-p[0])@normal))<1e-10,'Coarse panels must be planar'
  ring=[]
  for i in range(n):
   a,b=self.vertex(p[i]),self.vertex(p[(i+1)%n])
   # Canonical interpolation also welds edges traversed in the reverse direction.
   lo,hi=min(a,b),max(a,b); first=np.array(self.vertices[lo]); last=np.array(self.vertices[hi])
   for j in range(steps):
    t=j if a==lo else steps-j
    ring.append(self.vertex(((steps-t)*first+t*last)/steps))
  weights=np.array([1+.17*math.sin(i+.31)+.09*math.cos(2.1*i) for i in range(n)])
  center=self.vertex(weights@p/weights.sum()); cp=np.array(self.vertices[center])
  self.source += [(center,a,b) for a,b in zip(ring,ring[1:]+ring[:1])]
  def area(a,b,c): return float(np.dot(np.cross(np.array(self.vertices[b])-self.vertices[a],np.array(self.vertices[c])-self.vertices[a]),normal))
  remaining=ring[:]; triangles=[]
  while len(remaining)>3:
   ears=[]
   for i,b in enumerate(remaining):
    a,c=remaining[i-1],remaining[(i+1)%len(remaining)]
    if area(a,b,c)>1e-10:
     length=np.linalg.norm(np.array(self.vertices[a])-self.vertices[c]); ears.append((length,i,(a,b,c)))
   assert ears,'Convex panel ear clipping stalled'
   _,i,t=min(ears); triangles.append(t); remaining.pop(i)
  assert area(*remaining)>1e-13
  triangles.append(tuple(remaining))
  # Reinsert the same interior vertex into one strict interior triangle.
  hit=[]
  for i,(a,b,c) in enumerate(triangles):
   signs=[np.dot(np.cross(np.array(self.vertices[v])-self.vertices[u],cp-self.vertices[u]),normal)
          for u,v in [(a,b),(b,c),(c,a)]]
   if min(signs)>1e-12: hit.append(i)
  assert len(hit)==1,'Interior sample must avoid control diagonals'
  a,b,c=triangles.pop(hit[0]); triangles += [(a,b,center),(b,c,center),(c,a,center)]
  assert len(triangles)==len(ring)
  self.control += triangles

def wing():
 out=Panels(); n=12
 profile=[]
 for j in range(n):
  t=2*pi*j/n; chord=.48*math.cos(t)
  profile.append(np.array([chord,.075*math.sin(t)*(1+.23*math.cos(t))+.025*math.sin(t)**2]))
 rings=[]
 for x,scale,sweep,twist in [(-.75,1,0,0),(.75,.58,.26,.22)]:
  rings.append([np.array([x,sweep+scale*(p[0]*math.cos(twist)-p[1]*math.sin(twist)),
                         scale*(p[0]*math.sin(twist)+p[1]*math.cos(twist))]) for p in profile])
 # This winding is inward for the yz profile; reverse all panels below.
 faces=[list(reversed(rings[0])),rings[1]]
 for j in range(n):
  k=(j+1)%n; a,b,c,d=rings[0][j],rings[0][k],rings[1][k],rings[1][j]
  faces += [[a,b,c],[a,c,d]]
 # Profile orientation in yz is positive: caps above and outward sides are correct.
 for face in faces: out.panel(face,32)
 return out

def gear():
 out=Panels(); n=48
 outer=[]; inner=[]
 for z in [-.13,.13]:
  outer.append([np.array([(.63 if j%4 in [1,2] else .48)*math.cos(2*pi*j/n),
                          (.63 if j%4 in [1,2] else .48)*math.sin(2*pi*j/n),z]) for j in range(n)])
  inner.append([np.array([.20*math.cos(2*pi*j/n),.20*math.sin(2*pi*j/n),z]) for j in range(n)])
 for j in range(n):
  k=(j+1)%n
  for face in [[inner[1][j],outer[1][j],outer[1][k],inner[1][k]],
               [inner[0][k],outer[0][k],outer[0][j],inner[0][j]],
               [outer[0][j],outer[0][k],outer[1][k],outer[1][j]],
               [inner[0][k],inner[0][j],inner[1][j],inner[1][k]]]: out.panel(face,8)
 return out

def arch():
 out=Panels(); n=16
 sections=[]
 for j in range(n+1):
  t=(-.10+1.20*j/n)*pi
  sections.append([np.array([r*math.cos(t),y,r*math.sin(t)]) for r,y in [(.39,-.17),(.76,-.17),(.76,.17),(.39,.17)]])
 # Cross section orientation follows the sweep.
 for j in range(n):
  for k in range(4):
   q=(k+1)%4; out.panel([sections[j][k],sections[j][q],sections[j+1][q],sections[j+1][k]],16)
 out.panel(list(reversed(sections[0])),16); out.panel(sections[-1],16)
 return out

def grid(vertices,cols,rows,index,periodic):
 faces=[]
 for x in range(rows if periodic else rows-1):
  for j in range(cols if periodic else cols-1):
   a,b,c,d=index(x,j),index(x,(j+1)%cols),index((x+1)%rows,(j+1)%cols),index((x+1)%rows,j)
   faces += [(a,b,c),(a,c,d)]
 return faces

def knot():
 rows,cols=384,64; vertices=[]
 for x in range(rows):
  u=2*pi*x/rows; r=2+math.cos(3*u)
  p=.23*np.array([r*math.cos(2*u),r*math.sin(2*u),math.sin(3*u)])
  tangent=np.array([-3*math.sin(3*u)*math.cos(2*u)-2*r*math.sin(2*u),
                    -3*math.sin(3*u)*math.sin(2*u)+2*r*math.cos(2*u),3*math.cos(3*u)])
  tangent/=np.linalg.norm(tangent); a=np.cross(tangent,[0,0,1]); a/=np.linalg.norm(a); b=np.cross(tangent,a)
  for j in range(cols):
   t=2*pi*j/cols; vertices.append(tuple(p+.09*(a*math.cos(t)+b*math.sin(t))))
 source=grid(vertices,cols,rows,lambda x,j:x*cols+(j+2*x)%cols,True)
 control=grid(vertices,cols,rows,lambda x,j:x*cols+j,True)
 # Grid order winds inward around this local frame; orient both outward.
 return vertices,[(a,c,b) for a,b,c in source],[(a,c,b) for a,b,c in control]

def ramp():
 rows,cols=193,41; vertices=[]
 for x in range(rows):
  for j in range(cols):
   v=j/(cols-1); t=4*pi*x/(rows-1)+3*v; r=.16+.46*v
   vertices.append((r*math.cos(t),r*math.sin(t),.075*(t-2*pi)))
 source=grid(vertices,cols,rows,lambda x,j:x*cols+j,False); control=[]
 for i in range(0,len(source),2):
  a,b,c=source[i]; _,_,d=source[i+1]
  if np.linalg.norm(np.array(vertices[a])-vertices[c])>np.linalg.norm(np.array(vertices[b])-vertices[d]):
   control += [(a,b,d),(b,c,d)]
  else: control += source[i:i+2]
 return vertices,source,control

def audit(vertices,faces,closed):
 edges={}; volume=0
 for a,b,c in faces:
  p,q,r=np.array(vertices[a]),np.array(vertices[b]),np.array(vertices[c])
  assert np.linalg.norm(np.cross(q-p,r-p))>1e-12
  volume+=np.dot(p,np.cross(q,r))/6
  for u,v in [(a,b),(b,c),(c,a)]:
   e=min(u,v),max(u,v); count,sign=edges.get(e,(0,0)); edges[e]=(count+1,sign+(1 if u<v else -1))
 assert all(n in [1,2] and (n==1 or s==0) for n,s in edges.values()),'Manifold/winding failure'
 if closed: assert all(n==2 for n,s in edges.values())
 assert len({v for t in faces for v in t})==len(vertices)
 return volume,len([1 for n,s in edges.values() if n==1])

metadata=[]
descriptions={
 'SweptWing':'Closed thin tapered/swept/twisted wing, sharp facets; intentionally dense planar triangle fans; balanced planar control.',
 'ToothedWheel':'Closed concave 12-tooth wheel with a central through-hole, sharp creases; dense planar fans; balanced planar control.',
 'FacetedArch':'Closed U-shaped curved beam with planar front/back panels and inner/outer facets; dense planar fans; balanced planar control.',
 'TrefoilKnot':'Smooth closed self-occluding knot tube; integer helical UV shear of two tube samples per centerline ring; unsheared control.',
 'SpiralRamp':'Open two-turn helicoid with negative Gaussian curvature and sheared UV lattice; control only selects shorter cell diagonals.'}
for name,make in [('SweptWing',wing),('ToothedWheel',gear),('FacetedArch',arch),('TrefoilKnot',knot),('SpiralRamp',ramp)]:
 result=make()
 vertices,source,control=(result.vertices,result.source,result.control) if isinstance(result,Panels) else result
 closed=name!='SpiralRamp'; volume,boundary=audit(vertices,source,closed)
 if closed and volume<0: source=[(a,c,b) for a,b,c in source]; control=[(a,c,b) for a,b,c in control]
 audit(vertices,control,closed); assert len(source)==len(control)
 for suffix,faces in [('',source),('_control',control)]:
  path=assets/(name+suffix+'.obj')
  with path.open('w',newline='\n') as f:
   f.write('# '+descriptions[name]+'\n')
   for v in vertices: f.write('v '+' '.join(format(float(c),'.12g') for c in v)+'\n')
   for t in faces: f.write('f '+' '.join(str(i+1) for i in t)+'\n')
 metadata.append(dict(Name=name,Description=descriptions[name],Vertices=len(vertices),Triangles=len(source),
                      BoundaryEdges=boundary,Closed=closed,SourceSHA256=hashlib.sha256((assets/(name+'.obj')).read_bytes()).hexdigest()))
 print(name,len(vertices),len(source),'boundary',boundary,flush=True)
(root/'models.json').write_text(json.dumps(metadata,indent=2)+'\n')
