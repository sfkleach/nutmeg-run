# Plan for Step 3: before-and-after stack length

## Goal

Each instruction-log entry records the stack length on entry to the instruction
and on exit from it:

```json
{"opcode": "PUSH_LOCAL", "onEntry": {"stacklength": 3}, "onExit": {"stacklength": 4}}
```

with no run-time cost when `ENABLE_INSTRUCTION_LOG` is `false`.

## Review of the requirement

### What "the stack" means

The machine has two stacks: the operand stack (`operand_stack_`) and the return
stack (`return_stack_`). I read "the stack length" as the **operand stack**, i.e.
`operand_stack_.size()` (also exposed as `Machine::stack_size()`). It is the
one the instructions manipulate directly and the one the `STACK_LENGTH` /
`CHECK_*` instructions reason about. The return stack changes only at
calls/returns and could be added later as a further field. See Open Questions.

### Where entry and exit are in the code

`Machine::threaded_impl` (src/machine.cpp) dispatches by computed goto. Step 1
put one `log_instruction("NAME")` call as the first line of each of the 18
handlers, which is the *entry* point. Each handler (except `HALT`) ends with a
single `goto *(pc++)->label_addr;` (or the equivalent `goto *pc++->label_addr;`),
which is the *exit* point. The initial dispatch before the first handler is not
an instruction and needs no logging. This gives:

| Handler(s)                         | Entry                | Exit                                    |
|------------------------------------|----------------------|-----------------------------------------|
| 17 handlers ending in `goto`       | first line (exists)  | immediately before the final `goto`     |
| `L_HALT` (ends with `return;`)     | first line (exists)  | immediately before `return;`            |
| `L_LAUNCH`                         | first line (exists)  | after `LaunchInstruction(pc)`, before `goto` |

Every handler has exactly one exit, so no handler needs restructuring.

Two things to keep in mind when reading the output:

- For a call instruction (`CALL_GLOBAL_COUNTED`, the lazy variant, `DONE`), the
  "exit" is the point where control transfers to the callee. The callee's own
  instructions follow as later log lines, so a call's `onExit` is *not* the
  length after the call has completed.
- `IN_PROGRESS` has a handler but is not in `opcode_map_`. It is covered by the
  same mechanical edit, and is harmless.

## Design

### Zero cost when disabled

All new code sits inside `if constexpr (ENABLE_INSTRUCTION_LOG)`, including the
call that reads the stack size, so nothing is evaluated or emitted when the
flag is off. This is the same mechanism Step 1 uses.

### Two-phase writing (recommended)

The entry and exit values are known at different times, so the line is written
in two parts:

1. On entry: write `{"opcode": "NAME", "onEntry": {"stacklength": M}, ` and flush.
2. On exit: write `"onExit": {"stacklength": N}}` and a newline, and flush.

Reason: the log exists for post-hoc debugging, and the most common failures here
are stack-check exceptions and crashes in the middle of an instruction. Step 1
already flushes every line for that reason. If the line were only written
on exit, the failing instruction would be the one missing from the log.

Cost: after a crash or exception the **last line is truncated** (it has
`onEntry` but no `onExit`), so it is not valid JSON. Readers must tolerate a
bad final line (e.g. `head -n -1 file | jq .`). That truncated line is the
failing instruction, which I think is the right trade-off, and it needs no
special handling for exceptions, `std::terminate` or signals.

Alternative (not recommended): hold the entry in memory and write the whole line
on exit. All lines are always valid JSON, but the failing instruction is lost
on a hard crash (and on an exception unless the destructor runs and writes a
line with `"onExit": null`).

### API changes

`InstructionLog` (src/instruction_log.hpp/.cpp):

- Replace `log_opcode(const char* name)` with
  `log_entry(const char* name, size_t stacklength)` and
  `log_exit(size_t stacklength)`.
- Move the line formatting into small free functions
  (`format_entry(name, stacklength)` and `format_exit(stacklength)`, returning
  `std::string`) so they can be unit tested without the flag and without a file.

`Machine` (src/machine.cpp):

- Replace `log_instruction(const char*)` with two **macros**,
  `LOG_INSTRUCTION_ENTRY(NAME)` and `LOG_INSTRUCTION_EXIT()`, defined just above
  `threaded_impl` and undefined at the end of the file. Each expands to an
  `if constexpr (ENABLE_INSTRUCTION_LOG)` around a call to
  `instruction_log_.log_entry(NAME, operand_stack_.size())` or `log_exit(...)`.
  (Implemented as macros rather than the originally planned inline functions:
  at `-O0`, which is what `just build` produces, even an `always_inline` empty
  function leaves spilled arguments in every handler. Only a macro leaves
  nothing. See Verification.)

`threaded_impl` (src/machine.cpp):

- Rename the 18 existing `log_instruction("X");` calls to
  `LOG_INSTRUCTION_ENTRY("X");`.
- Add `LOG_INSTRUCTION_EXIT();` on the line before each of the 17 final
  `goto`s and before `HALT`'s `return;`. For `LAUNCH`, add it after
  `pc = LaunchInstruction(pc);`.

I would make both edits with a script, as in Step 1, then review the diff.
I prefer explicit calls to hiding the `goto` inside a `NEXT()` macro, because
the control flow stays visible and matches the existing style.

## Steps

1. Change `InstructionLog`: new `log_entry`/`log_exit` and the format helpers.
2. Change `Machine`: the two wrapper functions.
3. Edit the 18 handlers in `threaded_impl` (entry rename, exit insertion).
4. Add unit tests for the format helpers (tests/test_instruction_log.cpp).
5. Update `debug-log.md` only if the task description needs to change.

## Verification

1. **Build and tests with the flag off:** `just build`, run `./_build/tests`.
2. **Zero cost when off:** compile `src/machine.cpp` as it was before any logging
   existed (the parent of the Step 1 commit) and as it is now, both with the flag
   off and the project's flags, and compare the disassembly of
   `Machine::threaded_impl`. Done: identical instruction stream (only the load
   address and one data-member offset differ, because `Machine` gained a
   member).
   (`nfib` takes ~110 s per run, so I would not use it as a timing benchmark.)
3. **Output with the flag on:** temporarily set `ENABLE_INSTRUCTION_LOG` to
   `true`, run a good bundle from a scratch directory (a copy of
   `_bundles/poplocal.bundle`, not the repo-root `poplocal.bundle`, which is
   currently empty), then set the flag back to `false`.
   Expected for `poplocal`: 18 lines, each with both stack lengths, with
   consecutive lines chained (an instruction's `onExit` equals the next
   instruction's `onEntry`, except across the `LAUNCH` and call boundaries to
   be checked by eye), and `HALT` last. A script can check the chaining.
4. **Truncation behaviour:** run something that fails a stack check with the
   flag on and confirm the final line is the failing instruction, with
   `onEntry` and no `onExit`.
5. Confirm the flag is back to `false` and no `run.*.jsonl` is left in the repo.

## Decisions (resolved open questions)

1. **Which stack?** The operand stack only. The return stack can be added later.
2. **Truncated final line on failure:** the two-phase write is accepted. The
   half-written last line is a known trade-off and may be revisited once we have
   used it in anger.
3. **Opcode names:** unchanged from Step 1 (`PUSH_VALUE` stands for `PUSH_INT`,
   `PUSH_BOOL` and `PUSH_STRING`).
