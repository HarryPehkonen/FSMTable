cmake_minimum_required(VERSION 3.18)
project(FSMTable VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The gate counts warnings in the log as well as relying on -Werror, but a target that
# never got the flag would slip past. Every target below gets it explicitly.
set(FSMTABLE_WARNINGS -Wall -Wextra -Wpedantic -Werror)
set(FSMTABLE_SANITIZERS -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)

# The version number has exactly one home: project(... VERSION x.y.z) above. This is the
# second copy the gate's `version` stage compares against that line — generated, never
# typed, and at `build/generated/version.hpp` because that is the path the stage looks at
# by default (see CI_VERSION_HEADER). A .ci.env override would not survive a fresh clone:
# that file is gitignored, so the default has to be the real path.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/version.hpp
    @ONLY
)

option(FSMTABLE_BUILD_TESTS "Build the GTest suite" ON)
option(FSMTABLE_BUILD_FUZZING "Build the libFuzzer target (clang only)" OFF)

# Stage C's oracle (SPEC.md section 8): the differential cross-check holds this library's
# reading of a machine against FSMgine 2.1.0's compiled back end. That is an independent
# library, so its headers have to exist, and the gate says so rather than quietly skipping the
# stage. Only headers are used, and nothing is built inside FSMgine's own tree.
set(FSMTABLE_FSMGINE_DIR "$ENV{HOME}/hermes-workspace/FSMgine" CACHE PATH
    "FSMgine checkout whose headers the stage C differential test compiles against")
set(FSMTABLE_FSMGINE_HEADER "${FSMTABLE_FSMGINE_DIR}/include/FSMgine/compiled/Machine.hpp")
if(NOT EXISTS "${FSMTABLE_FSMGINE_HEADER}")
    message(FATAL_ERROR
        "SPEC.md section 8's stage C cross-check needs FSMgine 2.1.0's headers, and "
        "${FSMTABLE_FSMGINE_HEADER} does not exist. "
        "Point -DFSMTABLE_FSMGINE_DIR=<path-to-FSMgine> at a checkout.")
endif()

# Four translation units since 2026-10-06: the single 754-line src/fsmtable.cpp held the format's
# vocabulary, the parser, the writer and the graph analysis, and a reader looking for one had to walk
# past the other three. No logic changed in the split. `transform.cpp` is the fifth (2026-10-08): the
# text -> text direction, beside the parser and the writer rather than in the tool that calls it.
# `merge.cpp` is the sixth (2026-10-08): the other verb of that direction, split from transform.cpp
# for the same reason the direction itself was split from the parser — one concern per file, and a
# merge is a union rather than a surgical edit.
add_library(fsmTable src/tokens.cpp src/parse.cpp src/dump.cpp src/analysis.cpp src/transform.cpp
    src/merge.cpp)
target_include_directories(fsmTable PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(fsmTable PRIVATE ${FSMTABLE_WARNINGS})

# The other direction: text -> code. The generator links the library because it parses with it,
# and it is a shipped tool rather than a test fixture, so it is built in every configuration
# (which also means the sanitizer stages check it).
add_executable(fsmtable_gen gen/fsmtable-gen.cpp)
target_link_libraries(fsmtable_gen PRIVATE fsmTable)
target_compile_options(fsmtable_gen PRIVATE ${FSMTABLE_WARNINGS})
set_target_properties(fsmtable_gen PROPERTIES OUTPUT_NAME fsmtable-gen)

# Where the generator writes the headers it produces: the corpus inputs, the fixture, and
# the calculator example below.
set(FSMTABLE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${FSMTABLE_GENERATED_DIR}")

# --- the calculator example: the first real consumer of the generator. Its machine comes
# from its own .fsm through the same tool the tests' inputs go through, and its core is a
# library so the binary and the tests share one build of it. Semantics:
# examples/calculator/README.md.
set(FSMTABLE_CALCULATOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/examples/calculator")

add_custom_command(
    OUTPUT "${FSMTABLE_GENERATED_DIR}/fsm_calculator.hpp"
    COMMAND fsmtable_gen examples/calculator/calculator.fsm
            -o "${FSMTABLE_GENERATED_DIR}/fsm_calculator.hpp"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS fsmtable_gen "${FSMTABLE_CALCULATOR_DIR}/calculator.fsm"
    COMMENT "fsmtable-gen: calculator.fsm -> fsm_calculator.hpp"
    VERBATIM)
add_custom_target(fsmtable_calculator_header
    DEPENDS "${FSMTABLE_GENERATED_DIR}/fsm_calculator.hpp")

add_library(calculator_core STATIC examples/calculator/calculator.cpp)
add_dependencies(calculator_core fsmtable_calculator_header)
target_link_libraries(calculator_core PUBLIC fsmTable)
target_compile_options(calculator_core PRIVATE ${FSMTABLE_WARNINGS})
target_include_directories(calculator_core PUBLIC
    "${FSMTABLE_CALCULATOR_DIR}" "${FSMTABLE_GENERATED_DIR}")
target_include_directories(calculator_core SYSTEM PUBLIC "${FSMTABLE_FSMGINE_DIR}/include")

add_executable(calculator examples/calculator/main.cpp)
target_link_libraries(calculator PRIVATE calculator_core)
target_compile_options(calculator PRIVATE ${FSMTABLE_WARNINGS})

# --- the protocol example: a second consumer, and the first one with a clock. The clock is the
# driver's (`tick` carries the milliseconds that have passed), which is what keeps the machine a
# table. Semantics: examples/protocol/README.md.
set(FSMTABLE_PROTOCOL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/examples/protocol")

add_custom_command(
    OUTPUT "${FSMTABLE_GENERATED_DIR}/fsm_protocol.hpp"
    COMMAND fsmtable_gen examples/protocol/protocol.fsm
            -o "${FSMTABLE_GENERATED_DIR}/fsm_protocol.hpp"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS fsmtable_gen "${FSMTABLE_PROTOCOL_DIR}/protocol.fsm"
    COMMENT "fsmtable-gen: protocol.fsm -> fsm_protocol.hpp"
    VERBATIM)
add_custom_target(fsmtable_protocol_header
    DEPENDS "${FSMTABLE_GENERATED_DIR}/fsm_protocol.hpp")

add_library(protocol_core STATIC examples/protocol/protocol.cpp)
add_dependencies(protocol_core fsmtable_protocol_header)
target_link_libraries(protocol_core PUBLIC fsmTable)
target_compile_options(protocol_core PRIVATE ${FSMTABLE_WARNINGS})
target_include_directories(protocol_core PUBLIC
    "${FSMTABLE_PROTOCOL_DIR}" "${FSMTABLE_GENERATED_DIR}")
target_include_directories(protocol_core SYSTEM PUBLIC "${FSMTABLE_FSMGINE_DIR}/include")

add_executable(protocol examples/protocol/main.cpp)
target_link_libraries(protocol PRIVATE protocol_core)
target_compile_options(protocol PRIVATE ${FSMTABLE_WARNINGS})

# --- the inspector: the library half of the format with no generator in sight. It reads a .fsm at
# RUN time, so there is nothing to generate and nothing to link but the library itself.
add_executable(fsmtable-inspect examples/inspector/inspector.cpp)
target_link_libraries(fsmtable-inspect PRIVATE fsmTable)
target_compile_options(fsmtable-inspect PRIVATE ${FSMTABLE_WARNINGS})

# --- the transform: the text -> text direction (TRANSFORM.md). It reads a .fsm at run time and
# writes one back out, so like the inspector it links the library and nothing else. Its first verb
# is `rename`; a second (merge) would be a new card rather than a new flag.
add_executable(fsmtable-transform transform/fsmtable-transform.cpp)
target_link_libraries(fsmtable-transform PRIVATE fsmTable)
target_compile_options(fsmtable-transform PRIVATE ${FSMTABLE_WARNINGS})

# One install, then every repo on the machine runs the same engine:
#   cmake --install build-release --prefix ~/.local   ->  ~/.local/bin/fsmtable-gen
#                                                          ~/.local/bin/fsmtable-inspect
#                                                          ~/.local/bin/fsmtable-transform
# A consumer repo carries only its machine and the recipe (GENERATOR.md, "Two ways to consume a
# generated machine"): the tools are installed once per machine rather than built beside every
# tree — the model KitCI already set for gate engines. The shipped tools install together, because
# they are one engine's halves: a consumer's drift probe names `fsmtable-gen`, `fsmtable-inspect`
# is what the same reader reaches for on the artifact it guards, and `fsmtable-transform` is what
# it reaches for when a name in the machine has to move. Installing is also what keeps a probe
# honest: pointed at this tree's `build/`, it reads whatever the last build left there; pointed at
# a prefix, it reads the binary the machine actually runs.
install(TARGETS fsmtable_gen fsmtable-inspect fsmtable-transform RUNTIME DESTINATION bin)

if(FSMTABLE_BUILD_TESTS)
    include(CTest)
    find_package(GTest REQUIRED)
    add_executable(fsmTable_tests tests/parser_test.cpp tests/analysis_test.cpp)
    target_link_libraries(fsmTable_tests PRIVATE fsmTable GTest::gtest_main)
    target_compile_options(fsmTable_tests PRIVATE ${FSMTABLE_WARNINGS})
    # The corpus is input, not a build product: the test that walks it needs the source
    # directory, not the build directory, because ctest runs the binary outside the tree.
    target_compile_definitions(fsmTable_tests
        PRIVATE FSMTABLE_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus")
    # The generated version header is build-tree only, and nothing in the library includes
    # it — the test below is the one translation unit that must compile it.
    target_include_directories(fsmTable_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
    add_test(NAME fsmTable_tests COMMAND fsmTable_tests)

    # Stage C: the differential oracle against FSMgine. Its include directory is SYSTEM because
    # a third-party header's warnings are not this repo's to fix, and the header is used exactly
    # as FSMgine ships it.
    add_executable(fsmTable_differential tests/differential_test.cpp)
    target_link_libraries(fsmTable_differential PRIVATE fsmTable GTest::gtest_main)
    target_compile_options(fsmTable_differential PRIVATE ${FSMTABLE_WARNINGS})
    target_include_directories(fsmTable_differential SYSTEM PRIVATE
        "${FSMTABLE_FSMGINE_DIR}/include")
    target_compile_definitions(fsmTable_differential
        PRIVATE FSMTABLE_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus")
    add_test(NAME fsmTable_differential COMMAND fsmTable_differential)

    # --- The generator: its headers are produced at build time and compiled by the test that
    # follows, so a generator that emits something that does not compile fails the build rather
    # than a review. Paths are relative to the source directory so each header's provenance
    # comment is stable wherever the build happens.
    set(FSMTABLE_GENERATOR_INPUTS
        corpus/traffic_light.fsm
        corpus/guarded.fsm
        corpus/minimal.fsm
        tests/fixtures/refined_pair.fsm
        tests/fixtures/named_kinds.fsm
        tests/fixtures/entry_exit.fsm)
    set(FSMTABLE_GENERATED_HEADERS "")
    foreach(fsm IN LISTS FSMTABLE_GENERATOR_INPUTS)
        get_filename_component(stem "${fsm}" NAME_WE)
        set(header "${FSMTABLE_GENERATED_DIR}/fsm_${stem}.hpp")
        add_custom_command(
            OUTPUT "${header}"
            COMMAND fsmtable_gen "${fsm}" -o "${header}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            DEPENDS fsmtable_gen "${CMAKE_CURRENT_SOURCE_DIR}/${fsm}"
            COMMENT "fsmtable-gen: ${fsm} -> fsm_${stem}.hpp"
            VERBATIM)
        list(APPEND FSMTABLE_GENERATED_HEADERS "${header}")
    endforeach()
    add_custom_target(fsmtable_generated_headers DEPENDS ${FSMTABLE_GENERATED_HEADERS})

    add_executable(fsmTable_generator_tests tests/generator_test.cpp)
    add_dependencies(fsmTable_generator_tests fsmtable_generated_headers)
    target_link_libraries(fsmTable_generator_tests PRIVATE fsmTable GTest::gtest_main)
    target_compile_options(fsmTable_generator_tests PRIVATE ${FSMTABLE_WARNINGS})
    target_include_directories(fsmTable_generator_tests SYSTEM PRIVATE
        "${FSMTABLE_FSMGINE_DIR}/include")
    target_include_directories(fsmTable_generator_tests PRIVATE "${FSMTABLE_GENERATED_DIR}")
    target_compile_definitions(fsmTable_generator_tests PRIVATE
        FSMTABLE_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus"
        FSMTABLE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
        FSMTABLE_GENERATED_DIR="${FSMTABLE_GENERATED_DIR}")
    add_test(NAME fsmTable_generator_tests COMMAND fsmTable_generator_tests)

    # The generator's own failure paths and one positive shape check, as ctest cases: an input
    # that does not parse, a valid input whose names cannot be C++ identifiers, and the emitted
    # table of the spec's own example.
    add_test(NAME fsmtable_gen_rejects_invalid
        COMMAND fsmtable_gen "${CMAKE_CURRENT_SOURCE_DIR}/corpus/invalid_junk.fsm")
    set_tests_properties(fsmtable_gen_rejects_invalid PROPERTIES WILL_FAIL TRUE)
    add_test(NAME fsmtable_gen_rejects_keyword
        COMMAND fsmtable_gen "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/keyword_state.fsm")
    set_tests_properties(fsmtable_gen_rejects_keyword PROPERTIES WILL_FAIL TRUE)
    add_test(NAME fsmtable_gen_prints_a_header
        COMMAND fsmtable_gen "${CMAKE_CURRENT_SOURCE_DIR}/corpus/traffic_light.fsm")
    set_tests_properties(fsmtable_gen_prints_a_header PROPERTIES PASS_REGULAR_EXPRESSION
        "inline constexpr std::array<fsmgine::compiled::Transition<TrafficLightState, TrafficLightEvent>, 3> TrafficLightRows")

    add_executable(fsmTable_calculator_tests examples/calculator/calculator_test.cpp)
    target_link_libraries(fsmTable_calculator_tests PRIVATE calculator_core GTest::gtest_main)
    target_compile_options(fsmTable_calculator_tests PRIVATE ${FSMTABLE_WARNINGS})
    add_test(NAME fsmTable_calculator_tests COMMAND fsmTable_calculator_tests)

    # The end-to-end session: the executable, fed lines, compared line for line. Piped
    # stdin is not a terminal, so no prompt appears in the expected output.
    # Named event kinds (NAMED_KINDS.md): its own binary, so the frozen test target keeps the
    # exact composition the spec froze.
    add_executable(fsmTable_kinds_tests tests/kinds_test.cpp)
    target_link_libraries(fsmTable_kinds_tests PRIVATE fsmTable GTest::gtest_main)
    target_compile_options(fsmTable_kinds_tests PRIVATE ${FSMTABLE_WARNINGS})
    add_test(NAME fsmTable_kinds_tests COMMAND fsmTable_kinds_tests)

    # Entry and exit actions (ENTRY_EXIT.md): its own binary too, for the same reason.
    add_executable(fsmTable_state_actions_tests tests/state_actions_test.cpp)
    target_link_libraries(fsmTable_state_actions_tests PRIVATE fsmTable GTest::gtest_main)
    target_compile_options(fsmTable_state_actions_tests PRIVATE ${FSMTABLE_WARNINGS})
    add_test(NAME fsmTable_state_actions_tests COMMAND fsmTable_state_actions_tests)

    # The text -> text direction (TRANSFORM.md): `fsmtable::rename` and the rename spelling a
    # command line reads. Its own binary, like the two additions above. It walks the corpus and
    # the fixtures, so it needs both paths the same way the parser's suite does.
    add_executable(fsmTable_transform_tests tests/transform_test.cpp)
    target_link_libraries(fsmTable_transform_tests PRIVATE fsmTable GTest::gtest_main)
    target_compile_options(fsmTable_transform_tests PRIVATE ${FSMTABLE_WARNINGS})
    target_compile_definitions(fsmTable_transform_tests PRIVATE
        FSMTABLE_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus"
        FSMTABLE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME fsmTable_transform_tests COMMAND fsmTable_transform_tests)

    # The merge transform (TRANSFORM.md): `fsmtable::merge`, the union of two machines. Its own
    # binary too — it is the only suite that has to feed TWO machines to one call.
    add_executable(fsmTable_merge_tests tests/merge_test.cpp)
    target_link_libraries(fsmTable_merge_tests PRIVATE fsmTable GTest::gtest_main)
    target_compile_options(fsmTable_merge_tests PRIVATE ${FSMTABLE_WARNINGS})
    add_test(NAME fsmTable_merge_tests COMMAND fsmTable_merge_tests)

    add_test(NAME calculator_session
        COMMAND sh -c "\"$<TARGET_FILE:calculator>\" < \"${FSMTABLE_CALCULATOR_DIR}/input/session.txt\" | diff -u \"${FSMTABLE_CALCULATOR_DIR}/input/session.expected\" -")

    add_executable(protocol_tests examples/protocol/protocol_test.cpp)
    target_link_libraries(protocol_tests PRIVATE protocol_core GTest::gtest_main)
    target_compile_options(protocol_tests PRIVATE ${FSMTABLE_WARNINGS})
    add_test(NAME protocol_tests COMMAND protocol_tests)

    # The same end-to-end shape as the calculator's: the script through the built binary, diffed
    # line for line against the recorded trace. That binary exits 1 when a directive was not
    # answered, and this script contains two of those on purpose.
    add_test(NAME protocol_session
        COMMAND sh -c "\"$<TARGET_FILE:protocol>\" < \"${FSMTABLE_PROTOCOL_DIR}/input/session.txt\" | diff -u \"${FSMTABLE_PROTOCOL_DIR}/input/session.expected\" -")

    # The inspector, as ctest cases. Each pins the exit code as well as the output, because the
    # exit code is the part a script or a hook would use: 0 nothing to report, 1 the file could not
    # be read or a state is unreachable, 2 the command line is wrong.
    add_test(NAME inspect_reports_a_clean_machine
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-inspect>\" \"${FSMTABLE_PROTOCOL_DIR}/protocol.fsm\" > /dev/null")
    add_test(NAME inspect_finds_an_unreachable_state
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-inspect>\" \"${CMAKE_CURRENT_SOURCE_DIR}/examples/inspector/dirty.fsm\"); code=$?; printf '%s\\n' \"$out\"; [ $code -eq 1 ]")
    set_tests_properties(inspect_finds_an_unreachable_state PROPERTIES
        PASS_REGULAR_EXPRESSION "UNREACHABLE: Orphan")
    add_test(NAME inspect_canonical_is_stable
        COMMAND sh -c "first=$(\"$<TARGET_FILE:fsmtable-inspect>\" --canonical \"${FSMTABLE_PROTOCOL_DIR}/protocol.fsm\"); second=$(printf '%s\\n' \"$first\" | \"$<TARGET_FILE:fsmtable-inspect>\" --canonical /dev/stdin); [ \"$first\" = \"$second\" ]")
    add_test(NAME inspect_refuses_no_arguments
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-inspect>\" > /dev/null 2>&1; [ $? -eq 2 ]")

    # --trace: the table walked over a flat event list. What each invocation PRINTS is pinned
    # exactly by tools/check-doc-claims.sh (the inspector README's output block re-runs the
    # documented commands and diffs them line for line); these cases pin the EXIT CODES, which is
    # the part a script or a hook would use — 0 for a walk that finished, 1 for the first event the
    # table has no row for, 2 for a command line it cannot read.
    add_test(NAME inspect_trace_walks_the_machine
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-inspect>\" --trace \"${FSMTABLE_CALCULATOR_DIR}/calculator.fsm\" number add number equals); code=$?; printf '%s\\n' \"$out\"; [ $code -eq 0 ]")
    set_tests_properties(inspect_trace_walks_the_machine PROPERTIES PASS_REGULAR_EXPRESSION
        "Accum --equals--> Accum")
    add_test(NAME inspect_trace_takes_a_value_and_a_guard
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-inspect>\" --trace \"${FSMTABLE_PROTOCOL_DIR}/protocol.fsm\" open syn_ack fin tick=2500); code=$?; printf '%s\\n' \"$out\"; [ $code -eq 0 ]")
    set_tests_properties(inspect_trace_takes_a_value_and_a_guard PROPERTIES PASS_REGULAR_EXPRESSION
        "TimeWait --tick--> Closed")
    add_test(NAME inspect_trace_stops_at_an_unmatched_event
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-inspect>\" --trace \"${CMAKE_CURRENT_SOURCE_DIR}/examples/inspector/dirty.fsm\" step bump=5); code=$?; printf '%s\\n' \"$out\"; [ $code -eq 1 ]")
    set_tests_properties(inspect_trace_stops_at_an_unmatched_event PROPERTIES PASS_REGULAR_EXPRESSION
        "trace stopped at event 2: Running --bump--> .value 5. matched no row")
    add_test(NAME inspect_trace_refuses_an_unknown_kind
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-inspect>\" --trace \"${FSMTABLE_CALCULATOR_DIR}/calculator.fsm\" nosuchkind > /dev/null 2>&1; [ $? -eq 2 ]")
    add_test(NAME inspect_trace_needs_an_event
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-inspect>\" --trace \"${FSMTABLE_CALCULATOR_DIR}/calculator.fsm\" > /dev/null 2>&1; [ $? -eq 2 ]")
    add_test(NAME inspect_trace_is_one_mode
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-inspect>\" --canonical --trace \"${FSMTABLE_CALCULATOR_DIR}/calculator.fsm\" number > /dev/null 2>&1; [ $? -eq 2 ]")

    # --graph: the table drawn as a Mermaid stateDiagram-v2. The third projection, beside the
    # summary's findings and --canonical's text — it writes nothing back and reports nothing, so it
    # exits 0 and these cases pin the DRAWING: the initial marker, the state descriptions, a guard
    # and an action on an edge, a sink's class. Its bytes are pinned exactly by
    # tools/check-doc-claims.sh, which re-runs the invocation the inspector README documents and
    # diffs the output line for line — the same check KitCI v1.1 gives its own --graph.
    add_test(NAME inspect_graph_draws_the_machine
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-inspect>\" --graph \"${FSMTABLE_PROTOCOL_DIR}/protocol.fsm\"); code=$?; printf '%s\\n' \"$out\"; [ $code -eq 0 ]")
    set_tests_properties(inspect_graph_draws_the_machine PROPERTIES PASS_REGULAR_EXPRESSION
        "stateDiagram-v2")
    add_test(NAME inspect_graph_marks_the_initial_state
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-inspect>\" --graph \"${FSMTABLE_PROTOCOL_DIR}/protocol.fsm\"); code=$?; printf '%s\\n' \"$out\"; [ $code -eq 0 ]")
    set_tests_properties(inspect_graph_marks_the_initial_state PROPERTIES PASS_REGULAR_EXPRESSION
        "\\[\\*\\] --> s0")
    add_test(NAME inspect_graph_annotates_entry_and_exit
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-inspect>\" --graph \"${FSMTABLE_PROTOCOL_DIR}/protocol.fsm\"); code=$?; printf '%s\\n' \"$out\"; [ $code -eq 0 ]")
    set_tests_properties(inspect_graph_annotates_entry_and_exit PROPERTIES PASS_REGULAR_EXPRESSION
        "state \"SynSent<br/>entry arm_timer<br/>exit cancel_timer\" as s1")
    add_test(NAME inspect_graph_labels_a_guarded_edge
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-inspect>\" --graph \"${FSMTABLE_PROTOCOL_DIR}/protocol.fsm\"); code=$?; printf '%s\\n' \"$out\"; [ $code -eq 0 ]")
    set_tests_properties(inspect_graph_labels_a_guarded_edge PROPERTIES PASS_REGULAR_EXPRESSION
        "s1 --> s1 : tick when ge 1000 / retransmit")
    add_test(NAME inspect_graph_marks_a_sink
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-inspect>\" --graph \"${FSMTABLE_PROTOCOL_DIR}/protocol.fsm\"); code=$?; printf '%s\\n' \"$out\"; [ $code -eq 0 ]")
    set_tests_properties(inspect_graph_marks_a_sink PROPERTIES PASS_REGULAR_EXPRESSION
        "class s3 sink")
    # Byte-stable: two runs over the same file are the same bytes, the property KitCI's graph tests
    # pin with two renders of one config. The non-empty guard keeps an absent tool from passing
    # vacuously (the class of vacuity REPORT.md records for the exit-code-only cases).
    add_test(NAME inspect_graph_is_byte_stable
        COMMAND sh -c "first=$(\"$<TARGET_FILE:fsmtable-inspect>\" --graph \"${FSMTABLE_PROTOCOL_DIR}/protocol.fsm\"); second=$(\"$<TARGET_FILE:fsmtable-inspect>\" --graph \"${FSMTABLE_PROTOCOL_DIR}/protocol.fsm\"); [ -n \"$first\" ] && [ \"$first\" = \"$second\" ]")
    # A projection, not a report: dirty.fsm makes the summary exit 1 and print UNREACHABLE, and the
    # graph draws it and says nothing.
    add_test(NAME inspect_graph_is_a_projection_not_a_report
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-inspect>\" --graph \"${CMAKE_CURRENT_SOURCE_DIR}/examples/inspector/dirty.fsm\"); code=$?; printf '%s\\n' \"$out\"; [ $code -eq 0 ]")
    set_tests_properties(inspect_graph_is_a_projection_not_a_report PROPERTIES FAIL_REGULAR_EXPRESSION
        "UNREACHABLE")
    add_test(NAME inspect_graph_is_one_mode
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-inspect>\" --canonical --graph \"${FSMTABLE_PROTOCOL_DIR}/protocol.fsm\" > /dev/null 2>&1; [ $? -eq 2 ]")
    add_test(NAME inspect_graph_needs_a_file
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-inspect>\" --graph > /dev/null 2>&1; [ $? -eq 2 ]")

    # The transform (TRANSFORM.md), as ctest cases. What it PRINTS is pinned exactly by
    # tools/check-doc-claims.sh, which re-runs the invocations TRANSFORM.md documents and diffs
    # them line for line; these cases pin the EXIT CODES — the part a script or a hook uses — and
    # the one property a command line can show on its own: that a file's comments are still there
    # afterwards, and that a rename and its inverse are byte-identical to doing nothing.
    add_test(NAME transform_writes_a_renamed_machine
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-transform>\" rename \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/traffic_light.fsm\" state:Red=Stop machine:TrafficLight=Lamp); code=$?; printf '%s\\n' \"$out\"; [ $code -eq 0 ]")
    set_tests_properties(transform_writes_a_renamed_machine PROPERTIES PASS_REGULAR_EXPRESSION
        "transition Stop --1--> Green action on_red_green")
    add_test(NAME transform_keeps_the_comments_while_a_state_moves
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-transform>\" rename \"${FSMTABLE_PROTOCOL_DIR}/protocol.fsm\" state:SynSent=Opened); code=$?; printf '%s\\n' \"$out\"; [ $code -eq 0 ]")
    set_tests_properties(transform_keeps_the_comments_while_a_state_moves PROPERTIES PASS_REGULAR_EXPRESSION
        "# A connection lifecycle, with timeouts.")
    add_test(NAME transform_leaves_the_rows_the_rename_does_not_reach
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-transform>\" rename \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/traffic_light.fsm\" state:Green=Vert); code=$?; printf '%s\\n' \"$out\"; [ $code -eq 0 ]")
    set_tests_properties(transform_leaves_the_rows_the_rename_does_not_reach PROPERTIES PASS_REGULAR_EXPRESSION
        "transition Yellow --3--> Red when ge 30")
    add_test(NAME transform_refuses_a_collision
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-transform>\" rename \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/traffic_light.fsm\" state:Red=Green 2>&1); code=$?; printf '%s\\n' \"$out\"; [ $code -eq 1 ]")
    set_tests_properties(transform_refuses_a_collision PROPERTIES PASS_REGULAR_EXPRESSION
        "collides with the state 'Green'")
    add_test(NAME transform_refuses_an_unknown_name
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-transform>\" rename \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/traffic_light.fsm\" state:Nope=Amber > /dev/null 2>&1; [ $? -eq 1 ]")
    add_test(NAME transform_refuses_a_malformed_rename
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-transform>\" rename \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/traffic_light.fsm\" Red=Green > /dev/null 2>&1; [ $? -eq 2 ]")
    add_test(NAME transform_refuses_an_unknown_target
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-transform>\" rename \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/traffic_light.fsm\" colour:Red=Green > /dev/null 2>&1; [ $? -eq 2 ]")
    add_test(NAME transform_refuses_the_same_name_twice
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-transform>\" rename \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/traffic_light.fsm\" state:Red=A state:Red=B > /dev/null 2>&1; [ $? -eq 2 ]")
    add_test(NAME transform_refuses_a_file_that_does_not_parse
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-transform>\" rename \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/invalid_junk.fsm\" state:A=B 2>&1); code=$?; printf '%s\\n' \"$out\"; [ $code -eq 2 ]")
    set_tests_properties(transform_refuses_a_file_that_does_not_parse PROPERTIES PASS_REGULAR_EXPRESSION
        "invalid_junk.fsm:4:")
    add_test(NAME transform_refuses_a_file_it_cannot_read
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-transform>\" rename \"${CMAKE_CURRENT_SOURCE_DIR}/no_such_file.fsm\" state:A=B > /dev/null 2>&1; [ $? -eq 2 ]")
    add_test(NAME transform_needs_a_command
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-transform>\" > /dev/null 2>&1; [ $? -eq 2 ]")
    add_test(NAME transform_refuses_an_unknown_command
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-transform>\" frobnicate \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/traffic_light.fsm\" > /dev/null 2>&1; [ $? -eq 2 ]")
    add_test(NAME transform_rename_needs_a_file
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-transform>\" rename > /dev/null 2>&1; [ $? -eq 2 ]")
    add_test(NAME transform_rename_needs_a_rename
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-transform>\" rename \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/traffic_light.fsm\" > /dev/null 2>&1; [ $? -eq 2 ]")
    add_test(NAME transform_refuses_an_unknown_option
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-transform>\" --canonical rename x.fsm state:A=B > /dev/null 2>&1; [ $? -eq 2 ]")
    add_test(NAME transform_help_exits_zero
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-transform>\" --help > /dev/null; [ $? -eq 0 ]")
    # A rename and its inverse are the original bytes — and the first rename really did move
    # something, so the case cannot pass by doing nothing at all.
    add_test(NAME transform_a_rename_and_its_inverse_are_the_original_bytes
        COMMAND sh -c "original=$(cat \"${CMAKE_CURRENT_SOURCE_DIR}/examples/inspector/dirty.fsm\"); first=$(\"$<TARGET_FILE:fsmtable-transform>\" rename \"${CMAKE_CURRENT_SOURCE_DIR}/examples/inspector/dirty.fsm\" state:Start=Begin); second=$(printf '%s\\n' \"$first\" | \"$<TARGET_FILE:fsmtable-transform>\" rename /dev/stdin state:Begin=Start); [ -n \"$first\" ] && [ \"$first\" != \"$second\" ] && [ \"$second\" = \"$original\" ]")

    # The merge verb (TRANSFORM.md) — the union of two machines, which is what makes it a verb of its
    # own rather than a flag. These pin the exit codes a script uses and the properties a command
    # line can show on its own: the union really is the two files composed, the header names both
    # parents, `-o` writes exactly the bytes stdout would have carried, and a union that would leave
    # a state unreachable is refused rather than written. What the whole machine PRINTS is pinned by
    # tools/check-doc-claims.sh, the division the rename cases above keep.
    #
    # These cases do NOT set PASS_REGULAR_EXPRESSION, deliberately: ctest IGNORES a case's exit code
    # as soon as that property is set — "The process exit code is ignored" (cmake --help-property
    # PASS_REGULAR_EXPRESSION) — so a case that set it would pin the message while its
    # `[ $code -eq 1 ]` stopped counting. Measured rather than assumed: with the reachability refusal
    # removed from src/merge.cpp the run exits 0 while still printing the finding, and a case with
    # the property set passes anyway. Both halves therefore live in the shell — print the output for
    # a reader, grep it, then check the code. The cases above that set the property have exactly this
    # hole; card t_198cb151 repairs them.
    set(FSMTABLE_MERGE_FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME merge_writes_the_union
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-transform>\" merge \"${FSMTABLE_MERGE_FIXTURES}/merge_left.fsm\" \"${FSMTABLE_MERGE_FIXTURES}/merge_right.fsm\"); code=$?; printf '%s\\n' \"$out\"; printf '%s\\n' \"$out\" | grep -qF -e \"transition SignedIn --expire--> Anonymous\" && [ $code -eq 0 ]")
    # Both parents are named, and each on the line that belongs to it: the grep runs, so a header
    # that stopped naming one of them fails here rather than in a reader's eyes.
    add_test(NAME merge_header_names_both_parents
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-transform>\" merge \"${FSMTABLE_MERGE_FIXTURES}/merge_left.fsm\" \"${FSMTABLE_MERGE_FIXTURES}/merge_right.fsm\"); code=$?; printf '%s\\n' \"$out\" | grep -q 'merge_left.fsm (machine Login)' && printf '%s\\n' \"$out\" | grep -q 'merge_right.fsm (machine Session)' && [ $code -eq 0 ]")
    add_test(NAME merge_output_to_o_is_the_stdout_bytes
        COMMAND sh -c "work=$(mktemp -d); trap 'rm -rf $work' EXIT; \"$<TARGET_FILE:fsmtable-transform>\" merge \"${FSMTABLE_MERGE_FIXTURES}/merge_left.fsm\" \"${FSMTABLE_MERGE_FIXTURES}/merge_right.fsm\" -o $work/out.fsm && \"$<TARGET_FILE:fsmtable-transform>\" merge \"${FSMTABLE_MERGE_FIXTURES}/merge_left.fsm\" \"${FSMTABLE_MERGE_FIXTURES}/merge_right.fsm\" > $work/stdout.fsm && cmp $work/out.fsm $work/stdout.fsm")
    # The second machine's initial-entry clause is dropped AND said so: the clause is reported, and
    # the run still exits 0 because the union is well defined without it.
    add_test(NAME merge_drops_the_second_initial_entry_clause_with_a_warning
        COMMAND sh -c "err=$(\"$<TARGET_FILE:fsmtable-transform>\" merge \"${FSMTABLE_MERGE_FIXTURES}/merge_entry_left.fsm\" \"${FSMTABLE_MERGE_FIXTURES}/merge_entry_right.fsm\" 2>&1 >/dev/null); code=$?; printf '%s\\n' \"$err\"; printf '%s\\n' \"$err\" | grep -qE -e \"entry action 'on_entry_watch'.*is dropped\" && [ $code -eq 0 ]")
    add_test(NAME merge_refuses_a_kind_two_names_disagree_about
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-transform>\" merge \"${FSMTABLE_MERGE_FIXTURES}/merge_clash_left.fsm\" \"${FSMTABLE_MERGE_FIXTURES}/merge_clash_right.fsm\" 2>&1); code=$?; printf '%s\\n' \"$out\"; printf '%s\\n' \"$out\" | grep -qF -e \"kind 1 is named 'step' by one machine and 'tick' by the other\" && [ $code -eq 1 ]")
    # The composition failing to meet: nothing leads from the merged initial into the traffic light,
    # so the finding is printed, the states are named, and no machine is written.
    add_test(NAME merge_refuses_a_union_that_leaves_a_state_unreachable
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-transform>\" merge \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/minimal.fsm\" \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/traffic_light.fsm\" 2>&1); code=$?; printf '%s\\n' \"$out\"; printf '%s\\n' \"$out\" | grep -qF -e \"3 unreachable state(s)\" && [ $code -eq 1 ]")
    add_test(NAME merge_refuses_a_file_that_does_not_parse
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-transform>\" merge \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/invalid_junk.fsm\" \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/minimal.fsm\" 2>&1); code=$?; printf '%s\\n' \"$out\"; printf '%s\\n' \"$out\" | grep -qF -e \"invalid_junk.fsm:4:\" && [ $code -eq 2 ]")
    # The SECOND file's line number is reported against the second path, which is the reason the tool
    # parses both files itself before it calls the library.
    add_test(NAME merge_reports_the_second_files_own_line
        COMMAND sh -c "out=$(\"$<TARGET_FILE:fsmtable-transform>\" merge \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/minimal.fsm\" \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/invalid_junk.fsm\" 2>&1); code=$?; printf '%s\\n' \"$out\"; printf '%s\\n' \"$out\" | grep -qF -e \"invalid_junk.fsm:4:\" && [ $code -eq 2 ]")
    add_test(NAME merge_refuses_a_file_it_cannot_read
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-transform>\" merge \"${CMAKE_CURRENT_SOURCE_DIR}/no_such_file.fsm\" \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/minimal.fsm\" > /dev/null 2>&1; [ $? -eq 2 ]")
    add_test(NAME merge_needs_two_files
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-transform>\" merge \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/minimal.fsm\" > /dev/null 2>&1; [ $? -eq 2 ]")
    add_test(NAME merge_refuses_a_third_file
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-transform>\" merge \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/minimal.fsm\" \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/minimal.fsm\" \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/minimal.fsm\" > /dev/null 2>&1; [ $? -eq 2 ]")
    add_test(NAME merge_output_needs_a_file
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-transform>\" merge \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/minimal.fsm\" \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/minimal.fsm\" -o > /dev/null 2>&1; [ $? -eq 2 ]")
    add_test(NAME merge_refuses_a_second_output_option
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-transform>\" merge \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/minimal.fsm\" \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/minimal.fsm\" -o a.fsm -o b.fsm > /dev/null 2>&1; [ $? -eq 2 ]")
    # -o belongs to merge, and only to merge: a rename's output is stdout, and a command line that
    # asked for a file has to be told so rather than quietly ignored.
    add_test(NAME rename_refuses_the_output_option
        COMMAND sh -c "\"$<TARGET_FILE:fsmtable-transform>\" rename \"${CMAKE_CURRENT_SOURCE_DIR}/corpus/traffic_light.fsm\" state:Red=Stop -o out.fsm > /dev/null 2>&1; [ $? -eq 2 ]")

    # Every relative link in a markdown file in this tree has to point at something. The script
    # carries the reason it exists; the short version is that a false reference to a file this
    # repository does not have reached a shipped README once already.
    add_test(NAME docs_links_resolve
        COMMAND sh -c "cd \"${CMAKE_CURRENT_SOURCE_DIR}\" && sh tools/check-doc-links.sh")

    # The claims the examples' READMEs make, re-derived from the real tools: a recipe has to generate
    # the counts its document states, a quoted output has to be that output, a documented shell trap
    # has to still be a trap. tools/check-doc-claims.sh lists exactly what it re-derives, and its
    # teeth are proved by sabotage in that script's own terms.
    add_test(NAME docs_claims_hold
        COMMAND sh -c "cd \"${CMAKE_CURRENT_SOURCE_DIR}\" && sh tools/check-doc-claims.sh \"$<TARGET_FILE:fsmtable_gen>\" \"$<TARGET_FILE:fsmtable-inspect>\" \"$<TARGET_FILE:fsmtable-transform>\"")
    # The install rule, as a test rather than a promise. `cmake --install` into a throwaway
    # prefix has to put ALL THREE tools there, and the installed generator has to RUN from a cwd
    # that is not this tree — the two things a consumer relies on, and the two a renamed target or
    # a dropped install() would break silently: the binaries keep building and running in `build/`
    # either way, so nothing else in this gate would notice (the same class INCIDENTS.md records
    # for the symlink trap). The path needs no quoting — mktemp and this build tree have no
    # spaces — and the trap removes the prefix however the chain ends, so the case leaves no
    # directory behind.
    add_test(NAME install_puts_every_tool_on_the_prefix
        COMMAND sh -c "prefix=$(mktemp -d); trap 'rm -rf $prefix' EXIT; cmake --install ${CMAKE_CURRENT_BINARY_DIR} --prefix $prefix > /dev/null && [ -x $prefix/bin/fsmtable-gen ] && [ -x $prefix/bin/fsmtable-inspect ] && [ -x $prefix/bin/fsmtable-transform ] && (cd / && $prefix/bin/fsmtable-gen --help > /dev/null) && (cd / && $prefix/bin/fsmtable-transform --help > /dev/null)")
endif()

# The fuzz target is written against libFuzzer's entry point, so it can only be LINKED
# where the sanitizer runtime exists (clang). It is compiled in every configuration
# regardless, as an object library, because two stages need it to be: the warning count in
# `build` only covers what is compiled, and `lint` refuses to analyse a source the build
# never compiles — with reason, since a file linted only sometimes is not linted.
add_library(fsmtable_fuzz_obj OBJECT fuzz/fuzz_fsmtable.cpp)
target_link_libraries(fsmtable_fuzz_obj PRIVATE fsmTable)
target_compile_options(fsmtable_fuzz_obj PRIVATE ${FSMTABLE_WARNINGS}
    $<$<BOOL:${FSMTABLE_BUILD_FUZZING}>:${FSMTABLE_SANITIZERS}>)

if(FSMTABLE_BUILD_FUZZING)
    add_executable(fsmtable_fuzz $<TARGET_OBJECTS:fsmtable_fuzz_obj>)
    target_link_libraries(fsmtable_fuzz PRIVATE fsmTable)
    target_link_options(fsmtable_fuzz PRIVATE ${FSMTABLE_SANITIZERS})
endif()