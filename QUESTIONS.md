# QUESTIONS.md — what the spec left open, and which reading I took

SPEC.md section 0: "If something is genuinely ambiguous, write the question into
QUESTIONS.md and take the simplest reading rather than guessing elaborately." These are
the only places I found a genuine choice to make — two in stage A (Q1, Q2), two in stage B
(Q3, Q4), three in stage C (Q5, Q6, Q7), two in the generator that follows them (Q8, Q9),
two in the calculator example (Q10, Q11), one in the named kinds addition (Q12) and two in
the entry and exit addition (Q13, Q14). Each is a candidate for the spec to settle in a later
revision; none changes the frozen API, the frozen test names or the row format.

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

## Q5 — What is the second trace in stage C's cross-check?

Section 8 asks for "an identical trace (state after every event, plus the action log)" against
FSMgine. Stages A and B define no execution semantics — the frozen section 4 API is `parse`,
`dump` and (from stage B) two analyses — so that framework has to come from somewhere, and
section 8 does not name a function of this library that produces it.

**Reading taken:** the test carries a plain reference interpreter — for the current state and
the event's kind, the guarded row fires if its comparison passes, otherwise the unguarded row
for the same pair, otherwise nothing moves — and FSMgine's compiled back end is the independent
implementation it is compared against. That is what makes it a differential oracle rather than a
self-check. No public API is added: a runner on `Machine` would be a change to the frozen
section 4 surface, and section 8 does not ask for one.

**The alternative:** add an execution function to the library and compare library to library.
That is a spec revision rather than an implementation decision, and it would push this
harness's reading of the semantics into the published API.

## Q6 — For one (from, kind), does the guarded row refine the unguarded row regardless of the order they appear in?

Rule 9 allows exactly one unguarded and one guarded row per (from, kind), and the file may
write them in either order.

**Reading taken:** precedence — the guarded row is the refinement of the pair and is tried
first, wherever the file put it. Two things point the same way: rule 9's own wording ("A
machine may refine at most one value slot"), and `dump`'s canonical order, which places guarded
before unguarded *within* a (from, kind) — an order that only means something under
first-match-wins. Handing FSMgine the canonical order is therefore what makes the two
implementations comparable at all.

**The alternative:** strict file order, under which an unguarded row written first would shadow
the guarded row written after it — making behaviour depend on an order the canonical form then
discards.

## Q7 — Does `dump` preserve the order of `states`?

`Machine::states` is "first-appearance order, unique", but the canonical form declares no
states: they are re-derived from `initial` and from the sorted rows. So
`parse(dump(parse(t)))->states` is in a different order from `parse(t)->states` whenever t's
rows are not already canonical. Section 5's oracle asks for
`dump(parse(dump(parse(t)))) == dump(parse(t))` — stability, which holds — and section 7's rules
hold either way. Nothing in the spec requires the vector to survive a round trip.

**Reading taken:** a round trip preserves the machine's meaning, its set of states and its
canonical text, and not the file's original first-appearance order. Stage C's round-trip test
asserts exactly that. The first draft of that test asserted the opposite and failed, which is
how the distinction got noticed.

**The alternative:** treat first-appearance order as preserved data — impossible without a
`state` directive in the canonical form, which section 4 does not define.

## Q8 — What should the generator do with a name that is a C++ keyword?

The format accepts any identifier, so `machine KeywordState` / `initial new` / `transition new
--1--> Done` parses cleanly. As C++ it cannot: the state name would have to become an enumerator,
and an action name a function.

**Reading taken:** refuse the file — exit 1, naming the offender and its role (`the state name
'new' is a C++ keyword`). The alternative, mangling to `new_`, makes the header compile at the
cost of the correspondence the generator exists to preserve: the artifact would no longer say what
the text says, and a code → text round trip would emit the mangled spelling as a state name and
fail to parse it back. Refusing costs one edit to the `.fsm` and keeps the mapping total.

**The alternative:** a documented mangling scheme (trailing underscore, or a `k` prefix). Cheap
for the generator and wrong for the round trip.

## Q9 — A literal rows table, or FSMgine builder calls?

`fsmgine::compiled::Machine` accepts either: the constructor takes a pre-built rows vector, and the
chain (`from()…to()…when()…action()`) builds one internally.

**Reading taken:** emit the table. One row per line means the generated file diffs like the `.fsm`
it came from; the order is visible in the artifact rather than implied by builder state, which is
what makes the canonical-order property checkable at all (`generator_test.cpp` reads
`rows[0].refined`); and a table is data that a code → text direction can read back without
replaying a builder. Builder chains are not wrong — they were probed and behave as documented —
they are simply less readable and less checkable here.

**The alternative:** emit the chain, one statement per row: shorter to write, and it would hide
the canonical order inside a sequence of calls with a mutable builder between them.

## Q10 — Can a row branch on data the machine accumulated?

The calculator example needs two such branches: `x / 0` and an overflow. A `when` clause
compares one int that rides on the event (SPEC.md section 3) and the machine holds nothing
else, so the answer depends on where the data the branch needs already is.

**Reading taken:** on the event, yes; accumulated, no. Division by zero is expressible
because both halves of the question are already visible at the transition: the divisor is
this event's value, and the pending operator is a state (`PendingDiv`), so
`transition PendingDiv --1--> Error when eq 0` decides it, and the example's test asserts the
machine really ends in `Error` — not merely that an error was printed. Overflow is not
expressible: the product is in neither the event nor the state, so it is caught in the
arithmetic layer and reported by the shell.

The practical consequence is a split a caller has to design for, not a limitation to work
around: put the branch's case in the state (one `Pending*` per operator, an `Error` state)
and the operand on the event, and the machine decides; a branch that needs the running value
belongs to the arithmetic. `x / 0 + 3` also shows what the `Error` state buys: made total
over the rest of the line, it keeps the *first* failure as the reported one instead of the
later token that gets refused.

**The alternative:** a v2 guard that calls a caller-supplied predicate over the caller's own
data — `when divisor_is_zero` — which is the format decision this example forces. It would
move overflow into the table too, at the cost of rows no longer being plain data.

## Q11 — How does an action reach state the caller holds?

An action in the compiled back end is a plain `void (*)(const Event&)` function pointer — it
cannot capture — and the format names actions without passing anything to them.

**Reading taken:** a pointer the caller owns, installed for the duration of a line and
cleared when the line ends (`calc::install` in the example). The lifetime is one call, which
is what makes it safe to reason about: nothing outside a line can observe it, and the
example is single-threaded, which the tsan stage checks for it as well as for the library.

**The trap in it:** the pointer is per-thread global, not per-machine. Anything else running on
the same thread between the install and the clear can see it, and a machine driven from inside
another machine's action finds it already replaced — one machine per thread, with nothing but the
event in between, is part of the pattern rather than a detail of the examples. FSMgine's README
states the same reading from the library's side, under "Thread safety".

**The alternative:** a context parameter in the action type
(`void (*)(void* context, const Event&)`, the shape libuv's callbacks use), which keeps the
row a literal type but threads the caller's state through the back end explicitly. The
interpreted back end already offers the other answer: `std::function` actions can capture,
at the cost of the table's shape on the hot path.

## Q12 — Must a `kind` name be declared before the row that uses it?

Named event kinds are the one addition to the reader (NAMED_KINDS.md). The declaration could be
resolved in a second pass, the way `machine` and `initial` may appear after the transitions (Q2),
which would let a file declare its kinds at the bottom.

**Reading taken:** the declaration comes first. One pass, so that the row that is wrong is the row
that gets the line number — ``unknown kind name 'tick'`` pointing at line 4 is the message that
fixes the file, and a two-pass reader would have to keep a list of pending uses only to report that
same line at the end. The cost is one line of ordering in a file whose kinds are naturally written
at the top anyway.

**The alternative:** resolve in a second pass and report the use's line, matching Q2's ordering
freedom. It costs the pending-use list and buys nothing that a declarations block at the top does
not already give.

## Q13 — Does a transition that stays in its state run the entry and exit clauses?

Version 1 has no internal-transition syntax: the only way for a row to leave the machine where it
is is to name the same state as `from` and `to` — `Entry --clear--> Entry`. Entry and exit actions
make that row's meaning a real question.

**Reading taken:** it is an external transition. The state is left and entered again, so the exit
clause runs, then the row's action, then the entry clause. The row does name a target, and reading
it as "stay, and run nothing extra" would require knowing that the two names are equal — a special
case invented for this question alone. It is also what a reader of the file will expect, and the
calculator depends on it: its `Entry --clear--> Entry` row is what runs `clear_all` at the start of
a line.

**The alternative:** treat a self-named row as internal, so neither clause runs, matching the UML
distinction between an internal and an external transition. It is defensible statechart semantics,
but the format has no way to *ask* for the external one, so a machine that needs the reset on every
`clear` — the calculator does — could not be written.

## Q14 — Does the initial state's entry clause run when the machine is created?

`make<M>()` builds a machine and calls `setInitialState`, which is not a transition: the back end
runs no action for it. So the initial state is entered, in the ordinary sense of the word, without
its entry clause firing.

**Reading taken:** nothing calls it. The generator emits the clause as an ordinary function —
`enterInitial<M>()`, taking a placeholder event — and leaves the call to the caller, where the
side effect is visible: a factory that fired an action would be a side effect hidden in a
constructor, which is the kind of surprise SPEC.md section 0 asks to keep out. A machine whose
initial state needs setup calls it once after `make<M>()`; one that does not never sees it.

**The alternative:** call it inside `make<M>()`, which is what a statechart does when an object
enters its initial state. It reads well until a caller creates a machine per line, per request or
per test fixture and gets an unexpected action per construction — and the output would then depend
on something the `.fsm` does not say.
