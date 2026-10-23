"""Negative controls for resuming an interrupted native render."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from render_contract import (VALUE_FLAGS, BOOL_FLAGS, SURFACE_SETTINGS,
                             check_receipt, freeze_contract, make_contract)

class ContractTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.assets = self.root/'assets'; self.assets.mkdir()
        for name in ('solids.mesh','solid.sdf'):
            (self.assets/name).write_bytes(('fixture:'+name).encode())
        self.cmd = ['trace_native']
        for flag in VALUE_FLAGS: self.cmd.extend([flag,'test-value'])
        self.cmd.extend(BOOL_FLAGS)
        self.source = {'trace.cpp':'frozen-source','surface.cpp':'frozen-surface'}
        self.contract = make_contract(self.source,self.cmd,self.assets)
        self.receipt = {'source_code_sha256':self.source,'command':self.cmd,
                        'surface':dict(SURFACE_SETTINGS)}
        self.out=self.root/'rendered';self.out.mkdir()
    def tearDown(self): self.tmp.cleanup()
    def test_exact_resume(self):
        freeze_contract(self.out,self.contract);freeze_contract(self.out,self.contract)
        check_receipt(self.receipt,self.contract)
    def test_every_visual_setting_is_checked(self):
        for flag in VALUE_FLAGS:
            with self.subTest(flag=flag):
                r=copy.deepcopy(self.receipt);r['command'][r['command'].index(flag)+1]='changed'
                with self.assertRaises(RuntimeError):check_receipt(r,self.contract)
    def test_boolean_optics_are_checked(self):
        for flag in BOOL_FLAGS:
            with self.subTest(flag=flag):
                r=copy.deepcopy(self.receipt);r['command'].remove(flag)
                with self.assertRaises(RuntimeError):check_receipt(r,self.contract)
    def test_reconstruction_is_checked(self):
        for key in SURFACE_SETTINGS:
            with self.subTest(key=key):
                r=copy.deepcopy(self.receipt);r['surface'][key]='changed'
                with self.assertRaises(RuntimeError):check_receipt(r,self.contract)
    def test_changed_source_rejected(self):
        r=copy.deepcopy(self.receipt);r['source_code_sha256']['trace.cpp']='changed'
        with self.assertRaises(RuntimeError):check_receipt(r,self.contract)
    def test_changed_scene_geometry_rejected(self):
        freeze_contract(self.out,self.contract)
        (self.assets/'solids.mesh').write_bytes(b'different geometry')
        changed=make_contract(self.source,self.cmd,self.assets)
        with self.assertRaises(RuntimeError):freeze_contract(self.out,changed)
    def test_pre_contract_frames_not_silently_adopted(self):
        (self.out/'frame_000000.json').write_text(json.dumps(self.receipt))
        with self.assertRaises(RuntimeError):freeze_contract(self.out,self.contract)
    def test_changed_output_settings_rejected(self):
        freeze_contract(self.out,self.contract)
        changed=copy.deepcopy(self.contract);changed['visual_options']['--adaptive-aa']='4'
        with self.assertRaises(RuntimeError):freeze_contract(self.out,changed)
    def test_worker_and_work_directory_not_visual_settings(self):
        r=copy.deepcopy(self.receipt);r['command'] += ['--threads','4','--out','/another/frame']
        check_receipt(r,self.contract)
    def test_duplicate_or_missing_flag_rejected(self):
        for cmd in (self.cmd+['--spp','4'],self.cmd[0:2]):
            with self.assertRaises(RuntimeError):make_contract(self.source,cmd,self.assets)

if __name__=='__main__':unittest.main()
