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

## Q3 — Does a row carrying a `when` clause count as a path?

Section 8 defines `unreachable` as the states with "no path from `initial`" and says nothing
about guards. A guarded row exists in the text but may never fire.

**Reading taken:** a guarded row is a path. Reachability is a property of the graph the rows
draw; a guard decides whether a particular row fires at run time, and nothing in stage B
evaluates a guard's value. Under this reading the answer does not depend on the numbers in
the guards either, which is what makes this a static analysis rather than a simulation.

**The alternative:** treat guarded rows as absent, which turns the question into "cannot be
entered without some row firing first" — a different and far less predictable one, since it
depends on the untested `when_value` values. Guards start to matter in stage C, where the
differential oracle drives events into FSMgine and compares traces.

## Q4 — What does a row naming a state that is not in `Machine::states` mean?

`parse` declares every state name it meets, so a machine it produced always has both ends of
every row in `states`. The analyses take any `Machine`, though, and `Machine` is an
aggregate: a caller can build one by hand whose row names a state it never declared.

**Reading taken:** such a row leads nowhere. An undeclared name is not a state the analysis
can return, and an undeclared `to` is not entered — so both results always stay subsets of
`states`, in first-appearance order, which is the contract the header states.

**The alternative:** treat an undeclared name as a state of its own and return it. That
would make the functions total over hand-built machines as well, but it would hand back names
outside `states` (breaking the first-appearance ordering section 8 fixes) and it would need a
second reading for an undeclared `from`. The narrower reading keeps one contract for both
functions, and a test pins it.
