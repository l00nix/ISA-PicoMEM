#!/usr/bin/env python3
from pathlib import Path
import importlib.util
spec=importlib.util.spec_from_file_location('stamp',Path(__file__).with_name('stamp-lfn-rom.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
src=Path(__file__).resolve().parent.parent/'src/rom/pm2bios.bin'
b=src.read_bytes();a=m.stamp(b)
assert a[:m.OFFSET]==b[:m.OFFSET]
assert a[m.OFFSET+16:-1]==b[m.OFFSET+16:-1]
assert a[m.OFFSET:m.OFFSET+16]==m.DESCRIPTOR and sum(a)%256==0
for bad in (b[:-1],b+b'\0',bytes([b[0]^1])+b[1:],a):
 try:m.stamp(bad)
 except ValueError:pass
 else:raise AssertionError('Unaudited ROM accepted')
print('ROM descriptor: exact source, padding, checksum and refusal checks passed')

c=m.stamp(b,True)
assert c[m.CREATE_OFFSET:m.CREATE_OFFSET+16]==m.CREATE_DESCRIPTOR
assert c[m.OFFSET:m.OFFSET+16]==m.DESCRIPTOR and sum(c)%256==0
assert c[:m.OFFSET]==b[:m.OFFSET] and c[m.CREATE_OFFSET+16:-1]==b[m.CREATE_OFFSET+16:-1]
print('Separate create descriptor, unchanged metadata marker and checksum passed')
