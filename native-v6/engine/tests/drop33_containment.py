#!/usr/bin/env python3
"""Targeted read-only containment/distance check for the two drop33 spheres."""
import csv,hashlib,json,math,struct
from pathlib import Path
import numpy as np

root=Path(__file__).resolve().parents[1];out=root/'captures/drop33_regression'
mesh=out/'water.mesh';spheres=out/'whitewater.csv'
header=struct.Struct('<8sIIQ');dtype=np.dtype([('vertices','<f4',(3,6)),('material','<u4')])
magic,version,flags,n=header.unpack(mesh.open('rb').read(header.size))
assert (magic,version,flags)==(b'CYBRMSH1',1,0)
assert mesh.stat().st_size==header.size+n*dtype.itemsize
records=np.memmap(mesh,dtype=dtype,mode='r',offset=header.size,shape=(n,))
with spheres.open() as f:rows=list(csv.DictReader(f))
assert len(rows)==2 and all(int(q['type'])==0 for q in rows)
dot=lambda a,b:np.einsum('ij,ij->i',a,b)
results=[];bounds_lo=np.full(3,np.inf);bounds_hi=np.full(3,-np.inf)
for si,row in enumerate(rows):
    # Match appendSpheres' float32 parse before the double-precision audit.
    center=np.array([float(row[k]) for k in ['x','y','z']],dtype=np.float32).astype(np.float64);radius=float(np.float32(row['r']))
    min_distance2=np.inf;nearest=-1;solid_angles=[]
    for start in range(0,n,100000):
        p=np.array(records['vertices'][start:start+100000,:,:3],dtype=np.float64)
        bounds_lo=np.minimum(bounds_lo,p.min(axis=(0,1)));bounds_hi=np.maximum(bounds_hi,p.max(axis=(0,1)))
        a,b,c=(p[:,i]-center for i in range(3))
        la,lb,lc=(np.linalg.norm(q,axis=1) for q in [a,b,c])
        angle=2*np.arctan2(dot(a,np.cross(b,c)),la*lb*lc+dot(a,b)*lc+dot(b,c)*la+dot(c,a)*lb)
        solid_angles.append(math.fsum(angle.tolist()))
        ab=b-a;ac=c-a;normal=np.cross(ab,ac);n2=dot(normal,normal)
        numerator=dot(a,normal)
        projected=normal*(numerator/np.where(n2>0,n2,1))[:,None]
        inside=(dot(np.cross(ab,projected-a),normal)>=0)&(dot(np.cross(c-b,projected-b),normal)>=0)&(dot(np.cross(a-c,projected-c),normal)>=0)&(n2>0)
        dist2=np.where(inside,numerator*numerator/np.where(n2>0,n2,1),np.inf)
        for u,v in [(a,b),(b,c),(c,a)]:
            edge=v-u;denom=dot(edge,edge);t=np.clip(-dot(u,edge)/np.where(denom>0,denom,1),0,1)
            closest=u+edge*t[:,None];dist2=np.minimum(dist2,dot(closest,closest))
        best=int(np.argmin(dist2))
        if dist2[best]<min_distance2:min_distance2=float(dist2[best]);nearest=start+best
    distance=math.sqrt(min_distance2);winding=math.fsum(solid_angles)/(4*math.pi)
    results.append({'sphere':si,'center_m':center.tolist(),'radius_m':radius,'mesh_winding_number_at_center':winding,'minimum_center_to_triangle_distance_m':distance,'nearest_triangle_index':nearest,'sphere_to_mesh_clearance_m':distance-radius,'outside_mesh_and_separate':abs(winding)<1e-6 and distance>radius})
centers=[np.array(r['center_m']) for r in results]
pair_gap=float(np.linalg.norm(centers[0]-centers[1])-sum(r['radius_m'] for r in results))
camera=np.array([.088,.079,.142],dtype=np.float32).astype(np.float64);camera_outside_bounds=bool(np.any((camera<bounds_lo)|(camera>bounds_hi)))
report={'passed':all(r['outside_mesh_and_separate'] for r in results) and pair_gap>0 and camera_outside_bounds,'mesh_sha256':hashlib.sha256(mesh.read_bytes()).hexdigest(),'spheres_sha256':hashlib.sha256(spheres.read_bytes()).hexdigest(),'triangles':int(n),'spheres':results,'sphere_pair_surface_gap_m':pair_gap,'camera_m':camera.tolist(),'camera_outside_mesh_aabb':camera_outside_bounds,'mesh_bounds_m':[bounds_lo.tolist(),bounds_hi.tolist()],'method':'Float64 exact stored triangle positions; point-to-triangle minimum over every face; compensated solid-angle sum. No spatial welding or face filtering.','scope':'Only these two isolated primary spheres and the stated camera; no general self-intersection certificate.'}
(out/'containment.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report));raise SystemExit(0 if report['passed'] else 1)
