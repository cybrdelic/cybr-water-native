#!/usr/bin/env python3
"""Build/test/reproduce the native waterbending continuation on Linux."""
from __future__ import annotations
from pathlib import Path
import argparse,json,subprocess,sys
R=Path(__file__).resolve().parent

def run(cmd:list[str],name:str,cwd:Path=R)->None:
 print(' '.join(cmd),flush=True)
 with (R/'logs'/name).open('w') as f:p=subprocess.run(cmd,cwd=cwd,stdout=f,stderr=subprocess.STDOUT)
 if p.returncode:raise RuntimeError(f'{name} failed; inspect logs/{name}')

def main()->None:
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--tests-only',action='store_true');ap.add_argument('--skip-simulation',action='store_true');ap.add_argument('--workers',type=int,default=2);ap.add_argument('--native',action='store_true')
 a=ap.parse_args()
 for name in ('logs','tests','cache','rendered','delivery','mesh_cache'):(R/name).mkdir(exist_ok=True)
 run(['make','-j2','all','checks','finish-checks','continuation-checks']+(['build/trace_native'] if a.native else []),'rebuild_and_tests.log',R/'engine')
 # The verifier reads this current execution log, not historical test receipts.
 (R/'logs/all_tests.log').write_bytes((R/'logs/rebuild_and_tests.log').read_bytes())
 if a.tests_only:return
 if not a.skip_simulation:
  if any((R/'cache').glob('frame_*.bin')):raise RuntimeError('Existing simulation states: use a clean directory or --skip-simulation. Files are never silently overwritten.')
  run([str(R/'engine/build/gallery_sim'),'--scene','waterbending_trick','--dx','.03','--duration','10','--fps','24','--threads','2','--out',str(R/'cache'),'--apic','--particles-per-axis','3','--reseed-interval','0','--whitewater-rate','24000','--max-whitewater','180000','--surface-tension','.072','--auto-domain','--domain-padding','.48','--domain-lower','-1.86,-.12,-1.38','--domain-upper','1.86,2.76,1.38'],'simulation.log')
 if not (R/'cache/complete.json').exists():raise RuntimeError('Complete native state cache is required')
 if not (R/'scene/solids.mesh').exists() or not (R/'scene/solid.sdf').exists():
  (R/'scene').mkdir(exist_ok=True)
  run([str(R/'engine/build/gallery_export'),'--scene','waterbending_trick','--out',str(R/'scene/solids.mesh'),'--quality','1','--sdf-out',str(R/'scene/solid.sdf'),'--sdf-dx','.025','--sdf-lower','-5,-.12,-5','--sdf-upper','5,3,8'],'scene_export.log')
 cmd=[sys.executable,str(R/'continue_render.py'),'--workers',str(a.workers)]
 if not a.native:cmd+=['--portable']
 run(cmd,'render_continuation.log')
 run([sys.executable,str(R/'finish_continuation.py')],'final_verification.log')
 print('Verified ten-second movie is in delivery/.')
if __name__=='__main__':main()
