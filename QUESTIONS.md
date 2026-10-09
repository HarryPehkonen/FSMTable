# QUESTIONS.md — what the spec left open, and which reading I took

SPEC.md section 0: "If something is genuinely ambiguous, write the question into
QUESTIONS.md and take the simplest reading rather than guessing elaborately." These are
the only places I found a genuine choice to make — two in stage A (Q1, Q2), two in stage B
(Q3, Q4), three in stage C (Q5, Q6, Q7), two in the generator that follows them (Q8, Q9),
two in the calculator example (Q10, Q11), one in the named kinds addition (Q12), two in
the entry and exit addition (Q13, Q14), two in the rename transform that follows them
(Q15, Q16) and four in the merge transform that completes the pair (Q17–Q20). Each is a
candidate for the spec to settle in a later revision; none changes the frozen
API, the frozen test names or the row format.

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

## Q15 — Is a rename that takes a name the file already uses ever legal?

The rename transform (`TRANSFORM.md`) refuses a new name that another state (or kind) already
holds, rather than merging two names into one. That rule has to be read against a list, because the
command line may rename several names at once.

**Reading taken:** the test is over the names the WHOLE list *produces*, not over each rename in
turn. So `state:A=B` alone is refused when a `B` survives to be collided with, and
`state:A=B state:B=A` is legal: the two renames swap the names, and the result holds each exactly
once. The list applies at once — a chase, applying `A -> B` and then looking up `B`, is not a thing
this tool does or could do safely, since the second rename would move the first one's result.

**The alternative:** test each rename against the names the file has now. Every swap is then
refused, and a caller who wants to exchange two names has to invent a temporary name and make three
edits — a worse answer to a question the format does not force.

## Q16 — Is a `state <name> entry …` line a site a rename rewrites?

SPEC.md section 2's row format has four directives that carry a state name: `initial`, a row's
`from` and `to`, and a `state` line's own first field, which decorates a state with entry and exit
clauses (ENTRY_EXIT.md). A rename has to take all four or write a file that is wrong.

**Reading taken:** yes, the `state` line is a rename site, and only its first field. The entry and
exit action names further along the same line are NOT renamed: an action is not a state, and a
state that shares a spelling with an action must not drag it. This is the one site the card's own
list of sites did not name, and the reason it cannot be left out is rule 1's: the rewritten file
has to parse, and a decoration whose state name no longer exists is refused by the parser (a
misspelling rule 7's pattern cannot catch).

**The alternative:** refuse to rename a decorated state at all — "rename it by hand" — which keeps
the site list short at the cost of refusing a legal machine, and of making a rename's success depend
on something the caller did not ask about (whether someone wrote a `state` line elsewhere in the
file).

## Q17 — What line number does a merge refusal carry, when its two inputs are separate files?

The merge transform (`TRANSFORM.md`) composes two machines, and the card that specified it asks a
kind collision to be reported "naming both lines". `Error` carries one `line`, and the parse tree has
none to give: the parser resolves names while parsing and does not keep where they were written
(NAMED_KINDS.md's "resolved while parsing" is the same fact, read the other way).

**Reading taken:** `line == 0` for every refusal the merge itself makes, and the message names the two
DECLARATIONS rather than two line numbers — `kind 1 is named 'step' by one machine and 'tick' by the
other`, `the name 'tick' stands for kind 1 in one machine and kind 2 in the other`, `the two machines
both have an unguarded row for state 'X' and kind 1`. Those name exactly the two things a caller has
to go and change, which is what "both lines" was for. The one refusal that *does* carry a line is an
input that does not parse, and there the tool prints `parse`'s own line against the path it belongs
to — which is why the tool parses both files itself before calling the library.

**The alternative:** thread positions through the reader (a new `parse` that records where each
declaration was, or an error type holding two `file:line` pairs) so a collision could point at both
lines directly. It is the better message and the more expensive one: a second reader entry point, a
second place for the parser to keep positions, and a merge error that knows about file names — a
concept the library does not have and the tool does.

## Q18 — Is a union that leaves a state unreachable a refusal, or a machine with a finding?

The card asks the question and answers half of it in its own words ("print it as a finding, still
exit 0? NO: exit 1"), leaving the part that matters open: whether the machine is written anyway. A
merge of two machines that share no state name is disconnected by construction — everything after the
seam is unreachable — so this is not a rare case, it is the ordinary case for two unrelated machines.

**Reading taken:** a refusal. Nothing is written, the exit code is 1, and the finding names the states
(`the merge leaves 3 unreachable state(s) — no path from the merged initial 'A': Green, Yellow, Red`).
The card phrases its requirement on the output — the result "must parse and reach every state from the
initial" — and a requirement the output cannot meet is a reason not to produce it.

**The alternative:** write the union and report the finding, the way `fsmtable-inspect` reads an
unreachable state as a property of a machine rather than a failure of the run. It is defensible, and
it is what the inspector's convention literally does. What decided it here is the difference between
the two tools: an inspector reads a file that exists, and a merge creates one. A new file that is a
machine with a state nobody can reach is a worse thing to leave on disk than no file, and the caller
who wants the union anyway can add the state name that connects them and run it again.

**The consequence, recorded rather than discovered later:** merging unrelated machines always exits 1.
The seam is a state NAME the two machines share, so a composition that has no shared name is a
composition that does not meet — and inventing a cross-machine row to join them would be inventing
semantics the format has no way to write down (there is no `merge`-only directive, and a v2 one is
the format decision this is deliberately not making).

## Q19 — Whose identity does the merged machine take, and what is it called?

The card settles `initial` — "the merged machine takes A's initial" — and says nothing about
`machine`, which is the same kind of choice: both machines have a name and the union can only have
one. It also leaves one edge of the initial rule unstated: what happens when the two machines begin in
the same state, so that the second machine's initial is the merged initial rather than an ordinary
state.

**Reading taken:** the merged machine takes the first machine's name as well as its initial, and the
provenance header names both parents with their own machine names, so nothing is lost — the union's
own name says which machine it is, and the header says which two it came from. When the two initial
names are equal the state IS the merged initial, and nothing is dropped: the "former initial" wording
only describes a state that stopped being one.

**The alternative:** compose the name (`Login+Session`), which is a name no `.fsm` can write back —
`+` is not in rule 7's pattern — so the merged file could not be renamed, regenerated or parsed back
under the name it carries. The header is the honest place for the second identity.

## Q20 — Which clause wins when both machines decorate one state?

Entry and exit clauses (ENTRY_EXIT.md) belong to a state, and two machines may decorate the same
fused state differently, or one may decorate it and the other not. The card settles only the second
machine's initial-entry clause: it is dropped, with a warning.

**Reading taken:** fuse per state and per clause — the first machine's `entry` wins where both declare
one, and its `exit` likewise, and a clause only the second machine declares is carried — with the one
exception the card names: the entry clause of the second machine's former initial is dropped and the
drop is reported on stderr, because the merged machine is CREATED rather than entered (ENTRY_EXIT.md,
Q14) and that clause described a machine being built.

**The honest edge of it:** the drop is lossy in one direction, and it is worth saying so. If the
second machine had a row leading back into its own initial state, that row ran nothing at the start
and would run nothing at the end either — the clause is gone — while a row into the same state from
the first machine's side would have run it. The alternative was to carry the clause and have the
merged machine run an action the second machine never ran on entry, which is the kind of hidden side
effect Q14 exists to keep out; the card chose the drop, and this is the note that it was a choice
rather than a discovery. A test pins both readings (`tests/merge_test.cpp`,
`DropsTheSecondMachinesInitialEntryClauseAndWarns`,
`DoesNotDropTheSecondEntryClauseWhenTheTwoInitialsAreTheSameState`).

## Q21 — Is a lone carriage return a line end, or a byte of data?

RFC 4180 ends a record with CRLF, and the machine has one `newline` kind, so something has to decide
what those two bytes are — and what a `\r` with no `\n` after it is. The design brief named the
collapse and left the lone byte open.

**Reading taken:** CRLF is one `newline` event, consumed as two bytes by the driver; a `\r` that no `\n`
follows is **data**, appended to the field like any other byte and printed as itself. The rule is the
smallest one that covers the format: only the exact pair RFC 4180 calls a terminator is a terminator, and
every other byte is a byte.

**The alternative:** a lone `\r` also ends a record, which is the older Mac line ending and what some
lenient readers do. It is a defensible convenience, and it is a second way for a record to end that the
format being implemented does not have — a file with an embedded `\r` in an unquoted field would split
into records this reader was asked to read as one. The change is cheap in either direction, which is the
reason it is written down: `examples/csv/README.md`'s worked adaptation is exactly the four rows and one
`kind cr = 5` that adopt it, and `tools/check-doc-claims.sh` re-derives those rows' counts on every gate
run. A test pins the reading taken (`examples/csv/csv_test.cpp`, `ALoneCarriageReturnIsData`).

## Q22 — Is a quote inside an unquoted field refused, or absorbed?

`a"b` is not RFC 4180: a quote may open a quoted field or appear escaped inside one, and a bare field
may not contain one. The format is total — every byte string parses or is an error with a position — so
the reader has to say which this is.

**Reading taken:** absorbed. `Unquoted --quote--> Unquoted action append` appends the byte and stays in
the field, so `a"b` reads as the single cell `a"b`. This is the one place the CSV machine is lenient
rather than total, and it is taken deliberately: the byte is unambiguous in context — it cannot be
opening a quoted field, because the field is already unquoted, and it cannot be escaping anything,
because nothing is quoted — so refusing it would reject files whose meaning no reader disputes.

**The alternative:** refuse it, either by leaving `Unquoted --quote-->` out (the same treatment
`QuoteSeen --data-->` gets) or by routing it to a state with no exit. That is the stricter reading of the
format, and the cost is that a stray quote anywhere in a bare field — a real thing in hand-written and
exported files — loses the whole record. The shape of the trade is worth noticing: the tolerant reading
is one row and the strict one is an absence, so the difference between the two is a row rather than
machinery. `csv_test.cpp` (`AQuoteInsideAnUnquotedFieldIsAppended`) pins the reading taken.

## Q23 — What does the machine do with a data byte after a closing quote?

`"ab"x` is malformed RFC 4180: the closing quote ended the field, and `x` is neither a delimiter nor a
line end. The design brief said the machine should have no row for it and the driver should report it,
and left the consequences of that absence unstated.

**Reading taken:** no row, and the absence IS the error handling. `QuoteSeen --data-->` is left out on
purpose; the back end's `process` returns false, the state does not change, and the driver reports the
byte's position and stops. Nothing is emitted for the record, because a record that never ended has no
cells to print, and the run exits 1 — the part a script uses.

**The alternative:** a `QuoteSeen --data--> QuoteSeen` row that swallows the byte, which is what lenient
readers do and what makes `"ab"x` read as `abx`. It is rejected here because the format this library
enforces is total — "any byte string is either a valid machine or an error with a line number" (SPEC.md
section 1) — and a row that absorbed malformed input would make that claim false without anything
failing. Recording the position rather than the row is what keeps the refusal useful: the machine can
only say no, and saying *where* is the driver's job (the same division as Q10). The README shows the run;
`csv_test.cpp` (`DataAfterAClosingQuoteIsRefused`, `ARefusedByteLeavesTheMachineWhereItWas`) pins that
the machine was left where it was.

## Q24 — A row has one action slot; what does a line end's action do?

The design brief wrote the newline rows as `action end_cell, end_record`, because a line end ends the
last field and the record, and those are two things. The frozen format gives a row exactly one optional
`action` clause (SPEC.md section 2), and the generator rejects the second name with a line number
(`action end_cell, end_record` is `malformed action name 'end_cell,'`), so the brief's row cannot be
written as it stands.

**Reading taken:** the line end's single action, `end_record`, closes the pending field **and** the
record — it runs `end_cell`'s work, then prints `NEW LINE`. The output is identical to the two-action
form; what cannot be expressed is the decomposition into two named actions, and the format is right to
refuse to guess at it, because a second action slot is a language change and this is one example.

**The alternative:** a second action slot on a row (a v2 item), or a chain of states that composes the
two through an `entry` clause — `Unquoted --newline--> RecordEnd action end_cell` with `RecordEnd`'s
entry clause running `end_record`. The state form is legal in the frozen format and would name both
actions, at the price of a fifth state that exists only to host one clause, and of the question of what
leaves it (nothing does: a `RecordEnd` would be a dead name). The reading taken is the smaller machine,
and the pair of names is preserved where it matters — `end_cell` for the comma's three rows, `end_record`
for the line end's three — so the "many rows, few actions" shape survives the format's one slot intact.

## Q25 — What ends a record at end of input, when the table has no such event?

A file whose last record has no trailing line end, and a quoted field whose closing quote never arrived,
are both facts about the byte stream as a whole. The machine has four byte kinds and no `eof`, so neither
case reaches a row and both are the driver's to decide.

**Reading taken:** two rules, and they differ. A record with no trailing line end is **closed and
reported** — the driver runs `end_record`, so `a,b` with no final newline yields the same `Cell:` and
`NEW LINE` lines as `a,b\n`. A quoted field left open at end of input is **refused**: `end of input
inside a quoted field`, exit 1, nothing emitted, because the closing quote the format requires never came
and the driver can see the machine is still in `Quoted`. A quoted field whose closing quote *did* arrive
(the state `QuoteSeen`) is whole, and is closed like any other last record.

**The alternative:** treat end of input as no event at all and emit nothing for a record left open, which
silently drops the last line of every file that lacks a trailing newline — the commonest CSV in the
world. Or refuse both cases, which would make an unterminated *unquoted* last field an error the format
never called one. The division taken is the same one the protocol example draws around its clock
(`examples/protocol/README.md`): the table decides legality byte by byte, and everything that is true of
the stream rather than of a byte is the driver's, named in one place and tested (`csv_test.cpp`,
`TheLastRecordNeedsNoTrailingLineEnd`, `AQuotedFieldAtEndOfInputIsClosed`,
`EndOfInputInsideAQuotedFieldIsRefused`).
