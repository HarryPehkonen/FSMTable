#!/usr/bin/env bash
# guards: PLUNK-IN.md
# guards: .gitignore
#
# Kit-conformance probe for the kit fix of 2026-10-06: the gate-footprint recipe must use
# SLASH-FREE patterns.
#
# WHY A PROBE AND NOT A HASH: the same reason as every other probe here. A repo's .gitignore
# is its own file, so no byte comparison against the kit can say whether the COPY carries the
# fix — and the same is true of the gate, because every repo's copy of it is a fork.
#
# THE CONTRACT. The gate's `tree` stage audits a list of paths it is about to create, and it
# runs FIRST — on a fresh clone, before anything has created them. `git check-ignore` cannot
# match a DIRECTORY-ONLY pattern against a path that does not exist, so a recipe of `build/`
# reads NOT ignored on the first run and passes on the second. Measured on git 2.47.3, same
# path, directory absent: `build/` NOT IGNORED, `build` ignored. The failure message ("the
# next run would fail on its own log files") then names a problem the reader cannot find in
# their .gitignore, because the rule IS there — it just carries a trailing slash.
#
# THE LIST IS DERIVED FROM THE GATE BEING CHECKED: the loop that feeds its first
# `check-ignore`, with the `$CI_*` names in it resolved to their defaults. That is
# deliberate, and it is the one thing this probe got wrong in its first version, which
# demanded the kit's audit line byte-for-byte and would have reported three of the five C++
# repos as missing the fix when what they actually do is audit a DIFFERENT SET (FSMTable adds
# its fuzz build dir; jsonTools and Permuto audit fewer dirs because they run fewer stages).
# A fork MAY audit a different set; it may not audit a path its own recipe fails to ignore.
#
# A gate with no footprint audit at all gets a printed SKIP, not a FAIL: the contract has no
# subject there, so calling that copy "behind" would be the false verdict this probe exists
# to avoid (JSOM is the live example — its tree stage uses `git ls-files --others
# --exclude-standard` and never asks `check-ignore` about a path that does not exist yet).
#
# WHAT IT CHECKS
#   P1  the recipe ignores every path this gate audits, with none of them on disk
#   P2  NEGATIVE CONTROL — the pre-fix recipe (`build/` et al) does NOT: if the control
#       passed too, slash-free patterns would be asserting nothing
#   P3  the audit list is non-empty (a check with no subject is not a check)
#
# WHICH FILE IT READS. The kit's own runner passes the guarded file: PLUNK-IN.md (the recipe
# is extracted from the step-3 fenced block) or .gitignore (read straight through). A repo's
# `kitprobes` stage passes its GATE SCRIPT instead, and then the recipe checked is that
# repo's own .gitignore — which is the point: the fix propagates as a check on each copy.
#
# Exit 0 = PROBE VERIFIED (or SKIP, printed), 1 = PROBE FAILED.
set -uo pipefail

G=${1:?usage: gitignore-footprint.sh <PLUNK-IN.md|.gitignore|gate-script> [repo-root]}
[ -f "$G" ] || { printf 'PROBE FAILED (no such file: %s)\n' "$G"; exit 1; }
ROOT=${2:-$(cd "$(dirname "$G")" && pwd)}
KIT_DEFAULTS="build build-asan build-tsan build-release .ci-logs .ci.env"   # only used if no gate is reachable

fails=0
oks=0
check() {  # check <rc> <description>
    if [ "$1" -eq 0 ]; then oks=$((oks + 1)); printf '  ok   %s\n' "$2"
    else fails=$((fails + 1)); printf '  FAIL %s\n' "$2"; fi
}

printf '=== probe: the gate-footprint recipe is slash-free\n'

case "$(basename "$G")" in
    PLUNK-IN.md) SRC="$G";              FROM="PLUNK-IN.md step 3, the fenced block"; GATE=$ROOT/templates/cpp/ci.sh ;;
    .gitignore)  SRC="$G";              FROM="read straight through";                 GATE=$ROOT/templates/cpp/ci.sh ;;
    *)           SRC="$ROOT/.gitignore"; FROM="this repo's own copy";                 GATE=$G ;;
esac
printf '    recipe: %s\n    source: %s\n' "$SRC" "$FROM"
[ -f "$SRC" ] || { printf 'PROBE FAILED (no recipe to read at %s)\n' "$SRC"; exit 1; }

if [ "$(basename "$G")" = "PLUNK-IN.md" ]; then
    BODY=$(awk '/^## Step 3 /{s=1} s&&/<<.EOF.$/{g=1;next} g&&/^EOF$/{exit} g' "$SRC")
else
    BODY=$(sed '/^[[:space:]]*#/d;/^[[:space:]]*$/d' "$SRC")
fi
lines=$(printf '%s\n' "$BODY" | grep -c .)
check $([ "$lines" -gt 0 ] && echo 0 || echo 1) "the recipe has body lines to test ($lines)"

# --- the paths THIS gate audits: the loop that iterates $CI_* paths, resolved -------------
audit_paths() {  # audit_paths <gate-script> — one resolved path per line on success
    local gate=$1 line tok name val default
    # The audit loop is the one whose token list names CI_* paths. Not "the loop nearest
    # check-ignore": the dispatcher's own `for stage in ...` sits above it in some copies
    # (measured), and a proximity guess picks that instead.
    line=$(grep -m1 -E 'for [a-zA-Z_][a-zA-Z_0-9]* in [^;]*\$CI_' "$gate" 2>/dev/null)
    [ -n "$line" ] || line=$(grep -m1 'for path in ' "$gate" 2>/dev/null)
    [ -n "$line" ] || return 1
    for tok in $(printf '%s' "$line" | sed 's/.* in //; s/; *do.*//'); do
        case "$tok" in
            '"$CI_'*)
                name=$(printf '%s' "$tok" | sed 's/^"\$//; s/"$//')
                val=$(grep -m1 "^${name}=" "$gate" 2>/dev/null | cut -d= -f2-)
                case "$val" in
                    '${'*':-'*'}') default=${val#*:-}; default=${default%\}} ;;
                    *)             default=$val ;;
                esac
                [ -n "$default" ] && printf '%s\n' "$default"
                ;;
            '"'*'"')
                printf '%s\n' "$(printf '%s' "$tok" | tr -d '"')"
                ;;
        esac
    done
}

AUDIT=""
NO_AUDIT=0
if [ -f "$GATE" ]; then
    AUDIT=$(audit_paths "$GATE" | grep . | sort -u)
    [ -n "$AUDIT" ] || NO_AUDIT=1
else
    printf '    gate: no script at %s — using the kit defaults\n' "$GATE"
fi
if [ -n "$AUDIT" ]; then
    list=$AUDIT
    printf '    audited by this gate: %s\n' "$(printf '%s' "$list" | tr '\n' ' ')"
else
    list=$KIT_DEFAULTS
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

unignored() {  # how many of the audited paths does the .gitignore in THIS directory not ignore?
    local p n=0
    for p in $list; do git check-ignore -q "$p" 2>/dev/null || n=$((n + 1)); done
    printf '%s' "$n"
}
scratch() {  # scratch <recipe-body-file> — a repo with NONE of the audited paths on disk
    rm -rf "$TMP/repo"
    mkdir -p "$TMP/repo"
    cd "$TMP/repo" || exit 1
    git init -q . >/dev/null 2>&1
    cp "$1" .gitignore
}

printf '%s\n' "$BODY" > "$TMP/shipped"
scratch "$TMP/shipped"
n=$(unignored)
total=$(printf '%s' "$list" | wc -w)
for p in $list; do
    printf '       %-14s %s\n' "$p" "$(git check-ignore -v "$p" 2>/dev/null | sed 's/\t/ /g')"
done
if [ "$NO_AUDIT" = "1" ]; then
    printf '  SKIP this gate audits no footprint paths (no loop over $CI_* dirs), so there is\n'
    printf '       nothing for the recipe to satisfy — its recipe would leave %s of the kit default(s)\n' "$n"
    printf '       unaudited before creation, and `check-ignore` appears %s time(s) in the gate.\n' "$(grep -c 'check-ignore' "$GATE" 2>/dev/null | head -1)"
    printf '       Porting the slash-free recipe is harmless; porting the audit the kit'"'"'s tree stage\n'
    printf '       does is the bigger half.\n'
    exit 0
fi

check $([ "$n" -eq 0 ] && echo 0 || echo 1) "every path this gate audits is ignored before it exists ($n of $total not ignored)"

printf '.ci-logs/\nbuild/\nbuild-*/\n.ci.env\n' > "$TMP/pre-fix"
scratch "$TMP/pre-fix"
control=$(unignored)
check $([ "$control" -gt 0 ] && echo 0 || echo 1) "negative control: the pre-fix recipe fails on $control of $total (must be > 0, else this probe asserts nothing)"

check $([ "$total" -gt 0 ] && echo 0 || echo 1) "the audit list has $total path(s) to hold the recipe to"

printf 'PROBE %s (%d ok, %d failed)\n' \
    "$([ "$fails" -eq 0 ] && echo VERIFIED || echo FAILED)" "$oks" "$fails"
exit $([ "$fails" -eq 0 ] && echo 0 || echo 1)
