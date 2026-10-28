#!/bin/sh
set -eu
kit=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/pm-create.XXXXXX")
trap 'rm -f "$tmp/test"; rmdir "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
 "$kit/lfn.c" "$kit/create.c" "$kit/tests/test-create.c" -o "$tmp/test"
"$tmp/test"
${CC:-cc} -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
 -I"$kit/tests" "$kit/lfn.c" "$kit/create-fatfs.c" "$kit/tests/test-create-adapter.c" -o "$tmp/test"
"$tmp/test"
