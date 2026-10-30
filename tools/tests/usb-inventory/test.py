#!/usr/bin/env python3
import os,pathlib,subprocess,tempfile
kit=pathlib.Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='pm-usb-inventory-') as tmp:
 exe=pathlib.Path(tmp)/'test'
 subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-DPICOMEM_EXPERIMENTAL_USB_SYNC=1','-I'+str(kit),str(kit/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1'))
