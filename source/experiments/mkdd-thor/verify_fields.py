"""Verify selected field displacements against the exact private retail DOL.

Stores only authored layout metadata and function addresses, never game bytes or
disassembly. Static field evidence is separate from live gameplay acceptance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

SHA1 = 'e389e50357d4a5076ab3d38672204658f411123d'
# (instruction address, field displacement). D-form load/store or address calculation.
FIELDS = [
    ('RaceMgr.director',0x8018a1bc,0x18),
    ('RaceDirector.phase',0x8018a1c0,0x0c),
    ('RaceMgr.info',0x80187a1c,0x38),
    ('RaceInfo.mode',0x80187a64,8),
    ('RaceInfo.kartCount',0x80189ad4,0x1c),
    ('RaceInfo.screens',0x80187afc,0x20),
    ('RaceInfo.demoType',0x80187a58,0x28),
    ('RaceInfo.mirror',0x80187cf0,0x2c),
    ('RaceMgr.course',0x80189bc4,0x44),
    ('RaceMgr.checkers',0x80189a5c,0x48),
    ('RaceMgr.totalLaps',0x80189ab4,0x2e),
    ('KartChecker.rank',0x80189a74,0x88),
    ('KartChecker.time',0x80189a84,0x84),
    ('KartChecker.timeWrite',0x80186d70,0x84),
    ('KartChecker.lap',0x8018936c,0x2c),
    ('KartChecker.finished',0x8018935c,0x29),
    ('KartInfo.character0',0x8018548c,8),
    ('KartInfo.character1',0x80185470,0x10),
    ('CharacterDB.id',0x80185480,0),
    ('ItemMgr.stock',0x80209100,0x24c),
    ('ItemMgr.stockWrite',0x8020910c,0x24c),
    ('ItemMgr.equipped',0x80209a28,0x2cc),
    ('ItemMgr.shuffle',0x8020b5fc,0x35c),
    ('ItemShuffle.rollFlags',0x8020b604,6),
]


def verify(dol):
    data = dol.read_bytes()
    if hashlib.sha1(data).hexdigest()!=SHA1:
        raise ValueError('Unexpected DOL; no layout accepted')
    offsets = struct.unpack_from('>18I',data)
    addresses = struct.unpack_from('>18I',data,0x48)
    sizes = struct.unpack_from('>18I',data,0x90)
    result=[]
    for name,pc,expected in FIELDS:
        offset=next(o+pc-a for o,a,s in zip(offsets,addresses,sizes) if a<=pc<a+s)
        word=struct.unpack_from('>I',data,offset)[0]
        # Generic PowerPC D-form address calculation and scalar load/store opcodes.
        if word>>26 not in (14,32,34,36,38,40,42,44,48,52) or word&0xffff!=expected:
            raise ValueError(f'Field mismatch: {name} at {pc:08x}')
        result.append({'field':name,'retail_pc':hex(pc),'offset':hex(expected),'pass':True})
    return {'status':'PASS_STATIC_RETAIL_FIELD_EVIDENCE','dol_sha1':SHA1,'checks':len(result),
            'fields':result,'live_game_verified':False}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('dol',type=Path)
    p.add_argument('--receipt',type=Path)
    a=p.parse_args(); report=verify(a.dol)
    if a.receipt:
        a.receipt.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f"PASS {report['checks']} exact-retail field checks; live acceptance pending")


if __name__=='__main__':
    main()
