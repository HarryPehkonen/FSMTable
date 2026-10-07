#!/usr/bin/env bash
#
# fuzz — split out of the old tools/ci.sh (2026-10-06, card t_075c0a6f).
#
# Called from gate.toml as `[stage.fuzz] cmd = "scripts/fuzz.sh"`. The verdict
# vocabulary (ci_begin/ci_pass/ci_fail/ci_skip) is defined in scripts/gate-env.sh.
set -uo pipefail
. "$(dirname "$0")/gate-env.sh"

run_stage() {
    ci_begin "fuzz (libFuzzer + asan + ubsan, ${CI_FUZZ_SECONDS}s against corpus/)"
    if ! command -v clang++ > /dev/null 2>&1; then
        ci_skip fuzz "clang++ not installed (gcc ships no libFuzzer runtime)"
        return 0
    fi
    if [ ! -d corpus ]; then
        ci_fail fuzz "no corpus/ directory — the fuzzer runs against the checked-in corpus"
    fi
    local stale
    stale=$(fuzz_artifacts)
    if [ -n "$stale" ]; then
        printf '%s\n' "$stale" | head -5 | sed 's/^/      /'
        ci_fail fuzz "artifact(s) from an earlier run are still here — investigate them"
    fi
    # shellcheck disable=SC2086
    cmake -S . -B "$CI_FUZZ_BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_COMPILER=clang++ -DFSMTABLE_BUILD_TESTS=OFF \
        -DFSMTABLE_BUILD_FUZZING=ON > "$CI_LOG_DIR/fuzz-configure.log" 2>&1 \
        || ci_fail fuzz "cmake configure failed" "$CI_LOG_DIR/fuzz-configure.log"
    cmake --build "$CI_FUZZ_BUILD_DIR" -j "$CI_JOBS" --target fsmtable_fuzz \
        > "$CI_LOG_DIR/fuzz-build.log" 2>&1 \
        || ci_fail fuzz "fuzz target build failed" "$CI_LOG_DIR/fuzz-build.log"
    # libFuzzer WRITES new coverage-increasing inputs into the first corpus directory it
    # is handed. Giving it the tracked corpus/ would leave corpus/ dirty and fail
    # `tree --require-clean` on the next push, so the run works on a copy in the ignored
    # build dir; the checked-in corpus/ stays the frozen seed set (SPEC.md section 7),
    # and anything the fuzzer discovers survives in the copy between runs.
    local corpus="$CI_FUZZ_BUILD_DIR/corpus"
    if [ ! -d "$corpus" ]; then
        mkdir -p "$corpus"
        cp corpus/*.fsm "$corpus/" 2> /dev/null || true
    fi
    "$CI_FUZZ_BUILD_DIR/fsmtable_fuzz" "$corpus" \
        -max_total_time="$CI_FUZZ_SECONDS" -timeout=10 \
        -artifact_prefix="$CI_FUZZ_BUILD_DIR/" \
        -print_final_stats=1 > "$CI_LOG_DIR/fuzz.log" 2>&1
    local rc=$?
    local fresh
    fresh=$(fuzz_artifacts)
    if [ -n "$fresh" ]; then
        printf '%s\n' "$fresh" | head -5 | sed 's/^/      /'
        ci_fail fuzz "the fuzzer wrote artifact(s) — each is a bug, not a file to delete" \
            "$CI_LOG_DIR/fuzz.log"
    fi
    if [ "$rc" -ne 0 ]; then
        ci_fail fuzz "the fuzz target exited $rc" "$CI_LOG_DIR/fuzz.log"
    fi
    grep -E '^#[0-9]+' "$CI_LOG_DIR/fuzz.log" | tail -1 | sed 's/^/      /'
    grep -q 'DONE' "$CI_LOG_DIR/fuzz.log" \
        || ci_fail fuzz "the run did not finish (no DONE in the log)" "$CI_LOG_DIR/fuzz.log"
    ci_pass fuzz
}

run_stage "$@"
