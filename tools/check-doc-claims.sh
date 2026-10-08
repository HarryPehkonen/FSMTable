#!/bin/sh
# Re-derive the claims the examples' READMEs make, from the real tools.
#
# A document that quotes a count, an output or a recipe is making a claim, and no build notices when
# a claim goes stale: two wrong statements reached this repository's shipped documentation in two
# days (a link to a file that does not exist, and an analysis the format cannot have). The link half
# is tools/check-doc-links.sh; this is the other half.
#
# Usage, from the repository root, after a build:
#
#   sh tools/check-doc-claims.sh <fsmtable-gen> <fsmtable-inspect> <fsmtable-transform>
#
# What it re-derives:
#
#   1. examples/{calculator,protocol}/README.md — the worked recipes. The first fenced block under
#      the marker named in this script is appended to the example's real .fsm, the generator must
#      accept it, and the numbers must be the ones the document states. The checked form of the
#      statement is exactly   **N states and M rows**   — bold, two integers, on one line. That is
#      the whole convention, and a document that stops using it fails here rather than passing
#      quietly.
#   2. examples/protocol/README.md — every `From_leaving_to_To` name written between backticks in
#      that document must exist in the header the generator wrote for the recipe above.
#   3. examples/inspector/README.md — its output block. Lines beginning `$ ` are commands;
#      `./build/fsmtable-inspect` and `./build/fsmtable-transform` are the two it understands, and
#      an unknown command is a failure rather than a skip. Every other line is what those commands
#      must print, compared exactly with diff. A blank line is a separator when a command follows
#      it, and content anywhere else — an output can be a file, and a file has blank lines.
#   4. examples/inspector/README.md — the tree check: every .fsm under corpus/ and examples/ must
#      inspect clean, with a floor on how many files were checked, because a glob that matches
#      nothing proves nothing.
#   5. examples/inspector/README.md — the canonical-form recipe: writing onto the input really does
#      empty it and then fail to parse, and the temporary-file form really does work. A documented
#      footgun is still a claim about behaviour.
#   6. TRANSFORM.md — its output block, the same way (3) reads the inspector's: the two renames it
#      documents must print what it says, and so must the refusal it ends with. The commands there
#      include one that exits 1 on purpose, so the block's commands are read for their OUTPUT and
#      the exit codes stay the ctest cases' business.
#
# Exit codes: 0 every claim re-derived, 1 one of them did not hold, 2 the command line is wrong.
set -u

if [ "$#" -ne 3 ]; then
    echo "usage: sh tools/check-doc-claims.sh <fsmtable-gen> <fsmtable-inspect> <fsmtable-transform>" >&2
    exit 2
fi

gen=$1
inspect=$2
transform=$3
root=$(pwd)

# The canonical-form recipe does its work in a temporary directory, so a relative tool path would
# vanish mid-check — and the recipe would then "pass" against the shell's own error message.
case "$gen" in
    /*) : ;;
    *) gen="$root/$gen" ;;
esac
case "$inspect" in
    /*) : ;;
    *) inspect="$root/$inspect" ;;
esac
case "$transform" in
    /*) : ;;
    *) transform="$root/$transform" ;;
esac

if [ ! -x "$gen" ] || [ ! -x "$inspect" ] || [ ! -x "$transform" ]; then
    echo "check-doc-claims: needs the built tools, got '$gen', '$inspect' and '$transform'" >&2
    exit 2
fi

work=$(mktemp -d) || exit 2
trap 'rm -rf "$work"' EXIT INT TERM

failed=0
recipe_hpp= # set by check_recipe when a recipe generates; named in check_composed_names
fail() {
    failed=1
    printf 'CLAIM FAILED: %s\n' "$*"
}
note() {
    printf '  ok  %s\n' "$*"
}

# The number of the first line of file $2 that contains $1.
line_of() {
    grep -n -F -- "$1" "$2" | head -1 | cut -d: -f1
}

# The contents of the first fenced block that opens after line $1 of file $2.
block_after() {
    awk -v start="$1" '
        NR > start && /^```/ { if (inblock) exit; inblock = 1; next }
        inblock { print }
    ' "$2"
}

# Whatever comes back from the tools, as plain integers: "8 state(s), 27 row(s)" -> "8 27".
digits_of() {
    sed 's/[^0-9][^0-9]*/ /g; s/^ *//; s/ *$//'
}

# The statement the convention requires, as written: **N states and M rows**
claimed_of() {
    grep -o '\*\*[0-9][0-9]* states and [0-9][0-9]* rows\*\*' "$1" | head -1
}

# $1 document, $2 marker, $3 the real .fsm, $4 what to call it. Sets recipe_hpp on success.
check_recipe() {
    doc=$1
    marker=$2
    source=$3
    label=$4

    start=$(line_of "$marker" "$doc")
    if [ -z "$start" ]; then
        fail "$label: '$marker' is no longer in $doc, so nothing was checked"
        return
    fi

    fsm=$work/recipe.fsm
    hpp=$work/recipe.hpp
    if ! cp "$source" "$fsm"; then
        fail "$label: cannot read $source"
        return
    fi
    # Appended exactly the way the document tells a reader to append it: no newline is inserted
    # first, so an example .fsm without a final newline fails here rather than quietly gluing the
    # block onto its last row.
    block_after "$start" "$doc" >> "$fsm"

    stated=$(claimed_of "$doc")
    if [ -z "$stated" ]; then
        fail "$label: $doc no longer states its counts as '**N states and M rows**'"
        return
    fi

    out=$("$gen" "$fsm" -o "$hpp" 2>&1)
    code=$?
    if [ "$code" -ne 0 ]; then
        fail "$label: the generator refused the recipe (exit $code)"
        printf '%s\n' "$out" | sed 's/^/      /'
        return
    fi

    made=$(printf '%s\n' "$out" | grep -o '[0-9][0-9]* state(s), [0-9][0-9]* row(s)' | head -1)
    if [ "$(printf '%s' "$stated" | digits_of)" != "$(printf '%s' "$made" | digits_of)" ]; then
        fail "$label: the recipe generates '$made' and $doc claims '$stated'"
        return
    fi

    recipe_hpp=$hpp
    note "$label: $made, as $doc claims"
}

# $1 document, $2 the header the recipe generated.
check_composed_names() {
    doc=$1
    hpp=$2
    if [ -z "$hpp" ] || [ ! -f "$hpp" ]; then
        fail "no generated header to check $doc's composed names against (did the recipe run?)"
        return
    fi
    names=$(grep -o '`[A-Za-z_][A-Za-z_]*_leaving_to_[A-Za-z_][A-Za-z_]*`' "$doc" | tr -d '`' | sort -u)
    if [ -z "$names" ]; then
        fail "no composed function names are named in $doc any more, so nothing was checked"
        return
    fi
    for name in $names; do
        if ! grep -q "$name" "$hpp"; then
            fail "$doc names $name, and the generated header has no such function"
        fi
    done
    note "composed names: $(printf '%s ' $names)"
}

# $1 document, $2 the marker above its output block.
check_output_block() {
    doc=$1
    marker=$2

    start=$(line_of "$marker" "$doc")
    if [ -z "$start" ]; then
        fail "'$marker' is no longer in $doc, so its output block was not checked"
        return
    fi

    block_after "$start" "$doc" > "$work/block.txt"
    expected=$work/expected.txt
    actual=$work/actual.txt
    commands=$work/commands.txt
    : > "$actual"

    # The block, split into what the commands must print and the commands themselves. A blank
    # line is a SEPARATOR exactly when a command follows it (or it closes the block); anywhere
    # else it is content, because an output can be a file whose blank lines are the point
    # (TRANSFORM.md's output is a machine, and a machine has blank lines in it). The rule the
    # block always had — "a blank line separates the commands; it is not part of what they
    # print" — is the `$ ` case below, unchanged.
    awk -v start="$start" -v expected="$expected" -v commands="$commands" '
        NR > start && /^```/ { if (inblock) exit; inblock = 1; next }
        inblock { lines[++n] = $0 }
        END {
            for (i = 1; i <= n; ++i) {
                if (lines[i] == "" && (i == n || substr(lines[i + 1], 1, 2) == "$ ")) continue
                if (substr(lines[i], 1, 2) == "$ ") { print substr(lines[i], 3) > commands; continue }
                print lines[i] > expected
            }
        }
    ' "$doc"

    # A block with no command would compare two empty files and pass: the same vacuity the tree
    # check below guards against.
    if [ ! -s "$commands" ]; then
        fail "the output block in $doc runs no command, so nothing was checked"
        return
    fi

    while IFS= read -r command; do
        set -- $command
        case "$1" in
            ./build/fsmtable-inspect)
                shift
                (cd "$root" && "$inspect" "$@") >> "$actual" 2>&1
                ;;
            ./build/fsmtable-transform)
                # One command in TRANSFORM.md's block refuses on purpose and exits 1: a block
                # pins what a command PRINTS, and the exit codes are the ctest cases' business.
                shift
                (cd "$root" && "$transform" "$@") >> "$actual" 2>&1
                ;;
            *)
                fail "the output block in $doc runs a command this script does not know:" \
                     "$command"
                ;;
        esac
    done < "$commands"

    if diff -u "$expected" "$actual" > "$work/diff.txt"; then
        note "the output block in $doc is what the tool prints"
    else
        fail "the output block in $doc is not what the tool prints:"
        sed 's/^/      /' "$work/diff.txt"
    fi
}

# $1 document, $2 the lowest number of .fsm files that check must look at.
check_tree() {
    doc=$1
    floor=$2
    checked=0

    bad=0
    for f in "$root"/corpus/*.fsm "$root"/examples/*/*.fsm; do
        [ -f "$f" ] || continue
        rel=${f#"$root"/}
        # The exclusions the document names, in the same words: the corpus keeps deliberately broken
        # files for the parser's own tests, and this example's dirty.fsm exists to be reported.
        case "$rel" in
            corpus/invalid_* | examples/inspector/dirty.fsm) continue ;;
        esac
        checked=$((checked + 1))
        if ! out=$(cd "$root" && "$inspect" "$rel" 2>&1); then
            bad=$((bad + 1))
            fail "$doc claims the tree it names inspects clean; $rel does not"
            printf '%s\n' "$out" | sed 's/^/      /'
        fi
    done

    if [ "$checked" -lt "$floor" ]; then
        fail "$doc's tree check looked at only $checked .fsm file(s); a glob that matches nothing proves nothing"
    elif [ "$bad" -eq 0 ]; then
        note "the tree check: $checked .fsm files, all clean"
    fi
}

# $1 document, $2 a marker that must still be there, $3 a .fsm to work on.
check_canonical_recipe() {
    doc=$1
    marker=$2
    source=$3

    if [ -z "$(line_of "$marker" "$doc")" ]; then
        fail "'$marker' is no longer in $doc, so its canonical-form recipe was not checked"
        return
    fi

    cp "$source" "$work/trap.fsm"
    cp "$source" "$work/safe.fsm"

    # The trap: the shell truncates before the process starts, so the tool reads an empty file.
    (cd "$work" && "$inspect" --canonical trap.fsm > trap.fsm) > "$work/trap.out" 2>&1
    if [ -s "$work/trap.fsm" ]; then
        fail "$doc says '> f.fsm' empties the file; it did not"
    elif ! grep -q 'trap.fsm' "$work/trap.out"; then
        fail "$doc says the emptied file is then reported as unparseable; the tool said nothing about it"
    else
        note "the canonical-form trap is still a trap"
    fi

    # The safe form the document recommends, which has to produce a machine that parses again.
    if ! (cd "$work" && "$inspect" --canonical safe.fsm > safe.new && mv safe.new safe.fsm); then
        fail "$doc's safe form ('> f.new && mv f.new f.fsm') did not run"
    elif ! grep -q '^version 1' "$work/safe.fsm"; then
        fail "$doc's safe form produced no canonical machine"
    elif ! (cd "$work" && "$inspect" safe.fsm > /dev/null 2>&1); then
        fail "$doc's safe form produced text the tool cannot parse back"
    else
        note "the canonical-form safe recipe works, and its output parses"
    fi
}

check_recipe "$root/examples/calculator/README.md" 'Worked example: add a' \
    "$root/examples/calculator/calculator.fsm" 'calculator recipe'
check_recipe "$root/examples/protocol/README.md" 'A worked adaptation' \
    "$root/examples/protocol/protocol.fsm" 'protocol recipe'
check_composed_names "$root/examples/protocol/README.md" "$recipe_hpp"
check_output_block "$root/examples/inspector/README.md" 'Real output, on this repository'
# The floor is a vacuity guard, not a pin on the number: today the tree check finds 5 files
check_tree "$root/examples/inspector/README.md" 4
check_canonical_recipe "$root/examples/inspector/README.md" 'Writing the canonical form back needs' \
    "$root/examples/protocol/protocol.fsm"
# The transform's own output block: two renames and one refusal, re-run and diffed line for line.
check_output_block "$root/TRANSFORM.md" 'Real output, on this repository'

if [ "$failed" -eq 0 ]; then
    printf 'check-doc-claims: every claim re-derived from the tools\n'
else
    printf 'check-doc-claims: at least one claim no longer holds\n'
fi
exit "$failed"
