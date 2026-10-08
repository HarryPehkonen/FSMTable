#!/usr/bin/env bash
#
# deno — the second back end: generate the TypeScript, then type-check and test it.
#
# Called from gate.toml as `[stage.deno] cmd = "scripts/deno.sh"`. The verdict vocabulary
# (ci_begin/ci_pass/ci_fail/ci_skip) is defined in scripts/gate-env.sh.
#
# What one stage proves: `fsmtable-gen --target deno` runs over the same inputs the C++ generator
# tests compile (CMakeLists.txt's FSMTABLE_GENERATOR_INPUTS), `deno check` accepts every module it
# wrote — so a generator that emits something that does not compile fails the gate, not a review —
# and the committed tests under tests/deno/ drive the emitted tables and assert what they do.
#
# Nothing is written into the source tree. The generated modules and a copy of the tests live
# together inside the gate's own footprint ($CI_LOG_DIR), which .gitignore already covers, so the
# tests import their modules as siblings (the same arrangement tests/generator_test.cpp has with
# the generated headers, one directory over).
set -uo pipefail
. "$(dirname "$0")/gate-env.sh"

run_stage() {
    ci_begin "deno (the generated TypeScript: deno check + deno test)"
    require_tool deno deno || return 0

    local gen="$CI_BUILD_DIR/fsmtable-gen"
    if [ ! -x "$gen" ]; then
        ci_fail deno "no $gen — run the build stage first"
    fi

    local dir="$CI_LOG_DIR/deno"
    rm -rf "$dir"
    mkdir -p "$dir"

    # One list of machines, two back ends: the same inputs CMakeLists.txt generates the C++
    # headers from. Extending the list here means extending it there too, deliberately.
    local input stem out
    for input in corpus/traffic_light.fsm corpus/guarded.fsm corpus/minimal.fsm \
        tests/fixtures/refined_pair.fsm tests/fixtures/named_kinds.fsm \
        tests/fixtures/entry_exit.fsm; do
        stem=$(basename "$input" .fsm)
        if ! out=$("$gen" --target deno "$input" -o "$dir/fsm_$stem.ts" 2>&1); then
            printf '%s\n' "$out" | sed 's/^/      /'
            ci_fail deno "the generator refused $input with --target deno"
        fi
    done
    cp tests/deno/*.ts "$dir/"

    if ! (cd "$dir" && deno check ./*.ts) > "$CI_LOG_DIR/deno-check.log" 2>&1; then
        ci_fail deno "deno check refused the generated modules (or the tests that import them)" \
            "$CI_LOG_DIR/deno-check.log"
    fi
    if ! (cd "$dir" && deno test) > "$CI_LOG_DIR/deno-test.log" 2>&1; then
        ci_fail deno "deno test failed" "$CI_LOG_DIR/deno-test.log"
    fi

    local checked
    checked=$(grep -c '^Check ' "$CI_LOG_DIR/deno-check.log")
    printf '      deno check: %s module(s) type-check\n' "$checked"
    grep -E '^ok \|' "$CI_LOG_DIR/deno-test.log" | tail -1 | sed 's/^/      deno test: /'
    ci_pass deno
}

run_stage "$@"
