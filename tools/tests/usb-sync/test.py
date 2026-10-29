#!/usr/bin/env python3
"""Actual adapter/TinyUSB tests; faults affect host models only, never hardware."""
import os,pathlib,platform,subprocess,tempfile
kit=pathlib.Path(__file__).resolve().parent
root=kit.parents[2]
os.environ['UBSAN_OPTIONS']='halt_on_error=1'
cc=os.environ.get('CC','cc')
def run(*a):subprocess.run(list(map(str,a)),check=True)
with tempfile.TemporaryDirectory(prefix='pm-usb-sync-') as d:
    tmp=pathlib.Path(d)
    flags=['-std=gnu11','-g','-fsanitize=address,undefined','-ffunction-sections','-DPICOMEM_EXPERIMENTAL_USB_SYNC=1','-I'+str(kit),'-I'+str(root/'src/lib/tinyusb/src'),'-I'+str(root/'src/lib/FatFs_SPI/ff15/source')]
    gc='-Wl,-dead_strip' if platform.system()=='Darwin' else '-Wl,--gc-sections'
    for name in ('msc','adapter'):
        run(cc,*flags,kit/(name+'-test.c'),gc,'-o',tmp/name)
    run(tmp/'msc')
    run(tmp/'adapter','write','report')
    count=1
    for op in ('read','write','sync'):
        for mode in ('guards','normal','reentrant','scsi','phase','signature','tag','residue','userdata','cbw','pointer','device','null','timeout','disconnect','reject','tagwrap','notready','bad_failed_residue'):
            run(tmp/'adapter',op,mode);count+=1
    for op,mode in [('write','established_flush'),('write','dirty_detach'),('write','syncfail'),('sync','syncfail'),('write','write_timeout'),('write','dirty_syncfail')]:
        run(tmp/'adapter',op,mode);count+=1
    for mode in ('fixed','field','unit','deferred','validbit','descriptor','desc_deferred','short','short_header','advertised','format','status','phase','signature','tag','residue','userdata','cbw','pointer','device','null','timeout','detach','reject','skip_ready','skip_tag'):
        run(tmp/'adapter','write','sense_'+mode);count+=1
    for prefix in ('cache_','cache10_'):
        for mode in ('off','on','ps','block','padded','status','phase','short','empty','length','total_small','descriptor','desc_odd','media','page','spf','page_short','page_long','signature','tag','residue','userdata','cbw','pointer','device','null','timeout','disconnect','reject'):
            run(tmp/'adapter','write',prefix+mode);count+=1
    for mode in ('cache_skip_ready','cache_skip_tag','cache10_longlba'):
        run(tmp/'adapter','write',mode);count+=1
    print(f'PASS {count} adapter scenarios with ASan/UBSan')
    fat=root/'src/lib/FatFs_SPI/ff15/source'
    run(cc,*flags,'-DPICOMEM_EXPERIMENTAL_LFN=1',kit/'fatfs-test.c',fat/'ff.c',fat/'ffunicode.c','-o',tmp/'fatfs')
    for mode in ('normal','unsupported','writefail','syncfail','mountfail'):
        img=tmp/(mode+'.img')
        with img.open('wb') as f:f.truncate(64*1024*1024)
        run('mformat','-i',img,'-F','::')
        run(tmp/'fatfs',img,mode)
        if mode=='normal':
            run('mcopy','-i',img,'::Checked USB cache payload.txt',tmp/'readback')
            assert (tmp/'readback').read_bytes()==bytes((i*13+7)&255 for i in range(1537))
    print('PASS five real FatFs/mount scenarios and independent long-name/content readback')
