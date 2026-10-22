"""Rebuild and rerun the complete 10-second, 24-fps native fluid review.
Requirements: CMake 3.30+, C++17 compiler, Python 3, ffmpeg/ffprobe.
"""
from pathlib import Path
import argparse, os, subprocess, sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native import build, executable, runtime_environment, test

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--threads',type=int,default=4)
    parser.add_argument('--dx',type=float,default=.045)
    parser.add_argument('--openmp',type=str.upper,choices=('AUTO','ON','OFF'),default='AUTO')
    parser.add_argument('--checks-only',action='store_true')
    args=parser.parse_args()
    if args.threads < 1:parser.error('--threads must be positive')
    root=Path(__file__).resolve().parent;engine=root/'CYBR_WATER_VFX';cache=root/'production_cache'
    build(engine/'build', 'retained-trick', jobs=args.threads, openmp=args.openmp)
    os.environ.update(runtime_environment(engine/'build'))
    test(engine/'build')
    if args.checks_only:return
    # Fresh integration, no repeated states, no interpolation or video retiming.
    subprocess.run([str(executable(engine/'build', 'gallery_sim')),'--scene','waterbending_trick','--dx',str(args.dx),
        '--duration','10','--fps','24','--threads',str(args.threads),'--out',str(cache),
        '--apic','--no-whitewater','--reseed-interval','0'],check=True)
    # Delete cached renders before an intentionally different numerical run.
    rendered=root/'rendered';rendered.mkdir(exist_ok=True)
    for p in rendered.glob('frame_*'):
        if p.suffix in ('.png','.json'):p.unlink()
    subprocess.run([sys.executable,str(root/'render_sequence.py'),'--bin-dir',str(engine/'build/bin'),'--threads',str(args.threads)],check=True)
    subprocess.run(['ffmpeg','-v','error','-y','-framerate','24','-i',str(rendered/'frame_%06d.png'),
        '-frames:v','240','-c:v','libx264','-preset','slow','-crf','17','-pix_fmt','yuv420p',
        '-movflags','+faststart',str(root/'CYBR_WATERBENDING_RETAINED_TRICK_10S.mp4')],check=True)
if __name__=='__main__':main()
