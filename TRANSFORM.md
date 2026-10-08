# fsmtable-transform — the text → text direction

`fsmtable-transform` reads `.fsm` files and writes a `.fsm`: the same version 1 format in, the same
version 1 format out. It has two verbs, and they differ in exactly one promise. `rename` rewrites
ONE machine's names and keeps every byte it does not reach. `merge` composes TWO machines into one,
and the result is a new machine — so what it writes is canonical text with a provenance header, not
the two inputs' bytes with the names moved. The tool is what sits beside the library's
[rename and merge](src/fsmtable.hpp) the way `fsmtable-gen` sits beside the parser, and it is the
first thing in this repository that writes a machine back as *text* rather than as code
([GENERATOR.md](GENERATOR.md) is the other direction).

```
fsmtable-transform rename <file.fsm> <rename>...
fsmtable-transform merge <a.fsm> <b.fsm> [-o <out.fsm>]
fsmtable-transform --help

A rename is <target>:<old>=<new>, one of
  machine:TrafficLight=Lamp   the machine's own name
  state:Red=Green             a state, wherever the file names one
  kind:tick=beat              a kind's NAME — a row that writes the number keeps it
```

`merge` writes to stdout like `rename`, and `-o <out.fsm>` writes the very same bytes to a file
instead. That option is the only one this tool has, and `rename` refuses it rather than ignoring it:
a rename's output belongs on stdout, which is what makes the `> f.new && mv f.new f.fsm` shape below
work.

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

## The merge verb — two machines composed into one

`merge` is the other direction the same file shape opened: two version 1 machines in, one version 1
machine out. What makes it a verb of its own rather than a flag on `rename` is that its output is a
NEW machine, and the composition policy has to be written down rather than inferred from the two
inputs:

* **A state name the two machines share is ONE state.** That shared name is the seam, and it is the
  whole point of composing: the first machine's `Done` is the second machine's `Initial`. Nothing
  here inspects what each machine meant by the name — you fuse because you mean it, and a
  composition where the two meant different things is a mistake this tool cannot see.
* **The first machine's name and `initial` win.** The second machine's initial becomes an ordinary
  state, and its former initial-entry clause is dropped with a warning on stderr: creation is not
  entering ([ENTRY_EXIT.md](ENTRY_EXIT.md)), and a merged machine is created. When both machines
  begin in the same state, that state *is* the merged initial and nothing is dropped.
* **The two files may not disagree about a kind.** One number named differently by the two, or one
  name standing for two numbers, is a refusal that names both declarations, because the union would
  hold two declarations for one thing. Renaming a kind out of the way first is exactly what `rename`
  is for.
* **The two files may not both give a `(from, kind)` pair a row of the same guardedness.** SPEC.md
  rule 9 allows one guarded row and one unguarded row per pair; the union would hold two of one
  kind. A guarded row from one machine and an unguarded row from the other are the pair the format
  has room for, and they merge.
* **The result has to be a machine, and a connected one.** The union is dumped and parsed back, and
  every state of it has to be reachable from the merged initial. A merge that leaves a state out of
  reach is the composition failing to meet — the two machines share no name, and nothing leads from
  the first into the second — and it is refused with the states named rather than written to a file.
  That is the finding this verb exists to report, and the next block ends with the smallest example
  of it.

The output is canonical text, so **a merge does not keep the two files' comments** — the thing
`rename` exists to keep. That is not the two verbs contradicting each other, it is what they are
named for: a rename edits a file a reader already has, and a merge writes a file no reader has yet.
What a merged file carries instead is a provenance header, with the three claims a generated
artifact's header makes ([GENERATOR.md](GENERATOR.md)):

* the two parents, each with the machine name it had, so the union's own name (`Login` below) is not
  the only identity left in the file;
* the exact command that merges them again — with `<this file>` in place of the output path, the
  placeholder the generator's header uses, so the same merge written to two places produces one file
  rather than two;
* the fingerprint of the canonical form (FNV-1a 64), the same value `fsmtable-gen` stamps into a
  header generated from the same machine, so a merged file and an artifact can be compared without
  diffing text.

## Real output: a merge

```
$ ./build/fsmtable-transform merge tests/fixtures/merge_left.fsm tests/fixtures/merge_right.fsm
# Merged by fsmtable-transform from tests/fixtures/merge_left.fsm (machine Login)
# and tests/fixtures/merge_right.fsm (machine Session), fused on the state names the two
# machines share. Re-merge with: fsmtable-transform merge tests/fixtures/merge_left.fsm tests/fixtures/merge_right.fsm -o <this file>
# Fingerprint of the canonical form (FNV-1a 64): 0xE13EEFCA61697ED7
version 1
machine Login
initial Anonymous
kind login = 1
kind logout = 2
kind expire = 3
transition Anonymous --login--> SignedIn
transition SignedIn --logout--> Anonymous
transition SignedIn --expire--> Anonymous

$ ./build/fsmtable-transform merge corpus/minimal.fsm corpus/traffic_light.fsm
corpus/minimal.fsm + corpus/traffic_light.fsm: the merge leaves 3 unreachable state(s) — no path from the merged initial 'A': Green, Yellow, Red — the two machines do not meet there; give the second machine a state name the first already reaches, or a row into it
```

The two fixtures are the smallest composition there is: `merge_left.fsm` names `Anonymous` and
`SignedIn`, `merge_right.fsm` begins in `SignedIn`, and so `SignedIn` is the seam and comes out
once — the union has three rows and two states. The second command is the refusal: `minimal.fsm`
begins in `A` and has no rows, and the traffic light's cycle has no name in common with it, so
nothing leads into the cycle, its three states are unreachable, and no machine is written.

Read both for what a merge is *not*: the two fixtures carry comments and neither survives, and
`merge_right.fsm`'s own machine name `Session` appears only in the header, because the merged machine
took the first parent's name. The block is compared line for line on every gate run, the same way
the block above it is.

## The rename's rules

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
| 0 | the transform was written: the renamed machine, or the union |
| 1 | the transform could not be made — rename: an old name the file does not have, or a new name another name already holds; merge: a kind the two machines disagree about, a `(from, kind)` pair both give a row of the same guardedness, or a union that would leave a state unreachable |
| 2 | the command line is wrong, or a file could not be read, parsed or written |

The 1/2 split is the reason the tool parses the files itself before calling the library: a file that
does not parse is the caller's (2), a transform that cannot be made is the transform's (1). It is the
one place this tool's numbers differ from `fsmtable-gen`'s, which exits 1 for an input it cannot use.
`merge` needs the same split for one more reason — the line number a refusal carries belongs to one
of the two files, and only the tool knows which one — which is why it parses both before it calls the
library rather than leaving it to the library's error. A merge's warning, the dropped initial-entry
clause, is printed and does not move the code: the union is well defined without the clause.

## The library calls behind it

```
fsmtable::parse(text, error, kind_names)          the reader, to know the machine and its names
fsmtable::parse_rename_spec(spec, rename, message)  one `state:Old=New` off a command line
fsmtable::rename(text, renames, error)            the rewrite, proved by re-parsing its own output
fsmtable::merge(first, second, error)             the union, proved the same way and by reachability
```

`SPEC.md` section 4's block is frozen and both verbs are additions beside it, in the shape
[NAMED_KINDS.md](NAMED_KINDS.md) and [ENTRY_EXIT.md](ENTRY_EXIT.md) took: the frozen `parse` and
`dump` signatures are unchanged, and the output of either verb is version 1 text.

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
* **A merge does not keep comments, and cannot be made to.** The two inputs keep theirs; the union
  is a new machine and gets a provenance header instead. A merged file that has to carry a paragraph
  of reasoning is a file to edit by hand afterwards, and the header says which two files to go back
  to.
* **A merge is not symmetric, and it is not a `rename`.** The first machine's name and initial win,
  so the same two files merged the other way round come out with the other machine's identity. And a
  machine merged with itself is refused: fusion is by name, so every row would double, and rule 9
  forbids two rows of one kind for a pair. Two files that grew from a common ancestor meet the same
  wall on the rows they share, and the message names the pair.
* **A state name is the only seam a merge has.** Two machines sharing no state name produce a
  disconnected union, which is refused with the unreachable states named. There is no cross-machine
  wiring to invent and no flag that turns the check off: the fix is a shared state name — or a
  `rename` that makes one — rather than a second verb.
* **The edit is textual, so a v2 directive is a change here too.** A new line kind that carries a
  name would need a case in `edit_line` — the cost of not going through `dump`, and the reason the
  file's own comment says so. `merge` is the cheaper half of that pair: it goes through `dump`, so a
  new directive reaches it as soon as the writer knows about it.

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

**Compose two machines into a new file** — one `merge`, and the header records the command so the
union can be repeated rather than remembered:

```sh
fsmtable-transform merge login.fsm session.fsm -o session_full.fsm
fsmtable-gen session_full.fsm -o fsm_session_full.hpp      # the union, as code
```

A merge that exits 1 wrote nothing, so `> out.fsm` is the shape that leaves an empty file behind;
the two-step form the rename recipe uses is the one to keep here too:

```sh
fsmtable-transform merge login.fsm session.fsm > session_full.new && mv session_full.new session_full.fsm
```

The union is a source file of its own, and it is the one file in this direction that is *committed*:
the two parents stay as they are, and the header's fingerprint says which union an artifact was
generated from.

**Add the verb you need.** The two here are the shape to follow: a function taking text and returning
text or an error with `line == 0` ([src/fsmtable.hpp](src/fsmtable.hpp)), and a command that reads
files and writes one. `rename` keeps the input's bytes and is held to that; `merge` writes a new
machine and is held to the two properties a new machine has — the output parses, and every state in
it is reachable from the initial. `tests/transform_test.cpp`
and `tests/merge_test.cpp` are where the tests would go, and both suites say at the top which
property is the one holding them up.
