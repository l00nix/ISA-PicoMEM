#!/bin/sh
set -eu
kit=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/pm-diag.XXXXXX")
trap 'rm -f "$tmp/test"; rmdir "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
  "$kit/test.c" -o "$tmp/test"
"$tmp/test"
