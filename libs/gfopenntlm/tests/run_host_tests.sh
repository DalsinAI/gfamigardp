#!/bin/sh
# Build and run gfopenntlm's PC tests with the host C compiler (HOST_CC, default cc).
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${OUT:-"${TMPDIR:-/tmp}/gfopenntlm-host"}
mkdir -p "$OUT"
${HOST_CC:-cc} -std=gnu99 -O2 -Wall -Wextra -Werror -I"$HERE/include" -I"$HERE/src" \
  -o "$OUT/test_vectors" "$HERE/tests/test_vectors.c" \
  "$HERE/src/gn_md4.c" "$HERE/src/gn_md5.c" "$HERE/src/gn_sha256.c" "$HERE/src/gn_hmac.c" \
  "$HERE/src/gn_rc4.c" "$HERE/src/gn_util.c" "$HERE/src/gn_der.c" "$HERE/src/gn_ntlm.c" \
  "$HERE/src/gn_cssp.c" "$HERE/src/gn_api.c" "$HERE/src/gn_sys_host.c"
python3 "$HERE/tools/gen_headers.py" --check
"$OUT/test_vectors"
