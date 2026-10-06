#!/usr/bin/env bash
# release.sh — one library's release process, in one place and in that order.
#
# WHY THIS EXISTS: a declared version once sat at 1.0.1 while v1.3.1 was already tagged,
# because "how a release is made" existed only in someone's head. This script is the
# process. Copy it to tools/release.sh in a repo that tags releases, and add `.release/`
# to that repo's .gitignore — the notes are a draft until they are published.
#
#   tools/release.sh status          what version this repo declares, and what is unreleased
#   tools/release.sh prepare [--apply] [--set X.Y.Z]
#                                    propose the next version from the commits since the
#                                    last tag; --apply writes it to CMakeLists.txt
#   tools/release.sh notes [--open]  draft the release notes, compatibility table first
#   tools/release.sh publish --yes   tag and publish on GitHub (refuses unless everything
#                                    is in order, including a filled-in compatibility table)
#
# THE VERSION HAS ONE HOME: `project(<name> VERSION x.y.z)` in CMakeLists.txt, and the name
# is read from that line, loosely, so there is nothing to configure. The installed header
# <name>/version.hpp is generated from the same line at configure time; the gate's `version`
# stage is what stops the two drifting.
#
# WHAT THIS SCRIPT WILL NOT DO: write your compatibility table. The mechanical parts are
# inferable from git; how an existing user's code is affected is a judgement, and a
# generated answer would be a confidently wrong one. It scaffolds the table and blocks
# publish until you fill it in.
#
# Everything here is local. No GitHub Actions, no CI service: the gate is tools/ci.sh.
set -uo pipefail
# Resolve our own path BEFORE changing directory: the help block is read back out of this
# file, and a relative $0 stops resolving the moment we cd. Needs this script at tools/.
SELF=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
cd "$(dirname "$0")/.." || exit 1

NOTES_DIR=".release"          # gitignored: notes are drafts until published on GitHub

die()  { printf 'release: %s\n' "$*" >&2; exit 1; }
info() { printf '  %s\n' "$*"; }
head_() { printf '\n== %s ==\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }

declared_version() {
    sed -n 's/^project *( *[A-Za-z0-9_.-]* *VERSION *\([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1
}
latest_tag() { git describe --tags --abbrev=0 --match 'v[0-9]*' 2>/dev/null || true; }
first_commit() { git rev-list --max-parents=0 HEAD | tail -1; }
range_since_tag() { local t; t=$(latest_tag); printf '%s..HEAD' "${t:-$(first_commit)}"; }

# The project name, read from CMakeLists.txt the same loose way the gate's `version` stage
# reads the number: nothing to configure and nothing to keep in sync. Placed after the
# helpers because it needs die().
PROJECT=$(sed -n 's/^project *( *\([A-Za-z0-9_.-]*\) *VERSION .*/\1/p' CMakeLists.txt | head -1)
[ -n "$PROJECT" ] || die "no project name in CMakeLists.txt (project(<name> VERSION <x.y.z>))"

# The commits that break compatibility, if any: either the `!` marker or a BREAKING line.
breaking_commits() {
    git log --format='%h%x09%s' "$(range_since_tag)" \
        | grep -E '	[a-z]+(\([^)]*\))?!:' || true
}

# Harri's convention for his own libraries: while a library has no consumers, a breaking
# change is a MINOR bump (see how 1.4.0 disclosed the TransitionBuilder change). Switch this
# to `major` when this library actually has users to warn.
recommend_bump() {
    if [ -n "$(breaking_commits)" ]; then echo minor
    elif git log --format='%s' "$(range_since_tag)" | grep -qE '^feat(\([^)]*\))?:'; then echo minor
    else echo patch; fi
}

bump_version() { # bump_version <version> <major|minor|patch>
    local v="$1" kind="$2"
    local maj min pat
    IFS=. read -r maj min pat <<< "$v"
    case "$kind" in
        major) printf '%d.0.0' "$((maj + 1))" ;;
        minor) printf '%d.%d.0' "$maj" "$((min + 1))" ;;
        patch) printf '%d.%d.%d' "$maj" "$min" "$((pat + 1))" ;;
    esac
}

cmd_status() {
    git rev-parse --git-dir >/dev/null 2>&1 || die "not a git repository"
    local v tag n
    v=$(declared_version); [ -n "$v" ] || die "no version in CMakeLists.txt"
    tag=$(latest_tag)
    head_ "$PROJECT release status"
    info "declared version : $v   (CMakeLists.txt project(), the only home)"
    info "newest tag       : ${tag:-none}"
    if [ -n "$tag" ] && [ "$(git rev-list -n1 "$tag")" = "$(git rev-parse HEAD)" ]; then
        info "HEAD             : tagged $tag"
    else
        info "HEAD             : untagged"
    fi
    if [ -n "$tag" ]; then
        n=$(git rev-list --count "$tag..HEAD")
        info "unreleased       : $n commit(s) since $tag"
    fi
    if [ -n "$tag" ] && [ "$v" != "${tag#v}" ]; then
        info "pending release  : $v is declared but untagged — THIS is the release to make"
        info "                   next: tools/ci.sh, tools/release.sh notes, then publish"
    else
        info "recommended bump : $(recommend_bump)  ->  would make $(bump_version "$v" "$(recommend_bump)")"
    fi
    local b; b=$(breaking_commits)
    if [ -n "$b" ]; then
        info "note             : breaking commits present; by convention that is a MINOR bump"
        head_ "commits marked breaking"
        printf '%s\n' "$b" | sed 's/^/  /'
    fi
    if [ -n "$(git status --porcelain)" ]; then
        info "working tree     : DIRTY — commit before releasing"
    else
        info "working tree     : clean"
    fi
    info "gate             : tools/ci.sh  (all stages; the pre-push hook runs them too)"
    info "version header   : generated at configure time; the gate's version stage compares it"
}

cmd_prepare() {
    local apply=0 v kind next set=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --apply)   apply=1 ;;
            --set)     shift; set="${1:-}" ;;
            --set=*)   set="${1#--set=}" ;;
            *)         die "unknown prepare option: $1" ;;
        esac
        shift
    done
    v=$(declared_version)
    if [ -n "$set" ]; then
        # A deliberate bump: the convention below (breaking -> minor) is a default, not a rule.
        # A MAJOR is the honest version when a public method is removed or renamed.
        printf '%s' "$set" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$' || die "--set wants X.Y.Z, got '$set'"
        kind="set"; next="$set"
    else
        kind=$(recommend_bump); next=$(bump_version "$v" "$kind")
    fi
    head_ "prepare"
    local t; t=$(latest_tag); info "commits since ${t:-the first commit}:"
    git log --format='  %h %s' "$(range_since_tag)" | head -25
    info ""
    info "recommended: $kind  ->  $next   (declared today: $v)"
    if [ -z "$set" ] && [ -n "$(latest_tag)" ] && [ "$v" != "$(latest_tag | sed 's/^v//')" ]; then
        info "NOTE: $v is declared but untagged, so a release at $v looks pending. Bumping now"
        info "      skips past it; --apply is only right if you mean to abandon $v."
    fi
    if [ "$apply" != "1" ]; then
        info "re-run with --apply to write it to CMakeLists.txt"
        return 0
    fi
    grep -q "^project(${PROJECT} VERSION ${v} " CMakeLists.txt \
        || die "CMakeLists.txt does not declare $v: refusing to guess"
    sed -i "s/^project(${PROJECT} VERSION ${v} /project(${PROJECT} VERSION ${next} /" CMakeLists.txt
    grep -q "^project(${PROJECT} VERSION ${next} " CMakeLists.txt \
        || die "the bump did not land: check CMakeLists.txt"
    info ""
    info "wrote $next to CMakeLists.txt — the generated version header follows on rebuild"
    info "next: run tools/ci.sh, commit, push, then tools/release.sh notes"
}

cmd_notes() {
    local open=0 v tag
    for arg in "$@"; do [ "$arg" = "--open" ] && open=1; done
    v=$(declared_version); tag=$(latest_tag)
    mkdir -p "$NOTES_DIR"
    local out="$NOTES_DIR/notes-v$v.md"
    {
        printf '# %s v%s\n\n' "$PROJECT" "$v"
        printf '_Released %s from `%s`._\n\n' "$(date +%F)" "$(git rev-parse --short HEAD)"
        printf '## Compatibility\n\n'
        printf '| for code that… | before | now |\n'
        printf '| :--- | :--- | :--- |\n'
        printf '| TODO: compiles against the public headers (signatures, headers, macros) | | |\n'
        printf '| TODO: behaves the same at run time for the same input | | |\n'
        printf '| TODO: builds and packages (CMake targets, find_package version, options) | | |\n'
        printf '| TODO: anything removed, renamed, or newly required — and say explicitly when something still exists but CHANGED | | |\n'
        printf '| TODO: ABI/soname, if anything crosses the library boundary | | |\n\n'
        if [ -n "$(breaking_commits)" ]; then
            printf '## Breaking changes\n\n'
            breaking_commits | sed 's/^[0-9a-f]*	/- /'
            printf '\n'
        fi
        printf '## Commits since %s\n\n' "${tag:-the first commit}"
        git log --format='- %s (%h)' "$(range_since_tag)"
        printf '\n## Verification\n\n'
        printf 'The full local gate is green at this commit: `tools/ci.sh` (no arguments runs the\n'
        printf 'repo default tier; the pre-push hook runs the rest).\n\n'
        printf 'No stage list and no counts here on purpose: they drift. Read the gate output.\n'
    } > "$out"
    head_ "notes"
    info "drafted $out"
    info "THE COMPATIBILITY TABLE IS A PLACEHOLDER — fill it in yourself; release.sh publish refuses while it says TODO."
    [ "$open" = "1" ] && ${EDITOR:-vi} "$out"
    return 0
}

cmd_publish() {
    local yes=0 v tag notes
    for arg in "$@"; do [ "$arg" = "--yes" ] && yes=1; done
    v=$(declared_version); notes="$NOTES_DIR/notes-v$v.md"
    head_ "publish v$v"
    [ -n "$(git status --porcelain)" ] || true
    [ -z "$(git status --porcelain)" ] || die "working tree is dirty: commit first"
    [ "$(git rev-parse --abbrev-ref HEAD)" = "main" ] || die "not on main"
    [ -z "$(git describe --tags --exact-match 2>/dev/null)" ] || die "HEAD is already tagged"
    git rev-parse "v$v" >/dev/null 2>&1 && die "tag v$v already exists"
    git rev-parse origin/main >/dev/null 2>&1 || die "no origin/main to compare against"
    [ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || die "HEAD differs from origin/main: push first"
    have gh || die "gh not installed"
    [ -f "$notes" ] || { info "no draft yet: generating"; cmd_notes; }
    grep -q 'TODO' "$notes" && die "the compatibility table in $notes is still a placeholder"
    [ "$yes" = "1" ] || die "this publishes publicly: re-run with --yes"
    # The draft may have been written days ago, so stamp it at publish time: BOTH the date and
    # the commit being released. Re-dating while keeping the old commit told a small public lie
    # on the v2.0.0 release ("from 1d4b207" — the commit before the release commit).
    sed -i "s/^_Released .* from \`[0-9a-f]*\`\._$/_Released $(date +%F) from \`$(git rev-parse --short HEAD)\`._/" "$notes"
    info "tagging and publishing…"
    git tag -a "v$v" -m "$PROJECT v$v"
    git push origin "v$v" || die "could not push the tag"
    gh release create "v$v" --title "$PROJECT v$v" --notes-file "$notes" \
        || die "tag pushed, but creating the GitHub release failed: run gh release create manually"
    info "published v$v"
}

usage() {
    sed -n '2,30p' "$SELF" | sed 's/^# \{0,1\}//'
}

cmd="${1:-status}"; shift || true
case "$cmd" in
    status)  cmd_status "$@" ;;
    prepare) cmd_prepare "$@" ;;
    notes)   cmd_notes "$@" ;;
    publish) cmd_publish "$@" ;;
    help|-h|--help) usage ;;
    *) die "unknown command '$cmd' (try: status, prepare, notes, publish)" ;;
esac
