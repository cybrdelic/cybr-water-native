#!/usr/bin/env python3
"""Reproduce the V7 native duet from source. No network, images or pretrained models."""
from __future__ import annotations
import argparse, csv, hashlib, json, math, os, shutil, struct, subprocess, sys, time
from pathlib import Path
ROOT=Path(__file__).resolve().parent

CHECKPOINT_HEADER=struct.Struct('<8sIQfiii3fdd')

def file_sha(path:Path)->str:
 h=hashlib.sha256()
 with path.open('rb') as f:
  for block in iter(lambda:f.read(1<<20),b''):h.update(block)
 return h.hexdigest()

def float32(value:float)->float:
 return struct.unpack('<f',struct.pack('<f',value))[0]

def checkpoint_header(stream)->dict:
 values=CHECKPOINT_HEADER.unpack(stream.read(CHECKPOINT_HEADER.size))
 magic,version,signature,dx,nx,ny,nz,x,y,z,seconds,last_dt=values
 if magic!=b'CYBRFLP4' or version!=4 or not math.isfinite(dx) or dx<=0 or min(nx,ny,nz)<4:
  raise RuntimeError('Unsupported or invalid native checkpoint header')
 if not all(math.isfinite(v) for v in (x,y,z,seconds,last_dt)) or seconds<0:
  raise RuntimeError('Invalid native checkpoint time/domain')
 lower=[x,y,z];grid=[nx,ny,nz]
 upper=[float32(lo+float32(float(n)*dx)) for lo,n in zip(lower,grid)]
 return {'version':version,'signature':signature,'dx':dx,'grid':grid,
         'lower':lower,'upper':upper,'time':seconds,'last_dt':last_dt}

def preserve_frame_checkpoint(cache:Path,fps:float=24.)->bool:
 """Copy the immutable file opened before an atomic producer rename.

 Growth checkpoints can be between video frames. Keep the latest complete
 frame checkpoint separately, because the inherited CLI rounds resume time.
 """
 source=cache/'checkpoint.bin'
 if not source.is_file():return False
 recovery=cache/'recovery';recovery.mkdir(exist_ok=True)
 with source.open('rb') as stream:
  header=checkpoint_header(stream);frame=round(header['time']*fps)
  if abs(header['time']-frame/fps)>1e-9:return False
  receipt=recovery/'frame_checkpoint.json';target=recovery/'frame_checkpoint.bin'
  if receipt.is_file() and target.is_file():
   saved=json.loads(receipt.read_text())
   if all(saved.get(k)==header[k] for k in ('time','signature','grid','lower')):return False
  temporary=target.with_suffix('.bin.tmp');stream.seek(0)
  digest=hashlib.sha256()
  with temporary.open('wb') as destination:
   for block in iter(lambda:stream.read(8<<20),b''):
    digest.update(block);destination.write(block)
   destination.flush();os.fsync(destination.fileno())
  temporary.replace(target)
  header.update(frame=frame,fps=fps,checkpoint_sha256=digest.hexdigest(),bytes=target.stat().st_size)
  receipt_tmp=receipt.with_suffix('.json.tmp');receipt_tmp.write_text(json.dumps(header,indent=2)+'\n');receipt_tmp.replace(receipt)
 return True

def run(cmd:list[str],log:Path,cwd:Path|None=None)->None:
 print(' '.join(cmd),flush=True)
 with log.open('w') as f:p=subprocess.run(cmd,cwd=cwd,stdout=f,stderr=subprocess.STDOUT)
 if p.returncode:raise RuntimeError(f'Command failed; inspect {log}')

def option(command:list[str],flag:str)->str:
 if command.count(flag)!=1:raise RuntimeError('Expected exactly one simulation option '+flag)
 return command[command.index(flag)+1]

def replace_option(command:list[str],flag:str,value:str)->None:
 if command.count(flag)>1:raise RuntimeError('Duplicate simulation option '+flag)
 if flag in command:command[command.index(flag)+1]=value
 else:command.extend([flag,value])

def growth_history(cache:Path)->list[dict]:
 rows=[];seen=set()
 for path in (cache/'recovery/growth_prefix.jsonl',cache/'domain_growth.jsonl'):
  if not path.is_file():continue
  for line in path.read_text().splitlines():
   if not line.strip():continue
   row=json.loads(line);key=json.dumps(row,sort_keys=True)
   if key not in seen:seen.add(key);rows.append(row)
 return rows

def finish_growth_history(cache:Path)->None:
 prefix=cache/'recovery/growth_prefix.jsonl'
 if not prefix.is_file():return
 rows=growth_history(cache);temporary=cache/'domain_growth.jsonl.tmp'
 temporary.write_text(''.join(json.dumps(row,separators=(',',':'))+'\n' for row in rows))
 temporary.replace(cache/'domain_growth.jsonl');prefix.unlink()

def prepare_resume(command:list[str],cache:Path,fps:float=24.)->tuple[list[str],dict]:
 """Roll back only the uncommitted suffix and retain all original bytes.

 No display snapshot is imported. Native C matrices, pressure and secondary
 state come from a complete ABI checkpoint, with exact float32 domain bounds.
 """
 candidates=[]
 for path in (cache/'checkpoint.bin',cache/'recovery/frame_checkpoint.bin'):
  if not path.is_file():continue
  with path.open('rb') as f:header=checkpoint_header(f)
  frame=round(header['time']*fps)
  if abs(header['time']-frame/fps)<=1e-9:candidates.append((header['time'],path,header,frame))
 if not candidates:raise RuntimeError('No exact frame-boundary native checkpoint; refusing to resume by rounding a subframe or importing display particles')
 _,path,header,frame=max(candidates,key=lambda candidate:candidate[0])
 if frame>round(float(option(command,'--duration'))*fps):raise RuntimeError('Checkpoint is beyond the requested simulation')
 if abs(header['dx']-float(option(command,'--dx')))>1e-8:raise RuntimeError('Checkpoint grid spacing differs from the recorded command')
 # Every retained prefix state must exist before any suffix is moved.
 for i in range(frame+1):
  for extension in ('.bin','.white','.ids','.volumes'):
   if not (cache/f'frame_{i:06d}{extension}').is_file():raise RuntimeError(f'Missing committed prefix frame {i}{extension}')
 history=growth_history(cache);prefix=[]
 for row in history:
  t=float(row['time'])
  same_domain=all(abs(float(a)-float(b))<=1e-6 for k in ('lower','upper') for a,b in zip(row[k],header[k]))
  if t<header['time']-1e-9 or (abs(t-header['time'])<=1e-9 and same_domain):prefix.append(row)
 backup=cache/'recovery'/('resume_'+str(time.time_ns()));backup.mkdir(parents=True)
 immutable=backup/'checkpoint.bin';shutil.copyfile(path,immutable)
 # Keep every pre-recovery CSV, old completion record, and superseded state.
 for name in ('diagnostics.csv','retention.csv','domain_audit.csv','domain_growth.jsonl','complete.json','manifest.json'):
  file=cache/name
  if file.exists():shutil.copy2(file,backup/name)
 for name in ('diagnostics.csv','retention.csv','domain_audit.csv'):
  file=cache/name
  if not file.is_file():raise RuntimeError('Missing native ledger '+name)
  with file.open(newline='') as stream:reader=csv.DictReader(stream);fields=reader.fieldnames;rows=list(reader)
  if not fields or 'time' not in fields:raise RuntimeError('Malformed native ledger '+name)
  keep=[row for row in rows if float(row['time'])<header['time']-1e-9] if name=='diagnostics.csv' else [row for row in rows if int(row['frame'])<=frame]
  temporary=file.with_suffix(file.suffix+'.resume.tmp')
  with temporary.open('w',newline='') as stream:
   writer=csv.DictWriter(stream,fieldnames=fields);writer.writeheader();writer.writerows(keep)
  temporary.replace(file)
 for state in sorted(cache.glob('frame_*')):
  try:index=int(state.name.split('.')[0].removeprefix('frame_'))
  except ValueError:continue
  if index>frame:state.replace(backup/state.name)
 if (cache/'complete.json').exists():(cache/'complete.json').unlink()
 (cache/'recovery/growth_prefix.jsonl').write_text(''.join(json.dumps(row,separators=(',',':'))+'\n' for row in prefix))
 resumed=list(command)
 # Original arguments retain the exact thread count, operators and force scene.
 replace_option(resumed,'--out',str(cache))
 replace_option(resumed,'--domain-lower',','.join(format(v,'.9g') for v in header['lower']))
 replace_option(resumed,'--domain-upper',','.join(format(v,'.9g') for v in header['upper']))
 replace_option(resumed,'--resume',str(immutable))
 if '--skip-initial' not in resumed:resumed.append('--skip-initial')
 receipt={**header,'frame':frame,'checkpoint_sha256':file_sha(immutable),'backup_directory':str(backup),'command':resumed}
 (backup/'RECOVERY.json').write_text(json.dumps(receipt,indent=2)+'\n')
 return resumed,receipt

def run_simulation(command:list[str],log:Path,cache:Path)->None:
 print(' '.join(command),flush=True)
 with log.open('w') as stream:
  process=subprocess.Popen(command,stdout=stream,stderr=subprocess.STDOUT)
  try:
   while process.poll() is None:
    preserve_frame_checkpoint(cache,float(option(command,'--fps')))
    time.sleep(.5)
   preserve_frame_checkpoint(cache,float(option(command,'--fps')))
  except BaseException:
   process.terminate();process.wait();raise
  finally:finish_growth_history(cache)
 if process.returncode:raise RuntimeError(f'Native simulation failed; a frame-boundary recovery checkpoint is retained. Inspect {log}')

def main()->None:
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--out',type=Path,default=ROOT/'runs/duet');ap.add_argument('--tests-only',action='store_true')
 continuation=ap.add_mutually_exclusive_group()
 continuation.add_argument('--skip-simulation',action='store_true');continuation.add_argument('--resume',action='store_true')
 ap.add_argument('--simulation-only',action='store_true');ap.add_argument('--width',type=int,default=960);ap.add_argument('--height',type=int,default=540)
 ap.add_argument('--threads',type=int,default=2);ap.add_argument('--workers',type=int,default=2)
 a=ap.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True);logs=out/'logs';logs.mkdir(exist_ok=True)
 if not 1<=a.threads<=32 or not 1<=a.workers<=16:ap.error('Thread/worker count out of range')
 if a.width<32 or a.height<32 or a.width%2 or a.height%2:ap.error('Even image dimensions >=32 required')
 engine=ROOT/'engine'
 if not (engine/'src/fluid.hpp').exists():raise RuntimeError('Run python3 scripts/materialize_native.py from repository root first')
 if a.resume:
  # Recompilation can invalidate the checkpoint's executable fingerprint.
  for name in ('gallery_sim','gallery_export','surface','trace_native'):
   if not (engine/'build'/name).is_file():raise RuntimeError('Restore the original native binary before resuming: '+name)
 else:run(['make','-j2','all','build/trace_native','checks','finish-checks','continuation-checks','choreography-checks'],logs/'build_and_tests.log',engine)
 if a.tests_only:return
 b=engine/'build';assets=out/'scene';assets.mkdir(exist_ok=True)
 if not (assets/'solids.mesh').exists() or not (assets/'solid.sdf').exists():
  run([str(b/'gallery_export'),'--scene','waterbending_duet','--out',str(assets/'solids.mesh'),'--quality','1','--sdf-out',str(assets/'solid.sdf'),'--sdf-dx','.025','--sdf-lower','-5,-.12,-5','--sdf-upper','5,3,8'],logs/'scene_export.log')
 cmd=[str(b/'gallery_sim'),'--scene','waterbending_duet','--dx','.03','--duration','10','--fps','24','--threads',str(a.threads),'--out',str(out/'cache'),'--apic','--particles-per-axis','3','--reseed-interval','0','--whitewater-rate','24000','--max-whitewater','180000','--surface-tension','.072','--auto-domain','--domain-padding','.48','--domain-lower','-1.98,-.12,-1.50','--domain-upper','1.98,2.82,1.50']
 if a.resume:
  original=json.loads((out/'simulation_command.json').read_text())
  original[0]=str(b/'gallery_sim')
  complete=out/'cache/complete.json'
  finished=complete.is_file() and json.loads(complete.read_text()).get('frame')==240
  if not finished:
   resumed,recovery=prepare_resume(original,out/'cache',float(option(original,'--fps')))
   print(f'Resuming full native checkpoint at frame {recovery["frame"]}; original suffix retained in {recovery["backup_directory"]}',flush=True)
   run_simulation(resumed,logs/('simulation_resume_'+str(time.time_ns())+'.log'),out/'cache')
 elif not a.skip_simulation:
  if any((out/'cache').glob('frame_*.bin')):raise RuntimeError('Use a fresh --out; existing fluid states are never overwritten')
  (out/'simulation_command.json').write_text(json.dumps(cmd,indent=2));run_simulation(cmd,logs/'simulation.log',out/'cache')
 if not (out/'cache/complete.json').exists():raise RuntimeError('Completed source cache required')
 if a.simulation_only:return
 run([sys.executable,str(ROOT/'render_take.py'),'--take',str(out),'--scene-assets',str(assets),'--width',str(a.width),'--height',str(a.height),'--workers',str(a.workers)],logs/'native_render.log')
 run([sys.executable,str(ROOT/'verify_export.py'),'--take',str(out)],logs/'export_verification.log')
 print(out/'delivery/CYBR_WATERBENDING_DUET_V7_10S.mp4')
if __name__=='__main__':main()
