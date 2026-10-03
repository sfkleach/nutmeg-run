# Plan for Step 8: lifecycle events

## Goal

A second, independent JSONL log, `events.{YYYY-MM-DD-HH.MM.SS}.jsonl`, switched on
by `ENABLE_EVENTS_LOG` in `trace.hpp`. It records the landmarks of a run:

1. loading code from the bundle
2. finding the entry point
3. launching the program
4. memory allocations

With the flag off there is no run-time cost (same standard as Steps 3 to 7: the
disassembly of the affected functions is unchanged).

## Review of the requirement

### Where each event happens today

| Event                  | Where in the code                                                        |
|------------------------|--------------------------------------------------------------------------|
| bundle opened          | `main.cpp`: `BundleReader reader(...)`                                   |
| entry point found      | `main.cpp`: either `-e NAME` (given) or the single `get_entry_points()` result (discovered) |
| code loaded            | `main.cpp`: the loop over `deps` calling `parse_function_object`, `allocate_function`, `define_global` — once per binding |
| program launched       | `Machine::execute(func_obj)`, just before `threaded_impl` runs the launcher |
| memory allocation      | `Pool::allocate(n)` (`heap.cpp`) is the single choke point: `Heap::allocate_string`, `allocate_function`, the three datakeys in `init_datakeys`, and `ObjectBuilder::commit` all go through it |

Allocation is a bump pointer in a fixed 1 MB pool, so an allocation is fully
described by (address, number of cells, pool usage afterwards).

### Observations that shape the design

- **Allocations happen before `main` has a machine.** The `Machine` constructor builds
  its `Heap`, whose constructor allocates the three datakeys. The events log therefore
  cannot be a member of `Machine` and cannot be passed to `Pool`; it has to be reachable
  from `Pool::allocate` without plumbing.
- **At run time nothing allocates yet.** Strings are allocated when the code is parsed
  (`parse_function_object.cpp:236`) and functions at load. So today every allocation
  event falls in the load phase, and the "launch" event cleanly separates load from run.
  (A future runtime allocation, e.g. string concatenation, will show up after launch
  with no changes.)
- **Step 7's purpose** was to cross-check the instruction stream against events. For
  that, each event needs to say where in the instruction stream it happened.

## Design

### A process-wide event log

New files `src/event_log.hpp` / `.cpp`:

- `class EventLog` owning the `std::ofstream`, a line counter, and the file name
  (`events.{stamp}.jsonl`, using the same `make_log_filename` as the instruction log,
  which moves from a `static` in `instruction_log.cpp` to a function declared in
  `instruction_log.hpp`... or, better, to a tiny shared `log_file.hpp`; see Open
  Questions 5).
- `EventLog& event_log()`: a function-local static, created on first use. The first
  use is the first `Pool::allocate` (the datakeys), so every event, including those
  allocations, is captured, and the file is not created at all when the flag is off.
- Pure formatting functions (`format_event(n, name, instruction, fields)`), unit
  testable without the flag or a file, as with the instruction log.

### Line format

```json
{"n": 4, "event": "allocate", "instruction": 0, "address": "&0x7f..", "cells": 12, "used": 31}
```

- `n`: 1-based event number, equal to the line number in the file (same convention as
  the instruction log's `n`).
- `event`: the event name.
- `instruction`: the `n` of the last instruction logged in `run.*.jsonl` when the event
  happened (`0` before the first instruction). This is the cross-reference to the
  instruction log: "this event happened after run line K".
- Then event-specific fields. Addresses use the existing `&0x...` form
  (`format_pointer`). Names of functions are JSON-quoted through the existing quoting
  helper.
- Each event is a complete line written and flushed in one go (no two-phase write, as
  an event has no "exit").

`instruction` is read from a shared counter. `InstructionLog` currently owns `count_`;
it becomes a small global `inline uint64_t instructions_logged` that `log_entry`
increments (only compiled in when `ENABLE_INSTRUCTION_LOG`). When only the events log is
on, it stays `0`; the field is then always `0` and is documented as meaning "instruction
log not enabled". No cost with either flag off.

### Events

| Event            | Emitted by                         | Fields                                                    |
|------------------|------------------------------------|-----------------------------------------------------------|
| `bundle.open`    | `main.cpp`, after the reader opens | `file`                                                    |
| `entry.point`    | `main.cpp`, once it is settled     | `name`, `source`: `"option"` or `"bundle"`                |
| `load.binding`   | `main.cpp`, per binding loaded     | `name`, `lazy`, `instructions`, `nlocals`, `nparams`, `address` (the function object) |
| `launch`         | `Machine::execute`                 | `name` (by reverse lookup, as in Step 6), `address`       |
| `allocate`       | `Pool::allocate`                   | `address`, `cells`, `used` (cells in use afterwards)      |

Event order for `poplocal` is therefore: `allocate` ×3 (datakeys), `bundle.open`,
`entry.point`, then per binding some `allocate`s (strings, the function) and a
`load.binding`, then `launch`. `load.binding` comes after the binding's allocations,
which is the natural nesting: its `allocate` events are visible just above it.

Note `bundle.open` comes after the datakey allocations only because the `Machine` is
constructed after the reader; we do not reorder `main` for the log's sake.

### Zero cost when disabled

Same pattern as Steps 3 to 7: macros that expand to
`if constexpr (ENABLE_EVENTS_LOG) { ... }`, e.g.

```cpp
#define LOG_EVENT(NAME, ...) do { if constexpr (ENABLE_EVENTS_LOG) { \
    event_log().log(NAME, __VA_ARGS__); } } while (0)
```

so no call, no argument evaluation, and no spill at `-O0` when the flag is off.
`Pool::allocate` is the one hot-ish path (every allocation), so it is the key place to
verify.

### Files touched

- `src/trace.hpp`: add `ENABLE_EVENTS_LOG = false` (committed as `false`; the working
  tree's `ENABLE_INSTRUCTION_LOG = true` stays out of commits as before).
- `src/event_log.hpp/.cpp` (new): `EventLog`, `event_log()`, `format_event`, `LOG_EVENT`.
- `src/heap.cpp`: one `LOG_EVENT("allocate", ...)` in `Pool::allocate`.
- `src/machine.cpp`: `LOG_EVENT("launch", ...)` in `Machine::execute`.
- `src/main.cpp`: the `bundle.open`, `entry.point` and `load.binding` events.
- `src/instruction_log.*`: share the timestamped-name helper and the instruction counter.
- `tests/test_event_log.cpp` (new).
- `docs/tasks/.../debug-log.md`: tick Step 8 when done.

## Steps

1. Add the flag; move the filename helper and the instruction counter to where both logs
   can use them; refactor `InstructionLog` accordingly (tests stay green).
2. `EventLog`, `format_event`, `LOG_EVENT`, with unit tests.
3. Wire the five events in.
4. Verify (below).

## Verification

1. **Unit tests** for `format_event`: valid JSON for each event shape, field order,
   quoting of a name with a quote or non-ASCII character, `n` and `instruction`
   present, newline-terminated.
2. **Flag off:** build, run all tests, and compare disassembly with the commit before
   this step for `Pool::allocate`, `Machine::execute` and `main` (as in Step 3,
   normalising addresses). Also confirm no `events.*.jsonl` is created.
3. **Flag on** (both logs), from a scratch directory with a copy of `poplocal.bundle`:
   - every line parses; `n` equals the line number;
   - the first three events are the datakey `allocate`s, in pool order, starting at the
     pool base with `used` = 5, 10, 15;
   - `entry.point` precedes all `load.binding`s; `launch` is last and its `name` equals
     the `entry.point` name;
   - **allocation accounting:** the sum of `cells` over all `allocate` events equals the
     final `used`, and each `address` follows the previous one (bump allocation, no gaps);
   - **cross-check with the bundle:** the `load.binding` names are exactly the
     transitive dependencies in the bundle's `bindings` table, and `nlocals` matches the
     bundle JSON;
   - **cross-check with the run log:** `launch` has `instruction` = 0, and the `LAUNCH`
     instruction in `run.*.jsonl` (line 1) shows `fn` with the same name;
   - a bundle with a string literal (`helloworld`) produces a string `allocate` whose
     `cells` equal the `2 + ceil(bytes/8)` rule in `Heap::allocate_string`.
4. **Option form:** run with `-e main` and without, and check `source` is `"option"` /
   `"bundle"`.
5. **Pool exhaustion:** the failing `allocate` (throws `bad_alloc`) is still of
   interest. Decide per Open Question 3 and test with a tiny pool in a unit test if
   the event is logged before the check.
6. Delete any stray `events.*.jsonl` and `run.*.jsonl` (they are gitignored by
   `*.jsonl`); leave `trace.hpp` as the user wants it.

## Open questions (with my suggestions)

1. **Allocation granularity.** Log at `Pool::allocate` (address, cells, used), which
   catches everything including datakeys and `ObjectBuilder`, or also say what kind of
   object it is (string, function)? [Pool level only. The kind is not known there, and
   the `load.binding` event already identifies functions. A `kind` can be added later
   by letting `Heap::allocate_*` pass a label down.]
2. **The `instruction` cross-reference field.** Fine as described, including that it is
   `0` when the instruction log is off? [Yes. It is what Step 7 was for.]
3. **Failed allocations.** Log the allocation request before the capacity check, so an
   out-of-memory attempt appears (with `"failed": true`), or only successful ones?
   [Log the failure too: that is the event you most want when debugging memory.
   One extra branch inside the `if constexpr`.]
4. **Where `launch` lives.** `Machine::execute` (knows the function, can look up its
   name via `MachineNames`) rather than `main`. [Yes. `main` already has the name, but
   `execute` is the one place any caller of the machine passes through.]
5. **Shared helper.** `make_log_filename` is currently `static` in `instruction_log.cpp`.
   Move it, with the instruction counter, to a small new `log_file.hpp/.cpp`, or just
   declare it in `instruction_log.hpp`? [New small header; `event_log` should not
   depend on `instruction_log`.]
6. **Extra landmarks.** The step lists four; a `halt`/`finished` event at the end (with
   total instructions executed) and a `bundle.open` are natural additions. [Include
   `bundle.open` (cheap, shown above); leave `halt` out unless you want it, as the run
   log's final `HALT` line already marks it.]
7. **Test runs with the flag on.** With `ENABLE_EVENTS_LOG = true` every test that
   builds a `Heap` appends to a process-wide `events.*.jsonl`, which is harmless noise
   but large. [Accept; the file is gitignored and disposable.]

## Decisions

Open questions 1 to 5 were accepted as suggested. In addition a `halt` event was requested:
emitted in `Machine::execute` after `threaded_impl` returns, so it appears only for a normal
halt (after an exception the log just ends). Its `instruction` field is the total number of
instructions executed. Question 6 (extra landmarks) therefore includes `halt`.
