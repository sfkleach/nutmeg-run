# Plan for Step 4: instruction operands ("opargs") in the log

## Goal

Each log entry also reports the operands that are inlined in the code stream
after the instruction's label word, interpreted using
`docs/specs/tagging-scheme.md`. For example:

```json
{"opcode": "CALL_GLOBAL_COUNTED", "opargs": ["0d3,0x3", "&0x55d0c8a4e2f0"], "onEntry": {"stacklength": 1}, "onExit": {"stacklength": 0}}
```

and, as before, no run-time cost when `ENABLE_INSTRUCTION_LOG` is off.

## Review of the requirement

### Three kinds of operand

The step describes three renderings, which map onto three kinds of operand cell:

| Kind        | Meaning                                         | Rendered as                                             |
|-------------|-------------------------------------------------|---------------------------------------------------------|
| **raw**     | an uninterpreted 64-bit pattern (offsets, etc.) | `"0dNNNN,0xNNNN"`: **signed** decimal and hex of the bit pattern, no leading zeros |
| **tagged**  | a tagged value (spec: low-order tag bits)       | non-pointers: a description; pointers: `&0xNNNN`        |
| **pointer** | an untagged C++ pointer (Ident*, function, ...) | `&0xNNNN`, no leading zeros                             |

The tagged case splits by the low bits, per the tagging spec:

| Low bits | Description produced                                              |
|----------|-------------------------------------------------------------------|
| `x00`    | integer, e.g. `int 42` (using `as_detagged_int`, so 62-bit)        |
| `x10`    | float, e.g. `float 1.5`                                           |
| `001`    | pointer: `&0x...` of the **detagged** address (low 3 bits cleared) |
| `111`    | special literal: `false`, `true`, `nil`, `undef`                  |
| `011`, `101` | reserved: `reserved 0x...` (so a bad cell is visible)         |

Note that `cell_to_string` in `value.cpp` is *not* reused: it prints pointers in
a different form, does not know `undef`, and prints `<unknown cell ...>`. The log
format needs its own small function, which also keeps the log format stable if
`cell_to_string` changes.

### Operands of each instruction

Read from the handlers in `threaded_impl` and the planting code. At the top of a
handler `pc` has already moved past the label word, so it points at the first
operand, and the log call can read the operands without consuming them.

| Handler                                       | Operands (in order)                        |
|-----------------------------------------------|--------------------------------------------|
| `PUSH_VALUE` (INT / BOOL / STRING)            | tagged                                     |
| `POP_LOCAL`, `PUSH_LOCAL`                     | raw (frame offset)                         |
| `STACK_LENGTH`, `CHECK_BOOL`, `CHECK_COUNT_IS_1` | raw (frame offset)                      |
| `GOTO`, `IF_NOT`                              | raw (relative jump, can be negative)       |
| `PUSH_GLOBAL`, `PUSH_GLOBAL_LAZY`             | pointer (`Ident*`)                         |
| `CALL_GLOBAL_COUNTED`, `..._LAZY`             | raw (frame offset), pointer (`Ident*`)     |
| `DONE`                                        | raw (frame offset), pointer (`Ident*`)     |
| `SYSCALL_COUNTED`                             | raw (frame offset), pointer (C++ function) |
| `LAUNCH`                                      | pointer (function object)                  |
| `RETURN`, `HALT`                              | none                                       |
| `IN_PROGRESS`                                 | pointer (`Ident*`); not reachable today    |

I will confirm each row against the planting code (`parse_function_object.cpp`)
rather than rely on the handler reads alone, since a wrong table here would
silently mislabel operands in the log.

## Design

### Where the operand kinds are stated

At each handler's log call, as part of the existing macro, so the knowledge
stays next to the code that reads the operands:

```cpp
LOG_INSTRUCTION_ENTRY("CALL_GLOBAL_COUNTED", OP_RAW, OP_PTR);
LOG_INSTRUCTION_ENTRY("RETURN");
```

The macro becomes variadic (`__VA_OPT__`, available in the C++20 this project
already uses) and passes `pc` and the list of kinds to
`InstructionLog::log_entry`. This is deliberately not a separate
opcode-to-operand-kinds table, because the one handler shared by several
opcodes (`PUSH_VALUE`) has a single operand kind anyway, and a second table
would be one more thing to keep in step with the handlers.

The macro uses the local variable `pc`, as it already uses the member
`instruction_log_`. I will say so in the macro's comment.

### Cost when disabled

All of it, including the operand list and `pc`, sits inside the existing
`if constexpr (ENABLE_INSTRUCTION_LOG)`, so nothing is emitted. This is checked
the same way as in Step 3 (disassembly of `threaded_impl` against the
pre-logging code).

### Placement in the line and the two-phase write

`"opargs"` goes between `"opcode"` and `"onEntry"`. Operands are known at entry,
so they are written in the first half of the line. After a crash the truncated
last line therefore shows the failing instruction **and** its operands, which is
helpful.

`"opargs"` is always present, as `[]` for instructions with no operands, so every
line has the same shape.

### Code changes

`InstructionLog` (src/instruction_log.hpp/.cpp):

- `enum class OpArgKind { Raw, Tagged, Pointer }` and constants `OP_RAW`,
  `OP_TAGGED`, `OP_PTR`.
- Pure formatting functions, unit-testable without the flag or a file:
  - `format_raw(uint64_t)` -> `"0d..,0x.."`
  - `format_pointer(const void*)` -> `"&0x.."`
  - `format_tagged(Cell)` -> description, per the table above
  - `format_opargs(const Cell* operands, std::initializer_list<OpArgKind>)` ->
    the JSON array text
- `format_entry(name, opargs, stacklength)` gains the opargs text.
- `log_entry(name, operands, kinds, stacklength)`.

`machine.cpp`:

- Make `LOG_INSTRUCTION_ENTRY` variadic and update the 18 call sites with their
  operand kinds from the table above.

## Steps

1. Confirm the operand table against the planting code.
2. Add the formatting functions and unit tests (tests/test_instruction_log.cpp).
3. Wire them into `log_entry` and the macro.
4. Update the 18 call sites.
5. Verify (below).

## Verification

1. **Unit tests** for the formatters, including: raw `0`, small, large and
   negative values; tagged even and odd integers (bit 2), the 62-bit extremes,
   float, `true`, `false`, `nil`, `undef`, both reserved tags, a tagged pointer;
   a null pointer.
2. **Build and all tests** with the flag off, and the **disassembly comparison**
   of `threaded_impl` against the pre-logging code (as in Step 3).
3. **Flag-on runs** from a scratch directory using copies of the compiler's
   bundles. `poplocal` first, where I can check the operands by hand, then
   `triangle` (189 lines) for broader coverage, checking that jumps, calls and
   syscalls show plausible values. A script checks that every line parses and
   that every instruction reports the expected number of operands.
4. **Hard-kill check** that the truncated final line still contains `opargs`.
5. Flag set back as the user wants it (it is currently `true` in the working
   tree), and no `run.*.jsonl` left in the repo.

## Decisions (resolved open questions)

1. **Output shape:** `"opargs"` is an array of strings in the spec's notation.
2. **Negative raw operands:** the decimal part is the *signed* value and the hex
   part is the bit pattern, so a jump of -3 is `"0d-3,0xfffffffffffffffd"`.
3. **Tagged pointers:** shown as the detagged address, in the same `&0x..` form
   as untagged pointers.
4. **Descriptions and hex:** `int 42`, `float 1.5`, `true`, `false`, `nil`,
   `undef`, `reserved 0x..`; lowercase hex.
5. **Out of scope:** printing global names or string contents.
