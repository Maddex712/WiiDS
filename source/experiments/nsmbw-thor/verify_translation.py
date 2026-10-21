"""Verify the private E2 translation artifacts and write a compact receipt."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from prepare_e2 import E2_DOL_SHA256, PIN, require, sha


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo',type=Path,required=True)
    parser.add_argument('--prepared',type=Path,required=True)
    args=parser.parse_args()
    repo=args.repo.resolve(); prepared=args.prepared.resolve()
    require('build' in prepared.parts,'Receipt must stay under ignored build/')
    require(sha((prepared/'game/main.dol').read_bytes())==E2_DOL_SHA256,'Wrong private DOL')
    metadata=json.loads((repo/'generated/base_translation_output.json').read_text())
    manifest=json.loads((repo/'build/base/nsmbw_base_manifest.json').read_text())
    config=(repo/'generated/RuntimeConfig.h').read_text()
    cmake=(repo/'generated/build_shards/shards.cmake').read_text()
    require(metadata['quality']=={'unsupportedInstructionCount':0,'invalidSsaFunctionCount':0},'Translated function quality failure')
    require(manifest['RelBaseAddress']==0x80768500,'E1 or wrong REL image in generated manifest')
    require('New Super Mario Bros. Wii (USA rev 2)' in config,'Wrong generated runtime config')
    require('SDA1_BASE = 0x8042F680u' in config and 'SDA2_BASE = 0x80433080u' in config,'Wrong SDA bases')
    shards=list((repo/'generated/build_shards/base_common').glob('shard_*.cpp'))
    require(len(shards)==72,'Unexpected shard count')
    funcs=len(metadata['functions'])
    require(funcs==41420,'Unexpected generated function count')
    files=['generated/base_translation_output.json','generated/base_translation_sources.bin',
           'generated/data_sections_init.cpp','generated/data_sections_init_blobs.S',
           'generated/RuntimeConfig.h','generated/build_shards/shards.cmake',
           'build/base/nsmbw_base_manifest.json']
    hashes={name:sha((repo/name).read_bytes()) for name in files}
    receipt={'status':'translation_artifacts_verified_not_gameplay', 'source_commit':PIN,
             'revision':2,'dol_sha256':E2_DOL_SHA256,'translated_metadata_functions':funcs,
             'shared_compiled_functions':41067,'shards':len(shards),'quality':metadata['quality'],
             'artifact_sha256':hashes,
             'known_cli_warnings':['Two speculative map seeds 80006618 and 8000669C skipped because word 18080000 at 800066D8 is unsupported.',
                                   'Branch 801AB010 to address 00000060 is outside the loaded image.'],
             'limits':['This verifier checks generated artifact identity and structure; it does not run the game.']}
    (prepared/'translation-verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({k:v for k,v in receipt.items() if k!='artifact_sha256'},indent=2))


if __name__=='__main__': main()
