#!/usr/bin/env python3
"""Native snapshots -> anisotropic surface -> V6 dielectric renderer.
Fresh output per scene/configuration. No generated images or temporal synthesis.
"""
from __future__ import annotations
import argparse, concurrent.futures, hashlib, json, os, subprocess, time
from pathlib import Path
from PIL import Image
from render_contract import make_contract, freeze_contract, check_receipt
ROOT=Path(__file__).resolve().parent

def sha(path:Path)->str:
 h=hashlib.sha256()
 with path.open('rb') as f:
  for block in iter(lambda:f.read(1<<20),b''):h.update(block)
 return h.hexdigest()

def run(cmd:list[str],log:Path)->None:
 with log.open('w') as f:
  p=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT)
 if p.returncode:raise RuntimeError(log.read_text()[-5000:])

def main()->None:
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--take',type=Path,required=True);ap.add_argument('--scene-assets',type=Path,required=True)
 ap.add_argument('--width',type=int,default=960);ap.add_argument('--height',type=int,default=540)
 ap.add_argument('--select',default='');ap.add_argument('--workers',type=int,default=2)
 ap.add_argument('--surface-threads',type=int,default=3)
 ap.add_argument('--samples',type=int,default=4);ap.add_argument('--adaptive-aa',type=int,default=16)
 a=ap.parse_args();take=a.take.resolve();assets=a.scene_assets.resolve()
 frames=[int(x) for x in a.select.split(',')] if a.select else list(range(240))
 if not frames or any(i<0 or i>=240 for i in frames):ap.error('Frame out of range')
 selected=bool(a.select);out=take/('selected' if selected else 'rendered');out.mkdir(parents=True,exist_ok=True)
 logs=out/'logs';work=out/'work';logs.mkdir(exist_ok=True);work.mkdir(exist_ok=True)
 b=ROOT/'engine/build';previous=None;futures=set();start=time.monotonic()
 source={p.name:sha(p) for p in (ROOT/'engine/src').glob('*') if p.is_file()}
 preset=json.loads((take/'cache/gallery_scene.json').read_text());camera=','.join(map(str,preset['camera']));target=','.join(map(str,preset['target']))
 def render_command(mesh:Path,spheres:Path,stem:Path)->list[str]:
  return [str(b/'trace_native'),'--water',str(mesh),'--spheres',str(spheres),'--solids',str(assets/'solids.mesh'),'--out',str(stem),
       '--width',str(a.width),'--height',str(a.height),'--spp',str(a.samples),'--bounces','20','--branch-cutoff','.001',
       '--threads','1','--camera',camera,'--target',target,'--fov',str(preset['vertical_fov_degrees']),
       '--aperture','0','--environment','overcast','--sky-power','1.2','--sun-power','0','--deterministic-preview',
       '--absorption','.18,.045,.025','--wet-level','.20','--wet-reach','.10','--coherent-normals','--oriented-foam',
       '--adaptive-aa',str(a.adaptive_aa),'--aa-threshold','.035','--diffuse-samples','24',
       '--diffuse-cache',str(out/'static_diffuse.bin'),'--background-cache',str(out/'immutable_background.bin')]
 contract=make_contract(source,render_command(work/'identity.mesh',work/'identity.csv',work/'identity'),assets)
 contract['native_binary_sha256']={name:sha(b/name) for name in ('surface','trace_native')}
 freeze_contract(out,contract)
 def render(i:int,mesh:Path,spheres:Path,geometry:dict,source_record:dict,phase_record:dict)->None:
  name=f'frame_{i:06d}';stem=work/name;png=out/(name+'.png')
  cmd=render_command(mesh,spheres,stem)
  run(cmd,logs/(name+'_trace.log'));report=json.loads(stem.with_suffix('.json').read_text())
  if report['nonfinite_paths'] or report['finite_pixels']!=a.width*a.height:raise RuntimeError(f'Nonfinite frame {i}')
  with Image.open(stem.with_suffix('.ppm')) as image:
   if image.size!=(a.width,a.height):raise RuntimeError('Wrong size')
   rgb_sha=hashlib.sha256(image.tobytes()).hexdigest();image.save(png)
  receipt={'frame':i,'time_s':i/24,'selected_frame_review':selected,'png_sha256':sha(png),'rgb_sha256':rgb_sha,
           'source_states':source_record,'source_code_sha256':source,'render_contract_sha256':sha(out/'render_contract.json'),'surface':geometry,'render':report,'command':cmd}
  receipt.update(phase_record)
  tmp=out/(name+'.json.tmp');tmp.write_text(json.dumps(receipt,indent=2));tmp.rename(out/(name+'.json'))
  for f in work.glob(name+'*'):f.unlink()
  print(json.dumps({'frame':i,'elapsed_s':time.monotonic()-start,'surface_s':geometry['seconds'],'trace_s':report['render_seconds'],'triangles':geometry['triangles']}),flush=True)
 with concurrent.futures.ThreadPoolExecutor(max_workers=a.workers) as pool:
  for i in frames:
   while len(futures)>=a.workers+1:
    done,futures=concurrent.futures.wait(futures,return_when=concurrent.futures.FIRST_COMPLETED)
    for f in done:f.result()
   name=f'frame_{i:06d}';src=take/'cache'/(name+'.bin');waiting=time.monotonic()
   while not src.with_suffix('.white').is_file():
    if time.monotonic()-waiting>1800:raise RuntimeError(f'Missing state {i}')
    time.sleep(.5)
   mesh=work/(name+'.mesh');spheres=work/(name+'.csv');phase=out/(name+'.phase')
   receipt=out/(name+'.json');png=out/(name+'.png')
   if receipt.exists() and png.exists() and phase.exists():
    saved=json.loads(receipt.read_text())
    check_receipt(saved,contract)
    if saved.get('render_contract_sha256')!=sha(out/'render_contract.json'):raise RuntimeError('Changed render contract: '+name)
    if saved.get('phase_sha256')!=sha(phase):raise RuntimeError('Missing or changed reconstruction history: '+name)
    expected_previous=sha(previous) if previous is not None and not selected else None
    if saved.get('previous_phase_sha256')!=expected_previous:raise RuntimeError('Broken reconstruction history chain: '+name)
    if saved['source_code_sha256']!=source or saved['png_sha256']!=sha(png):raise RuntimeError('Stale render: '+name)
    if saved['render'].get('width') not in (None,a.width):raise RuntimeError('Resume resolution mismatch')
    if Image.open(png).size!=(a.width,a.height):raise RuntimeError('Resume resolution mismatch')
    if any(saved['source_states'][ext]!=sha(src.with_suffix(ext)) for ext in ('.bin','.white','.ids','.volumes')):raise RuntimeError('Changed state: '+name)
    previous=phase
    continue
   cmd=[str(b/'surface'),'--in',str(src),'--out',str(mesh),'--spheres',str(spheres),'--threads',str(a.surface_threads),
        '--surface-scale','1.2','--support','6','--smooth','4','--normal-smooth','0','--topology-normal-smooth','4',
        '--center-relax','.25','--max-iso-offset','1.5','--solid-field',str(assets/'solid.sdf'),
        '--coherent-fragments','--phase-out',str(phase)]
   if previous is not None and not selected:cmd+=['--phase-in',str(previous)]
   prior_phase_sha=sha(previous) if previous is not None and not selected else None
   run(cmd,logs/(name+'_surface.log'));previous=phase
   phase_record={'phase_sha256':sha(phase),'previous_phase_sha256':prior_phase_sha}
   geometry=json.loads(Path(str(mesh)+'.json').read_text())
   err=abs(geometry['mesh_signed_volume_m3']-geometry['dense_marker_target_volume_m3'])/max(geometry['dense_marker_target_volume_m3'],1e-12)
   if err>.0021:raise RuntimeError(f'Surface volume error frame {i}: {err}')
   geometry['relative_volume_error']=err
   hashes={ext:sha(src.with_suffix(ext)) for ext in ('.bin','.white','.ids','.volumes')}
   fut=pool.submit(render,i,mesh,spheres,geometry,hashes,phase_record);futures.add(fut)
   if i==frames[0]:fut.result();futures.remove(fut)
  for f in futures:f.result()
 completion=out/'complete.json.tmp'
 completion.write_text(json.dumps({'frames':frames,'width':a.width,'height':a.height,'selected':selected,'elapsed_s':time.monotonic()-start},indent=2))
 completion.replace(out/'complete.json')
if __name__=='__main__':main()
