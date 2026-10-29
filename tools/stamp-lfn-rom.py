#!/usr/bin/env python3
"""Stamp only audited unused PicoMEM 2 ROM padding; preserve the BIOS checksum."""
from pathlib import Path
import hashlib,sys
EXPECTED='c63ff8fe9a34f43138e55c0c70cab5fe12fe960c9f156363706d2548d4a62c6f'
OFFSET=0x3fc0
DESCRIPTOR=b'PMLN\x01\x70\x1a\x34l00nix\x00\x00'
CREATE_OFFSET=0x3fd0
CREATE_DESCRIPTOR=b'PMLW\x01\x71\x1a\x34l00nix\x00\x00'
RENAME_OFFSET=0x3fe0
RENAME_DESCRIPTOR=b'PMLR\x01\x72\x1a\x34l00nix\x00\x00'
DIAG_OFFSET=0x3fb0
DIAG_DESCRIPTOR=b'PMDG\x01\x73\x08\x34l00nix\x00\x00'
def stamp(data, create=False, rename=False, diag=False):
    if diag and not rename: raise ValueError("Diagnostics require rename")
    if rename and not create: raise ValueError("Rename requires create")
    if len(data)!=16384 or hashlib.sha256(data).hexdigest()!=EXPECTED:
        raise ValueError('Unaudited BIOS: descriptor placement must be reviewed')
    if data[OFFSET:OFFSET+16]!=b'\xff'*16 or sum(data)%256:
        raise ValueError('Unexpected padding or BIOS checksum')
    result=bytearray(data);result[OFFSET:OFFSET+16]=DESCRIPTOR
    if create:
        if data[CREATE_OFFSET:CREATE_OFFSET+16]!=b'\xff'*16:
            raise ValueError('Unexpected create descriptor padding')
        result[CREATE_OFFSET:CREATE_OFFSET+16]=CREATE_DESCRIPTOR
    if rename:
        if data[RENAME_OFFSET:RENAME_OFFSET+16]!=b'\xff'*16: raise ValueError("Unexpected rename padding")
        result[RENAME_OFFSET:RENAME_OFFSET+16]=RENAME_DESCRIPTOR
    if diag:
        if data[DIAG_OFFSET:DIAG_OFFSET+16]!=b'\xff'*16: raise ValueError("Unexpected diagnostics padding")
        result[DIAG_OFFSET:DIAG_OFFSET+16]=DIAG_DESCRIPTOR
    result[-1]=0;result[-1]=(-sum(result))&255
    assert not sum(result)%256
    return bytes(result)
if __name__=='__main__':
    diag='--diag' in sys.argv
    if diag:sys.argv.remove('--diag')
    rename='--rename' in sys.argv
    if rename:sys.argv.remove('--rename')
    create='--create' in sys.argv
    if create:sys.argv.remove('--create')
    if len(sys.argv)!=3:raise SystemExit('Usage: stamp-lfn-rom.py INPUT OUTPUT')
    try:result=stamp(Path(sys.argv[1]).read_bytes(),create,rename,diag)
    except ValueError as e:raise SystemExit(str(e))
    Path(sys.argv[2]).write_bytes(result)
