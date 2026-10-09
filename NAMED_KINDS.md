# Named event kinds — an addition to the frozen format

SPEC.md section 2 is frozen and says a row's arrow slot holds a decimal integer, `0` to `255`.
This file documents what was added to the *reader* without changing that: a name for a number,
declared in the file, so that a row reads as `Idle --tick--> Running` instead of
`Idle --1--> Running`.

## The syntax

```
kind <name> = <number>
```

- One directive per line, like every other directive.
- `<name>`: `[A-Za-z_][A-Za-z0-9_]*`, at most 64 characters — the same pattern as a state or an
  action name, which is also what keeps a name from ever being mistakable for a number.
- `<number>`: `0` to `255`, the range the frozen format gives a kind.
- The declaration must come **before** the row that uses the name. One pass, so that the row that
  is wrong is the row that gets the line number (QUESTIONS.md Q12).
- One name per number, and one number per name. Two names for one number would make `dump`
  ambiguous, which is the only reason for the second half of the rule.

A row then accepts either spelling, and they are the same row (a comment is its own line — the
format has no trailing comments, so the notes go above the row):

```
# the declared name
transition Idle --tick--> Running
# the number it stands for
transition Idle --1--> Running
```

`-- 1 -->`, `--1x-->`, `--+1-->` and an empty slot are still `malformed kind token`: rule 4 is not
relaxed, a name has to be a name.

## What it changes, and what it deliberately does not

Unchanged:

- **`Machine` is the same machine either way.** A name is resolved while parsing and the result
  does not record which spelling was used, so a caller who does not care about names sees no
  difference at all.
- **`parse(text, error)`** — the frozen two-argument form — still parses a file that declares
  names; it simply has nowhere to put them.
- **`dump(machine)`** writes numbers, as it always did, byte for byte.

Added, below the frozen block, in the shape stage B used:

- **`parse(text, error, kind_names)`** reports the declarations in file order (like
  `Machine::states`: the file's order, not sorted). It is cleared on entry, so a refused file
  reports no names — the contract is that a failure produces nothing partial.
- **`dump(machine, kind_names)`** writes the declarations ascending by kind — canonical, so two
  files that declare the same names in different orders dump identically — and then uses a name
  wherever one is declared. A declared name no row uses is still written: it was declared, so it
  is part of the file, and dropping it would make the round trip lossy.
  `dump(parse(text, error, names), names)` parses back to the same machine and the same names as a
  set.
- **The generator** emits the name as the enumerator — `CalculatorKind::add`, not
  `CalculatorKind::k2` — and adds `<Machine>KindNames` plus `<Machine>KindNameOf(kind)`, so code
  can write a name back into text. Only kinds the file named appear in that table: an undeclared
  kind has no name to give back, and inventing `k3` there would make a code → text round trip
  write a name the format would then reject.
- **The generator's fingerprint** covers the *named* canonical form, so renaming a kind changes it.
  A file that declares no names hashes exactly what it always hashed, because the two dumps are
  the same text.

This is the split the exercise was for, in Harri's words: **strings in on the input side, symbols
out on the output side, with the string kept beside the symbol** so that code → text stays
possible.

## Why this is inside version 1

`version 2` is rejected by rule 1, and a frozen test holds that (`RejectsWrongVersionNumber`). The
declarations are a compatible relaxation for a reader that knows about them: a v1 file with no
declarations parses identically, byte for byte, which the frozen corpus, the frozen tests and
their dump oracle all check. So this is an extension to version 1's reader rather than a second
format. If the format ever needs to *break* a v1 file, that is the point at which version 2 has to
be argued for and the frozen rule reopened.

## Not in this change

- **Names on the refinement value.** `when eq 0` compares the event's value; that is a comparison,
  not a kind, and naming those is a different feature with a different owner (QUESTIONS.md Q10 is
  the neighbouring question — whether a row can branch on accumulated data at all).
- **A name used before its declaration** — Q12.
- **Automatic numbering** (`kind tick` with the number implied by position) — refused on purpose:
  reordering the declarations would silently renumber the machine, which is exactly the class of
  change this exercise exists to make visible.
