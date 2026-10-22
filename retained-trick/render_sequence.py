"""Render consecutive, real fluid snapshots using the native C++ renderer.

This review mode is deterministic direct-light/Whitted transport (not full GI).
It does not synthesize/interpolate motion and does not use a generated image.
"""
from __future__ import annotations
import argparse, hashlib, json, os, subprocess, time
from pathlib import Path

def run(cmd: list[str], log: Path) -> None:
    with log.open('w') as stream:
        p=subprocess.run(cmd,stdout=stream,stderr=subprocess.STDOUT)
    if p.returncode:
        raise RuntimeError(f"{Path(cmd[0]).name} failed: {log.read_text()[-1800:]}")


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

def main() -> None:
    a=argparse.ArgumentParser();a.add_argument('--root',type=Path,default=Path(__file__).resolve().parent)
    a.add_argument('--start',type=int,default=0);a.add_argument('--end',type=int,default=240)
    a.add_argument('--threads',type=int,default=4);a.add_argument('--bin-dir',type=Path);args=a.parse_args()
    root=args.root;binary=(args.bin_dir or root/'CYBR_WATER_VFX'/'build'/'bin').resolve();cache=root/'production_cache'
    use_build_runtime(binary)
    out=root/'rendered';out.mkdir(exist_ok=True);(out/'logs').mkdir(exist_ok=True)
    temp=out/'work';temp.mkdir(exist_ok=True)
    solid=root/'review'/'solids.mesh';sdf=root/'review'/'solid.sdf'
    if not solid.exists() or not sdf.exists():
        solid.parent.mkdir(exist_ok=True)
        run([native_binary(binary,'gallery_export'),'--scene','waterbending_trick','--out',str(solid),'--quality','2','--sdf-out',str(sdf),'--sdf-dx','.025'],out/'logs/solid_export.log')
    start=time.monotonic()
    for frame in range(args.start,args.end):
        name=f'frame_{frame:06}';target=out/(name+'.png');record=out/(name+'.json')
        if target.exists() and record.exists():continue
        source=cache/(name+'.bin')
        if not source.exists():raise FileNotFoundError(source)
        mesh=temp/(name+'.mesh');spheres=temp/(name+'.csv');stem=temp/name
        run([native_binary(binary,'surface'),'--in',str(source),'--out',str(mesh),'--spheres',str(spheres),
             '--threads',str(args.threads),'--surface-scale','.8','--support','3.6','--smooth','5',
             '--solid-field',str(sdf)],out/'logs'/(name+'_surface.log'))
        surface=json.loads(Path(str(mesh)+'.json').read_text())
        run([native_binary(binary,'trace'),'--water',str(mesh),'--spheres',str(spheres),'--solids',str(solid),
             '--out',str(stem),'--width','960','--height','540','--spp','4','--bounces','12',
             '--threads',str(args.threads),'--camera','1.9,1.65,4.6','--target','0,.78,0','--fov','29',
             '--aperture','0','--environment','studio','--sky-power','1.8','--sun-power','0',
             '--deterministic-preview'],out/'logs'/(name+'_trace.log'))
        # Lossless per-frame encode; no temporal averaging or interpolation.
        run(['ffmpeg','-v','error','-y','-i',str(stem)+'.ppm','-frames:v','1',str(target)],out/'logs'/(name+'_png.log'))
        render=json.loads(Path(str(stem)+'.json').read_text())
        method=json.loads(Path(str(stem)+'_method.json').read_text())
        rec={'frame':frame,'time_s':frame/24,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
             'png_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'surface':surface,'render':render,'method':method}
        record.write_text(json.dumps(rec,indent=2))
        # Raw float images / full meshes can be regenerated from the actual state.
        for file in temp.glob(name+'*'):file.unlink()
        if frame%12==0 or frame==args.end-1:
            print(json.dumps({'rendered_frame':frame,'elapsed_s':round(time.monotonic()-start,2),'triangles':surface['triangles']}),flush=True)
    print('RENDER_COMPLETE',args.start,args.end,flush=True)
if __name__=='__main__':main()
