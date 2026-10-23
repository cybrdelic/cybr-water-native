#!/usr/bin/env python3
"""Reproduce the V7 native duet from source. No network, images or pretrained models."""
from __future__ import annotations
import argparse, json, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent

def run(cmd:list[str],log:Path,cwd:Path|None=None)->None:
 print(' '.join(cmd),flush=True)
 with log.open('w') as f:p=subprocess.run(cmd,cwd=cwd,stdout=f,stderr=subprocess.STDOUT)
 if p.returncode:raise RuntimeError(f'Command failed; inspect {log}')

def main()->None:
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--out',type=Path,default=ROOT/'runs/duet');ap.add_argument('--tests-only',action='store_true')
 ap.add_argument('--skip-simulation',action='store_true');ap.add_argument('--width',type=int,default=960);ap.add_argument('--height',type=int,default=540)
 ap.add_argument('--threads',type=int,default=2);ap.add_argument('--workers',type=int,default=2)
 a=ap.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True);logs=out/'logs';logs.mkdir(exist_ok=True)
 if not 1<=a.threads<=32 or not 1<=a.workers<=16:ap.error('Thread/worker count out of range')
 if a.width<32 or a.height<32 or a.width%2 or a.height%2:ap.error('Even image dimensions >=32 required')
 engine=ROOT/'engine'
 if not (engine/'src/fluid.hpp').exists():raise RuntimeError('Run python3 scripts/materialize_native.py from repository root first')
 run(['make','-j2','all','build/trace_native','checks','finish-checks','continuation-checks','choreography-checks'],logs/'build_and_tests.log',engine)
 if a.tests_only:return
 b=engine/'build';assets=out/'scene';assets.mkdir(exist_ok=True)
 if not (assets/'solids.mesh').exists() or not (assets/'solid.sdf').exists():
  run([str(b/'gallery_export'),'--scene','waterbending_duet','--out',str(assets/'solids.mesh'),'--quality','1','--sdf-out',str(assets/'solid.sdf'),'--sdf-dx','.025','--sdf-lower','-5,-.12,-5','--sdf-upper','5,3,8'],logs/'scene_export.log')
 cmd=[str(b/'gallery_sim'),'--scene','waterbending_duet','--dx','.03','--duration','10','--fps','24','--threads',str(a.threads),'--out',str(out/'cache'),'--apic','--particles-per-axis','3','--reseed-interval','0','--whitewater-rate','24000','--max-whitewater','180000','--surface-tension','.072','--auto-domain','--domain-padding','.48','--domain-lower','-1.98,-.12,-1.50','--domain-upper','1.98,2.82,1.50']
 if not a.skip_simulation:
  if any((out/'cache').glob('frame_*.bin')):raise RuntimeError('Use a fresh --out; existing fluid states are never overwritten')
  (out/'simulation_command.json').write_text(json.dumps(cmd,indent=2));run(cmd,logs/'simulation.log')
 if not (out/'cache/complete.json').exists():raise RuntimeError('Completed source cache required')
 run([sys.executable,str(ROOT/'render_take.py'),'--take',str(out),'--scene-assets',str(assets),'--width',str(a.width),'--height',str(a.height),'--workers',str(a.workers)],logs/'native_render.log')
 run([sys.executable,str(ROOT/'verify_export.py'),'--take',str(out)],logs/'export_verification.log')
 print(out/'delivery/CYBR_WATERBENDING_DUET_V7_10S.mp4')
if __name__=='__main__':main()
