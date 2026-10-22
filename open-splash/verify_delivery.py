from __future__ import annotations
import argparse,hashlib,json,math,subprocess,shutil,struct
from pathlib import Path
import numpy as np
import pandas as pd
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parent
OUT=ROOT/'delivery';OUT.mkdir(exist_ok=True)
VIDEO=OUT/'CYBR_WATERBENDING_OPEN_SPLASH_10S.mp4'

def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
def command(args):return subprocess.check_output(args,text=True)
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--previous-video',type=Path)
 args=parser.parse_args()
 manifest=json.loads((ROOT/'cache/manifest.json').read_text());complete=json.loads((ROOT/'cache/complete.json').read_text())
 assert complete['time']==10 and complete['frame']==240
 records=[];input_hashes=[];output_hashes=[]
 for i in range(240):
  p=ROOT/'rendered'/f'frame_{i:06d}.png';rec=json.loads(p.with_suffix('.json').read_text())
  src=ROOT/'cache'/f'frame_{i:06d}.bin'
  assert rec['frame']==i and abs(rec['time_s']-i/24)<1e-9
  assert sha(src)==rec['source_sha256'] and sha(p)==rec['png_sha256']
  assert sha(src.with_suffix('.ids'))==rec['ids_sha256']
  assert sha(src.with_suffix('.volumes'))==rec['volumes_sha256']
  if 'white_sha256' in rec:assert sha(src.with_suffix('.white'))==rec['white_sha256']
  records.append(rec);input_hashes.append(rec['source_sha256']);output_hashes.append(rec['png_sha256'])
 assert len(set(input_hashes))==240 and len(set(output_hashes))==240
 probe=json.loads(command(['ffprobe','-v','error','-select_streams','v:0','-count_frames','-show_entries',
  'stream=width,height,nb_read_frames,r_frame_rate,duration','-of','json',str(VIDEO)]))['streams'][0]
 assert probe['width']==960 and probe['height']==540 and probe['nb_read_frames']=='240'
 assert probe['r_frame_rate']=='24/1' and abs(float(probe['duration'])-10)<1e-9
 timestamps=json.loads(command(['ffprobe','-v','error','-select_streams','v:0','-show_frames','-show_entries','frame=best_effort_timestamp_time','-of','json',str(VIDEO)]))['frames']
 times=np.array([float(x['best_effort_timestamp_time']) for x in timestamps]);assert len(times)==240
 assert np.max(np.abs(times-np.arange(240)/24))<1e-6
 md5=command(['ffmpeg','-v','error','-i',str(VIDEO),'-f','framemd5','-'])
 lines=[x for x in md5.splitlines() if x and not x.startswith('#')];hashes=[x.split(',')[-1].strip() for x in lines]
 assert len(lines)==len(set(hashes))==240
 (OUT/'DECODED_FRAMES.framemd5').write_text(md5)
 domain=pd.read_csv(ROOT/'cache/domain_audit.csv');diag=pd.read_csv(ROOT/'cache/diagnostics.csv');ret=pd.read_csv(ROOT/'cache/retention.csv')
 vol=np.fromfile(ROOT/'cache/frame_000240.volumes',dtype='<f4')
 radii=np.cbrt(3*vol.astype(float)/(4*np.pi));size=2*radii*1000
 volume_errors=[abs(r['surface']['mesh_signed_volume_m3']-r['surface']['dense_marker_target_volume_m3'])/max(r['surface']['dense_marker_target_volume_m3'],1e-12) for r in records]
 assert max(volume_errors)<.00301
 assert domain.within_2cells.max()==0, 'Final liquid still approaches numerical side boundaries'
 assert ret.outflow_volume.iloc[-1]==0, 'Visible-spray window still loses liquid'
 assert int(ret.particles.iloc[0])==int(ret.particles.iloc[-1])
 assert all(r['render']['nonfinite_paths']==0 for r in records)
 tests={}
 for f in ['solver_tests.log','optics_native_tests.json','splash_boundary_checks.json','fragment_phase_checks.json','retained_guidance_checks.json']:
  tests[f]=json.loads((ROOT/f).read_text().strip());assert tests[f]['passed']
 release=domain[domain['time']>=8.55];energy=release.kinetic_energy+release.gravitational_energy
 free_rise=float((energy-energy.iloc[0]).max())
 boundary=json.loads((ROOT/'cache/boundary_conditions.json').read_text())
 report={
  'status':'completed_native_open_splash_revision','video':VIDEO.name,'sha256':sha(VIDEO),
  'width':960,'height':540,'duration_s':10,'fps':24,'native_rendered_frames':240,'native_simulation_states':241,
  'unique_simulation_state_hashes':len(set(input_hashes)),'unique_native_image_hashes':len(set(output_hashes)),
  'unique_decoded_frames':len(set(hashes)),'all_frame_timestamps_checked':True,'full_movie_decoded':True,
  'image_generation':False,'frame_interpolation':False,'temporal_denoising':False,
  'solver':manifest['solver'],'uniform_grid_spacing_m':manifest['dx_m'],'marker_spacing_m':manifest['marker_spacing_m'],
  'grid':manifest['grid'],'lower':manifest['lower'],'upper':manifest['upper'],
  'domain_handoff':json.loads((ROOT/'DOMAIN_HANDOFF.json').read_text()),
  'handoff_conservation_check':json.loads((ROOT/'handoff_conservation_test.json').read_text()),
  'closed_frictionless_faces':boundary['closed_frictionless'],'face_order':boundary['order'],
  'lateral_boundaries_open':all(not boundary['closed_frictionless'][j] for j in [0,1,4,5]),
  'minimum_lateral_clearance_m':float(domain.min_lateral_clearance.min()),
  'max_markers_within_1cm_of_lateral_domain':int(domain.within_1cm.max()),
  'max_markers_within_2cells_of_lateral_domain':int(domain.within_2cells.max()),
  'initial_markers':int(ret.particles.iloc[0]),'final_markers':int(ret.particles.iloc[-1]),
  'initial_volume_m3':float(ret.initial_volume.iloc[0]),'final_volume_m3':float(ret.volume.iloc[-1]),
  'emitted_volume_m3':float(ret.emitted_volume.iloc[-1]),'outflow_volume_m3':float(ret.outflow_volume.iloc[-1]),
  'final_volume_ledger_error_m3':complete['volume_ledger_error_m3'],
  'max_abs_volume_ledger_error_m3':float(np.abs(diag['volume_ledger_error_m3']).max()) if 'volume_ledger_error_m3' in diag else float(abs(complete['volume_ledger_error_m3'])),
  'ring_elevated_volume_above_08m_at_75s_m3':float(ret.loc[ret.frame==180,'elevated_08'].iloc[0]),
  'control_off_after_s':8.55,'whitewater_max_count':int(domain.whitewater_count.max()),
  'whitewater_final_count':int(domain.whitewater_count.iloc[-1]),
  'primary_sample_equivalent_drop_diameter_mm_min':float(size.min()),
  'primary_sample_equivalent_drop_diameter_mm_max':float(size.max()),
  'primary_droplet_size_scope':'Volumes of numerical liquid markers, NOT a prediction of the physical atomization spectrum. No rendered-radius shrinking.',
  'fragment_hysteresis_enter_marker_spacings':3.2,'fragment_hysteresis_rejoin_marker_spacings':2.2,
  'surface_lattice_m':.012,'surface_support_radius_m':.032,'scalar_smoothing_passes':3,
  'max_reconstructed_dense_volume_relative_error':max(volume_errors),
  'all_surface_solves_passed':True,'integrator':'native deterministic direct-light Whitted preview',
  'aa_samples_per_pixel':4,'max_bounces':12,'full_global_illumination':False,
  'maximum_rendered_nonfinite_paths':max(r['render']['nonfinite_paths'] for r in records),
  'post_release_translational_plus_gravitational_energy_initial_J':float(energy.iloc[0]),
  'post_release_translational_plus_gravitational_energy_final_J':float(energy.iloc[-1]),
  'post_release_max_rise_above_initial_energy_J':free_rise,
  'energy_scope':'Sampled translational kinetic plus gravitational potential only. Does not include APIC affine energy, free-surface energy, or work carried out through open exits. Not a full energy theorem.',
  'tests':tests,
  'scope_limitations':[
   'Uniform 3 cm spacing with an explicit domain extension at 9 s, not an adaptive hierarchy.',
   'At the 9 s snapshot handoff p/v/volume/IDs were unchanged; APIC affine gradients were reconstructed and pressure warm start reset. Not a bitwise checkpoint continuation.',
   'One-way flow-derived visual spray/foam/bubble parcels; no resolved air pressure or validated physical atomization-size spectrum.',
   'Primary detached markers still have finite numerical volume; the 1 cm sampling does not resolve microscopic droplets or sub-grid capillary breakup.',
   'Deterministic preview approximates indirect illumination; not converged global illumination.'
  ]
 }
 (OUT/'VERIFICATION.json').write_text(json.dumps(report,indent=2))
 shutil.copy2(ROOT/'rendered/frame_000234.png',OUT/'SPLASH_9_75_SECONDS.png')
 for name,times,cols in [('SEQUENCE.jpg',[12,48,96,156,210,234],3),('SPLASHDOWN_SEQUENCE.jpg',[204,210,216,222,228,239],2)]:
  w=480;h=270;bar=28;rows=math.ceil(len(times)/cols)
  image=Image.new('RGB',(cols*w,rows*(h+bar)),(16,22,29));draw=ImageDraw.Draw(image)
  font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',16)
  for k,i in enumerate(times):
   with Image.open(ROOT/'rendered'/f'frame_{i:06d}.png') as f:image.paste(f.resize((w,h),Image.Resampling.LANCZOS),((k%cols)*w,(k//cols)*(h+bar)))
   draw.text(((k%cols)*w+12,(k//cols)*(h+bar)+h+5),f'{i/24:.2f} s  |  native simulated geometry',font=font,fill=(227,233,240))
  image.save(OUT/name,quality=94)
 old=args.previous_video
 if old is not None:
  subprocess.run(['ffmpeg','-v','error','-y','-i',str(old),'-vf','select=eq(n\\,234)','-frames:v','1',str(OUT/'PREVIOUS_9_75_SECONDS.png')],check=True)
  image=Image.new('RGB',(960,1144),(15,21,28));draw=ImageDraw.Draw(image);font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',20)
  for y,p,label in [(0,OUT/'PREVIOUS_9_75_SECONDS.png','PREVIOUS — closed boundaries, coarse liquid markers'),(572,OUT/'SPLASH_9_75_SECONDS.png','REVISED — open boundaries, finer simulation and coherent fragments')]:
   with Image.open(p) as f:image.paste(f,(0,y+32))
   draw.text((14,y+5),label,font=font,fill=(235,239,245))
  image.save(OUT/'SPLASH_BEFORE_AFTER.jpg',quality=94)
 print(json.dumps({k:report[k] for k in ['final_markers','minimum_lateral_clearance_m','outflow_volume_m3','whitewater_max_count','max_reconstructed_dense_volume_relative_error','post_release_max_rise_above_initial_energy_J']},indent=2))
if __name__=='__main__':main()
