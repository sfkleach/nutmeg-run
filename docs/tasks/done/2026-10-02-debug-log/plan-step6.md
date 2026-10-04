# Plan for Step 6: printing functions correctly

## Goal

Where an instruction refers to a function, the log shows `fn NAME` when the name is
available, instead of an address. As before, no run-time cost when
`ENABLE_INSTRUCTION_LOG` is off.

## Review of the requirement

The step says the operands are "tagged pointers to the functions". In the code, they are not.

### What the operands really are

| Operand                                 | Instructions                                                     | What it is                              |
|-----------------------------------------|------------------------------------------------------------------|-----------------------------------------|
| `Ident*` (untagged, `OP_PTR` today)     | `CALL_GLOBAL_COUNTED`, `..._LAZY`, `PUSH_GLOBAL`, `..._LAZY`, `DONE`, `IN_PROGRESS` | a global binding; its `cell` field holds the value |
| `Cell*` function object (untagged)      | `LAUNCH`                                                         | the entry-point function                |
| C++ function pointer (untagged)         | `SYSCALL_COUNTED`                                                | a built-in such as `println` or `+`     |
| tagged pointer to a function object     | none today (`PUSH_VALUE` only holds ints, bools and strings)     | would print as `{"key": "function", ...}` |

So the call instructions do not hold the function; they hold an `Ident`, whose `cell`
(a tagged pointer to a function object) is the function. The plan works from the
`Ident`, and also covers `LAUNCH`, the syscalls, and (for completeness) a tagged pointer
to a function.

### Where names come from

Names exist in only one place: `Machine::globals_` maps name to `Ident*`. Neither
`Ident` nor a function object stores its own name. So a name is found by **reverse
lookup**: scan `globals_` for the entry whose `Ident*` is the operand (for an `Ident`
operand), or whose `Ident::cell` is the tagged function pointer (for `LAUNCH` and
tagged pointers). Syscall names come from reversing `sysfunctions_table`.

Reverse lookup is a linear scan, which is fine for a debugging aid. It costs nothing
when the log is off, and it leaves `Ident` unchanged. (Storing the name in `Ident`
would be O(1) but would change a hot data structure for the sake of a debug feature.)

### What gets printed for an `Ident`

Not every global is a function. Lazy globals are top-level constants: a `lazy` `Ident`
holds a thunk until first use, after which it holds the computed value and `lazy` is
cleared. If the rule were "does `cell` currently hold a function?", the same
`PUSH_GLOBAL` of a constant would print `fn answer` the first time and an address
afterwards. So the rule looks at the `Ident`, not just its current contents:

| Case                                                          | Printed             |
|---------------------------------------------------------------|---------------------|
| name found, `Ident` is not lazy and `cell` is a function      | `fn NAME`           |
| name found, otherwise (lazy constant, or a non-function value) | `global NAME`       |
| the operand is not a known `Ident`                            | `&0x...` (as now)   |

`global NAME` is a small addition to what the step asks for; see Open Questions.

Other operands:

| Operand                                  | Printed                                   |
|------------------------------------------|-------------------------------------------|
| `LAUNCH`'s function object, name found   | `fn NAME`; otherwise `&0x...`             |
| syscall pointer, name found              | `sys NAME`; otherwise `&0x...`            |
| tagged pointer to a function, name found | `fn NAME`; otherwise the Step 5 object form |

If one function has several names (an alias), the lookup picks the
lexicographically smallest, so the output is deterministic.

### Safety

The operand is an untrusted pointer. An `Ident*` is **never dereferenced until it has
been found in `globals_`** by comparing addresses, so a damaged operand cannot make the
logger crash.

## Design

### Resolving names without coupling the formatters to `Machine`

`machine.hpp` includes `instruction_log.hpp`, so the formatters cannot use `Machine`
directly. Instead:

- `instruction_log.hpp` declares an abstract `NameResolver` with three methods:
  - `global_at(const void* ident)`: if `ident` is a known global, its name and whether
    it is a (non-lazy) function.
  - `function_name(const Cell* function_object)`
  - `sys_function_name(const void* function)`
- The three lookups return `std::optional`, so "not available" is explicit.
- `Machine` provides the real resolver (`MachineNames`, defined in `machine.cpp` and
  declared in `machine.hpp` so tests can use it). Unit tests of the formatters use a
  tiny fake resolver, so they need no `Machine`.

The formatter's growing parameter list (`heap`, `nlocals`, now `names`) is bundled
into one `OpArgContext { Heap& heap; int nlocals; const NameResolver& names; }`.
`format_opargs` and `log_entry` take it in place of the separate arguments.

### Operand kinds

- `OP_GLOBAL` (new): an `Ident*`. Replaces `OP_PTR` at the `PUSH_GLOBAL`,
  `PUSH_GLOBAL_LAZY`, `CALL_GLOBAL_COUNTED`, `..._LAZY`, `DONE` and `IN_PROGRESS`
  call sites.
- `OP_FUNCTION` (new): a raw pointer to a function object. Used by `LAUNCH`.
- `OP_SYSCALL` (new): a built-in function pointer. Used by `SYSCALL_COUNTED`.
- `OP_PTR` remains for plain pointers, though no instruction would use it after this
  step. It stays as the fallback rendering and for future use.

### Cost when disabled

As in Steps 3 to 5: the resolver is constructed inside the macro's
`if constexpr (ENABLE_INSTRUCTION_LOG)`, as a temporary, so nothing is emitted or
stored when the log is off. Verified by comparing the disassembly of
`threaded_impl` with the pre-logging code.

## Steps

1. Add `NameResolver`, `OpArgContext` and the three new kinds; refactor the
   formatter signatures; update the existing tests to match.
2. Implement the rendering rules above, with unit tests using a fake resolver.
3. Implement `MachineNames` on `Machine` and test it against a real `Machine` with
   defined globals.
4. Change the 18 call sites' kinds (10 instructions affected).
5. Verify (below).

## Verification

1. **Unit tests** with a fake resolver: function `Ident` (`fn NAME`); lazy `Ident` and a
   non-function `Ident` (`global NAME`); unknown `Ident` (address); `LAUNCH`-style
   function object, known and unknown; syscall, known and unknown; a tagged pointer
   to a function; names that need JSON escaping or look like operators (`+`, `===`, a
   name with a quote); an alias picks the smallest name.
2. **`MachineNames` tests** with a real `Machine`: define a function and a lazy constant
   with `define_global`; check each lookup, that an unrelated pointer is not found,
   and that nothing is dereferenced for it.
3. **Flag off:** build, run all tests, and compare the `threaded_impl` disassembly
   with the pre-logging code, as before.
4. **Flag on**, from a scratch directory with copies of the compiler's bundles:
   - `poplocal`: `CALL_GLOBAL_COUNTED` shows `fn poplocal`, `LAUNCH` shows `fn main`,
     the syscalls show `sys println` / `sys +` or similar. Cross-check each name
     against the `bindings` table in the bundle.
   - `triangle` (calls itself recursively): a script checks every global operand is
     `fn NAME` or `global NAME` with a name that exists in the bundle.
   - Find a bundle with a lazy binding (`bindings.lazy = 1`), and check that its
     `PUSH_GLOBAL_LAZY` and later `PUSH_GLOBAL` of the constant both print
     `global NAME`, not `fn NAME` and then an address.
5. Hard-kill check; delete any stray `run.*.jsonl`; leave `trace.hpp` as the user wants it.

## Open questions (with my suggestions)

1. **The premise.** The operands are `Ident*` (a global binding), not tagged function
   pointers; the function is the `Ident`'s `cell`. The plan handles this. Is that what
   you expected?
2. **Globals that are not functions** (lazy constants such as `answer`): print
   `global NAME`, or leave them as an address? [`global NAME`: it is the same
   information and avoids a confusing mix of `fn` and addresses.]
3. **Built-in functions** (`SYSCALL_COUNTED`): `sys NAME`, or leave as an address?
   [`sys NAME`: they are functions that are called, and the name is available.]
4. **Shape:** plain strings `"fn NAME"` as the step says, or objects like the
   Step 5 heap references? [Plain strings. The objects are for heap values, keyed by
   type; a named binding is not a heap value, and since string literals are now
   objects, `"fn main"` can no longer be mistaken for one.]
5. **Name lookup** by reverse scan of the globals, leaving `Ident` unchanged, or by
   adding a name to `Ident`? [Reverse scan.]
