import os,subprocess,time,json,cv2,hashlib,sys
from pathlib import Path
base=Path(__file__).resolve().parents[1]; eng=base/'engine'; out=base/'CYBR_WATERBENDING_FULL_TRICK_NATIVE_10S_24FPS.mp4'
w,h=320,180
startframe=int(sys.argv[1]); endframe=int(sys.argv[2]); out=base/f'batch_{startframe:03}_{endframe:03}.mp4'; writer=cv2.VideoWriter(str(out),cv2.VideoWriter_fourcc(*'mp4v'),24,(w,h))
if not writer.isOpened():raise RuntimeError('Video writer unavailable')
records=[]; start=time.monotonic()
for frame in range(startframe,endframe):
    snapshot=base/'cache'/f'frame_{frame:06}.bin'
    mesh=base/'current.mesh'; stem=base/'current_render'
    su=subprocess.run([str(eng/'build/surface'),'--in',str(snapshot),'--out',str(mesh),'--threads','4','--surface-scale','1','--support','2.4','--smooth','0'],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    if su.returncode: raise RuntimeError(f'surface frame {frame}: {su.stderr.decode()[-500:]}')
    tr=subprocess.run([str(eng/'build/trace'),'--water',str(mesh),'--solids',str(base/'solids.mesh'),'--out',str(stem),'--width',str(w),'--height',str(h),'--spp','4','--bounces','8','--threads','4','--camera','1.45,1.85,3.55','--target','0,.83,0','--fov','29','--aperture','0','--environment','neutral'],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    if tr.returncode:raise RuntimeError(f'trace frame {frame}: {tr.stderr.decode()[-500:]}')
    raw=cv2.imread(str(stem)+'.ppm')
    if raw is None:raise RuntimeError(f'frame read failed {frame}')
    writer.write(raw)
    if frame in (0,24,48,72,96,120,144,168,192,216,239):cv2.imwrite(str(base/f'frame_{frame:03}.png'),raw)
    records.append({'frame':frame,'source_frame':frame,'time_s':frame/24,'sha256':hashlib.sha256(raw.tobytes()).hexdigest()})
    if frame%30==0:print('RENDER',frame,'elapsed',round(time.monotonic()-start,1),flush=True)
writer.release()
print('COMPLETE',out,flush=True)
