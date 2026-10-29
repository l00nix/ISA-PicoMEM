#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
fat="$here/../../../FatFs_SPI/ff15/source"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=gnu11 -g -fsanitize=address,undefined \
 -DPICOMEM_EXPERIMENTAL_FILE_ERRORS=1 -I"$here/usb-include" -I"$fat" \
 "$here/usb-test.c" -lm -o "$tmp/test"
export UBSAN_OPTIONS=halt_on_error=1
for op in read write; do
 for mode in guards normal scsi short reject timeout disconnect; do
  "$tmp/test" "$op" "$mode"
 done
done
