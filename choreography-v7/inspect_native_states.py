#!/usr/bin/env python3
"""Audit a complete or bounded native cache without claiming visual acceptance.

Cross-section widths are marker-distribution proxies, not measured mesh thickness.
Only the native surface report can classify reconstructed primary droplets.
"""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import math
import struct
from pathlib import Path
import numpy as np

HEADER = struct.Struct('<8sIIIfff')

def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()

def primary(path: Path, frame: int, fps: float):
    raw = path.read_bytes()
    if len(raw) < HEADER.size:
        raise RuntimeError(f'Truncated header: {path}')
    magic, version, count, index, time, spacing, support = HEADER.unpack_from(raw)
    if magic != b'CYBRWTR1' or version != 1 or index != frame:
        raise RuntimeError(f'Incorrect primary identity: {path}')
    if not math.isfinite(time) or abs(time - frame / fps) > 1e-6:
        raise RuntimeError(f'Incorrect primary time: {path}')
    if not math.isfinite(spacing) or spacing <= 0 or not math.isfinite(support):
        raise RuntimeError(f'Invalid primary scale: {path}')
    if len(raw) != HEADER.size + count * 32 or count == 0:
        raise RuntimeError(f'Incorrect primary payload: {path}')
    p = np.frombuffer(raw, '<f4', offset=HEADER.size).reshape(count, 8)
    ids = np.fromfile(path.with_suffix('.ids'), '<u4')
    volumes = np.fromfile(path.with_suffix('.volumes'), '<f4')
    if len(ids) != count or len(volumes) != count:
        raise RuntimeError(f'Incorrect primary companions: {path}')
    if not np.isfinite(p).all() or not np.isfinite(volumes).all() or not (volumes > 0).all():
        raise RuntimeError(f'Invalid primary values: {path}')
    return p, ids, volumes, spacing

def sections(p, volumes, dx: float):
    result = []
    for height in (.5, .8, 1.0):
        for side in (-1, 1):
            mask = (np.abs(p[:, 1] - height) <= dx / 2) & (p[:, 0] * side > 0)
            q = p[mask, :3]
            area = float(volumes[mask].sum(dtype=np.float64)) / dx
            result.append({'height_m': height, 'side': 'left' if side < 0 else 'right',
                           'markers': len(q), 'slice_width_m': dx,
                           'volume_over_slice_width_m2': area,
                           'equivalent_circular_diameter_m': math.sqrt(4 * area / math.pi),
                           'x_z_90_percent_spans_m': np.ptp(np.quantile(q[:, [0, 2]], [.05, .95], axis=0), axis=0).tolist() if len(q) else None})
    return result

def audit(take: Path, dx: float, fps: float, end: int | None, picks: set[int], allow_partial_ledger: bool = False):
    cache = take / 'cache'
    manifest = json.loads((cache / 'manifest.json').read_text())
    if abs(manifest['fps']-fps)>1e-8:
        raise RuntimeError('Recorded simulation fps differs from requested fps')
    if abs(manifest['dx_m'] - dx) > 1e-7:
        raise RuntimeError('Recorded solver grid does not match requested dx')
    available = sorted(int(p.stem.split('_')[-1]) for p in cache.glob('frame_*.bin'))
    if end is None:
        if not (cache / 'complete.json').is_file():
            raise RuntimeError('Use --end-frame for an explicitly bounded unfinished cache')
        end = int(json.loads((cache / 'complete.json').read_text())['frame'])
    if end < 0 or available[:end + 1] != list(range(end + 1)):
        raise RuntimeError('Missing native prefix states')
    initial_ids = initial_volumes = None
    state_hashes, representative = [], []
    maximum_volume_change = 0.
    for frame in range(end + 1):
        path = cache / f'frame_{frame:06d}.bin'
        p, ids, volumes, spacing = primary(path, frame, fps)
        if abs(spacing - dx / 3) > 1e-7:
            raise RuntimeError('Expected three markers per pressure-grid axis')
        if initial_ids is None:
            initial_ids, initial_volumes = ids.copy(), volumes.copy()
            if len(np.unique(ids)) != len(ids):
                raise RuntimeError('Repeated primary IDs')
            initial_volume = float(volumes.sum(dtype=np.float64))
        elif not np.array_equal(ids, initial_ids) or not np.array_equal(volumes, initial_volumes):
            raise RuntimeError('Primary ownership or reference volumes changed')
        volume = float(volumes.sum(dtype=np.float64))
        maximum_volume_change = max(maximum_volume_change, abs(volume - initial_volume))
        white_path = path.with_suffix('.white')
        white = white_path.read_bytes()
        if len(white) < HEADER.size:
            raise RuntimeError('Truncated secondary state')
        magic, version, n, index, time, wdx, _ = HEADER.unpack_from(white)
        if magic != b'CYBRWW01' or version != 1 or index != frame or abs(time-frame/fps) > 1e-6 or abs(wdx-dx)>1e-7 or len(white)!=HEADER.size+n*40:
            raise RuntimeError('Invalid secondary state identity')
        w = np.frombuffer(white, dtype=np.dtype([('f','<f4',(9,)), ('type','<u4')]), offset=HEADER.size)
        if not np.isfinite(w['f']).all() or not (w['type'] <= 2).all():
            raise RuntimeError('Invalid secondary values')
        if n and (not (w['f'][:,6]>0).all() or not (w['f'][:,8]>w['f'][:,7]).all()):
            raise RuntimeError('Invalid secondary radius or age')
        state_hashes.append({'frame': frame, 'sha256': {ext:sha(path.with_suffix(ext)) for ext in ('.bin','.ids','.volumes','.white')}})
        if frame in picks or frame == end:
            representative.append({'frame':frame,'time_s':frame/fps,'primary_count':len(p),'volume_m3':volume,
                'elevated_volume_m3':{str(h):float(volumes[p[:,1]>h].sum(dtype=np.float64)) for h in (.5,.8,1.1)},
                'bounds_m':[p[:,:3].min(0).tolist(),p[:,:3].max(0).tolist()],
                'maximum_speed_m_s':float(np.linalg.norm(p[:,3:6],axis=1).max()),
                'secondary_types':np.bincount(w['type'],minlength=3).tolist(),
                'cross_section_proxies':sections(p,volumes,dx)})
    with (cache/'diagnostics.csv').open() as f:
        diag=[r for r in csv.DictReader(f) if float(r['time'])<=end/fps+1e-8]
    if not diag:
        raise RuntimeError('Missing solver ledger')
    ledger_end=max(float(r['time']) for r in diag)
    ledger_complete=abs(ledger_end-end/fps)<=1e-6
    if not ledger_complete and not allow_partial_ledger:
        raise RuntimeError('Solver ledger does not cover the audited prefix')
    finite_fields=('time','volume_m3','volume_ledger_error_m3','pressure_relative_residual','divergence_after_l2_s_inv')
    if any(not math.isfinite(float(r[k])) for r in diag for k in finite_fields):
        raise RuntimeError('Nonfinite solver ledger')
    if any(float(r[k])!=0 for r in diag for k in ('emitted_volume_m3','outflow_volume_m3')):
        raise RuntimeError('This no-inlet/no-outflow comparison requires conserved primary ownership')
    completion=json.loads((cache/'complete.json').read_text()) if (cache/'complete.json').exists() else None
    return {'format':'cybr-v11-native-state-audit-v1','numerical_state_checks_passed':True,
        'visual_acceptance':False,'scene':manifest['scene'],'solver_dx_m':dx,'marker_spacing_m':dx/3,'fps':fps,
        'audited_frames':[0,end],'audited_time_s':[0,end/fps],'full_ten_seconds':end==round(10*fps),
        'initial_volume_m3':initial_volume,'maximum_saved_volume_change_m3':maximum_volume_change,
        'primary_ids_and_volumes_identical':True,'primary_values_finite':True,'secondary_values_finite':True,
        'ledger_coverage_complete':ledger_complete,'diagnostics_time_range_s':[min(float(r['time']) for r in diag),ledger_end],
        'maximum_ledger_error_m3':max(abs(float(r['volume_ledger_error_m3'])) for r in diag),
        'maximum_pressure_relative_residual':max(float(r['pressure_relative_residual']) for r in diag),
        'maximum_post_projection_divergence_l2_s_inv':max(float(r['divergence_after_l2_s_inv']) for r in diag),
        'sum_step_wall_seconds':sum(float(r['step_seconds']) for r in diag),
        'completion':completion,'representative_states':representative,'state_files':state_hashes,
        'limitations':['Marker reference-volume conservation does not establish local incompressibility or surface fidelity.',
        'Cross-section proxies include every marker in each half-space, including separated fragments; they are not minimum sheet thickness.',
        'Secondary parcels carry no primary mass and do not establish physical atomization.',
        'The audit does not certify temporal surface continuity, optical convergence, or choreography acceptance.']}

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--take',type=Path,required=True)
    ap.add_argument('--dx',type=float,required=True)
    ap.add_argument('--fps',type=float,default=24)
    ap.add_argument('--end-frame',type=int)
    ap.add_argument('--allow-partial-ledger',action='store_true',help='Audit saved states while explicitly reporting incomplete substep diagnostics; never imply full ledger coverage')
    ap.add_argument('--frames',default='24,36,48,60,84,96,108,120,144,168,192,204,216,228,240')
    ap.add_argument('--out',type=Path,required=True)
    a=ap.parse_args()
    if not math.isfinite(a.dx) or a.dx<=0 or not math.isfinite(a.fps) or a.fps<=0:
        ap.error('Positive finite dx and fps required')
    result=audit(a.take.resolve(),a.dx,a.fps,a.end_frame,{int(i) for i in a.frames.split(',')},a.allow_partial_ledger)
    a.out.parent.mkdir(parents=True,exist_ok=True)
    a.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('state_files','representative_states')},indent=2))
if __name__=='__main__':main()
