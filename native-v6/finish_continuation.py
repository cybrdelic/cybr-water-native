#!/usr/bin/env python3
"""Publish only a complete, independently checked 240-frame native movie."""
from __future__ import annotations
import argparse,hashlib,json,subprocess,struct,shutil,csv,time
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parent
COUNT=240
FPS=24
W,H=960,540

def sha(path:Path)->str:
 h=hashlib.sha256()
 with path.open('rb') as f:
  for chunk in iter(lambda:f.read(1048576),b''):h.update(chunk)
 return h.hexdigest()
def run(cmd:list[str],log:Path)->str:
 r=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 log.write_text(r.stdout+'\n'+r.stderr)
 if r.returncode:raise RuntimeError(f'Command failed ({r.returncode}): {cmd}\n{r.stderr[-4000:]}')
 return r.stdout

def main()->None:
 d=ROOT/'delivery';d.mkdir(exist_ok=True);checks=ROOT/'tests';checks.mkdir(exist_ok=True)
 complete=json.loads((ROOT/'cache/complete.json').read_text())
 if complete['frame']!=240 or abs(complete['time']-10)>1e-8:raise RuntimeError('Incomplete simulation')
 subprocess.run(['python',str(ROOT/'audit_states.py')],stdout=(ROOT/'logs/state_audit.log').open('w'),stderr=subprocess.STDOUT,check=True)
 state_audit=json.loads((checks/'simulation_audit.json').read_text())
 records=[];rgb_hashes=[];source_match=[]
 baseline=json.loads((ROOT/'baseline_delivery/FRAME_RECORDS.json').read_text()) if (ROOT/'baseline_delivery/FRAME_RECORDS.json').exists() else None
 source_hashes={name:sha(ROOT/'engine/src'/name) for name in ['trace.cpp','surface.cpp','fluid.hpp','gallery_scenes.hpp']}
 for i in range(COUNT):
  name=f'frame_{i:06d}';png=ROOT/'rendered'/(name+'.png');rec=json.loads((ROOT/'rendered'/(name+'.json')).read_text())
  if rec['frame']!=i or rec['simulation_time_s']!=i/FPS:raise RuntimeError(f'Wrong frame identity: {i}')
  if rec['png_sha256']!=sha(png) or rec['source_code_sha256']!=source_hashes:raise RuntimeError(f'Stale source/render: {i}')
  for ext,expected in rec['source'].items():
   if sha(ROOT/'cache'/(name+ext))!=expected:raise RuntimeError(f'Source hash mismatch: {i}{ext}')
  with (ROOT/'cache'/(name+'.bin')).open('rb') as f:magic,v,n,frame,t,spacing,support=struct.unpack('<8sIIIfff',f.read(32))
  if magic!=b'CYBRWTR1' or frame!=i or abs(t-i/FPS)>1e-6:raise RuntimeError('Source timing changed')
  with Image.open(png) as im:
   if im.size!=(W,H):raise RuntimeError('Native resolution mismatch')
   rgb_hashes.append(hashlib.sha256(im.convert('RGB').tobytes()).hexdigest())
  sr=rec['surface'];target=sr['dense_marker_target_volume_m3'];err=abs(sr['mesh_signed_volume_m3']-target)/max(target,1e-12)
  if err>.0021:raise RuntimeError(f'Invalid reconstructed volume {i}: {err}')
  rr=rec['render']
  if rr['nonfinite_paths'] or rr['finite_pixels']!=W*H or not rr['coherent_dielectric_normals'] or rr['adaptive_aa_max']!=16 or not rr['oriented_foam']:raise RuntimeError(f'Native render check failed: {i}')
  if sha(ROOT/'mesh_cache'/(name+'.mesh.gz'))!=rec['mesh_gzip_sha256'] or sha(ROOT/'mesh_cache'/(name+'.csv'))!=rec['sphere_csv_sha256']:raise RuntimeError(f'Corrupt mesh cache {i}')
  if baseline:source_match.append(all(rec['source'][ext]==baseline[i][key] for ext,key in [('.bin','source_sha256'),('.ids','ids_sha256'),('.volumes','volumes_sha256'),('.white','whitewater_sha256')]))
  records.append(rec)
 if len(set(rgb_hashes))!=COUNT:raise RuntimeError('Duplicated native RGB frame')
 movie=d/'CYBR_WATERBENDING_V6_COMPLETE_10S.mp4';tmp=d/'encoding.tmp.mp4'
 run(['ffmpeg','-v','error','-y','-framerate',str(FPS),'-start_number','0','-i',str(ROOT/'rendered/frame_%06d.png'),'-frames:v',str(COUNT),'-c:v','libx264','-preset','medium','-crf','16','-pix_fmt','yuv420p','-threads','2','-movflags','+faststart',str(tmp)],ROOT/'logs/encode.log')
 probe=json.loads(run(['ffprobe','-v','error','-count_frames','-select_streams','v:0','-show_entries','stream=width,height,nb_read_frames,nb_frames,r_frame_rate,duration,codec_name,pix_fmt','-of','json',str(tmp)],ROOT/'logs/ffprobe.log'))['streams'][0]
 if (probe['width'],probe['height'])!=(W,H) or int(probe['nb_read_frames'])!=COUNT or probe['r_frame_rate']!='24/1' or abs(float(probe['duration'])-10)>1e-6:raise RuntimeError('Encoded stream metadata mismatch')
 hashes=run(['ffmpeg','-v','error','-i',str(tmp),'-map','0:v:0','-f','framemd5','-'],ROOT/'logs/full_decode_hashes.log')
 lines=[x for x in hashes.splitlines() if x and not x.startswith('#')];decoded=[x.split(',')[-1].strip() for x in lines]
 if len(decoded)!=COUNT or len(set(decoded))!=COUNT:raise RuntimeError('Decoded frame count or uniqueness failed')
 stamp=json.loads(run(['ffprobe','-v','error','-select_streams','v:0','-show_frames','-show_entries','frame=best_effort_timestamp_time','-of','json',str(tmp)],ROOT/'logs/frame_timestamps.log'))['frames']
 if len(stamp)!=COUNT:raise RuntimeError('Timestamp count mismatch')
 terr=max(abs(float(v['best_effort_timestamp_time'])-i/FPS) for i,v in enumerate(stamp))
 if terr>1e-6:raise RuntimeError('Nonuniform or missing frame timing')
 tmp.replace(movie)
 (d/'FRAME_RECORDS.json').write_text(json.dumps(records,indent=2))
 (d/'NATIVE_RGB_HASHES.txt').write_text('\n'.join(f'{i:06d} {h}' for i,h in enumerate(rgb_hashes))+'\n')
 (d/'DECODED_FRAME_HASHES.txt').write_text(hashes)
 groups=[]
 for line in (ROOT/'logs/all_tests.log').read_text().splitlines():
  if line.startswith('{'):
   try:
    x=json.loads(line)
    if x.get('passed') is not True:raise RuntimeError('Regression report failed')
    groups.append(x)
   except json.JSONDecodeError:pass
 if len(groups)<10:raise RuntimeError('Missing regression tests')
 result={'status':'complete_verified_native_export','video':movie.name,'sha256':sha(movie),'bytes':movie.stat().st_size,'width':W,'height':H,'fps':FPS,'duration_s':10,
  'native_rendered_frames':COUNT,'unique_native_rgb_frames':len(set(rgb_hashes)),'unique_decoded_frames':len(set(decoded)),'full_movie_decoded':True,'timestamp_max_error_s':terr,
  'image_generation':False,'frame_interpolation':False,'frame_duplication':False,'post_denoising':False,'temporal_denoising':False,
  'scope':'Completion of the V6 native waterbending render and continuation fixes; not an all-seventeen-scene or new multiphase-physics release.',
  'simulation':state_audit,'source_frames_matching_V5_byte_for_byte':sum(source_match) if baseline else None,'source_comparison_total':COUNT if baseline else None,
  'source_code_sha256':source_hashes,'regression_test_groups':groups,'surface_max_relative_volume_error':max(x['surface']['verified_relative_volume_error'] for x in records),
  'nonfinite_paths':sum(x['render']['nonfinite_paths'] for x in records),'medium_underflows':sum(x['render']['medium_underflows'] for x in records),
  'unclosed_medium_escapes':sum(x['render']['unclosed_medium_escapes'] for x in records),
  'renderer':{'method':'Deterministic native dielectric reflection/refraction preview','camera_samples':'4 base, adaptively refined to 16','coherent_normals':True,'surface_oriented_foam':True,'diffuse_hemisphere_samples':24,'max_bounces':20,'branch_weight_cutoff':.001,'ior':1.333},
  'remaining_limitations':['Uniform 3 cm pressure grid, not adaptive refinement','One-way secondary whitewater, not coupled air pressure','Microscopic atomization unresolved','Six-lobe foam orientation filtering is a visual approximation, not gas dynamics','Deterministic finite-depth preview is not converged full global illumination','Some fine geometry roughness and thick guided structures remain']}
 (d/'VERIFICATION.json').write_text(json.dumps(result,indent=2))
 font_path='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
 try:font=ImageFont.truetype(font_path,18)
 except OSError:font=ImageFont.load_default()
 selected=[24,72,120,180,216,234];cw,ch=480,302
 sheet=Image.new('RGB',(cw*3,ch*2),(15,19,24));draw=ImageDraw.Draw(sheet)
 for k,fr in enumerate(selected):
  with Image.open(ROOT/'rendered'/f'frame_{fr:06d}.png') as im:sheet.paste(im.resize((480,270),Image.Resampling.LANCZOS),((k%3)*cw,(k//3)*ch))
  draw.text(((k%3)*cw+12,(k//3)*ch+275),f'{fr/24:.2f} s  |  native frame {fr}',font=font,fill=(225,230,235))
 sheet.save(d/'NATIVE_SEQUENCE.jpg',quality=93)
 shutil.copyfile(ROOT/'rendered/frame_000180.png',d/'NATIVE_RING.png')
 shutil.copyfile(ROOT/'rendered/frame_000234.png',d/'NATIVE_SPLASH.png')
 html=f'''<!doctype html><html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>CYBR WATER / V6 native continuation</title><style>body{{background:#11161b;color:#e5e8eb;font:17px/1.55 system-ui;max-width:1100px;margin:40px auto;padding:0 24px}}video,img{{width:100%;height:auto}}a{{color:#9dcfe2}}small{{color:#9ba8b2}}</style><h1>CYBR WATER / V6 native continuation</h1><p>Verified: 240 distinct native frames, 24 fps, 10 seconds, 960 × 540.</p><video controls preload="metadata" poster="NATIVE_RING.png" src="{movie.name}"></video><p><a href="VERIFICATION.json">Independent verification</a> · <a href="CYBR_WATER_V6_SOURCE_AND_VIDEO.zip">Source and video archive</a></p><img src="NATIVE_SEQUENCE.jpg" alt="Six native frames"><p><small>The existing APIC/MAC dynamics remain unchanged. This finishes the optical/reconstruction continuation, not coupled-air or microscopic-breakup development. No generated artwork, duplicated frames, interpolation, or image denoising.</small></p></html>'''
 (d/'RELEASE.html').write_text(html)
 print(json.dumps({'status':result['status'],'video':str(movie),'frames':COUNT,'matching_v5_states':sum(source_match),'max_surface_error':result['surface_max_relative_volume_error'],'native_test_groups':len(groups)}),flush=True)
if __name__=='__main__':main()
