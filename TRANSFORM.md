# fsmtable-transform — the text → text direction

`fsmtable-transform` reads one `.fsm` file and writes one `.fsm` file: the same version 1 format in,
the same version 1 format out, with names substituted. Its first verb is `rename`. It is the tool
beside the library's [rename](src/fsmtable.hpp) the way `fsmtable-gen` is the tool beside the
parser — and it is the first thing in this repository that writes a machine back as *text* rather
than as code ([GENERATOR.md](GENERATOR.md) is the other direction).

```
fsmtable-transform rename <file.fsm> <rename>...
fsmtable-transform --help

A rename is <target>:<old>=<new>, one of
  machine:TrafficLight=Lamp   the machine's own name
  state:Red=Green             a state, wherever the file names one
  kind:tick=beat              a kind's NAME — a row that writes the number keeps it
```

The output goes to stdout. `> f.fsm` therefore truncates the file before the tool reads it — the
same shell trap `fsmtable-inspect --canonical` documents; the safe shape is
`> f.new && mv f.new f.fsm` ([examples/inspector/README.md](examples/inspector/README.md)).

## Why a surgical edit, and not `dump`

`fsmtable::dump` is the canonical writer, and the canonical form has no comments in it. The
machines in this fleet carry comments that are load-bearing — they are what made the cold-agent
experiment work — so a rename that went `parse` → edit the machine → `dump` would answer the right
question about the machine and throw away the file's argument for it. A rename is instead a text
edit: `parse` says where the names are, and only those bytes move.

## What moves, and what must not

| the file's line | what a rename may touch |
| --- | --- |
| `machine <name>` | the name |
| `initial <state>` | the state |
| `state <name> entry … exit …` | the name — **not** the entry and exit action names |
| `kind <name> = <n>` | the name — **not** the number |
| `transition <from> --<kind>--> <to> …` | `from`, `to`, and the NAME inside the arrow — **not** a row that spells the kind as a number, and **not** the row's `action` |
| anything else | nothing |

Every comment line, every blank line, every directive the list does not reach, and the spacing
inside a line it does reach come back byte for byte — tabs, doubled spaces and all. The one site
missing from that table's story is a `state` line: renaming a decorated state rewrites the state
name on it, because leaving it alone would write a file that does not parse
([QUESTIONS.md](QUESTIONS.md) Q16).

## Real output, on this repository's own machines

```
$ ./build/fsmtable-transform rename corpus/guarded.fsm state:C=Gamma
# every op used at least once
version 1
machine Guarded
initial A
transition A --1--> B when eq 0
transition B --2--> Gamma when lt 10
transition Gamma --3--> D when le 10
transition D --4--> A when gt -5
transition A --5--> Gamma when ge 30

$ ./build/fsmtable-transform rename tests/fixtures/named_kinds.fsm kind:tick=beat
version 1
machine Named
initial Idle
kind beat = 1
kind stop = 2
kind reset = 7
transition Idle --beat--> Running action on_tick
transition Running --beat--> Running when ge 3
transition Running --stop--> Idle
transition Idle --3--> Idle when ge 5

$ ./build/fsmtable-transform rename examples/inspector/dirty.fsm state:Orphan=Stray
# A deliberately imperfect machine, so the inspector's own documentation can show what it reports.
#
#   Orphan   nothing can reach it — it is not the initial state and no row leads to it. That is
#            always a mistake, and `fsmtable-inspect` exits non-zero for it.
#   Done     nothing leaves it — a sink state. That is sometimes exactly what you want (a refused
#            connection, a terminal error), so it is reported and does not fail the run.
#   Running --bump-->  guarded, and the only row for that pair: when the clause is false nothing
#            else matches, so the event comes back unhandled rather than being ignored on purpose.
#            Also reported, and also not a failure — the shape is legal, it is just worth knowing.
#
# `Orphan` has an outgoing row and no incoming one; `Done` has an incoming row and no outgoing one.
# All three are one line away from being correct, which is the point.
version 1
machine Dirty
initial Start

kind step = 1
kind stop = 2
kind bump = 3

transition Start --step--> Running
transition Running --stop--> Done
transition Running --bump--> Running when lt 3 action note_bump
transition Stray --step--> Start

$ ./build/fsmtable-transform rename corpus/guarded.fsm state:B=C
corpus/guarded.fsm: renaming the state 'B' to 'C' collides with the state 'C'
```

Read the second command for the rule that needs care: `kind:tick=beat` moves the declaration and
both rows that spell the kind by NAME, and the fourth row — which spells the same kind as `--3-->`
— is untouched. Read the third for the limit: the row moved to `Stray`, and the comment above it
still explains `Orphan`, because a comment is prose and not a directive. That is the honest edge of
this tool, below.

The block above is compared line for line by `tools/check-doc-claims.sh` on every gate run: lines
beginning `$ ` are the commands it runs, and every other line is what those commands must print.
The last command exits 1, which the block does not pin — the exit codes are pinned by ctest cases
in `CMakeLists.txt` instead.

## The rules

* **The parse tree is the judge.** The file is parsed first, and a file that does not parse is
  refused before anything is written. Which field of a row holds a name — and whether an arrow's
  slot is a name or a number — is what the parser decided, not what the text looks like.
* **The list applies at once.** `state:A=B state:B=A` is a swap, not a chase: the result of the
  whole list is computed, and only then is the text edited.
* **A collision is refused, never merged.** A new name that another name already holds is an error
  that names both — unless that other name is itself being renamed away, which is what makes the
  swap legal. The test is over the names the list *produces*, so `state:A=B` alone is refused when
  a `B` survives, and `state:A=B state:B=A` is not.
* **The namespaces are separate.** A state may take a name a kind has, and the machine's own name
  is its own namespace: nothing here treats a state name and a kind name as the same string.
* **The output is proved, not assumed.** After the edit the tool parses its own output and compares
  it with the machine the input was, names substituted, kind names included. A rewrite that reached
  the wrong field fails there rather than in a file. It costs one extra parse of a file that was
  just parsed — small enough to be free, and the alternative is a class of bug (`Open` renamed
  inside `Opening`, an action called `open`) that a review would have to catch by eye.

## Exit codes

| code | meaning |
| --- | --- |
| 0 | the renamed machine was written |
| 1 | the rename could not be made: an old name the file does not have, or a new name another name already holds |
| 2 | the command line is wrong, or the file could not be read or parsed |

The 1/2 split is the reason the tool parses the file itself before calling the library: a file that
does not parse is the caller's (2), a rename that cannot be made is the rename's (1). It is the one
place this tool's numbers differ from `fsmtable-gen`'s, which exits 1 for an input it cannot use.

## The library calls behind it

```
fsmtable::parse(text, error, kind_names)          the reader, to know the machine and its names
fsmtable::parse_rename_spec(spec, rename, message)  one `state:Old=New` off a command line
fsmtable::rename(text, renames, error)            the rewrite, proved by re-parsing its own output
```

`SPEC.md` section 4's block is frozen and `rename` is an addition beside it, in the shape
[NAMED_KINDS.md](NAMED_KINDS.md) and [ENTRY_EXIT.md](ENTRY_EXIT.md) took: the frozen `parse` and
`dump` signatures are unchanged, and the output is version 1 text.

## Limits, stated rather than discovered

* **A comment is prose, and this tool does not rewrite prose.** Rename a state and any comment that
  names it keeps the old spelling — the third command above is exactly that. The alternative was to
  rename the word inside comments too, which would corrupt every comment that uses the word in its
  ordinary sense ("open" the door, "stop" the loop, a sentence about `Done`), and it would need a
  parser for English. A machine whose comments name its states is worth re-reading after a rename;
  nothing here can do that for you.
* **Actions are not renamed.** This card renames `machine`, `state` and `kind` names. An action that
  happens to share a spelling with a state keeps its own name, which is what stops `action open`
  from moving when the state `open` does.
* **A kind's number never moves.** Renaming a kind moves the name; a row that wrote `--3-->` still
  writes `--3-->`, because the number was never a name.
* **There is no `merge` yet.** One machine in, one machine out. Composing two machines is the next
  verb, and it is not here.
* **The edit is textual, so a v2 directive is a change here too.** A new line kind that carries a
  name would need a case in `edit_line` — the cost of not going through `dump`, and the reason the
  file's own comment says so.

## How to adapt it

**Rename and regenerate, in the order that keeps a committed artifact honest** (the two-step edit
[GENERATOR.md](GENERATOR.md) describes for a consumer repository):

```sh
fsmtable-transform rename my_machine.fsm state:Idle=Waiting > my_machine.new && mv my_machine.new my_machine.fsm
fsmtable-gen my_machine.fsm -o fsm_my_machine.hpp      # and --target deno for the module
```

The `.fsm` is the source of truth and the artifact says `DO NOT EDIT`, so both change in one commit:
a rename that moved only one of them is exactly what a consumer's drift probe exists to catch.

**Rename across a set of files** — one invocation per file, because each output is one machine:

```sh
for f in $(git ls-files 'machines/*.fsm'); do
    if fsmtable-transform rename "$f" state:Idle=Waiting > "$f.new"; then
        mv "$f.new" "$f"
    else
        rm -f "$f.new"      # this file does not have that state: refused, and left alone
    fi
done
```

A name that only some of the files have is not a problem: the ones that do not have it exit 1 —
that is the answer a caller wants — so the loop reads the exit code per file rather than putting
`set -e` over a batch where a refusal is expected.

**Add the verb you need.** `rename` is the first of them; the shape to follow is a function taking
the text and a list, returning the rewritten text or an error with `line == 0`
([src/fsmtable.hpp](src/fsmtable.hpp)) and a command that reads one file and writes one file, the
way this one does. `tests/transform_test.cpp` is where its tests would go, and the two properties to
hold are the two this verb is held to: the output parses to the machine the input was with the
change asked for, and every byte the change does not reach is unchanged.
