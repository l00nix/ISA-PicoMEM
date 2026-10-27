#!/bin/sh
set -eu
kit=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/pm-lfn.XXXXXX")
trap 'rm -f "$tmp/test" "$tmp/adapter.o" "$tmp/dispatch"; rmdir "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$kit/tests" "$kit/lfn.c" "$kit/fatfs-adapter.c" "$kit/tests/test-lfn.c" -o "$tmp/test"
"$tmp/test"
# Optional compile against the audited firmware's real FatFs headers.
if [ -n "${PICOMEM_SOURCE:-}" ]; then
 ${CC:-cc} -std=c99 -Wall -Wextra -Werror \
  -I"$PICOMEM_SOURCE/src/lib/FatFs_SPI/include" \
  -I"$PICOMEM_SOURCE/src/lib/FatFs_SPI/ff15/source" \
  -c "$kit/fatfs-adapter.c" -o "$tmp/adapter.o"
fi

${CC:-cc} -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$kit/tests" "$kit/lfn.c" "$kit/fatfs-adapter.c" "$kit/dispatch.c" "$kit/tests/test-dispatch.c" -o "$tmp/dispatch"
"$tmp/dispatch"
