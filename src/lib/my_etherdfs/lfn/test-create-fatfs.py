#!/usr/bin/env python3
"""Test with the audited PicoMEM FatFs source and a disposable image, never hardware.
Usage: python3 experimental/lfn/test-fatfs.py /path/to/ISA-PicoMEM
Requires a C compiler and mtools (mformat, mcopy, mmd).
"""
import os, pathlib, subprocess, sys, tempfile, argparse
kit=pathlib.Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('source')
parser.add_argument('--fat32',action='store_true')
args=parser.parse_args()
source=pathlib.Path(args.source).resolve()/"src/lib/FatFs_SPI/ff15/source"
cc=os.environ.get('CC','cc')
def run(*args):subprocess.run(list(map(str,args)),check=True)
with tempfile.TemporaryDirectory(prefix='pm-lfn-fatfs-') as d:
    tmp=pathlib.Path(d); image=tmp/'fixture.img';payload=tmp/'payload.txt'
    payload.write_bytes(b'LFN fixture\n')
    if args.fat32:
        with image.open('wb') as f:f.truncate(64*1024*1024)
        run('mformat','-i',image,'-F','::')
    else:
        run('mformat','-C','-i',image,'-f','1440','::')
    for name in ('Long Filename.txt','Mixed Case.txt','This filename exceeds 26 bytes.txt','Cafe Long Filename é.txt'):
        run('mcopy','-i',image,payload,'::'+name)
    run('mmd','-i',image,'::Long Directory')
    run('mcopy','-i',image,payload,'::Long Directory/Nested Long Filename.txt')
    flags=['-std=c99','-g','-fsanitize=address,undefined','-I'+str(source)]
    # Upstream code retains its own source-adjacent ffconf.h, as in its CMake build.
    for name in ('ff','ffunicode'):
        run(cc,*flags,'-c',source/(name+'.c'),'-o',tmp/(name+'.o'))
    run(cc,*flags,'-Wall','-Wextra','-Werror',kit/'lfn.c',kit/'fatfs-adapter.c',
        kit/'create.c',kit/'create-fatfs.c',kit/'tests/test-create-fatfs.c',tmp/'ff.o',tmp/'ffunicode.o','-lm','-o',tmp/'test')
    run(tmp/'test',image)
    run('mdir','-i',image,'::Created Long Directory')
    copied=tmp/'readback.txt'
    run('mcopy','-i',image,'::Created Long Directory/Created Long Filename.txt',copied)
    assert copied.read_bytes()==b'new long-name payload'
    print('Independent mtools long-name lookup and payload check passed')
