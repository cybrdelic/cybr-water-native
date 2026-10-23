#!/usr/bin/env python3
"""Render actual native 24-Hz states with resumable per-frame evidence and meshes."""
from __future__ import annotations
import argparse,concurrent.futures,gzip,hashlib,json,shutil,subprocess,threading,time
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parent
PRINT=threading.Lock()
def sha(p:Path)->str:
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
def write_json(p:Path,data:dict)->None:
 t=p.with_suffix(p.suffix+'.tmp');t.write_text(json.dumps(data,indent=2));t.replace(p)
def run(cmd:list[str],log:Path)->None:
 with log.open('w') as f:r=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT)
 if r.returncode:raise RuntimeError(f'Native command failed ({r.returncode}): {cmd}\n{log.read_text()[-4000:]}')
def main()->None:
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--start',type=int,default=0);ap.add_argument('--end',type=int,default=240)
 ap.add_argument('--width',type=int,default=960);ap.add_argument('--height',type=int,default=540)
 ap.add_argument('--workers',type=int,default=2);ap.add_argument('--render-threads',type=int,default=1);ap.add_argument('--surface-threads',type=int,default=1);ap.add_argument('--portable',action='store_true')
 a=ap.parse_args()
 if not 0<=a.start<a.end<=240 or not 1<=a.workers<=4:ap.error('Invalid frame interval or workers')
 out=ROOT/'rendered';work=out/'work';logs=out/'logs';phases=out/'phases';cache=ROOT/'mesh_cache';b=ROOT/'engine/build'
 for p in(out,work,logs,phases,cache,ROOT/'delivery'):p.mkdir(parents=True,exist_ok=True)
 trace=b/('trace' if a.portable else 'trace_native');surface=b/'surface'
 sources={n:sha(b.parent/'src'/n) for n in ('trace.cpp','surface.cpp','fluid.hpp','gallery_scenes.hpp')}
 binaries={'trace':sha(trace),'surface':sha(surface)}
 previous=phases/f'phase_{a.start-1:06d}.bin' if a.start else None
 if previous and not previous.exists():raise RuntimeError('Missing previous fragment state')
 started=time.monotonic()
 def render(i:int,mesh:Path,spheres:Path,reconstruction:dict,source:dict)->int:
  name=f'frame_{i:06d}';stem=work/name;png=out/(name+'.png')
  cmd=[str(trace),'--water',str(mesh),'--spheres',str(spheres),'--solids',str(ROOT/'scene/solids.mesh'),'--out',str(stem),
       '--width',str(a.width),'--height',str(a.height),'--spp','4','--bounces','20','--branch-cutoff','.001','--threads',str(a.render_threads),
       '--camera','1.9,1.65,4.6','--target','0,.78,0','--fov','29','--aperture','0','--environment','overcast','--sky-power','1.2',
       '--sun-power','0','--deterministic-preview','--absorption','.18,.045,.025','--wet-level','.20','--wet-reach','.10',
       '--coherent-normals','--oriented-foam','--adaptive-aa','16','--aa-threshold','.035',
       '--diffuse-samples','24','--diffuse-cache',str(out/'static_diffuse.bin'),'--background-cache',str(out/'immutable_background.bin')]
  run(cmd,logs/(name+'_trace.log'));report=json.loads(stem.with_suffix('.json').read_text())
  if report['nonfinite_paths'] or report['finite_pixels']!=a.width*a.height:raise RuntimeError(f'Nonfinite frame {i}')
  with Image.open(stem.with_suffix('.ppm')) as im:
   if im.size!=(a.width,a.height):raise RuntimeError('Wrong dimensions')
   im.save(png)
  rec={'frame':i,'simulation_time_s':i/24,'source':source,'png_sha256':sha(png),'source_code_sha256':sources,'binary_sha256':binaries,
       'surface':reconstruction,'render':report,'render_command':cmd,'mesh_sha256':sha(mesh),'sphere_csv_sha256':sha(spheres)}
  target=cache/(name+'.mesh.gz');temp=target.with_suffix('.tmp')
  with mesh.open('rb') as f,gzip.open(temp,'wb',compresslevel=1) as z:shutil.copyfileobj(f,z,1048576)
  temp.replace(target);shutil.copyfile(spheres,cache/(name+'.csv'));rec['mesh_gzip_sha256']=sha(target)
  write_json(out/(name+'.json'),rec)
  for p in work.glob(name+'*'):
   if p.is_file():p.unlink()
  with PRINT:print(json.dumps({'frame':i,'elapsed_s':round(time.monotonic()-started,2),'surface_s':reconstruction['seconds'],'render_s':report['render_seconds'],'triangles':reconstruction['triangles']}),flush=True)
  return i
 futures=set()
 try:
  with concurrent.futures.ThreadPoolExecutor(max_workers=a.workers) as pool:
   for i in range(a.start,a.end):
    name=f'frame_{i:06d}';src=ROOT/'cache'/(name+'.bin');phase=phases/f'phase_{i:06d}.bin';receipt=out/(name+'.json');png=out/(name+'.png')
    if receipt.exists() and png.exists() and phase.exists():
     record=json.loads(receipt.read_text())
     if record['source_code_sha256']!=sources or record['binary_sha256']!=binaries or record['png_sha256']!=sha(png):raise RuntimeError(f'Unverified/stale existing frame {i}')
     previous=phase;continue
    while len(futures)>=a.workers+1: # allow one reconstructed frame ready ahead of ray workers
     done,futures=concurrent.futures.wait(futures,return_when=concurrent.futures.FIRST_COMPLETED)
     for f in done:f.result()
    waiting=time.monotonic()
    while not src.with_suffix('.white').exists():
     if time.monotonic()-waiting>900:raise RuntimeError(f'Simulation state {i} unavailable')
     time.sleep(.5)
    mesh=work/(name+'.mesh');spheres=work/(name+'.csv')
    cmd=[str(surface),'--in',str(src),'--out',str(mesh),'--spheres',str(spheres),'--threads',str(a.surface_threads),'--surface-scale','1.2','--support','6',
         '--smooth','4','--normal-smooth','0','--topology-normal-smooth','4','--center-relax','.25','--max-iso-offset','1.5',
         '--solid-field',str(ROOT/'scene/solid.sdf'),'--coherent-fragments','--phase-out',str(phase)]
    if previous:cmd+=['--phase-in',str(previous)]
    run(cmd,logs/(name+'_surface.log'));previous=phase
    surf=json.loads(Path(str(mesh)+'.json').read_text());expected=surf['dense_marker_target_volume_m3'];error=abs(surf['mesh_signed_volume_m3']-expected)/max(expected,1e-12)
    if error>.0021:raise RuntimeError(f'Frame {i}: volume error {error}')
    surf['verified_relative_volume_error']=error;surf['command']=cmd
    source={ext:sha(src.with_suffix(ext)) for ext in ('.bin','.white','.ids','.volumes')}
    future=pool.submit(render,i,mesh,spheres,surf,source);futures.add(future)
    if i==0:future.result();futures.remove(future) # one immutable-cache writer
   for f in futures:f.result()
  write_json(out/'complete.json',{'status':'rendered','start':a.start,'end_exclusive':a.end,'elapsed_s':time.monotonic()-started,'width':a.width,'height':a.height,'source_code_sha256':sources,'binary_sha256':binaries})
  print('RENDER_COMPLETE',flush=True)
 except Exception as e:
  write_json(out/'failure.json',{'status':'failed','error':str(e),'elapsed_s':time.monotonic()-started});raise
if __name__=='__main__':main()
