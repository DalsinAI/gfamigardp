#!/bin/sh
# The Open SDK parts GFAmigaRDP builds with, each at a pinned commit, into
# DEPS/<name>: gfopensdk (OpenNDK's headers, the soft-float specs),
# gfopentls (its library headers), gfopenlayout (libolui's source),
# amicradle-guest (GFOpenGadTools' theme reader and themes) and gfopenntlm
# (NTLMv2 and CredSSP for NLA, built from its source).
#   tools/fetch_deps.sh DEPS
# A checkout named in GFOPENSDK=, GFOPENTLS=, OLUI=, OGT_GUEST= (the
# amicradle-guest top) or GFOPENNTLM= is used as it is instead. A clean build machine
# (Cradle's stove_build) fetches them; when a fetch fails, a checkout
# next to this one (../<name> or ../DalsinAI__<name>) is used, and named.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
DEPS=${1:?usage: fetch_deps.sh DEPS}
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$DEPS"

# name pin
PINS='gfopensdk a7384f334c3eb64b30dd27cc5a6f480d72471f74
gfopentls aa6f6a5a34a0558351ece25f29066f51a4e85499
gfopenlayout da4a066ee2fe4a7ca93d4175c44f38a5b9ac80e6
amicradle-guest 27832e77a4590810b40e57b3c182abb4e7fa366c
gfopenntlm 4bb461a0a0026271f79e5d25344da7c089706cbd'

fetch() {
    name=$1 pin=$2 dst="$DEPS/$1"
    if [ -f "$dst/.pin" ] && [ "$(cat "$dst/.pin")" = "$pin" ]; then
        return 0
    fi
    rm -rf "$dst.tmp"
    mkdir -p "$dst.tmp"
    if git -C "$dst.tmp" init -q &&
       git -C "$dst.tmp" fetch -q --depth 1 "https://github.com/DalsinAI/$name.git" "$pin" &&
       git -C "$dst.tmp" -c advice.detachedHead=false checkout -q FETCH_HEAD; then
        rm -rf "$dst" "$dst.tmp/.git"
        mv "$dst.tmp" "$dst"
        echo "$pin" > "$dst/.pin"
        echo "dep: $name $pin"
        return 0
    fi
    rm -rf "$dst.tmp"
    for d in "$HERE/../$name" "$HERE/../DalsinAI__$name"; do
        if [ -d "$d" ]; then
            rm -rf "$dst"
            ln -s "$(CDPATH= cd -- "$d" && pwd)" "$dst"
            echo "dep: $name from $d ($(git -C "$d" rev-parse HEAD 2>/dev/null || echo 'no git'); fetch of $pin failed)"
            return 0
        fi
    done
    echo "fetch_deps.sh: cannot fetch DalsinAI/$name at $pin, and no checkout beside this one" >&2
    return 1
}

given() {
    if [ -n "$2" ]; then
        [ -d "$2" ] || { echo "fetch_deps.sh: $2 ($1) is not a folder" >&2; exit 2; }
        rm -rf "$DEPS/$1"
        ln -s "$(CDPATH= cd -- "$2" && pwd)" "$DEPS/$1"
        echo "dep: $1 from $2"
        return 0
    fi
    return 1
}

echo "$PINS" | while read -r name pin; do
    case $name in
        gfopensdk) v=${GFOPENSDK:-} ;;
        gfopentls) v=${GFOPENTLS:-} ;;
        gfopenlayout) v=${OLUI:-} ;;
        amicradle-guest) v=${OGT_GUEST:-} ;;
        gfopenntlm) v=${GFOPENNTLM:-} ;;
    esac
    given "$name" "$v" || fetch "$name" "$pin"
done
