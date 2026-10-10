# Proposal: Fleet-wide entry/exit semantics

Card t_c925b912 — differential evidence + recommendation for review finding #1.

## The problem

Three incompatible readings of the same machine exist in the fleet today:

| Back end | Order on ordinary transition | Self-transition |
|---|---|---|
| `fsmgine::FSM` (interpreted) | action → exit → enter | action only (skips exit/enter) |
| `fsmgine::compiled::Machine` | action only (no entry/exit) | action only |
| fsmtable-gen generated code | exit → action → entry | exit → action → entry (external) |

The `Machine.hpp` doc claims "Semantics match fsmgine::FSM<TEvent> exactly" — false on
both the order and the self-transition question. Each side's own suite pins its own
reading, so nothing is red.

## What the harness measured

Three back ends driven through the same 2-state, 3-transition machine (A→B, B→B, B→A)
with entry/exit decorations on both states. Every action call labeled by type:

```
fsmgine::FSM (interpreted): [action:on_ab, exit:on_exit_a, enter:on_entry_b, action:on_bb, action:on_ba, exit:on_exit_b, enter:on_entry_a]
compiled::Machine (plain):  [action:on_ab, action:on_bb, action:on_ba]
fsmtable-gen (generated):   [exit:on_exit_a, action:on_ab, enter:on_entry_b, exit:on_exit_b, action:on_bb, enter:on_entry_b, exit:on_exit_b, action:on_ba, enter:on_entry_a]
```

Key observations:
- For A→B: interpreted runs action FIRST (before exit); generated runs exit first (before action).
- For B→B (self): interpreted skips exit/enter entirely; generated runs both (external transition).
- compiled::Machine has no entry/exit at all — its doc claim is vacuously true only if you
  ignore the entire feature.

## Candidate rules

### Option A: FSMTable's rule (exit → action → entry, external self-transition)

This is the statechart standard (UML) and what ENTRY_EXIT.md already documents. Q13
recorded the deliberate choice: "The row does name a target, and reading it as 'stay,
and run nothing extra' would require knowing that the two names are equal — a special
case invented for this question alone." The calculator depends on it: its
`Entry --clear--> Entry` row runs `clear_all` at the start of a line.

**Blast radius in FSMgine:**
- `tests/test_FSM.cpp:175` (`SelfTransitionNoStateChangeActions`): two assertions flip
  from `EXPECT_FALSE` to `EXPECT_TRUE` (exit and enter now run on self-transition).
- `include/FSMgine/FSM.hpp:427-433` (`process()`): reorder to run exit → action → enter
  (swap `transition.executeActions` below the exit/enter block, and remove the
  `current_state_ != target_state` guard so self-transitions run both).
- `include/FSMgine/compiled/Machine.hpp:125`: the doc claim "Semantics match
  fsmgine::FSM<TEvent> exactly" becomes true for order but still false for entry/exit
  presence — compiled::Machine has no onEnter/onExit. The claim needs a caveat or the
  compiled back end needs entry/exit hooks.
- Total: 1 test flips, ~10 lines of production code change.

**Blast radius in FSMTable:**
- None. The spec already says this. The generator already emits this order. The
  differential corpus has no entry/exit decorations, so it is unaffected.

**Blast radius in generated code:**
- None. The composed wrapper functions already implement exit → action → entry.

### Option B: FSMgine's rule (action → exit → enter, internal self-transition)

This matches the current interpreted back end's behavior. It is non-standard: the
action runs while the machine is still in the source state, which means the action
sees the old state. Self-transitions are internal (UML sense): neither exit nor enter
runs.

**Blast radius in FSMgine:**
- None. Already implemented.

**Blast radius in FSMTable:**
- `ENTRY_EXIT.md:39`: rewrite the order to "action, then exit, then enter".
- `QUESTIONS.md Q13`: reverse the reading — self-transitions become internal.
- `gen/fsmtable-gen.cpp`: the composed wrapper functions need reordering from
  `exit → action → entry` to `action → exit → entry`. Self-transition wrappers need
  to drop the exit/entry calls entirely.
- Every generator test that pins the composed function order (the Door fixture's
  `Closed_leaving_to_Open`, `Open_leaving_to_Open`, etc.) needs updating.
- The calculator's `Entry --clear--> Entry` row stops running `clear_all` — the
  calculator breaks. A new internal-transition syntax would be needed.
- Total: spec rewrite, generator rewrite, multiple test updates, calculator redesign.

### Option C: Status quo (three different stories)

No changes. The disagreement persists. The `Machine.hpp` doc claim remains false.
Every new consumer that uses entry/exit gets a different machine depending on which
back end they pick.

## Recommendation

**Option A (FSMTable's rule).** Reasons:

1. It is the statechart standard. Anyone who reads a .fsm file with entry/exit
   decorations expects exit → action → entry.
2. The .fsm format already documents it (ENTRY_EXIT.md) and the generator already
   emits it. The format is frozen — changing it would break the contract.
3. The calculator depends on external self-transitions. Option B would break it.
4. The blast radius in FSMgine is minimal: one test flip, ~10 lines of reordering.
5. The blast radius in FSMTable is zero.

The one open question: `compiled::Machine` has no entry/exit hooks at all. The doc
claim "Semantics match fsmgine::FSM<TEvent> exactly" would become true for order
but still false for the entry/exit feature. Two sub-options:
- (A1) Add onEnter/onExit to compiled::Machine (feature parity).
- (A2) Update the doc claim to say "first-match-wins and action order match;
  entry/exit hooks are an fsmgine::FSM-only feature" (honest caveat).

A2 is the smaller change and can land in the same card. A1 is a separate card.

## Files in this changeset

- `tests/fixtures/entry_exit_trace.fsm` — the test machine
- `tests/entry_exit_differential.cpp` — the harness (RED, expected to fail)
- `CMakeLists.txt` — new test target + generator input
- This proposal file