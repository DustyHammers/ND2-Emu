#!/usr/bin/env bash
# Fetches the pinned dependencies from scripts/deps.env into work/deps/ (repository root).
# Usage: scripts/fetch_deps.sh [juce] [rmlui] [lua] [freetype]   (no arguments: all)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=deps.env
source "$root/scripts/deps.env"
mkdir -p "$root/work/deps"

fetch() {   # name url commit
    local dir="$root/work/deps/$1" url="$2" commit="$3"
    if [ -d "$dir/.git" ] && [ "$(git -C "$dir" rev-parse HEAD)" = "$commit" ]; then
        echo "$1: $commit (already present)"; return
    fi
    rm -rf "$dir"
    git init -q "$dir"
    git -C "$dir" fetch -q --depth 1 "$url" "$commit"
    git -C "$dir" -c advice.detachedHead=false checkout -q FETCH_HEAD
    [ "$(git -C "$dir" rev-parse HEAD)" = "$commit" ] || { echo "$1: wrong commit" >&2; exit 1; }
    echo "$1: $commit"
}

want="${*:-juce rmlui lua freetype}"
for d in $want; do
    case "$d" in
        juce)    fetch JUCE "$JUCE_URL" "$JUCE_COMMIT" ;;
        rmlui)   fetch RmlUi "$RMLUI_URL" "$RMLUI_COMMIT" ;;
        lua)     fetch Lua "$LUA_URL" "$LUA_COMMIT" ;;
        freetype) fetch FreeType "$FREETYPE_URL" "$FREETYPE_COMMIT" ;;
        *) echo "unknown dependency $d" >&2; exit 2 ;;
    esac
done
