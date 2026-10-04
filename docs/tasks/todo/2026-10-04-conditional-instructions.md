# Implement the five missing conditional instructions

## Background

The compiler (`nutmeg-compiler`, `pkg/codegen/codegen.go`, `plantConditionalJump`) chooses a
jump instruction from the kinds of the success and failure labels (continue, simple label,
return). Five of the instructions it can emit are not supported by the runtime, so a bundle
containing any of them fails to load with `Unknown instruction type: ...`. They are specified
in the compiler's `docs/bundle-schema.md`.

| JSON `type`     | Fields                         | Semantics                                          | Emitted when (success / failure) |
|-----------------|--------------------------------|----------------------------------------------------|----------------------------------|
| `erase`         | none                           | pop and discard the top of the stack               | continue / continue              |
| `if.so`         | `value`: label                 | pop; jump to the label if it is true               | label / continue; also label / return (followed by `return`) |
| `if.not.return` | none                           | pop; return from the function if it is false       | continue / return                |
| `if.so.return`  | none                           | pop; return from the function if it is true        | return / continue                |
| `if.then.else`  | `name`: then-label, `value`: else-label | pop; jump to the then-label if true, else to the else-label | label / label |

The runtime already has `if.not` (pop; jump if false), `goto` and `return`, so the pieces
exist. None of the five appears in any bundle in `_bundles/` today (counted with sqlite), so
nothing currently exercises them.

Note that the runtime's `IF_NOT` jumps only when the value is exactly `false`
(`condition.u64 == SPECIAL_FALSE.u64`) and falls through for anything else. The compiler
always emits `check.bool` first, so the value is a boolean by then. The new instructions
follow the same convention: `if.so` jumps only when the value is exactly `true`.

## Design

### New opcodes

Add `ERASE`, `IF_SO`, `IF_NOT_RETURN`, `IF_SO_RETURN` and `IF_THEN_ELSE` to `enum class Opcode`
(`src/instruction.hpp`), to `string_to_opcode_map` (`"erase"`, `"if.so"`, `"if.not.return"`,
`"if.so.return"`, `"if.then.else"`) and to `opcode_to_string` (`src/instruction.cpp`), and to
`opcode_map_` in `Machine` (init mode of `threaded_impl`).

### Layout in the code stream, and planting

| Instruction     | Code stream                               | Planting                                    |
|-----------------|-------------------------------------------|---------------------------------------------|
| `ERASE`         | `[label]`                                 | nothing (as `RETURN`)                       |
| `IF_NOT_RETURN` | `[label]`                                 | nothing                                     |
| `IF_SO_RETURN`  | `[label]`                                 | nothing                                     |
| `IF_SO`         | `[label][offset]`                         | `plant_jump_instruction(label = inst.value)`, exactly as `IF_NOT` |
| `IF_THEN_ELSE`  | `[label][then offset][else offset]`       | `plant_jump_instruction` twice: first with `inst.name`, then with `inst.value` |

`plant_jump_instruction` reserves one operand and either resolves a backward jump at once or
records a forward reference that `plant_label` patches later. Both use the base
`operand_pos + 1`, i.e. each offset is relative to the cell **after its own operand**. Using
the function twice therefore needs no change to the patching code. The handler must apply the
two offsets with their different bases (see below). `plant_instruction`'s `switch` gets the
new cases (today's `default:` throws "Unhandled opcode during compilation").

Missing fields give the usual `"X requires a value field"` errors. For `IF_THEN_ELSE` both
`name` and `value` are required.

**A trap in the loader.** `ParseFunctionObject` treats any instruction with a `name` as a
reference to a global when deciding whether it is lazy (`deps_.find(inst.name)`). For
`if.then.else`, `name` is a *label*. If a label happened to share a name with a lazy global,
the lookup would pick the second opcode of the pair. Both elements of the pair are the same
opcode for these instructions, so this is harmless, but it deserves a one-line comment where
`string_to_opcode_map` is defined.

### Handlers (`threaded_impl`, `src/machine.cpp`)

Each handler starts with `LOG_INSTRUCTION_ENTRY` and ends with `LOG_INSTRUCTION_EXIT()` before
its `goto`, as for the existing 18. Operand kinds for the log: `ERASE`, `IF_NOT_RETURN`,
`IF_SO_RETURN`: none; `IF_SO`: `OP_RAW`; `IF_THEN_ELSE`: `OP_RAW, OP_RAW`.

- **`ERASE`**: `operand_stack_.pop();` then dispatch. (The existing stack checks apply, so
  erasing an empty stack throws.)
- **`IF_SO`**: the mirror of `L_IF_NOT`: read the offset, pop, `pc += offset` if the value
  equals `SPECIAL_TRUE`.
- **`IF_THEN_ELSE`**: read `then_off` and `else_off` as two operand cells starting at `pc`;
  pop the condition. The base for the then-offset is the cell after the first operand, and for
  the else-offset the cell after the second:

  ```cpp
  const Cell* operands = pc;
  Cell condition = pop();
  pc = (condition.u64 == SPECIAL_TRUE.u64) ? operands + 1 + operands[0].i64
                                           : operands + 2 + operands[1].i64;
  ```

  What if the value is neither `true` nor `false`? `IF_NOT` treats "not false" as true, so for
  consistency `if.then.else` should be `condition == FALSE ? else : then`, and `if.so` should
  be `condition != FALSE` too. See Open Questions.
- **`IF_NOT_RETURN` / `IF_SO_RETURN`**: pop the condition; if it matches, perform exactly the
  `RETURN` sequence; otherwise dispatch to the next instruction. The `RETURN` sequence
  (pop the return address, the function object and `nlocals` locals; set `pc`) must not be
  copied. It also must not be reached with `goto L_RETURN`, because that would write a second
  `RETURN` line in the instruction log and the `if.so.return` line would never be completed.
  Factor it into a macro, `PERFORM_RETURN()`, used by `L_RETURN` and the two new handlers, as
  was done for the logging macros. A macro rather than a function means the generated code
  for `L_RETURN` is unchanged, which can be checked as in Step 3 of the debug-log task.

### Instruction log

The instruction log's formatter needs nothing new: the operands are raw offsets, `OP_RAW`.
For `IF_THEN_ELSE` the log shows two raw operands, the then and else offsets, each relative to
its own cell (documented in the handler comment).

## Steps

1. Add the five opcodes (enum, string map, `opcode_to_string`, `opcode_map_`).
2. Plant them (`plant_instruction` cases and any small `plant_*` functions).
3. Add the handlers and `PERFORM_RETURN()`.
4. Tests (below).
5. Verify the flag-off disassembly of `threaded_impl` against the previous commit, as in
   Step 3 of `docs/tasks/done/2026-10-02-debug-log/plan-step3.md`. The only expected
   differences are the five new handlers and the opcode-map entries.
6. Update `docs/how-tos/how-to-write-an-instruction.md` if it lists instructions.

## Tests

The project already has in-process tests that build a function from JSON, parse it with
`machine.parse_function_object`, `allocate_function` and `execute`, and then inspect the
stack (`tests/test_machine.cpp`, the jump tests). Use that pattern. It needs no bundle,
works with any instruction mix and avoids the compiler's bundles, not all of which run.

Per instruction, a true case and a false case, asserting the stack contents afterwards:

- `erase`: `push.int 1; push.int 2; erase` leaves `[1]`. Erase on an empty stack throws.
- `if.so`: true jumps over an instruction, false falls through; a backward jump (loop that
  counts down with a bounded iteration count) to exercise the backward-reference path.
- `if.then.else`: true takes the then-branch, false the else-branch; check both orders of
  label definition (then before else and else before then) since the two offsets have
  different bases; both forward, and one backward.
- `if.not.return` / `if.so.return`: a *called* function (via the `ident` mechanism the
  existing tests use, or `LAUNCH`) that returns early in the matching case, and carries on
  in the other, checking the value left on the stack and that the return stack is balanced
  (including a function with non-zero `nlocals`, which is what the return sequence pops).
- Errors: missing `value`/`name` fields; an undefined label (existing forward-reference
  validation); a label name equal to a global.
- Log format (if the unit tests for the formatter allow): the opargs of `IF_THEN_ELSE` are two
  `0d..` raw operands.

A tiny end-to-end case, once the compiler can produce them, is to compile a source file with
an `if`/`and`/`or` in each of the shapes in the table above and compare the output. That
belongs to the compiler's tests, but it is the real confirmation that the names and field
conventions (especially `if.then.else`'s `name` and `value`) agree between the two repos.

## Open questions (with my suggestions)

1. **Non-boolean conditions.** `IF_NOT` jumps only if the value is exactly `false`. For
   `if.so` and `if.then.else`, treat "true" as "not exactly false" (consistent with `IF_NOT`,
   so `if.so` and `if.not` are exact complements) or as exactly `true`? [Not-exactly-false,
   for consistency. The compiler's `check.bool` guarantees a boolean anyway. If we later want
   strictness it should be in `check.bool`, not scattered across the jumps.]
2. **`PERFORM_RETURN()`**: a macro (code for `RETURN` unchanged) or an inline member function
   (cleaner, probably a call at `-O0`)? [Macro, as with the log macros.]
3. **`if.then.else` operand encoding.** Two offsets, each relative to its own cell (reusing
   `plant_jump_instruction` unchanged), or both relative to the end of the instruction (one
   shared base, simpler handler, but a change to the patching code)? [Each relative to its own
   cell. No change to the patcher; the handler is two lines.]
4. **Compiler documentation.** `bundle-schema.md` in the compiler repo still documents
   `in.progress`, which is no longer emitted. Out of scope for this task, but worth a note in
   that repo.
