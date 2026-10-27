#!/usr/bin/env python3
"""Stamp only audited unused PicoMEM 2 ROM padding; preserve the BIOS checksum."""
from pathlib import Path
import hashlib,sys
EXPECTED='c63ff8fe9a34f43138e55c0c70cab5fe12fe960c9f156363706d2548d4a62c6f'
OFFSET=0x3fc0
DESCRIPTOR=b'PMLN\x01\x70\x1a\x34l00nix\x00\x00'
def stamp(data):
    if len(data)!=16384 or hashlib.sha256(data).hexdigest()!=EXPECTED:
        raise ValueError('Unaudited BIOS: descriptor placement must be reviewed')
    if data[OFFSET:OFFSET+16]!=b'\xff'*16 or sum(data)%256:
        raise ValueError('Unexpected padding or BIOS checksum')
    result=bytearray(data);result[OFFSET:OFFSET+16]=DESCRIPTOR
    result[-1]=0;result[-1]=(-sum(result))&255
    assert not sum(result)%256
    return bytes(result)
if __name__=='__main__':
    if len(sys.argv)!=3:raise SystemExit('Usage: stamp-lfn-rom.py INPUT OUTPUT')
    try:result=stamp(Path(sys.argv[1]).read_bytes())
    except ValueError as e:raise SystemExit(str(e))
    Path(sys.argv[2]).write_bytes(result)
