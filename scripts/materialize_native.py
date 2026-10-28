#!/usr/bin/env python3
"""Restore exact readable V6/V7 sources from checked, repository-local deltas.

No network access, pickle, eval, shell commands or archive path extraction.
Existing files with unexpected edits are refused rather than overwritten.
"""
from __future__ import annotations
import argparse, base64, hashlib, json, lzma, os, tempfile
from pathlib import Path, PurePosixPath
ROOT=Path(__file__).resolve().parents[1]

def digest(b:bytes)->str:return hashlib.sha256(b).hexdigest()
def safe(rel:str)->Path:
 p=PurePosixPath(rel)
 if p.is_absolute() or '..' in p.parts or not p.parts or '\\' in rel:raise ValueError('Unsafe path: '+rel)
 out=ROOT.joinpath(*p.parts)
 if not out.resolve().is_relative_to(ROOT):raise ValueError('Escaping path: '+rel)
 return out

def bundle(directory:str)->dict:
 folder=safe(directory);manifest=json.loads((folder/'manifest.json').read_text())
 parts=[]
 for rel in manifest['parts']:
  if PurePosixPath(rel).name!=rel:raise ValueError('Invalid bundle part')
  data=(folder/rel).read_bytes();parts.append(data)
  if digest(data)!=manifest['part_sha256'][rel]:raise ValueError('Corrupt bundle part: '+rel)
 compressed=base64.b64decode(b''.join(parts),validate=True)
 if digest(compressed)!=manifest['compressed_sha256']:raise ValueError('Corrupt bundle')
 raw=lzma.decompress(compressed,memlimit=256<<20)
 if len(raw)>4000000:raise ValueError('Unexpected payload size')
 return json.loads(raw)

def apply(base:bytes,spec:dict)->bytes:
 if 'text' in spec:out=spec['text'].encode('utf-8')
 else:
  if digest(base)!=spec['before_sha256']:raise ValueError('Base source identity mismatch')
  lines=base.decode('utf-8').splitlines(keepends=True);pieces=[]
  for op in spec['ops']:
   if isinstance(op,str):pieces.append(op)
   elif isinstance(op,list) and len(op)==2 and all(type(x)==int for x in op) and 0<=op[0]<=op[1]<=len(lines):pieces.extend(lines[op[0]:op[1]])
   else:raise ValueError('Invalid delta operation')
  out=''.join(pieces).encode('utf-8')
 if digest(out)!=spec['sha256']:raise ValueError('Restored source identity mismatch')
 return out

def planned()->dict[str,bytes]:
 migration=bundle('native-v6/migration');extras=bundle('choreography-v7/migration')
 if migration['format']!='cybr-native-delta-v1' or extras['format']!='cybr-duet-overlay-v1':raise ValueError('Unsupported format')
 output={};baseline={}
 for rel,spec in migration['files'].items():
  safe('native-v6/engine/'+rel)
  base=safe(spec['base']).read_bytes() if 'base' in spec else b''
  baseline[rel]=apply(base,spec);output['native-v6/engine/'+rel]=baseline[rel]
 for rel,data in baseline.items():output['choreography-v7/engine/'+rel]=data
 for rel,spec in extras['engine_changes'].items():
  if rel not in baseline:raise ValueError('Missing V6 delta base')
  output['choreography-v7/engine/'+rel]=apply(baseline[rel],spec)
 for rel,spec in extras['files'].items():
  if not rel.startswith(('native-v6/','choreography-v7/')):raise ValueError('Unapproved output prefix')
  if rel in output:raise ValueError('Duplicate destination')
  output[rel]=apply(b'',spec)
 for rel in output:safe(rel)
 provenance={rel:digest(data) for rel,data in sorted(output.items())}
 output['native-v6/SOURCE_MANIFEST.json']=(json.dumps({'format':'cybr-materialized-source-v1','files':provenance},indent=2)+'\n').encode()
 return output

def main()->None:
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');a=ap.parse_args()
 output=planned()
 # Validate every destination before any write, so local edits are never lost.
 for rel,data in output.items():
  p=safe(rel)
  if p.exists() and (not p.is_file() or p.read_bytes()!=data):raise RuntimeError('Refusing to overwrite edited output: '+rel)
  if a.check and not p.exists():raise RuntimeError('Missing materialized file: '+rel)
 if not a.check:
  for rel,data in output.items():
   p=safe(rel)
   if p.exists():continue
   p.parent.mkdir(parents=True,exist_ok=True)
   fd,tmp=tempfile.mkstemp(prefix=p.name+'.',dir=p.parent)
   try:
    with os.fdopen(fd,'wb') as f:f.write(data)
    os.replace(tmp,p)
   finally:
    if os.path.exists(tmp):os.unlink(tmp)
 print(json.dumps({'passed':True,'check_only':a.check,'files':len(output),'v6_preserved':True}))
if __name__=='__main__':main()
