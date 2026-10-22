"""Rebuild and rerun the complete 10-second, 24-fps native fluid review.
Requirements: C++17 compiler with OpenMP, make, Python 3, ffmpeg/ffprobe.
"""
from pathlib import Path
import argparse, subprocess, sys

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--threads',type=int,default=4)
    parser.add_argument('--dx',type=float,default=.045);args=parser.parse_args()
    root=Path(__file__).resolve().parent;engine=root/'CYBR_WATER_VFX';cache=root/'production_cache'
    subprocess.run(['make',f'-j{args.threads}','all','build/solver_checks'],cwd=engine,check=True)
    subprocess.run([str(engine/'build/solver_checks')],check=True)
    subprocess.run([str(engine/'build/trace'),'--self-test'],check=True)
    # Fresh integration, no repeated states, no interpolation or video retiming.
    subprocess.run([str(engine/'build/gallery_sim'),'--scene','waterbending_trick','--dx',str(args.dx),
        '--duration','10','--fps','24','--threads',str(args.threads),'--out',str(cache),
        '--apic','--no-whitewater','--reseed-interval','0'],check=True)
    # Delete cached renders before an intentionally different numerical run.
    rendered=root/'rendered';rendered.mkdir(exist_ok=True)
    for p in rendered.glob('frame_*'):
        if p.suffix in ('.png','.json'):p.unlink()
    subprocess.run([sys.executable,str(root/'render_sequence.py'),'--threads',str(args.threads)],check=True)
    subprocess.run(['ffmpeg','-v','error','-y','-framerate','24','-i',str(rendered/'frame_%06d.png'),
        '-frames:v','240','-c:v','libx264','-preset','slow','-crf','17','-pix_fmt','yuv420p',
        '-movflags','+faststart',str(root/'CYBR_WATERBENDING_RETAINED_TRICK_10S.mp4')],check=True)
if __name__=='__main__':main()
