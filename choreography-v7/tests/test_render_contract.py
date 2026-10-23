"""Negative controls for resuming an interrupted native render."""
import copy
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from render_contract import (VALUE_FLAGS, BOOL_FLAGS, SURFACE_SETTINGS,
                             check_receipt, freeze_contract, make_contract,
                             save_png_atomic)

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


class AtomicPngTests(unittest.TestCase):
    @staticmethod
    def png_bytes(rgb):
        """A valid one-pixel RGB fixture, generated without an image library."""
        def chunk(kind, data):
            return (struct.pack('>I',len(data))+kind+data+
                    struct.pack('>I',zlib.crc32(kind+data)))
        return (b'\x89PNG\r\n\x1a\n'+
                chunk(b'IHDR',struct.pack('>IIBBBBB',1,1,8,2,0,0,0))+
                chunk(b'IDAT',zlib.compress(b'\x00'+bytes(rgb)))+
                chunk(b'IEND',b''))

    class EncodedImage:
        def __init__(self, payload, fail=False):
            self.payload=payload;self.fail=fail

        def save(self, stream, *, format):
            if format!='PNG':raise AssertionError('Explicit PNG encoding required')
            if self.fail:
                stream.write(self.payload[:len(self.payload)//2])
                stream.flush()
                raise OSError('injected partial PNG write')
            stream.write(self.payload)

    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.root=Path(self.tmp.name);self.png=self.root/'frame.png'
        self.original=self.png_bytes((255,0,0));self.replacement=self.png_bytes((0,0,255))

    def tearDown(self):self.tmp.cleanup()

    def test_partial_write_preserves_completed_png_and_removes_temporary(self):
        self.png.write_bytes(self.original)
        with self.assertRaisesRegex(OSError,'injected partial PNG write'):
            save_png_atomic(self.EncodedImage(self.replacement,fail=True),self.png)
        self.assertEqual(self.png.read_bytes(),self.original)
        self.assertEqual(list(self.root.iterdir()),[self.png])

    def test_partial_write_does_not_publish_new_png(self):
        with self.assertRaisesRegex(OSError,'injected partial PNG write'):
            save_png_atomic(self.EncodedImage(self.replacement,fail=True),self.png)
        self.assertFalse(self.png.exists())
        self.assertEqual(list(self.root.iterdir()),[])

    def test_sync_and_replace_failures_preserve_completed_png(self):
        for operation in ('fsync','replace'):
            with self.subTest(operation=operation):
                self.png.write_bytes(self.original)
                with patch('render_contract.os.'+operation,side_effect=OSError('injected '+operation)):
                    with self.assertRaisesRegex(OSError,'injected '+operation):
                        save_png_atomic(self.EncodedImage(self.replacement),self.png)
                self.assertEqual(self.png.read_bytes(),self.original)
                self.assertEqual(list(self.root.iterdir()),[self.png])

    def test_success_flushes_and_syncs_complete_bytes_before_publication(self):
        self.png.write_bytes(self.original);events=[]
        real_fsync=os.fsync;real_replace=os.replace

        def synced(fd):
            self.assertEqual(os.fstat(fd).st_size,len(self.replacement))
            self.assertEqual(self.png.read_bytes(),self.original)
            real_fsync(fd);events.append('synced')

        def published(source,destination):
            self.assertEqual(events,['synced'])
            self.assertEqual(Path(source).parent,self.root)
            self.assertEqual(Path(source).read_bytes(),self.replacement)
            self.assertEqual(self.png.read_bytes(),self.original)
            real_replace(source,destination);events.append('published')

        with patch('render_contract.os.fsync',side_effect=synced),patch('render_contract.os.replace',side_effect=published):
            save_png_atomic(self.EncodedImage(self.replacement),self.png)
        self.assertEqual(events,['synced','published'])
        self.assertEqual(self.png.read_bytes(),self.replacement)
        self.assertEqual(list(self.root.iterdir()),[self.png])

if __name__=='__main__':unittest.main()
