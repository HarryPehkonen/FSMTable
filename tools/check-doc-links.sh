#!/bin/sh
# Every relative link in a tracked markdown file must point at something that exists.
#
# This is here because a document that names a file the repository does not have is worse than no
# document at all: it sends a reader looking for something that was never there, and nothing else in
# the gate notices. (A false reference to the Kit's DESIGN-NOTES.md reached a shipped README before
# this script existed.)
#
# Run from the repository root:   sh tools/check-doc-links.sh
# Exit codes: 0 every link resolves, 1 at least one does not.
#
# Links are matched as `](target)`. Absolute URLs, `mailto:` and bare `#anchors` are skipped; a
# `#anchor` or `?query` on a relative path is stripped before the check. Paths with spaces are not
# supported in a link, which is a rule this repository happens to keep.
set -u

if [ -d .git ]; then
    # Tracked AND untracked-but-not-ignored: a document written minutes ago is exactly the one whose
    # links nobody has looked at yet, and a checker that only sees committed files reports success on
    # the file you just wrote.
    files=$(git ls-files --cached --others --exclude-standard '*.md')
else
    # No git here — `git archive HEAD` piped into tar, which is how the pristine stage receives this
    # tree. Take every markdown file, minus the ones a build produces: CMake downloads a dependency
    # tree full of its own READMEs, and those are not this repository's documents.
    files=$(find . -name '*.md' -not -path './build*' -not -path './.ci-logs/*' -not -path '*/_deps/*')
fi

if [ -z "$files" ]; then
    echo "check-doc-links: no markdown found — run me from the repository root" >&2
    exit 1
fi

status=0
for file in $files; do
    directory=$(dirname "$file")
    for target in $(grep -o '](\([^)]*\))' "$file" | sed 's/^](//; s/)$//; s/#.*$//; s/?.*$//'); do
        [ -z "$target" ] && continue
        case "$target" in
            http://* | https://* | mailto:*) continue ;;
        esac
        if [ ! -e "$directory/$target" ]; then
            echo "$file: link to '$target' points at nothing"
            status=1
        fi
    done
done

if [ "$status" -eq 0 ]; then
    echo "check-doc-links: every relative link resolves"
fi
exit "$status"
