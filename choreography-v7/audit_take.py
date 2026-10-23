#!/usr/bin/env python3
"""Read every native state and check finite values, ownership, identity and bounds.

This audit does not certify choreography aesthetics, two-phase dynamics, or
microscopic breakup. All quantities are computed from saved native state bytes.
"""
from __future__ import annotations
import argparse, csv, hashlib, json, struct
from pathlib import Path
import numpy as np

HEADER=struct.Struct('<8sIIIfff')

def table(path:Path)->list[dict]:
 with path.open(newline='') as f:return list(csv.DictReader(f))

def main()->None:
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--take',required=True,type=Path)
 ap.add_argument('--solid-bounds',type=float,nargs=6,default=[-5.,-.12,-5.,5.,3.,8.])
 a=ap.parse_args();take=a.take.resolve();cache=take/'cache'
 if not (cache/'complete.json').is_file():raise RuntimeError('Incomplete source simulation')
 rows=[];state_files=[];original_ids=None;original_volumes=None;hashes=set();glo=np.full(3,np.inf);ghi=-glo
 white_peak=0;radius_lo=float('inf');radius_hi=0.;sample_frames={0,24,36,48,72,80,108,126,150,180,204,215,216,228,234,239,240}
 for frame in range(241):
  path=cache/f'frame_{frame:06d}.bin';raw=path.read_bytes()
  magic,version,count,fr,time,spacing,support=HEADER.unpack(raw[:HEADER.size]);payload=raw[HEADER.size:]
  if magic!=b'CYBRWTR1' or version!=1 or fr!=frame or abs(time-frame/24)>1e-6 or len(payload)!=32*count:raise RuntimeError(f'Invalid primary header/payload {frame}')
  p=np.frombuffer(payload,'<f4').reshape(-1,8)
  id_bytes=path.with_suffix('.ids').read_bytes();volume_bytes=path.with_suffix('.volumes').read_bytes()
  ids=np.frombuffer(id_bytes,'<u4');vol=np.frombuffer(volume_bytes,'<f4')
  if not np.isfinite(p).all() or not np.isfinite(vol).all() or not (vol>0).all() or len(ids)!=count or len(vol)!=count:raise RuntimeError(f'Invalid primary values {frame}')
  if original_ids is None:
   original_ids=ids.copy();original_volumes=vol.copy()
   if len(np.unique(ids))!=count:raise RuntimeError('Repeated primary IDs')
  elif not np.array_equal(ids,original_ids) or not np.array_equal(vol,original_volumes):raise RuntimeError(f'Unaccounted primary identity/volume change {frame}')
  digest=hashlib.sha256(payload).hexdigest();hashes.add(digest)
  lo=p[:,:3].min(0);hi=p[:,:3].max(0);glo=np.minimum(glo,lo);ghi=np.maximum(ghi,hi)
  ww=path.with_suffix('.white').read_bytes();wm,wv,wc,wf,wt,wh,_=HEADER.unpack(ww[:HEADER.size]);wp=ww[HEADER.size:]
  if wm!=b'CYBRWW01' or wv!=1 or wf!=frame or wt!=time or len(wp)!=40*wc:raise RuntimeError(f'Invalid whitewater header {frame}')
  w=np.frombuffer(wp,dtype=np.dtype([('f','<f4',(9,)),('type','<u4')]))
  if not np.isfinite(w['f']).all() or not (w['type']<=2).all():raise RuntimeError(f'Invalid secondary values {frame}')
  if wc:
   if not (w['f'][:,6]>0).all() or not (w['f'][:,8]>w['f'][:,7]).all():raise RuntimeError(f'Invalid secondary radius/age {frame}')
   radius_lo=min(radius_lo,float(w['f'][:,6].min()));radius_hi=max(radius_hi,float(w['f'][:,6].max()))
  white_peak=max(white_peak,wc)
  # Hash the exact bytes already read for numerical validation. The exporter
  # uses these identities instead of rereading the multi-gigabyte state cache.
  state_files.append({'frame':frame,'time_s':frame/24,'sha256':{
   '.bin':hashlib.sha256(raw).hexdigest(),'.ids':hashlib.sha256(id_bytes).hexdigest(),
   '.volumes':hashlib.sha256(volume_bytes).hexdigest(),'.white':hashlib.sha256(ww).hexdigest()}})
  if frame in sample_frames:rows.append({'frame':frame,'time_s':float(time),'primary_count':count,'volume_m3':float(vol.sum(dtype=np.float64)),'bounds_m':[lo.tolist(),hi.tolist()],'max_speed_m_s':float(np.linalg.norm(p[:,3:6],axis=1).max()),'secondary_count':wc,'secondary_types':np.bincount(w['type'],minlength=3).tolist(),'primary_state_sha256':digest})
 ret=table(cache/'retention.csv');domain=table(cache/'domain_audit.csv');diag=table(cache/'diagnostics.csv')
 if len(ret)!=241 or len(domain)!=241 or len(hashes)!=241:raise RuntimeError('Missing or repeated native states')
 for i,(r,d) in enumerate(zip(ret,domain)):
  if int(r['frame'])!=i or int(d['frame'])!=i or abs(float(r['time'])-i/24)>1e-8:raise RuntimeError('Native state timestamp mismatch')
 emitted=float(ret[-1]['emitted_volume']);outflow=float(ret[-1]['outflow_volume'])
 if emitted!=0 or outflow!=0:raise RuntimeError('This take requires a source-free retained primary volume')
 dv=max(abs(float(r['volume'])-float(ret[0]['volume'])) for r in ret)
 if dv>1e-12:raise RuntimeError('Unexpected primary volume change')
 near=max(int(d['within_2cells']) for d in domain);clear=min(float(d['min_lateral_clearance']) for d in domain)
 if near!=0 or clear<=.06:raise RuntimeError('Primary water approaches the open lateral boundary')
 bounds=np.array(a.solid_bounds).reshape(2,3)
 if not (glo>=bounds[0]).all() or not (ghi<=bounds[1]).all():raise RuntimeError('Liquid outside declared solid field coverage')
 gp=cache/'domain_growth.jsonl';growth=[json.loads(l) for l in gp.read_text().splitlines() if l.strip()] if gp.is_file() else []
 result={'passed':True,'simulation_states':241,'unique_primary_position_velocity_states':len(hashes),'times_match_24_Hz':True,'finite_primary_and_secondary_values':True,'primary_count':len(original_ids),'unique_primary_ids':True,'primary_ids_and_volumes_identical_across_all_states':True,'initial_volume_m3':float(ret[0]['volume']),'final_volume_m3':float(ret[-1]['volume']),'maximum_primary_volume_change_m3':dv,'added_primary_water_m3':emitted,'outflow_primary_water_m3':outflow,'minimum_lateral_boundary_clearance_m':clear,'markers_within_two_cells_of_lateral_boundary':near,'domain_growth_events':len(growth),'growth_records':growth,'primary_global_bounds_m':[glo.tolist(),ghi.tolist()],'solid_field_coverage_bounds_m':bounds.tolist(),'peak_secondary_count':white_peak,'secondary_radius_range_m':[radius_lo,radius_hi] if white_peak else None,'pressure_relative_residual_max':max(float(d['pressure_relative_residual']) for d in diag),'divergence_after_l2_max_s_inv':max(float(d['divergence_after_l2_s_inv']) for d in diag),'volume_ledger_abs_max_m3':max(abs(float(d['volume_ledger_error_m3'])) for d in diag),'elevated_volume_above_08m_at_75s_m3':float(ret[180]['elevated_08']),'final_elevated_volume_above_08m_m3':float(ret[-1]['elevated_08']),'representative_states':rows,'scope':'Saved primary/secondary state and boundary audit. Does not establish physical convergence, coupled-air dynamics, or choreography visual acceptance.'}
 result['state_file_sha256']=state_files
 out=take/'delivery';out.mkdir(exist_ok=True);temporary=out/'SIMULATION_AUDIT.json.tmp'
 temporary.write_text(json.dumps(result,indent=2)+'\n');temporary.replace(out/'SIMULATION_AUDIT.json')
 print(json.dumps({k:v for k,v in result.items() if k!='state_file_sha256'},indent=2))
if __name__=='__main__':main()
