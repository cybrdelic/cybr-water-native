import csv
import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path
import numpy as np

spec=importlib.util.spec_from_file_location('inspection',Path(__file__).resolve().parents[1]/'inspect_native_states.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class NativePrefixAudit(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.take=Path(self.tmp.name);self.cache=self.take/'cache';self.cache.mkdir()
        (self.cache/'manifest.json').write_text(json.dumps({'scene':'waterbending_martial_v11','dx_m':.03,'fps':24}))
        for i in range(2):
            path=self.cache/f'frame_{i:06d}.bin'
            p=np.array([[-.2,.2,0,0,0,0,1,0],[.2,.2,0,0,0,0,1,0]],'<f4')
            path.write_bytes(m.HEADER.pack(b'CYBRWTR1',1,2,i,i/24,.01,.02)+p.tobytes())
            path.with_suffix('.ids').write_bytes(np.array([1,2],'<u4').tobytes())
            path.with_suffix('.volumes').write_bytes(np.array([1e-6,1e-6],'<f4').tobytes())
            path.with_suffix('.white').write_bytes(m.HEADER.pack(b'CYBRWW01',1,0,i,i/24,.03,0))
        fields=['time','volume_m3','volume_ledger_error_m3','pressure_relative_residual','divergence_after_l2_s_inv','emitted_volume_m3','outflow_volume_m3','step_seconds']
        with (self.cache/'diagnostics.csv').open('w') as f:
            writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader()
            writer.writerow({k:0 for k in fields})
            writer.writerow({**{k:0 for k in fields},'time':1/24})
    def run_audit(self):return m.audit(self.take,.03,24,1,{0,1})
    def test_bounded_prefix_never_claims_full_or_visual_acceptance(self):
        r=self.run_audit();self.assertTrue(r['numerical_state_checks_passed'])
        self.assertFalse(r['full_ten_seconds']);self.assertFalse(r['visual_acceptance'])
        self.assertEqual(r['audited_frames'],[0,1]);self.assertEqual(r['maximum_saved_volume_change_m3'],0)
    def test_missing_prefix_rejected(self):
        (self.cache/'frame_000000.bin').unlink()
        with self.assertRaisesRegex(RuntimeError,'Missing native prefix'):self.run_audit()
    def test_changed_primary_volume_rejected(self):
        (self.cache/'frame_000001.volumes').write_bytes(np.array([1e-6,2e-6],'<f4').tobytes())
        with self.assertRaisesRegex(RuntimeError,'ownership or reference volumes'):self.run_audit()
    def test_nonfinite_state_rejected(self):
        p=self.cache/'frame_000001.bin';b=bytearray(p.read_bytes());struct.pack_into('<f',b,32,float('nan'));p.write_bytes(b)
        with self.assertRaisesRegex(RuntimeError,'Invalid primary values'):self.run_audit()
    def test_truncated_ledger_rejected(self):
        path=self.cache/'diagnostics.csv';lines=path.read_text().splitlines();path.write_text('\n'.join(lines[:2])+'\n')
        with self.assertRaisesRegex(RuntimeError,'ledger does not cover'):self.run_audit()
    def test_explicit_partial_ledger_is_labelled(self):
        path=self.cache/'diagnostics.csv';lines=path.read_text().splitlines();path.write_text('\n'.join(lines[:2])+'\n')
        r=m.audit(self.take,.03,24,1,{1},True)
        self.assertFalse(r['ledger_coverage_complete']);self.assertEqual(r['diagnostics_time_range_s'],[0,0])
        self.assertFalse(r['visual_acceptance'])
    def test_grid_mislabel_rejected(self):
        with self.assertRaisesRegex(RuntimeError,'grid'):m.audit(self.take,.12,24,1,{1})
    def test_timestamp_mislabel_rejected(self):
        p=self.cache/'frame_000001.bin';b=bytearray(p.read_bytes());struct.pack_into('<f',b,20,.5);p.write_bytes(b)
        with self.assertRaisesRegex(RuntimeError,'time'):self.run_audit()

if __name__=='__main__':unittest.main()
