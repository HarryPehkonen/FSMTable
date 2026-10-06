# QUESTIONS.md — what the spec left open, and which reading I took

SPEC.md section 0: "If something is genuinely ambiguous, write the question into
QUESTIONS.md and take the simplest reading rather than guessing elaborately." These are
the only places I found a genuine choice to make — two in stage A (Q1, Q2) and two in
stage B (Q3, Q4). Each is a candidate for the spec to settle in a later revision; none
changes the frozen API, the frozen test names or the row format.

## Q1 — What line number does an error carry when the *required directive is absent*?

`Error::line` is documented as "1-based; 0 when the error is not tied to a line". A missing
`version`, `machine` or `initial` is not tied to any line — there is nothing to point at —
but SPEC.md section 5 requires each error test to assert a line number, so a reading has to
be chosen.

**Reading taken:** `line == 0` for a directive that never appeared at all. Where a
directive appeared but in the wrong place (`version` after another directive) the error
does point at a line, and carries that line instead.

**The alternative:** report line 1, on the grounds that the file is wrong from where the
missing directive should have been. It is defensible for `version` (which must be first)
and a lie for `machine`/`initial` (which may legally come anywhere), so 0 is the reading
that is true in every case.

## Q2 — May `machine` and `initial` appear after the transitions?

The spec fixes only that `version 1` is "the first non-comment line" and that each of the
three header directives appears "exactly once". It never says the header must precede the
rows.

**Reading taken:** any order is accepted once `version 1` is first, because the format
declares states on first appearance rather than declaring them up front — a row before
`initial` refers to the same state table. The canonical dump emits the header first
regardless, so order never survives a round trip.

**The alternative:** reject a header directive that follows a `transition`, which would be
an invented restriction: the spec's error list (section 3) is exhaustive by its own words
("Every violation below is an error"), and this is not in it.
