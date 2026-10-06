# REPORT.md — fsmTable stage A

    Gate verdict line (verbatim):
    all 12 stage(s) passed in 92s
    GATE PASSED

    Test count:
    24 (gtest: "24 tests from 3 test suites ran", "[  PASSED  ] 24 tests"). 21 of them are
    the frozen names of SPEC.md section 5, present under exactly those names and passing;
    the other three are RejectsVersionThatIsNotFirst (rule 1, which the frozen names do not
    reach), EveryCorpusFileObeysTheFrozenRules (section 7) and NumbersAgreeWithTheString
    (the kit's version template asks for it).

    Fuzz stage: runs / seconds / artifacts
    2917015 runs / 60 s / 0 artifacts
    "#2917015	DONE   cov: 150 ft: 443 corp: 39/2191b lim: 4096 exec/s: 47819 rss: 483Mb"
    The run uses a copy of the frozen corpus inside the build directory, not the tracked
    corpus/: libFuzzer writes new coverage-increasing inputs into the directory it is
    handed, and doing that to the tracked tree would leave it dirty.

    Stage reached:
    Stage A. Stages B and C not started — SPEC.md section 8 says to stop at the end of
    each stage and report.

    What I could not do:
    Nothing that stage A asks for. Two things are worth naming rather than hiding:
      * the kitprobes stage reports SKIP, not pass: this copy carries no tools/kit-probes/,
        which PLUNK-IN step 9 defines as "carries no probe yet", not as "behind";
      * two clang-tidy findings remain accepted in .ci/tidy-baseline.txt because the text
        they sit on cannot be changed here: the underlying type of `enum class Op`, frozen
        by SPEC.md section 4, and libFuzzer's entry-point signature. Every other finding
        the lint stage reported was fixed in the code.

    What I had to guess:
    Two readings, each with its alternative written up in QUESTIONS.md:
      Q1 — the line number an error carries when a required directive is ABSENT. Chosen:
           0, which is what Error::line documents itself to mean. Alternative: line 1.
      Q2 — whether `machine` and `initial` may follow the transitions. Chosen: yes, since
           section 3's list is exhaustive by its own words and does not make it an error.
