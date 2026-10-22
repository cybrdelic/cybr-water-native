#!/usr/bin/env python3
"""Exact rational winding check of the case table read from the actual source."""
import ast
from fractions import Fraction as F
import hashlib
import json
from pathlib import Path
import re

root=Path(__file__).resolve().parents[1]
source=(root/'src/surface.cpp').read_text()
def array(name,shape):
    text=re.search(re.escape(name+shape)+'=(\\{.*?\\});',source).group(1)
    return ast.literal_eval(text.replace('{','[').replace('}',']').replace('false','False').replace('true','True'))
corners=array('corner','[8][3]');tets=array('tets','[6][4]');reverse=array('reverseCase','[16]')
def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def centroid(ids,p):return tuple(sum(p[i][a] for i in ids)/len(ids) for a in range(3))
configs=0;triangles=0;failures=[];determinants=[]
patterns=[(1,1,1,1),(1,2,3,4),(F(1,1<<60),1,2,3),(1,1<<60,7,31)]
for ti,tet in enumerate(tets):
    p=[tuple(F(x) for x in corners[k]) for k in tet]
    determinants.append(dot(sub(p[1],p[0]),cross(sub(p[2],p[0]),sub(p[3],p[0]))))
    for mask in range(1,15):
        inside=[i for i in range(4) if mask&(1<<i)];outside=[i for i in range(4) if not mask&(1<<i)]
        for pattern in patterns:
            configs+=1;v=[F(pattern[i])*(-1 if i in inside else 1) for i in range(4)]
            def vertex(a,b):
                t=v[a]/(v[a]-v[b]);return tuple(p[a][j]+(p[b][j]-p[a][j])*t for j in range(3))
            if len(inside)==1:faces=[tuple(vertex(inside[0],i) for i in outside)]
            elif len(inside)==3:faces=[tuple(vertex(outside[0],i) for i in inside)]
            else:
                a=vertex(inside[0],outside[0]);b=vertex(inside[1],outside[0])
                c=vertex(inside[1],outside[1]);d=vertex(inside[0],outside[1]);faces=[(a,b,c),(a,c,d)]
            outward=sub(centroid(outside,p),centroid(inside,p))
            for a,b,c in faces:
                triangles+=1
                if reverse[mask]:b,c=c,b
                if dot(cross(sub(b,a),sub(c,a)),outward)<=0:failures.append({'tet':ti,'mask':mask})
passed=all(x==1 for x in determinants) and not failures and configs==336 and triangles==480
report={'passed':passed,'source_sha256':hashlib.sha256(source.encode()).hexdigest(),'tet_determinants':[int(x) for x in determinants],'configurations':configs,'triangle_orientation_checks':triangles,'failures':failures,'arithmetic':'Exact fractions; case table and tetrahedra parsed from current surface.cpp.'}
(root/'evidence/surface_winding_cases.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report));raise SystemExit(0 if passed else 1)
