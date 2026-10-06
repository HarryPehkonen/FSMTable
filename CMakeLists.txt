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

add_library(fsmTable src/fsmtable.cpp)
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

    add_test(NAME calculator_session
        COMMAND sh -c "\"$<TARGET_FILE:calculator>\" < \"${FSMTABLE_CALCULATOR_DIR}/input/session.txt\" | diff -u \"${FSMTABLE_CALCULATOR_DIR}/input/session.expected\" -")
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