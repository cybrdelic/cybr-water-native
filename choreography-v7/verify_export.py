#!/usr/bin/env python3
"""Publish only a complete, unique, timestamp-verified native 240-frame movie."""
from __future__ import annotations
import argparse, csv, hashlib, json, math, subprocess, sys
from fractions import Fraction
from pathlib import Path
from PIL import Image, ImageDraw
from render_contract import check_receipt
ROOT=Path(__file__).resolve().parent

def sha(p:Path)->str:
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
def call(cmd:list[str])->str:
 p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 if p.returncode:raise RuntimeError(p.stderr[-8000:])
 return p.stdout

def verify_receipt(record:dict,contract:dict,contract_sha:str,frame:int,
                   state_hashes:dict,phase_sha:str,previous_phase_sha:str|None)->None:
 """Validate independent state, history and quality evidence before encoding."""
 if record['frame']!=frame or abs(record['time_s']-frame/24)>1e-10:raise RuntimeError('Wrong source time')
 if record.get('selected_frame_review') is not False:raise RuntimeError('Selected review cannot enter a full take')
 check_receipt(record,contract)
 if record.get('render_contract_sha256')!=contract_sha:raise RuntimeError('Changed or mixed render contract')
 if record.get('source_states')!=state_hashes:raise RuntimeError('Native state changed after rendering')
 if record.get('phase_sha256')!=phase_sha:raise RuntimeError('Changed reconstruction phase state')
 if record.get('previous_phase_sha256')!=previous_phase_sha:raise RuntimeError('Broken reconstruction history chain')
 surface=record['surface'];target=surface['dense_marker_target_volume_m3']
 error=abs(surface['mesh_signed_volume_m3']-target)/max(target,1e-12)
 if not math.isfinite(error) or error>.0021 or abs(error-surface['relative_volume_error'])>1e-12:
  raise RuntimeError('Invalid native surface-volume evidence')
 render=record['render'];options=contract['visual_options']
 if render['nonfinite_paths'] or render['finite_pixels']!=int(options['--width'])*int(options['--height']):
  raise RuntimeError('Invalid native pixel/path evidence')

def main()->None:
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--take',type=Path,required=True);a=ap.parse_args()
 take=a.take.resolve();rendered=take/'rendered';delivery=take/'delivery';delivery.mkdir(exist_ok=True)
 contract_path=rendered/'render_contract.json';contract=json.loads(contract_path.read_text());contract_sha=sha(contract_path)
 source_now={p.name:sha(p) for p in (ROOT/'engine/src').glob('*') if p.is_file()}
 if source_now!=contract['source_code_sha256']:raise RuntimeError('Current source differs from the rendered source')
 for name,expected in contract['scene_assets_sha256'].items():
  if sha(take/'scene'/name)!=expected:raise RuntimeError('Changed static scene geometry')
 binary_hashes={name:sha(ROOT/'engine/build'/name) for name in ('surface','trace_native')}
 if contract.get('native_binary_sha256')!=binary_hashes:raise RuntimeError('Changed or unrecorded native rendering binary')
 # The full audit reads every raw state once and supplies its actual file hashes.
 audit_log=call([sys.executable,str(ROOT/'audit_take.py'),'--take',str(take)])
 (delivery/'simulation_audit.log').write_text(audit_log)
 audit_path=delivery/'SIMULATION_AUDIT.json';audit=json.loads(audit_path.read_text())
 if audit.get('passed') is not True:raise RuntimeError('Native simulation audit failed')
 state_records=audit['state_file_sha256']
 if len(state_records)!=241 or any(r['frame']!=i for i,r in enumerate(state_records)):raise RuntimeError('Incomplete native audit identities')
 complete=json.loads((rendered/'complete.json').read_text())
 if complete['frames']!=list(range(240)) or complete['selected']:raise RuntimeError('Incomplete native render')
 w,h=complete['width'],complete['height'];rows=[];hashes=[];source=None;previous_phase_sha=None
 if (w,h)!=(int(contract['visual_options']['--width']),int(contract['visual_options']['--height'])):raise RuntimeError('Completion resolution differs from contract')
 for i in range(240):
  name=f'frame_{i:06d}';record=json.loads((rendered/(name+'.json')).read_text());png=rendered/(name+'.png')
  phase_sha=sha(rendered/(name+'.phase'))
  verify_receipt(record,contract,contract_sha,i,state_records[i]['sha256'],phase_sha,previous_phase_sha)
  previous_phase_sha=phase_sha
  if record['png_sha256']!=sha(png):raise RuntimeError('Changed native frame')
  with Image.open(png) as im:
   if im.size!=(w,h):raise RuntimeError('Mixed resolution')
   rgb=hashlib.sha256(im.convert('RGB').tobytes()).hexdigest()
  if rgb!=record['rgb_sha256']:raise RuntimeError('Changed pixel payload')
  if source is None:source=record['source_code_sha256']
  if source!=record['source_code_sha256']:raise RuntimeError('Mixed source revision')
  if record['render']['nonfinite_paths']:raise RuntimeError('Nonfinite native trace')
  hashes.append(rgb);rows.append(record)
 if len(set(hashes))!=240:raise RuntimeError('Repeated source frames')
 movie=delivery/'CYBR_WATERBENDING_DUET_V7_10S.mp4';temp=delivery/'movie.pending.mp4'
 call(['ffmpeg','-v','error','-y','-framerate','24','-start_number','0','-i',str(rendered/'frame_%06d.png'),'-frames:v','240','-an','-c:v','libx264','-preset','slow','-crf','17','-pix_fmt','yuv420p','-movflags','+faststart',str(temp)])
 probe=json.loads(call(['ffprobe','-v','error','-select_streams','v:0','-show_streams','-show_frames','-of','json',str(temp)]))
 s=probe['streams'][0];frames=probe['frames']
 if len(frames)!=240 or Fraction(s['avg_frame_rate'])!=24 or abs(float(s['duration'])-10)>1e-8:raise RuntimeError('Video timing/length failed')
 for i,f in enumerate(frames):
  if abs(float(f['best_effort_timestamp_time'])-i/24)>1e-5:raise RuntimeError('Frame timestamp gap')
 decoded=call(['ffmpeg','-v','error','-i',str(temp),'-map','0:v:0','-f','framemd5','-'])
 dh=[line.split(',')[-1].strip() for line in decoded.splitlines() if line and not line.startswith('#')]
 if len(dh)!=240 or len(set(dh))!=240:raise RuntimeError('Repeated or missing decoded frames')
 sim=list(csv.DictReader((take/'cache/retention.csv').open()))
 if len(sim)!=241 or abs(float(sim[-1]['time'])-10)>1e-7:raise RuntimeError('Simulation incomplete')
 diagnostics=list(csv.DictReader((take/'cache/diagnostics.csv').open()))
 result={'status':'complete_verified_native_export','video':movie.name,'width':w,'height':h,'duration_s':10,'fps':24,
 'native_rendered_frames':240,'native_simulation_states':len(sim),'unique_native_frames':240,'unique_decoded_frames':240,
 'frame_timestamps_verified':True,'full_movie_decoded':True,'frame_interpolation':False,'image_generation':False,'denoising':False,
 'solver':'V6 native APIC/MAC; opt-in V7 DuetGuide','dx_m':.03,'marker_spacing_m':.01,
 'initial_markers':int(sim[0]['particles']),'final_markers':int(sim[-1]['particles']),
 'initial_volume_m3':float(sim[0]['volume']),'final_volume_m3':float(sim[-1]['volume']),
 'outflow_volume_m3':float(sim[-1]['outflow_volume']),'emitted_volume_m3':float(sim[-1]['emitted_volume']),
 'maximum_volume_ledger_error_m3':max(abs(float(r['volume_ledger_error_m3'])) for r in diagnostics),
 'pressure_relative_residual_max':max(float(r['pressure_relative_residual']) for r in diagnostics),
 'max_surface_volume_relative_error':max(r['surface']['relative_volume_error'] for r in rows),
 'guidance_fully_off_after_s':8.95,'render':'V6 deterministic Whitted preview; overcast dielectric, oriented foam, adaptive 4-16 samples',
 'source_code_sha256':source,'native_frame_rgb_sha256':hashes,'decoded_frame_md5':dh,
 'limitations':['Review movie resolution is stated above, not upscaled 960x540.','Guide curves are externally forced fantasy control, not unforced natural dynamics.','Uniform 3cm grid and one-way whitewater; no coupled air or microscopic atomization.','Preview approximates indirect illumination; not full converged GI.']}
 result['render_contract_sha256']=contract_sha;result['render_contract']=contract
 result['simulation_audit_sha256']=sha(audit_path);result['simulation_audit_passed']=True
 result['native_state_hashes_verified']=True;result['phase_history_chain_verified']=True
 result['render']='V6 deterministic Whitted preview; frozen camera, sampling, geometry, optics and native binaries'
 result['dx_m']=float(json.loads((take/'cache/manifest.json').read_text())['dx_m'])
 result['marker_spacing_m']=float(json.loads((take/'cache/manifest.json').read_text())['marker_spacing_m'])
 temp.replace(movie);result['sha256']=sha(movie)
 verification_tmp=delivery/'VERIFICATION.json.tmp';verification_tmp.write_text(json.dumps(result,indent=2)+'\n');verification_tmp.replace(delivery/'VERIFICATION.json')
 picks=[36,72,108,126,150,180,204,234];labels=['DRAW / LIFT','FIGURE EIGHT','COIL','STAGGERED STRIKES','ORBITAL CATCH','SHIELD','SPIRAL RETURN','FREE SPLASH']
 tw=384;th=round(h*tw/w);board=Image.new('RGB',(4*tw,2*(th+28)),(13,20,25));d=ImageDraw.Draw(board)
 for k,(i,label) in enumerate(zip(picks,labels)):
  x=(k%4)*tw;y=(k//4)*(th+28)
  with Image.open(rendered/f'frame_{i:06d}.png') as im:board.paste(im.resize((tw,th),Image.Resampling.LANCZOS),(x,y))
  d.text((x+10,y+th+7),f'{i/24:.2f}s  {label}',fill=(230,236,240))
 board.save(delivery/'CYBR_WATERBENDING_DUET_SEQUENCE.jpg',quality=92)
 print(json.dumps({k:v for k,v in result.items() if k not in ('source_code_sha256','native_frame_rgb_sha256','decoded_frame_md5')},indent=2))
if __name__=='__main__':main()
