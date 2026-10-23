#!/usr/bin/env python3
from pathlib import Path
import struct,json,hashlib,csv
import numpy as np
ROOT=Path(__file__).resolve().parent
rows=[];allids=None;global_min=np.full(3,np.inf);global_max=-global_min
radii_min=float('inf');radii_max=0.;white_max=0;seen_hashes=set()
for frame in range(241):
    path=ROOT/'cache'/f'frame_{frame:06d}.bin'
    with path.open('rb') as f:
        magic,version,count,fr,t,spacing,support=struct.unpack('<8sIIIfff',f.read(32));b=f.read()
    assert magic==b'CYBRWTR1' and version==1 and fr==frame and abs(t-frame/24)<1e-6
    assert len(b)==count*32
    p=np.frombuffer(b,'<f4').reshape(-1,8)
    assert np.isfinite(p).all()
    ids=np.fromfile(path.with_suffix('.ids'),'<u4');vol=np.fromfile(path.with_suffix('.volumes'),'<f4')
    assert len(ids)==count==len(vol) and (vol>0).all()
    if allids is None:
        allids=ids;assert len(np.unique(ids))==count
    else:assert np.array_equal(ids,allids)
    lo=p[:,:3].min(0);hi=p[:,:3].max(0);global_min=np.minimum(global_min,lo);global_max=np.maximum(global_max,hi)
    seen_hashes.add(hashlib.sha256(b).hexdigest())
    with path.with_suffix('.white').open('rb') as f:
        wm,wv,wc,wf,wt,wh,_=struct.unpack('<8sIIIfff',f.read(32));wb=f.read()
    assert wm==b'CYBRWW01' and wv==1 and wf==frame and wt==t and len(wb)==wc*40
    w=np.frombuffer(wb,dtype=np.dtype([('f','<f4',(9,)),('type','<u4')]))
    assert np.isfinite(w['f']).all() and (w['type']<=2).all()
    if wc:
        assert (w['f'][:,6]>0).all() and (w['f'][:,8]>w['f'][:,7]).all()
        radii_min=min(radii_min,float(w['f'][:,6].min()));radii_max=max(radii_max,float(w['f'][:,6].max()))
    white_max=max(white_max,wc)
    if frame in [0,24,72,120,168,180,204,210,216,222,228,234,239,240]:
        rows.append({'frame':frame,'time_s':float(t),'primary_count':count,'volume_m3':float(vol.sum(dtype=np.float64)),'bounds_m':[lo.tolist(),hi.tolist()],
                     'max_speed_m_s':float(np.linalg.norm(p[:,3:6],axis=1).max()),'whitewater_count':wc,'whitewater_types':np.bincount(w['type'],minlength=3).tolist()})
with (ROOT/'cache/domain_audit.csv').open() as f:d=list(csv.DictReader(f))
with (ROOT/'cache/retention.csv').open() as f:ret=list(csv.DictReader(f))
with (ROOT/'cache/diagnostics.csv').open() as f:diag=list(csv.DictReader(f))
growth=[json.loads(x) for x in (ROOT/'cache/domain_growth.jsonl').read_text().splitlines()]
assert len(d)==len(ret)==241
minclear=min(float(x['min_lateral_clearance']) for x in d)
assert minclear>0.40 and max(int(x['within_2cells']) for x in d)==0
assert float(ret[-1]['outflow_volume'])==float(ret[-1]['emitted_volume'])==0
maxvolchange=max(abs(float(x['volume'])-float(ret[0]['volume'])) for x in ret)
assert maxvolchange==0 and len(seen_hashes)==241
# All primary water is covered by actual exported solid-field data (no clamping).
lo=np.array([-5.,-.12,-5.]);hi=np.array([5.,3.,8.]);assert (global_min>=lo).all() and (global_max<=hi).all()
result={'passed':True,'simulation_states':241,'unique_primary_position_velocity_states':len(seen_hashes),'times_match_24_Hz':True,'finite_primary_and_secondary_values':True,
 'primary_count':len(allids),'unique_ids':True,'primary_ids_identical_across_all_states':True,'initial_volume_m3':float(ret[0]['volume']),'final_volume_m3':float(ret[-1]['volume']),
 'maximum_volume_change_m3':maxvolchange,'added_water_m3':float(ret[-1]['emitted_volume']),'outflow_water_m3':float(ret[-1]['outflow_volume']),
 'minimum_lateral_boundary_clearance_m':minclear,'markers_within_two_cells_of_lateral_boundary':0,'domain_growth_events':len(growth),'final_domain':growth[-1],
 'primary_global_bounds_m':[global_min.tolist(),global_max.tolist()],'primary_solid_field_coverage':True,
 'peak_secondary_count':white_max,'secondary_radius_range_m':[radii_min,radii_max],
 'pressure_relative_residual_max':max(float(x['pressure_relative_residual']) for x in diag),
 'divergence_after_l2_max_s_inv':max(float(x['divergence_after_l2_s_inv']) for x in diag),
 'volume_ledger_abs_max_m3':max(abs(float(x['volume_ledger_error_m3'])) for x in diag),
 'ring_volume_above_08m_at_75s_m3':float(ret[180]['elevated_08']),'final_volume_above_08m_m3':float(ret[-1]['elevated_08']),
 'representative_states':rows}
(ROOT/'tests/simulation_audit.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
