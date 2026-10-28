"""Independent corruption gates and a genuine native checkpoint continuation."""
from __future__ import annotations
import copy
import csv
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from render_contract import VALUE_FLAGS,BOOL_FLAGS,SURFACE_SETTINGS
from reproduce import (CHECKPOINT_HEADER,checkpoint_header,prepare_resume,
                       preserve_frame_checkpoint,run_simulation)
from verify_export import verify_receipt


class ExportEvidenceTests(unittest.TestCase):
    def setUp(self):
        options={flag:'1' for flag in VALUE_FLAGS}
        command=['trace_native']
        for flag,value in options.items():command.extend((flag,value))
        command.extend(BOOL_FLAGS)
        self.contract={'source_code_sha256':{'trace.cpp':'source'},
                       'visual_options':{**options,**{flag:True for flag in BOOL_FLAGS}},
                       'surface_settings':dict(SURFACE_SETTINGS)}
        self.state={extension:'raw:'+extension for extension in ('.bin','.ids','.volumes','.white')}
        self.record={'frame':12,'time_s':.5,'selected_frame_review':False,
                     'source_code_sha256':self.contract['source_code_sha256'],
                     'render_contract_sha256':'contract','command':command,
                     'source_states':self.state,'phase_sha256':'phase',
                     'previous_phase_sha256':'previous',
                     'surface':{**SURFACE_SETTINGS,'mesh_signed_volume_m3':.4,
                                'dense_marker_target_volume_m3':.4,'relative_volume_error':0.},
                     'render':{'nonfinite_paths':0,'finite_pixels':1}}

    def check(self,record):
        verify_receipt(record,self.contract,'contract',12,self.state,'phase','previous')

    def test_valid_independent_evidence(self):self.check(self.record)

    def test_each_state_sidecar_change_rejected(self):
        for extension in self.state:
            with self.subTest(extension=extension):
                record=copy.deepcopy(self.record);record['source_states'][extension]='changed'
                with self.assertRaises(RuntimeError):self.check(record)

    def test_phase_chain_and_contract_change_rejected(self):
        for field in ('phase_sha256','previous_phase_sha256','render_contract_sha256'):
            with self.subTest(field=field):
                record=copy.deepcopy(self.record);record[field]='changed'
                with self.assertRaises(RuntimeError):self.check(record)

    def test_selected_frame_and_nan_surface_rejected(self):
        record=copy.deepcopy(self.record);record['selected_frame_review']=True
        with self.assertRaises(RuntimeError):self.check(record)
        record=copy.deepcopy(self.record);record['surface']['mesh_signed_volume_m3']=float('nan')
        with self.assertRaises(RuntimeError):self.check(record)

    def test_mixed_sampling_and_false_volume_receipt_rejected(self):
        record=copy.deepcopy(self.record);record['command'][record['command'].index('--spp')+1]='16'
        with self.assertRaises(RuntimeError):self.check(record)
        record=copy.deepcopy(self.record);record['surface']['relative_volume_error']=.001
        with self.assertRaises(RuntimeError):self.check(record)


class ActualNativeRecoveryTests(unittest.TestCase):
    @unittest.skipUnless((ROOT/'engine/build/gallery_sim').is_file(),'native executable required')
    def test_grown_domain_checkpoint_matches_uninterrupted_native_states(self):
        with tempfile.TemporaryDirectory(prefix='cybr-native-recovery-') as temp:
            folder=Path(temp);reference=folder/'reference';interrupted=folder/'interrupted'
            common=[str(ROOT/'engine/build/gallery_sim'),'--scene','waterbending_duet',
                    '--dx','.15','--duration','.25','--fps','24','--threads','1','--apic',
                    '--particles-per-axis','2','--reseed-interval','0','--no-whitewater',
                    '--surface-tension','.072','--auto-domain','--domain-padding','.48',
                    '--domain-lower','-1.05,-.15,-1.05','--domain-upper','1.05,2.85,1.05']
            def simulate(command,name):
                with (folder/name).open('w') as log:
                    subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
            simulate(common+['--out',str(reference)],'reference.log')
            command=common+['--out',str(interrupted)]
            simulate(command+['--stop-after-frame','3'],'prefix.log')
            self.assertTrue(preserve_frame_checkpoint(interrupted))
            with (interrupted/'checkpoint.bin').open('rb') as stream:
                original=checkpoint_header(stream)
            self.assertEqual(original['time'],3/24)
            self.assertNotEqual(original['lower'],[-1.05,-.15,-1.05])
            # A real later saved-state suffix and ledger row emulate a failure
            # after a prior checkpoint. The checkpoint's time is then damaged
            # to a subframe value; it must never be passed to the native CLI.
            for extension in ('.bin','.white','.ids','.volumes'):
                shutil.copy2(reference/f'frame_000004{extension}',interrupted/f'frame_000004{extension}')
            for name in ('retention.csv','domain_audit.csv','diagnostics.csv'):
                with (reference/name).open(newline='') as stream:
                    reader=csv.DictReader(stream);fields=reader.fieldnames
                    suffix=[r for r in reader if float(r['time'])>original['time']]
                with (interrupted/name).open('a',newline='') as stream:
                    csv.DictWriter(stream,fieldnames=fields).writerows(suffix)
            corrupted=interrupted/'checkpoint.bin'
            with corrupted.open('r+b') as stream:
                values=list(CHECKPOINT_HEADER.unpack(stream.read(CHECKPOINT_HEADER.size)))
                values[-2]=3.8/24;stream.seek(0);stream.write(CHECKPOINT_HEADER.pack(*values))
            self.assertFalse(preserve_frame_checkpoint(interrupted))
            resumed,receipt=prepare_resume(command,interrupted)
            self.assertEqual(receipt['frame'],3)
            self.assertFalse((interrupted/'frame_000004.bin').exists())
            self.assertTrue((Path(receipt['backup_directory'])/'frame_000004.bin').exists())
            run_simulation(resumed,folder/'resume.log',interrupted)
            for frame in range(7):
                for extension in ('.bin','.white','.ids','.volumes'):
                    name=f'frame_{frame:06d}{extension}'
                    self.assertEqual((reference/name).read_bytes(),(interrupted/name).read_bytes(),name)
            for name in ('retention.csv','domain_audit.csv'):
                with (interrupted/name).open(newline='') as stream:rows=list(csv.DictReader(stream))
                self.assertEqual([int(row['frame']) for row in rows],list(range(7)))
            with (interrupted/'diagnostics.csv').open(newline='') as stream:
                times=[float(row['time']) for row in csv.DictReader(stream)]
            self.assertTrue(all(a<b for a,b in zip(times,times[1:])))
            before=[json.loads(line) for line in (reference/'domain_growth.jsonl').read_text().splitlines()]
            after=[json.loads(line) for line in (interrupted/'domain_growth.jsonl').read_text().splitlines()]
            self.assertEqual(before,after)


if __name__=='__main__':unittest.main()
