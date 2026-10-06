# REPORT.md — fsmTable, stages A and B

    Gate verdict line (verbatim):
    all 12 stage(s) passed in 95s
    GATE PASSED
    Both lines are from the run on commit 5219d60; this file is the only difference from that
    tree, and the run was repeated after it was added.

    Test count:
    41 (gtest: "41 tests from 6 test suites ran", "[  PASSED  ] 41 tests"), all passing.
    Stage A — 24: the 21 frozen names of SPEC.md section 5, under exactly those names, plus
    RejectsVersionThatIsNotFirst (rule 1, which the frozen names do not reach),
    EveryCorpusFileObeysTheFrozenRules (section 7) and NumbersAgreeWithTheString (the kit's
    version template asks for it).
    Stage B — 17, in tests/analysis_test.cpp: the two analyses of section 8, including one
    test that walks the corpus and holds every machine in it to the contract both share.

    Fuzz stage: runs / seconds / artifacts
    2743901 runs / 60 s / 0 artifacts
    "#2743901	DONE   cov: 252 ft: 927 corp: 56/4558b lim: 4096 exec/s: 44981 rss: 487Mb"
    The run uses a copy of the frozen corpus inside the build directory, not the tracked
    corpus/: libFuzzer writes new coverage-increasing inputs into the directory it is handed,
    and doing that to the tracked tree would leave it dirty.
    Stage B raised coverage from 150 to 252 (edges) and from 457 to 927 (features) by holding
    the analyses to the same invariants as the rest of the library.

    Stage reached:
    Stage B. Stage A was green first. Stage C — the differential cross-check against FSMgine
    2.1.0 — not started: SPEC.md section 8 says to stop at the end of each stage and report.

    What I could not do:
    Nothing that stages A and B ask for. Three things belong here rather than being left out:
      * stage A was delivered with the kit's hooks PRESENT but NOT ARMED in this repository:
        the copy from the arena brought the files, not .git/config, so core.hooksPath was
        unset and no hook ever ran. It is armed now, and stage B's commit is the first one a
        hook checked.
      * two clang-tidy findings remain accepted in .ci/tidy-baseline.txt because the text they
        sit on is not this repo's to change: the underlying type of `enum class Op`, frozen by
        SPEC.md section 4, and libFuzzer's entry-point signature. Every other finding the lint
        stage reported was fixed in the code rather than accepted.
      * the first version of the footprint probe this repo carries was too strict: it demanded
        the kit's audit line byte-for-byte, while this gate legitimately audits a seventh path
        (its fuzz build dir). The probe now derives the audited set from the gate it checks,
        and the kit was corrected as well.

    What I had to guess:
    Four readings, each with its alternative written up in QUESTIONS.md:
      Q1 — the line number an error carries when a required directive is ABSENT. Chosen: 0,
           which is what Error::line documents itself to mean. Alternative: line 1.
      Q2 — whether `machine` and `initial` may follow the transitions. Chosen: yes, since
           section 3's list is exhaustive by its own words and does not make it an error.
      Q3 — whether a row carrying a `when` clause is a path for `unreachable`. Chosen: yes —
           reachability is structural, and evaluating a guard is stage C's work.
      Q4 — what a row naming a state that is not declared means (only a hand-built Machine
           can produce one). Chosen: it leads nowhere, so both results stay subsets of
           `states`. Alternative: treat the name as a state of its own.
