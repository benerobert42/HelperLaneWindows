"""Offline OBJ fixtures from GraphEx implicit equations; no runtime SDF importer.

Equations: GraphEx/Shaders/Example/Lipschitz/Samplers/Procedures/Implicits/
heart.slang and implicit_TangleCube.slang. A fixed tetrahedral grid samples their
zero sets. These are polygonal approximations, not GraphEx ray-traced renderings.
"""
from pathlib import Path
import numpy as np

root = Path(__file__).resolve().parents[3]/'Data/Models'
root.mkdir(parents=True,exist_ok=True)
def heart(p):
    x,y,z = p[:,0],p[:,2],p[:,1]  # GraphEx XYZ -> XZY
    return (x*x+2.25*y*y+z*z-1)**3-x*x*z**3-0.1125*y*y*z**3
def tangle(p):
    return np.sum(p**4-5*p*p,axis=1)+11.8

def export(name, field, radius, n=40):
    axis = np.linspace(-radius,radius,n)+radius*0.00013
    points = np.stack(np.meshgrid(axis,axis,axis,indexing='ij'),axis=-1).reshape(-1,3)
    values = field(points)
    # Six tetrahedra around one cube diagonal; adjacent cubes share face diagonals.
    offsets = [dx*n*n+dy*n+dz for dx,dy,dz in [(0,0,0),(1,0,0),(0,1,0),(1,1,0),
                                               (0,0,1),(1,0,1),(0,1,1),(1,1,1)]]
    bases = np.array([x*n*n+y*n+z for x in range(n-1) for y in range(n-1) for z in range(n-1)])
    cubes = bases[:,None]+offsets
    signs = values[cubes]<0
    cubes = cubes[np.any(signs,axis=1) & ~np.all(signs,axis=1)]
    vertices, faces, edges = [], [], {}
    def vertex(a,b):
        key = tuple(sorted((int(a),int(b))))
        if key not in edges:
            edges[key]=len(vertices)
            t=values[a]/(values[a]-values[b])
            vertices.append(points[a]+t*(points[b]-points[a]))
        return edges[key]
    for cube in cubes:
        for tet in [(0,1,3,7),(0,3,2,7),(0,2,6,7),(0,6,4,7),(0,4,5,7),(0,5,1,7)]:
            ids = [cube[i] for i in tet]
            inside = [i for i in ids if values[i]<0]
            outside = [i for i in ids if values[i]>=0]
            if not inside or not outside: continue
            if len(inside)==2:
                a,d=inside; b,c=outside
                polygon=[vertex(a,b),vertex(a,c),vertex(d,c),vertex(d,b)]
            else:
                one,many=(inside,outside) if len(inside)==1 else (outside,inside)
                polygon=[vertex(one[0],b) for b in many]
            direction = np.mean(points[outside],axis=0)-np.mean(points[inside],axis=0)
            for i in range(1,len(polygon)-1):
                a,b,c=polygon[0],polygon[i],polygon[i+1]
                normal=np.cross(vertices[b]-vertices[a],vertices[c]-vertices[a])
                if np.dot(normal,direction)<0: b,c=c,b
                faces.append((a,b,c))
    # Fail an incomplete extraction rather than benchmarking a cropped zero set.
    incidence = {}
    for a,b,c in faces:
        for edge in [(a,b),(b,c),(c,a)]:
            key=tuple(sorted(edge)); incidence[key]=incidence.get(key,0)+1
    assert all(count==2 for count in incidence.values()), 'Non-closed extraction'
    with (root/(name+'.obj')).open('w') as f:
        f.write('# Offline GraphEx zero-set fixture, 40^3 samples, marching tetrahedra\n')
        for p in vertices: f.write('v '+' '.join(f'{v:.9g}' for v in p)+'\n')
        for face in faces: f.write('f '+' '.join(str(i+1) for i in face)+'\n')
    print(f'{name}: {len(vertices)} vertices, {len(faces)} triangles')
export('GraphExHeart',heart,1.6)
export('GraphExTangleCube',tangle,3.1)
