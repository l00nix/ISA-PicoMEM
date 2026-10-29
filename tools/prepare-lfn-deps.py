#!/usr/bin/env python3
"""Populate the two upstream-listed but untracked dependencies, at fixed commits.
Run from this checkout. Refuses unexpected revisions or local changes.
"""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parent.parent
items=(('tinyusb','https://github.com/hathach/tinyusb.git','331c263400dd5ccf77264cab1fa6bbd9f374b44e'),
       ('tusb_xinput','https://github.com/Ryzee119/tusb_xinput.git','ae4f3a9fd7c57a6c8c36fdba7f7b1abc9b6a1a56'))
for name,url,rev in items:
    p=root/'src/lib'/name
    def git(*args,**kwargs):return subprocess.run(['git','-C',str(p),*args],check=True,**kwargs)
    if not p.exists():
        subprocess.run(['git','clone',url,str(p)],check=True)
        git('checkout','--detach',rev)
    head=subprocess.check_output(['git','-C',str(p),'rev-parse','HEAD'],text=True).strip()
    if head!=rev:raise SystemExit(name+': expected '+rev+'; inspect before changing revisions')
    if subprocess.check_output(['git','-C',str(p),'diff','--cached'],text=True) or subprocess.check_output(['git','-C',str(p),'ls-files','--others','--exclude-standard'],text=True):
        raise SystemExit(name+': unexpected staged/untracked files; left unchanged')
    if name in ('tinyusb','tusb_xinput'):
        patch=root/'tools/patches'/('tinyusb-0.19-checked-msc.patch' if name=='tinyusb' else 'tusb-xinput-0.19.patch')
        expected=patch.read_text()
        diff=subprocess.check_output(['git','-C',str(p),'diff'],text=True)
        if diff==expected:continue
        if diff:raise SystemExit(name+': unexpected local edits; left unchanged')
        git('apply','--check',str(patch));git('apply',str(patch))
    elif subprocess.check_output(['git','-C',str(p),'status','--porcelain'],text=True):
        raise SystemExit(name+': unexpected local edits; left unchanged')
subprocess.run(['git','-C',str(root),'submodule','update','--init','src/lib/rp2040-psram'],check=True)
print('Pinned TinyUSB 0.19 (default-off checked MSC patch) + XInput adapter ready.')
