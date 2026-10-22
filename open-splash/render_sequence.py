"""Sequential native reconstruction + deterministic refraction/reflection rendering.
No image synthesis, image-space denoising, frame duplication, or interpolation.
"""
from __future__ import annotations
import argparse,hashlib,json,os,subprocess,time
from pathlib import Path
from PIL import Image

def sha(p:Path)->str:
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1<<20),b''):h.update(b)
    return h.hexdigest()
def run(cmd:list[str],log:Path)->None:
    with log.open('w') as f:
        p=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT)
    if p.returncode:raise RuntimeError(log.read_text()[-2500:])


def native_binary(directory: Path, name: str) -> str:
    suffix = '.exe' if os.name == 'nt' else ''
    for candidate in (name, name + '_native'):
        path = directory / (candidate + suffix)
        if path.is_file():return str(path)
    raise FileNotFoundError(f'Native target {name} missing in {directory}')

def use_build_runtime(binary_dir: Path) -> None:
    metadata = binary_dir.parent / 'water-build.json'
    if metadata.exists():
        directory = json.loads(metadata.read_text()).get('runtime_dir', '')
        if directory:
            if not Path(directory).is_dir():raise FileNotFoundError('Configured OpenMP runtime is missing; rebuild.')
            os.environ['PATH'] = directory + os.pathsep + os.environ.get('PATH', '')

def main()->None:
    ap=argparse.ArgumentParser();ap.add_argument('--start',type=int,default=0);ap.add_argument('--end',type=int,default=240)
    ap.add_argument('--threads',type=int,default=2);ap.add_argument('--bin-dir',type=Path);a=ap.parse_args()
    r=Path(__file__).resolve().parent;b=(a.bin_dir or r/'engine/build/bin').resolve();out=r/'rendered';work=out/'work';logs=out/'logs'
    use_build_runtime(b)
    for p in (out,work,logs):p.mkdir(exist_ok=True,parents=True)
    begin=time.monotonic();phase=out/'fragment_phase.bin'
    for i in range(a.start,a.end):
        n=f'frame_{i:06d}';src=r/'cache'/(n+'.bin');target=out/(n+'.png');rec=out/(n+'.json')
        if target.exists() and rec.exists():continue
        wait=time.monotonic()
        while not src.with_suffix('.white').exists():
            if time.monotonic()-wait>1800:raise TimeoutError(f'No complete solver state {i}')
            time.sleep(1)
        mesh=work/(n+'.mesh');spheres=work/(n+'.csv');stem=work/n
        cmd=[native_binary(b,'surface'),'--in',str(src),'--out',str(mesh),'--spheres',str(spheres),
          '--threads',str(a.threads),'--surface-scale','1.2','--support','3.2','--smooth','3',
          '--solid-field',str(r/('scene/solid_large.sdf' if i>=216 else 'scene/solid.sdf')),'--coherent-fragments','--phase-out',str(phase)]
        if phase.exists():cmd+=['--phase-in',str(phase)]
        run(cmd,logs/(n+'_surface.log'))
        surface=json.loads(Path(str(mesh)+'.json').read_text())
        run([native_binary(b,'trace'),'--water',str(mesh),'--spheres',str(spheres),
             '--solids',str(r/'scene/solids.mesh'),'--out',str(stem),'--width','960','--height','540',
             '--spp','4','--bounces','12','--threads',str(a.threads),'--camera','1.9,1.65,4.6',
             '--target','0,.78,0','--fov','29','--aperture','0','--environment','studio',
             '--sky-power','1.8','--sun-power','0','--deterministic-preview'],logs/(n+'_trace.log'))
        # Lossless format conversion only. Identical RGB values, no filters.
        with Image.open(str(stem)+'.ppm') as img:img.save(target)
        render=json.loads(Path(str(stem)+'.json').read_text())
        method=json.loads(Path(str(stem)+'_method.json').read_text())
        rec.write_text(json.dumps({'frame':i,'time_s':i/24,'source_sha256':sha(src),
           'white_sha256':sha(src.with_suffix('.white')),'volumes_sha256':sha(src.with_suffix('.volumes')),'ids_sha256':sha(src.with_suffix('.ids')),
           'png_sha256':sha(target),'surface':surface,'render':render,'method':method},indent=2))
        for p in work.glob(n+'*'):p.unlink()
        print(json.dumps({'frame':i,'elapsed_s':round(time.monotonic()-begin,2),
            'triangles':surface['triangles'],'primary_drops':surface['isolated_primary_spheres'],
            'secondary_parcels':surface['secondary_visual_parcels']}),flush=True)
    (out/'complete.json').write_text(json.dumps({'first_frame':a.start,'end_exclusive':a.end,'elapsed_s':time.monotonic()-begin}))
    print('RENDER_COMPLETE',flush=True)
if __name__=='__main__':main()
