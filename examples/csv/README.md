# csv — an RFC 4180 reader, and the rows that carry the actions

A comma-separated-field reader written as a four-state table, with the driver that decides what a
byte *is*. `csv.fsm` is the machine; `csv.cpp` classifies each byte into one of four kinds and
carries the byte itself on the event, so one `append` action serves every row that collects a
character. This is the worked example for **actions tied to rows**: fifteen rows share three
actions between them, and the output — a `Cell:` line per cell and a `NEW LINE` per record — is
what those rows did.

```
   FieldStart --data--> Unquoted --comma--> FieldStart          a bare field
       |  ^                 |
       |  +-----------------+--newline--> FieldStart            the record ends

   FieldStart --quote--> Quoted --quote--> QuoteSeen --quote--> Quoted     the "" escape
                            ^  |               |
                            |  |               +--comma / --newline--> FieldStart
                            |  +--data / --comma / --newline--> Quoted    all data
                            +-- (no row leaves QuoteSeen on a data byte)
```

The asymmetry in that picture is the whole point of the machine: inside `Quoted`, a comma and a
line end are **data** — the reason quoting exists at all — and the same two byte kinds end things
in every other state. One table, and the difference between `a,b` and `"a,b"` falls out of which
state the byte arrived in.

## The three layers

| what | where | who owns it |
| --- | --- | --- |
| the table | `csv.fsm` | **you.** The only file that decides anything. |
| the machine | `build/generated/fsm_csv.hpp` | **the generator.** Never edit it; it is rebuilt on every build. |
| the driver | `csv.cpp`, `main.cpp` | **you.** What each byte *is*, that CRLF is one line end, and what a record left open at end of input means. |

`csv.hpp` is the seam between the last two: the driver's whole interface to the machine.

## Reading `csv.fsm` as sentences

```
transition Unquoted --comma--> FieldStart action end_cell
```

*In `Unquoted`, a comma goes to `FieldStart` and runs `end_cell`.* One line, one sentence — and
the state it moves to **is** the answer to "what am I in the middle of": a field just begun, a bare
field, a quoted one, or the instant after a quote.

The four states, in the file's own words:

- **`FieldStart`** — a field is about to begin. A quote opens a quoted one; a comma ends an *empty*
  one in place (`FieldStart` to `FieldStart`, one `end_cell`); a line end ends an empty last field
  and the whole record; anything else is the field's first byte and moves to `Unquoted`.
- **`Unquoted`** — a bare field. A comma or a line end ends the field; any other byte, a quote
  included, is appended. (`Unquoted --quote--> Unquoted action append` — see the readings below.)
- **`Quoted`** — a quoted field. **Every** byte kind is appended here, the comma and the line end
  included, which is precisely what the opening quote bought. A quote may be the field's end or the
  first half of an escaped quote, so it is `QuoteSeen`'s to decide.
- **`QuoteSeen`** — the instant after a quote. Another quote is the `""` escape and is appended as one
  quote; a comma or a line end closes the field. A data byte has **no row** here at all, and that
  absence is the error handling (below).

The fifteen rows fall into four groups, and the grouping is the tie this example exists to show:

| rows | action | what it does |
| --- | --- | --- |
| `FieldStart --data-->`, the three inside `Quoted` (`--data-->`, `--comma-->`, `--newline-->`), the two inside `Unquoted` (`--data-->`, `--quote-->`), and `QuoteSeen --quote-->` — seven rows | `append` | pushes the byte the event carried onto the field being assembled |
| `FieldStart`, `Unquoted` and `QuoteSeen` on `--comma-->` — three rows | `end_cell` | closes the field and prints `Cell: <the bytes>` |
| the same three states on `--newline-->` — three rows | `end_record` | closes the field **and** the record, and prints `NEW LINE` |
| `FieldStart --quote-->` and `Quoted --quote-->` — two rows | *(none)* | a move, and a move is all they are |

Seven rows append, three end a cell, three end a record, and two are moves with no action at all.
Nothing in the table is guarded: no row compares a value, so the four kinds and the four states are
the whole of what the machine can say.

## The machine, walked without the driver

`fsmtable-inspect --trace` walks the table over a flat event list. The events below are the
classification `csv.cpp` would produce for the bytes `"a,b",c` followed by a line end — `data=97` is
the byte `a`, a comma inside the quotes is still the `comma` kind — and the trace names the action
each row carries:

```
$ ./build/fsmtable-inspect --trace examples/csv/csv.fsm quote data=97 comma data=98 quote comma data=99 newline
examples/csv/csv.fsm: Csv — trace of 8 event(s) from FieldStart
  FieldStart --quote--> Quoted
  Quoted --data--> Quoted  (action append)
  Quoted --comma--> Quoted  (action append)
  Quoted --data--> Quoted  (action append)
  Quoted --quote--> QuoteSeen
  QuoteSeen --comma--> FieldStart  (action end_cell)
  FieldStart --data--> Unquoted  (action append)
  Unquoted --newline--> FieldStart  (action end_record)
```

Read the `Quoted --comma--> Quoted` line against the `QuoteSeen --comma--> FieldStart` one: the same
byte kind, two different rows, and the state is the whole difference between "the comma is data" and
"the comma ends the field". (The machine has a third `--comma-->` row, `FieldStart --comma-->`, which
this walk never visits — a comma where no field has begun yet.)

A trace is not a driver — no bytes are classified, no actions run — which is exactly why it is the
honest way to show a row: it shows the row, and nothing else.

## The recorded run, line for line

`input/session.csv` is a nasty file on purpose — a quoted comma, a quoted line end, the `""`
escape, an empty field, a quote in an unquoted field, and a quoted empty field:

```
name,note,price
widget,"a,b",1.50
gadget,"line
two",2.00
"quoted",,"x""y"
a"b,plain
"",last
```

The recorded output is `input/session.expected`, compared line for line by the `csv_session` ctest
case:

```
Cell: name
Cell: note
Cell: price
NEW LINE
Cell: widget
Cell: a,b
Cell: 1.50
NEW LINE
Cell: gadget
Cell: line
two
Cell: 2.00
NEW LINE
Cell: quoted
Cell: 
Cell: x"y
NEW LINE
Cell: a"b
Cell: plain
NEW LINE
Cell: 
Cell: last
NEW LINE
```

Five things in it are the machine working, and each is worth naming:

- `Cell: a,b` is the quoted comma: the comma was `append`ed because the field was `Quoted`, and the
  `Cell:` line is the `end_cell` the `QuoteSeen --comma-->` row ran.
- The line that breaks after `Cell: gadget` is a cell that **contains a newline byte**.
  `"line\ntwo"` is one field, the line end inside the quotes was appended like any other byte, and
  the trace prints the bytes rather than a rendering of them — so the cell really does span two
  lines. That is the plainest proof that quoting changed what a newline meant, and it is also why
  this example's recorded output is compared byte for byte rather than line by line.
- `Cell: x"y` is the `""` escape: `QuoteSeen --quote--> Quoted action append` put **one** quote on
  the field, so the two quotes in `"x""y"` became one in the text.
- `Cell: a"b` is the tolerated quote — a `"` inside a bare field, appended rather than refused
  (readings below).
- The two `Cell: ` lines (empty, with a trailing space) are the empty fields: `,,` in the fourth
  record and the leading `""` in the last. `end_cell` printed the empty string as it found it.

## The refusal, end to end

The one row missing on purpose is `QuoteSeen --data-->`. A byte after a closing quote is not RFC
4180, and this machine has nothing to say about it: the format's refusal — no row — **is** the error
handling (QUESTIONS.md Q23). `input/malformed.csv` is the smallest file that reaches it:

```
ok,row
"ab"x,bad
```

The recorded output is `input/malformed.expected`, the message and all:

```
Cell: ok
Cell: row
NEW LINE
! byte 12: QuoteSeen has no row for a data byte
```

The run stops at the byte (`x`, the twelfth byte of the stream), reports it, and exits **1** — the
exit code is the part a script would use. Nothing is emitted for the record that never ended, and
the machine is left in `QuoteSeen`: a byte that matched no row changed nothing. The last part is
pinned by a gtest case, because it is the property that makes a refusal safe to report rather than a
half-applied change to undo.

The alternative was a `QuoteSeen --data--> QuoteSeen` row that absorbed the byte, and it is what many
lenient readers do. This example does not, for one reason: the format is total — any byte string is
either a valid record set or an error with a position — and a row that swallowed malformed input
would make the second half untrue without anyone noticing.

## Who owns what, in this machine's own terms

**The machine decides** what sequence of bytes is legal: that a quote opens a quoted field, that a
comma and a line end are data inside one and delimiters outside it, that `""` is one quote, and that
a byte after a closing quote is nothing this reader understands.

**The driver owns** everything the format cannot hold:

- **what a byte is.** The machine has four kinds and no notion of `'a'` or `'7'`; the driver reads a
  byte, decides which of the four it is, and carries the byte itself on the event. That is the same
  trick the calculator uses for an operand and the protocol for a timeout: the format's one
  refinement slot is a value, and here the value is the data.
- **the CRLF collapse.** RFC 4180 ends a record with the two bytes CRLF, and this machine has one
  `newline` kind. The driver reads `\r` and, if `\n` follows, consumes both and emits one `newline`
  event. The collapse is the driver's, like the clock in `protocol.fsm`: documented rather than
  hidden, and stated as a reading below.
- **the flush at end of input.** A file whose last record has no trailing line end, and a quoted
  field whose closing quote never arrived, are both facts about the *stream* rather than about a
  byte. There is no `eof` kind and no row for one, so the driver decides: a record with no trailing
  line end is closed and reported like any other, and a quoted field left open is refused.
- **the text.** `Cell: <bytes>` and `NEW LINE` are the driver's words. The machine's own vocabulary
  is state and row names; printing *those* would show the table rather than drive it.

## Decisions that look surprising in hindsight

- **A line end and a comma each carry one action, and `end_record` does two things.** The design
  brief had `action end_cell, end_record` on every `--newline-->` row, and the frozen format cannot
  write it: a row has exactly one `action` slot (SPEC.md section 2), the generator rejects
  `action a, b` with a line number, and the back end's own `action()` refuses a second call. So the
  reading taken is that the newline row's single action closes the field **and** the record — the
  arithmetic that was wanted, folded into the one name the format allowed (QUESTIONS.md Q24). The
  output is identical either way; what the format cannot express is the *decomposition*.
- **An empty cell prints as `Cell: ` with nothing after the space.** That is what `Cell: <the
  bytes>` means when the bytes are none, and it is deliberately not special-cased: a reader can see
  the empty field, and a trailing space is the honest rendering of one. A rendering that instead
  quoted or marked empty cells would hide the very fact the row just decided.
- **A lone `\r`, and everything else, is data.** Only the *pair* CRLF is the driver's rule; a `\r`
  that no `\n` follows is an ordinary byte, appended like any other. The alternative — a bare `\r`
  ending a record — is the older Mac line ending, and adopting it would mean a record ending on a
  byte RFC 4180 does not call a terminator (QUESTIONS.md Q21).
- **A quote inside a bare field is absorbed, not refused.** RFC 4180 forbids it; this reader appends
  it and moves on. It is the one place the machine is lenient rather than total, and it is a reading
  taken rather than a rule given (QUESTIONS.md Q22). The interesting part is that it is *one row*:
  refusing it instead is deleting a row, not adding machinery.
- **The machine is a flat four-state table, not a two-level reader.** The classic implementation of
  this reader is a per-character scan with a `bool in_quotes`; the table says the same thing in four
  states and four kinds, and adds one thing the flag cannot: the `QuoteSeen` state, which is what
  makes `""` and a closing quote the same input under two different states.

## The tests

- `csv_test.cpp` — sixteen cases: a plain record, an empty field, a quoted comma, a quoted line end,
  the `""` escape, a tolerated quote, an empty quoted field, a CRLF record end, a lone `\r`, a last
  record with no trailing line end, a quoted field at end of input, empty input, both refusals (a
  byte after a closing quote, and end of input inside one), that a refused byte leaves the machine
  where it was, and that a record leaves it in `FieldStart`.
- `csv_session` (ctest) — `input/session.csv` through the built binary, diffed byte for byte against
  `input/session.expected`, with the exit code pinned beside the bytes.
- `csv_refuses_a_byte_after_a_closing_quote` (ctest) — the refusal above, message and exit code 1
  checked together in the shell.
- `csv_trace_names_the_actions` (ctest) — the trace above, pinning that a row's action is named in it.
- `fsmtable-inspect examples/csv/csv.fsm` — exits 0, reporting no sink and no unreachable state. It is
  not a case of its own, because `tools/check-doc-claims.sh`'s tree check inspects every `.fsm` under
  `examples/` on every gate run, and this file is one of them.

## Build your own from this

1. **Write down what a byte can be.** The kinds are the driver's vocabulary: here it is `data`,
   `comma`, `quote` and `newline`, and one of them — `data` — carries the byte itself on the event.
   If your reader has a value-carrying kind, say which it is before writing rows.
2. **Write down the states as "what am I in the middle of".** Not "what happens next": the four here
   are *a field about to begin*, *inside a bare field*, *inside a quoted one*, and *just after a
   quote*. If you can name the situation, the rows write themselves.
3. **Write the rows as sentences**, one per legal byte kind per state: *in `<state>`, `<kind>` goes to
   `<state>` and runs `<action>`.* Where a byte is not legal — a data byte after a closing quote —
   write nothing, and let the driver report it. The absence is the error handling.
4. **Name the actions by what they do to the driver's data**, not by where they are: `append` is
   shared by rows in all four states, and `end_cell` and `end_record` by rows in three states apiece,
   which is what a small action vocabulary buys.
5. **Write the driver**: what a byte is, anything the format cannot see (a two-byte line ending, the
   end of the stream), and what happens when `process` returns false.
6. **Wire it into CMake** the way `CMakeLists.txt` does for this example: one `add_custom_command`
   that runs `fsmtable-gen`, one library, one executable, one test binary — plus its own recorded
   session file, fed through the binary and diffed.

### Worked adaptation: a lone carriage return ends a record

The reading taken above is that a `\r` with no `\n` after it is data. The other reading — the older
Mac line ending — is four rows and one new kind, because a byte kind that ends a record has to say
so in every state that can see one:

```fsm
kind cr = 5
transition FieldStart --cr--> FieldStart action end_record
transition Unquoted --cr--> FieldStart action end_record
transition Quoted --cr--> Quoted action append
transition QuoteSeen --cr--> FieldStart action end_record
```

Appended to `csv.fsm`, the machine is **4 states and 19 rows** (from 4 and 15), the states and their
reachability are unchanged, and the driver would have to classify a lone `\r` as `cr` rather than as
`data` — a one-line change in `csv.cpp` for a four-row change in the table. The recipe is checked
rather than believed: on every gate run `tools/check-doc-claims.sh` appends exactly this block to the
real `csv.fsm`, requires the generator to accept it, and requires the counts to be the two written
here. Note the shape of the change: the `cr` row is `append` inside `Quoted` and `end_record`
everywhere else, which is the same asymmetry the four kinds already had — a new byte kind costs one
row per state, never a new mechanism.

## Where the rest is written down

    SPEC.md                         the frozen format and API (sections 2 and 4 are the frozen ones)
    GENERATOR.md                    what fsmtable-gen emits and why the table is a literal array
    NAMED_KINDS.md                  the `kind` declarations this file uses, and what a name buys
    examples/README.md              the examples, the recipe they share, and what to build next
    examples/inspector/README.md    the tool this example is read with: the summary, --trace, --graph
    examples/protocol/README.md     the other driver: a clock instead of a byte stream
    examples/calculator/README.md   the value-carrying kind this example copies (event.value)
