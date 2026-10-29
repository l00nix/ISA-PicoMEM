#!/usr/bin/env python3
"""Fault injection against real DFS and FatFs; only disposable local images."""
import pathlib,subprocess,tempfile,os
kit=pathlib.Path(__file__).resolve().parent
src=kit.parents[1];fat=src.parent/'FatFs_SPI/ff15/source'
cc=os.environ.get('CC','cc')
os.environ['UBSAN_OPTIONS']='halt_on_error=1'
def run(*a):subprocess.run(list(map(str,a)),check=True)
with tempfile.TemporaryDirectory(prefix='pm-file-errors-') as d:
 tmp=pathlib.Path(d);objects=[]
 flags=['-std=c99','-g','-fsanitize=address,undefined','-DPICOMEM_EXPERIMENTAL_FILE_ERRORS=1','-I'+str(kit/'include'),'-I'+str(src),'-I'+str(fat)]
 for name,path,extra in [('fs',src/'fs.c',['-Df_stat=test_f_stat']),('dispatcher',src/'ethersrv-pico.c',[]),('checked',src/'checked-file.c',['-Df_close=test_f_close','-Wall','-Wextra','-Werror']),('ff',fat/'ff.c',[]),('unicode',fat/'ffunicode.c',[]),('test',kit/'test.c',['-Wall','-Wextra','-Werror'])]:
  out=tmp/(name+'.o');run(cc,*flags,*extra,'-c',path,'-o',out);objects.append(out)
 run(cc,'-fsanitize=address,undefined',*objects,'-lm','-o',tmp/'test')
 for mode in ('normal','writefail','syncfail','statfail','statclosefail'):
  image=tmp/(mode+'.img')
  with image.open('wb') as f:f.truncate(64*1024*1024)
  run('mformat','-i',image,'-F','::');run(tmp/'test',image,mode)
  if mode=='normal':
   run('mcopy','-i',image,'::CHECK.TXT',tmp/'readback')
   assert (tmp/'readback').read_bytes()==b'checked close payload'
 print('PASS FAT32 normal readback and injected close/data-write/sync/metadata failures')
