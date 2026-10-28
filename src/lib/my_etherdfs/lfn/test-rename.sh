#!/bin/sh
set -eu
kit=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/pm-rename.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined "$kit/lfn.c" "$kit/rename.c" "$kit/tests/test-rename.c" -o "$tmp/protocol"
"$tmp/protocol"
${CC:-cc} -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$kit/tests" "$kit/lfn.c" "$kit/rename-fatfs.c" "$kit/tests/test-rename-adapter.c" -o "$tmp/adapter"
"$tmp/adapter"
