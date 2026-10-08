// The TypeScript back end's tests.
//
// The modules under test are WRITTEN by `fsmtable-gen --target deno` (scripts/deno.sh) from the
// same inputs the C++ generator tests compile, and copied next to this file before `deno test`
// runs — so the imports below are siblings at run time, and the `deno check` the stage runs over
// them is what makes a generator that emits something that does not compile fail the gate rather
// than a review. That is the bargain tests/generator_test.cpp strikes with the generated headers,
// one directory over.
//
// A machine is read the way QUESTIONS.md Q5 records it: for the current state and the event's
// kind, the refined row fires when its comparison passes and otherwise the unguarded row for the
// same pair does — the canonical order the generator emits IS that precedence rule (SPEC.md rule
// 9) — and nothing moves when no row answers. What each machine's rows are is written out here by
// hand, so a change to the emitted table has to disagree with this file rather than with itself.
//
// Run it the way the gate does:
//
//     bash scripts/deno.sh          # generates the modules, then deno check + deno test

import { deepStrictEqual, strictEqual } from "node:assert/strict";

import * as door from "./fsm_entry_exit.ts";
import * as guarded from "./fsm_guarded.ts";
import * as minimal from "./fsm_minimal.ts";
import * as named from "./fsm_named_kinds.ts";
import * as refined from "./fsm_refined_pair.ts";
import * as light from "./fsm_traffic_light.ts";

// The caller's side of the contract: an action the .fsm named is a function the caller defines.
// This log is where those functions write, so a test can hold step()'s returned list and the calls
// it actually makes to the same order.
const log: string[] = [];

function record(name: string): () => void {
  return () => {
    log.push(name);
  };
}

// A registry typed by the generated Action union. Typing it is the compile-time half of the C++
// header's "a name the file uses and you never define is a link error": a name the .fsm wrote and
// this file does not define fails `deno check`, and so does an extra one.
const doorActions: Record<door.Action, () => void> = {
  on_opening: record("on_opening"),
  on_already_open: record("on_already_open"),
  on_entry_broken: record("on_entry_broken"),
  on_entry_closed: record("on_entry_closed"),
  on_exit_closed: record("on_exit_closed"),
  on_entry_open: record("on_entry_open"),
  on_exit_open: record("on_exit_open"),
};

const lightActions: Record<light.Action, () => void> = {
  on_red_green: record("on_red_green"),
};

const namedActions: Record<named.Action, () => void> = {
  on_tick: record("on_tick"),
};

// Drives a generated machine the way a caller would: step() decides, then the caller runs the
// action names it returned, in the order it returned them.
function drive<S, A extends string, E>(
  machine: { step(state: S, event: E): { state: S; fired: boolean; actions: readonly A[] } },
  registry: Record<A, () => void>,
  initial: S,
  events: readonly E[],
): { states: S[]; fired: boolean[] } {
  let state = initial;
  const states: S[] = [state];
  const fired: boolean[] = [];
  for (const event of events) {
    const result = machine.step(state, event);
    state = result.state;
    states.push(state);
    fired.push(result.fired);
    for (const name of result.actions) registry[name]();
  }
  return { states, fired };
}

Deno.test("the traffic light: the walk, and a guard with no fallback behind it", () => {
  strictEqual(light.initial, "Red");
  deepStrictEqual(light.states, ["Red", "Green", "Yellow"]);
  deepStrictEqual(light.actionNames, ["on_red_green"]);
  deepStrictEqual(light.initialEntry, []);

  log.length = 0;
  const walked = drive(light, lightActions, light.initial, [
    { kind: light.Kind.k1, value: 0 }, // Red -> Green, runs on_red_green
    { kind: light.Kind.k2, value: 0 }, // Green -> Yellow, no action of its own
    { kind: light.Kind.k3, value: 30 }, // Yellow -> Red, `when ge 30`
  ]);
  deepStrictEqual(walked.states, ["Red", "Green", "Yellow", "Red"]);
  deepStrictEqual(walked.fired, [true, true, true]);
  deepStrictEqual(log, ["on_red_green"]);

  // Yellow --3--> Red is guarded and carries no unguarded fallback, so 29 leaves the event
  // unhandled and the machine where it is: the partial pair the inspector reports.
  const refused = light.step("Yellow", { kind: light.Kind.k3, value: 29 });
  strictEqual(refused.fired, false);
  strictEqual(refused.state, "Yellow");
  deepStrictEqual(refused.actions, []);

  // A pair with no row at all is the same answer.
  strictEqual(light.step("Red", { kind: light.Kind.k2, value: 0 }).fired, false);
});

Deno.test("the door: exit, the row's action and entry, composed in that order", () => {
  strictEqual(door.initial, "Closed");
  deepStrictEqual(door.states, ["Closed", "Open", "Broken"]);
  deepStrictEqual(door.initialEntry, ["on_entry_closed"]);

  log.length = 0;
  const walked = drive(door, doorActions, door.initial, [
    { kind: door.Kind.open, value: 0 }, // Closed -> Open: exit, action, entry
    { kind: door.Kind.open, value: 0 }, // Open -> Open: a self transition is external, so both run
    { kind: door.Kind.close, value: 0 }, // Open -> Closed, no row action
    { kind: door.Kind.knock, value: 0 }, // Closed -> Broken
    { kind: door.Kind.close, value: 0 }, // Broken -> Closed: Broken has an entry clause only
  ]);
  deepStrictEqual(walked.states, ["Closed", "Open", "Open", "Closed", "Broken", "Closed"]);
  deepStrictEqual(log, [
    "on_exit_closed",
    "on_opening",
    "on_entry_open",
    "on_exit_open",
    "on_already_open",
    "on_entry_open",
    "on_exit_open",
    "on_entry_closed",
    "on_exit_closed",
    "on_entry_broken",
    "on_entry_closed",
  ]);

  // step() is pure: it decides, and the caller runs. Nothing happens on its own, and in
  // particular the initial state's entry clause is a name in initialEntry that nobody executed.
  log.length = 0;
  strictEqual(door.step("Closed", { kind: door.Kind.open, value: 0 }).actions.length, 3);
  deepStrictEqual(log, []);
});

Deno.test("a guarded row wins over the fallback the file wrote above it", () => {
  log.length = 0;
  // refined_pair.fsm writes the unguarded fallback FIRST and the refinement second; the emitted
  // table is in canonical order (refined first), which is what makes `high` reachable at all.
  const below = refined.step("A", { kind: refined.Kind.k1, value: 9 });
  strictEqual(below.state, "B");
  strictEqual(below.fired, true);
  deepStrictEqual(below.actions, []);

  const at = refined.step("A", { kind: refined.Kind.k1, value: 10 });
  strictEqual(at.state, "C");
  strictEqual(at.fired, true);
  deepStrictEqual(at.actions, ["high"]);

  // B has no outgoing row: a sink, and an event there matches nothing.
  strictEqual(refined.step("B", { kind: refined.Kind.k1, value: 0 }).fired, false);
});

Deno.test("every comparison a when clause can carry", () => {
  // guarded.fsm uses all five ops, one guarded row per pair and NO fallback, so a false guard is
  // a no-match rather than a silent move.
  const yes = (state: guarded.State, kind: guarded.Kind, value: number) =>
    guarded.step(state, { kind, value }).fired;
  const no = (state: guarded.State, kind: guarded.Kind, value: number) =>
    !guarded.step(state, { kind, value }).fired;

  strictEqual(yes("A", guarded.Kind.k1, 0), true); // when eq 0
  strictEqual(no("A", guarded.Kind.k1, 1), true);
  strictEqual(yes("B", guarded.Kind.k2, 9), true); // when lt 10
  strictEqual(no("B", guarded.Kind.k2, 10), true);
  strictEqual(yes("C", guarded.Kind.k3, 10), true); // when le 10
  strictEqual(no("C", guarded.Kind.k3, 11), true);
  strictEqual(yes("D", guarded.Kind.k4, -4), true); // when gt -5
  strictEqual(no("D", guarded.Kind.k4, -5), true);
  strictEqual(yes("A", guarded.Kind.k5, 30), true); // when ge 30
  strictEqual(no("A", guarded.Kind.k5, 29), true);
  strictEqual(no("A", guarded.Kind.k2, 0), true); // no row for the pair
});

Deno.test("named kinds become the enumerators, and a numeric enum still runs backwards", () => {
  // named_kinds.fsm declares tick = 1, stop = 2, reset = 7 (unused by any row) and uses a bare 3.
  strictEqual(named.Kind.tick, 1);
  strictEqual(named.Kind.stop, 2);
  strictEqual(named.Kind.k3, 3);
  strictEqual(named.Kind.reset, 7);
  // A numeric TypeScript enum carries its reverse map, so code -> text comes free here — the job
  // NAMED_KINDS.md gives the C++ header's KindNameOf, and the same round trip.
  strictEqual(named.Kind[1], "tick");
  strictEqual(named.Kind[7], "reset");
  strictEqual(named.Kind[3], "k3");

  log.length = 0;
  const walked = drive(named, namedActions, named.initial, [
    { kind: named.Kind.tick, value: 0 }, // Idle -> Running, runs on_tick
    { kind: named.Kind.tick, value: 3 }, // Running -> Running, `when ge 3`, no action
    { kind: named.Kind.stop, value: 0 }, // Running -> Idle
  ]);
  deepStrictEqual(walked.states, ["Idle", "Running", "Running", "Idle"]);
  deepStrictEqual(walked.fired, [true, true, true]);
  deepStrictEqual(log, ["on_tick"]);

  // The bare `3` is a kind like any other: Idle --3--> Idle when ge 5.
  strictEqual(named.step("Idle", { kind: named.Kind.k3, value: 5 }).fired, true);
  strictEqual(named.step("Idle", { kind: named.Kind.k3, value: 4 }).fired, false);
});

Deno.test("a machine with no kinds and no rows still compiles, and never moves", () => {
  strictEqual(minimal.initial, "A");
  deepStrictEqual(minimal.states, ["A"]);
  deepStrictEqual(minimal.actionNames, []);
  deepStrictEqual(minimal.initialEntry, []);

  // The machine declares no kind, so its Kind enum is empty and there is no member to pass. A
  // caller in the same position gets the value from somewhere outside the machine (a byte off a
  // wire, a number); the cast is that, written down.
  const anyKind = 0 as unknown as minimal.Kind;
  const result = minimal.step("A", { kind: anyKind, value: 7 });
  strictEqual(result.fired, false);
  strictEqual(result.state, "A");
  deepStrictEqual(result.actions, []);
});
