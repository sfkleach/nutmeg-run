# Plan for Step 5: improve the opargs formatting

## Goal

Three changes to how operands are rendered in `"opargs"`:

1. Operands that refer to local variables are reported as `local DDD`.
2. A tagged pointer to a string is reported as an object
   `{"key": "string", "value": "..."}` whose value is the string, shortened if
   it is long.
3. Any other tagged pointer is reported as `TYPE CONTENTS`.

As before, no run-time cost when `ENABLE_INSTRUCTION_LOG` is off.

## Review of the requirement

### 1. Local variables

Today these operands are `OP_RAW` and print as `"0d6,0x6"`. They need a new
operand kind, `OP_LOCAL` (`OpArgKind::Local`), rendered `"local 2"` (the index).

Which operands count as locals? Every one that the handler passes to
`get_local_variable(offset)`:

| Instruction                                   | Operand that becomes `OP_LOCAL` |
|-----------------------------------------------|---------------------------------|
| `PUSH_LOCAL`, `POP_LOCAL`                     | the only operand                |
| `STACK_LENGTH`, `CHECK_BOOL`, `CHECK_COUNT_IS_1` | the only operand (a compiler-allocated local holding a saved stack length) |
| `CALL_GLOBAL_COUNTED`, `..._LAZY`, `DONE`, `SYSCALL_COUNTED` | the first operand (same use) |

`GOTO` and `IF_NOT` keep `OP_RAW`: their operand is a relative jump, not a local.

**What DDD is.** DDD is the local's **index**, as in the compiler's JSON (decided
with the user; the step's wording said "offset"). The planted operand is a frame
offset, which the loader computed as `offset = nlocals - index + 2`
(`Instruction::calc_offset`), so the logger inverts it:

    index = nlocals + 2 - offset

`nlocals` is that of the function whose frame is current. At the top of the
handler that frame is still the caller's own, and its function object is the
cell just under the return address (`Machine::get_frame_function_object()`, see
`docs/specs/return-stack-layout.md`), so `nlocals` is
`heap_.get_function_nlocals(func_obj)`.

Details:

- The macro cannot read the frame unconditionally: for `LAUNCH` the return stack
  is still empty. A small private helper, `Machine::log_frame_nlocals()`, returns
  `-1` if the return stack has fewer than two entries, otherwise the current
  function's `nlocals`. It is called only inside the `if constexpr`, so it costs
  nothing when the log is off.
- `log_entry` and `format_opargs` gain an `nlocals` argument, used only for
  `OP_LOCAL` operands.
- If the index cannot be recovered (`nlocals` is `-1`, or the result is outside
  `0 .. nlocals-1`), print `local offset DDD` with the raw offset instead, so
  the oddity is visible rather than hidden or wrong.

### 2. Strings

A string is a heap object: `[-1: length incl. NUL][0: string datakey][1..: UTF-8 bytes]`.
A tagged pointer is a string if the object's datakey (cell 0) is
`Heap::get_string_datakey()`.

Rules from the step:

- Render as an object, `{"key": "string", "value": "..."}`, rather than as a bare
  string (decided with the user). A string literal can then never be mistaken for
  a description such as `"local 3"` or `"true"`, and the object has room for
  more fields later. The value is a JSON string, so quotes, backslashes,
  newlines and control characters in the text are escaped.
- More than 16 characters: keep the first 13 and append `...` (16 in all).
  Exactly 16 characters is shown in full.

Decisions needed (my suggestions in brackets):

- "Characters": the spec says strings are UTF-8, so truncating at 13 *bytes*
  could cut a multi-byte character in half and produce invalid UTF-8. [Count
  code points: a character is any byte that is not a UTF-8 continuation byte
  (`(b & 0xC0) != 0x80`). Invalid UTF-8 is counted byte by byte and is
  escaped/replaced when written, so the log stays valid JSON.]
- Read the length from the object's stored length, not by scanning for a NUL, so
  a damaged string cannot make the logger read past its end.

### 3. Other references: `TYPE CONTENTS`

"The type is the name of the datakey, and the contents as we print them already"
means the address form: for example `function &0x7f0012345670`.

Problem: **datakeys have no name.** The datakey object layout
(`docs/specs/heap-object-layout.md`, `heap.cpp`) has flavour, sizes and bit
width, but no name field. The only datakeys that exist are the three
fundamental ones the `Heap` creates, so the plan maps them by identity:

| Pointer / datakey                  | TYPE       |
|------------------------------------|------------|
| the object is the datakey-datakey  | `datakey`  |
| datakey is `get_string_datakey()`  | `string` (handled by rule 2, not printed this way) |
| datakey is `get_function_datakey()`| `function` |
| anything else                      | `unknown`  |

Datakey objects themselves need care: a tagged pointer to a datakey object
points at its first cell, which holds the flavour, not a datakey, so they cannot
be recognised by reading cell 0. They are recognised by comparing the pointer
with the three known datakey objects instead.

When more types are added, datakeys will need a name field; that is a separate
task and the lookup above can then be replaced.

**Today this case cannot occur in real operands.** The only tagged operand is
the one of `PUSH_VALUE`, and the loader only plants ints, bools and strings
there; functions are reached through `Ident*` pointers, which stay `OP_PTR`. So
this rule is implemented for completeness and tested with synthetic objects, not
exercised by a real bundle.

### Safety

Rules 2 and 3 read heap memory, which the formatter did not do before. The log is
a debugging aid, so it must not crash on a damaged operand. Before reading an
object, check `Pool::contains(ptr)`; a tagged pointer outside the pool is shown
as `unknown &0x...` rather than followed. Reads are bounded by the stored length.

## Design

### Formatters take the heap

The formatters need the `Heap` to recognise datakeys and read strings:

- `format_tagged(Cell, Heap&)`, `format_opargs(operands, kinds, Heap&, nlocals)`,
  and `InstructionLog::log_entry(name, operands, kinds, heap, nlocals, stacklength)`.
- The `LOG_INSTRUCTION_ENTRY` macro passes `heap_` (a `Machine` member), inside the
  existing `if constexpr`, so nothing is emitted when the log is off.
- `Heap&` rather than `const Heap&`, because `Heap::get_pool()` and
  `is_function_object` are not const. (Adding a const accessor is an option if
  that feels wrong.)

### Quoting and layout

`"opargs"` is now an array whose elements are either JSON strings (descriptions:
`"local 3"`, `"0d4,0x4"`, `"&0x55d0"`, `"int 42"`, `"true"`) or, for strings, an
object. The line is still built by hand with `", "` and `": "` separators so
that it matches the existing layout, rather than with nlohmann's compact
`dump()`.

Until now each element was wrapped in quotes without escaping, which was safe
because no description could contain a quote. String contents can. So all text
that goes inside quotes, descriptions included, is quoted through one helper
that uses nlohmann's serializer (`dump` with the `replace` error handler, so
invalid UTF-8 cannot make it throw). The truncation to 13 + `...` is applied to the
text before quoting. `format_tagged` therefore returns a small result (kind and
text) instead of one string, so `format_opargs` knows whether to emit a quoted
string or an object.

### New and changed pieces (src/instruction_log.hpp/.cpp)

- `OpArgKind::Local` and `OP_LOCAL`; `format_local(uint64_t offset, int nlocals)` -> `"local DDD"` (the index), or `"local offset DDD"` if it cannot be recovered.
- `format_string(...)`, a helper that does the code-point truncation.
- `format_tagged` returns: the string text (rule 2), `TYPE &0x...` (rule 3), or
  the existing descriptions for non-pointers (unchanged).
- `machine.cpp`: change the operand kinds at 10 of the 18 call sites (the table
  in section 1).

## Steps

1. Add `OP_LOCAL`/`format_local`; switch the call sites; update the existing unit
   tests that expect `0d..,0x..` for local operands.
2. Add the heap-aware tagged formatting (strings, other references, safety).
3. Move quoting into `format_opargs`.
4. Unit tests (below).
5. Verify (below).

## Verification

1. **Unit tests**, using a real `Heap`:
   - `local`: the first and last local of a function, an offset that is out of
     range, and `nlocals` unknown.
   - Strings: empty; short; exactly 16 characters (unchanged); 17 characters
     (first 13 + `...`); quotes, backslash, newline and a control character
     escaped; a multi-byte character straddling the cut; invalid UTF-8; the
     result always parses as JSON, as an object with exactly the keys `key`
     (`"string"`) and `value`.
   - Other references: a function object ("function &0x..."), a datakey object
     ("datakey &0x..."), a pointer to an object with an unknown datakey
     ("unknown &0x..."), a pointer outside the pool ("unknown &0x...").
   - The existing raw/pointer/tagged tests, updated.
2. **Flag off:** build, run all tests, and compare the `threaded_impl`
   disassembly with the pre-logging code, as before.
3. **Flag on**, from a scratch directory with copies of the compiler's bundles:
   - `helloworld` shows the string operand as
     `{"key": "string", "value": "Hello, world!"}`.
   - `poplocal` and `triangle` show `local N` for the local-variable instructions
     and still `0d..,0x..` for jumps. A script checks the expected kind per
     opcode, as in Step 4.
   - **Cross-check the indexes against the compiler.** Each bundle stores every
     function's instructions as JSON with the `index` fields (readable with
     `sqlite3`). Run `poplocal` and compare the logged `local N` of each local
     instruction with the `index` the compiler gave that instruction. This also
     checks that `get_function_nlocals` agrees with the loader's `nlocals`.
   - A long string: make a copy of a bundle, edit its string literal with
     `sqlite3` (the bundles are SQLite files) to 20 characters including a quote
     and a non-ASCII character, and check the logged form is the first 13
     characters + `...`.
4. Hard-kill check that the truncated last line still has valid `opargs`.
5. Delete any stray `run.*.jsonl`; leave `trace.hpp` as the user wants it.

## Decisions

1. **`local DDD` is the index** (the compiler's local number), not the stored
   offset.
2. **Strings are objects**, `{"key": "string", "value": "..."}`, not bare JSON
   strings. This removes the ambiguity between a string literal and a
   description, and leaves room for expansion.

## Open questions (with my suggestions)

1. **Other references** (the `TYPE CONTENTS` rule). The step asks for the text
   `TYPE CONTENTS`. Now that strings are `{"key": "string", ...}`, where `key` is
   the datakey's name, it would be consistent for other references to be
   `{"key": "function", "value": "&0x7f0012345670"}`, rather than a text
   `"function &0x7f0012345670"`. [Use the object form, so every heap reference is
   keyed by its type the same way. Today no real operand is a non-string
   reference, so this is only exercised by unit tests.]
2. **Count characters as code points** (above) rather than bytes. [Yes.]
3. **`TYPE` names** come from a fixed lookup of the three known datakeys, with
   `unknown` otherwise, until datakeys carry names. [Yes.]
