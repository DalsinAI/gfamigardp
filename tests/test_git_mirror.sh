#!/bin/sh
# tests/test_git_mirror.sh: scripts/git-mirror.sh sends git fetches of
# https://github.com/DalsinAI/... to the mirror when the mirror answers,
# and changes nothing when it doesn't (GF-88). Host only: a bare repository
# in a temporary folder stands in for the mirror (file:// URL).
#   sh tests/test_git_mirror.sh
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
pass=0; fail=0
ok() { pass=$((pass+1)); echo "ok   $1"; }
no() { fail=$((fail+1)); echo "FAIL $1"; }
unset GIT_CONFIG_COUNT GIT_CONFIG_KEY_0 GIT_CONFIG_VALUE_0 2>/dev/null || true
# a mirror holding DalsinAI/amicradle-guest with one commit
mkdir -p "$T/src" "$T/mirror/DalsinAI"
git -C "$T/src" init -q
git -C "$T/src" -c user.name=t -c user.email=t@t commit -q --allow-empty -m one
want=$(git -C "$T/src" rev-parse HEAD)
git clone -q --bare "$T/src" "$T/mirror/DalsinAI/amicradle-guest"

# 1: the mirror answers: a fetch by pin of the GitHub URL comes from it
r=$(AMIGACHROME_GIT_MIRROR="file://$T/mirror/" sh -c '. "$1"; git init -q "$2/a" && git -C "$2/a" fetch -q --depth 1 https://github.com/DalsinAI/amicradle-guest "$3" && git -C "$2/a" rev-parse FETCH_HEAD' x "$HERE/scripts/git-mirror.sh" "$T" "$want" 2>"$T/err1" || true)
[ "$r" = "$want" ] && ok "mirror answers: github.com/DalsinAI fetched from it by pin" || no "mirror answers: got '$r' ($(cat "$T/err1"))"
grep -q "from file://$T/mirror/" "$T/err1" && ok "the build says where DalsinAI's repositories come from" || no "no line naming the mirror"
# 2: the .git form of the URL goes there too
r=$(AMIGACHROME_GIT_MIRROR="file://$T/mirror/" sh -c '. "$1"; git ls-remote https://github.com/DalsinAI/amicradle-guest HEAD' x "$HERE/scripts/git-mirror.sh" 2>/dev/null | cut -f1 || true)
[ "$r" = "$want" ] && ok "github.com/DalsinAI/<name> without .git goes to the mirror" || no "ls-remote gave '$r'"
# 3: no mirror: nothing is set
r=$(AMIGACHROME_GIT_MIRROR="file://$T/none/" sh -c '. "$1"; echo "${GIT_CONFIG_COUNT:-unset}"' x "$HERE/scripts/git-mirror.sh" 2>/dev/null)
[ "$r" = unset ] && ok "no mirror: git's settings are left alone" || no "no mirror, but GIT_CONFIG_COUNT=$r"
# 4: only DalsinAI's URLs are rewritten
r=$(AMIGACHROME_GIT_MIRROR="file://$T/mirror/" sh -c '. "$1"; git config --get-regexp "^url\\." ' x "$HERE/scripts/git-mirror.sh" 2>/dev/null || true)
[ "$r" = "url.file://$T/mirror/DalsinAI/.insteadof https://github.com/DalsinAI/" ] && ok "only https://github.com/DalsinAI/ is rewritten" || no "insteadOf is '$r'"
# 5: a GIT_CONFIG_COUNT already set by the caller (the kitchen's own rewrite) is kept
r=$(GIT_CONFIG_COUNT=2 AMIGACHROME_GIT_MIRROR="file://$T/mirror/" sh -c '. "$1"; echo "$GIT_CONFIG_COUNT"' x "$HERE/scripts/git-mirror.sh" 2>/dev/null)
[ "$r" = 2 ] && ok "the caller's own git settings are kept" || no "GIT_CONFIG_COUNT became $r"
# 6: build-amiga.sh sources it before anything is fetched
grep -n '\. "\$HERE/scripts/git-mirror.sh"' "$HERE/build-amiga.sh" >/dev/null && ok "build-amiga.sh sources scripts/git-mirror.sh" || no "build-amiga.sh does not source scripts/git-mirror.sh"
echo "test_git_mirror: $pass passed, $fail failed"
[ $fail = 0 ]
