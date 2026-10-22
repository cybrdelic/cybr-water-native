#!/usr/bin/env python3
"""Diagnostic orthographic native APIC particle-state visualization. Not a water beauty renderer."""
from pathlib import Path
import numpy as np
import cv2,json
BASE=Path(__file__).resolve().parent
C=BASE/'cache';OUT=BASE/'CYBR_WATERBENDING_TRANSPORT_10S_SOLVER_DIAGNOSTIC.mp4'
W,H=960,540
writer=cv2.VideoWriter(str(OUT),cv2.VideoWriter_fourcc(*'mp4v'),24,(W,H))
assert writer.isOpened()
counts=[]
for f in range(240):
    p=C/f'frame_{f:06d}.bin'
    raw=np.fromfile(p,dtype='<f4',offset=32)
    assert len(raw)%8==0
    data=raw.reshape(-1,8)
    pos=data[:,:3]
    # fixed-world view: x horizontal, y vertical, z contributes mild horizontal shear
    u=(W*.50+pos[:,0]*210+pos[:,2]*58).astype(np.int32)
    v=(H*.83-pos[:,1]*230+pos[:,2]*12).astype(np.int32)
    img=np.zeros((H,W,3),np.uint8)
    img[:]=(20,16,13)
    cv2.line(img,(90,int(H*.83)),(870,int(H*.83)),(68,64,52),1,cv2.LINE_AA)
    order=np.argsort(pos[:,2])[::-1]
    for i in order:
        x,y=u[i],v[i]
        if 2<=x<W-2 and 2<=y<H-2:
            cv2.circle(img,(int(x),int(y)),1,(205,164,82),-1,cv2.LINE_AA)
    cv2.putText(img,'CYBR WATER / APIC-MAC / NUMERICAL STATE',(30,36),cv2.FONT_HERSHEY_SIMPLEX,.64,(210,220,226),1,cv2.LINE_AA)
    cv2.putText(img,f't={f/24:.2f}s  markers={len(pos)}  frame={f+1}/240  dx=0.10m',(30,70),cv2.FONT_HERSHEY_SIMPLEX,.55,(152,170,182),1,cv2.LINE_AA)
    cv2.putText(img,'DIAGNOSTIC ONLY - not final reconstructed or refracted water',(30,H-25),cv2.FONT_HERSHEY_SIMPLEX,.52,(140,146,153),1,cv2.LINE_AA)
    writer.write(img)
    counts.append(len(pos))
    if f in (24,72,120,168,216):cv2.imwrite(str(BASE/f'diagnostic_{f:03}.png'),img)
writer.release()
manifest={'scene':'full_choreographed_waterbending_trick','frames':240,'fps':24,'duration_s':10,'unique_solver_states':240,'fluid_grid_dx_m':.10,'render_type':'particle-state diagnostic, not beauty','min_markers':min(counts),'max_markers':max(counts),'midpoint_markers':counts[120],'source':'cache/frame_*.bin','native_surface_status':'failed volume solver at t=5 seconds; final native beauty render not produced'}
(BASE/'DIAGNOSTIC_VERIFICATION.json').write_text(json.dumps(manifest,indent=2))
print(manifest)
