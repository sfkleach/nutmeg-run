# Plan for Step 1: guard pages at the base and top of the value stack

## Goal

The value (operand) stack gets a protected page immediately below its first cell and
another immediately above its last cell, so that a stray push past the top or pop past the
bottom faults instead of silently reading or corrupting adjacent memory. Unit tests show it.

Decisions already made (`q-and-a.md`):

- Only the **value stack** (`Machine::operand_stack_`). The return stack is a separate task.
- Step 1 **relies on the crash**: no signal handler. The tests fork a child process and
  check how it died.
- The **software checks stay** (`ENABLE_STACK_CHECKS` is unchanged), for the better error
  messages. The guard pages are a hardware backstop behind them.
- The guard size is **one page**. The bulk operations keep their soft checks.
- Step 3 will grow the stack by reserving address space and committing it on demand, so Step 1
  is built to match.

## Review of the current code

`CellStack` (`src/cell_stack.hpp`) is a fixed array from `new Cell[capacity_]` (default
65536 cells = 512 KiB), with `base_`, `top_` and `limit_` pointers. The same class backs both
stacks of `Machine`, and `move_multiple` takes a `CellStack&` target (it moves arguments from
the value stack onto the return stack, using the target's private pointers). So the stack
type has to stay one class, usable both with and without guards.

What each end looks like to the hardware:

| Operation                        | Touches                          | Caught by                         |
|----------------------------------|----------------------------------|-----------------------------------|
| `push` when full                 | `*limit_` (write)                | top guard page                    |
| `pop` / `peek` when empty        | `*(base_ - 1)` (read)            | bottom guard page                 |
| `peek_at`, `offset_from_top` with a large index | `base_[k]`, `top_ - k` | soft check only (can jump a page) |
| `push_multiple`, `pop_multiple`, `move_multiple`, `discard_multiple`, `resize` | up to `count` cells away | soft check only |

So the guards are a backstop for the single-cell stray, which is the commonest fault from
bad generated code. The underflow case is a *read*, so the guards must be unreadable as well
as unwritable. The task text says "write protected"; the pages will be `PROT_NONE` (see
Open Questions).

## Design

### One class, two storage modes

`CellStack` gains a second constructor parameter:

```cpp
enum class Guard { None, Pages };
explicit CellStack(size_t capacity = DEFAULT_CAPACITY, Guard guard = Guard::None);
```

- `Guard::None`: today's behaviour (`new Cell[capacity]`), used by the return stack.
- `Guard::Pages`: the storage described below, used by `Machine::operand_stack_`.

`push`, `pop`, `peek` and the rest are untouched, so there is no change to the hot path and
no new branch in the interpreter. Only the constructor, the destructor and (for
`Guard::Pages`) the allocation differ. The destructor chooses `munmap` or `delete[]` from a
stored flag. `base_`, `top_` and `limit_` mean exactly what they do now.

### Storage with guards (Linux)

One anonymous mapping, laid out in whole pages (`page = sysconf(_SC_PAGESIZE)`, never
hard-coded):

```
| guard page (PROT_NONE) | data pages (PROT_READ|PROT_WRITE) | guard page (PROT_NONE) |
^ mapping               ^ base_                              ^ limit_
```

1. Round the requested capacity up to a whole number of pages
   (`data_bytes = round_up(capacity * sizeof(Cell), page)`; `capacity_` becomes
   `data_bytes / sizeof(Cell)`). The default of 65536 cells is already a whole number of 4 KiB
   pages, so nothing changes for the machine.
2. `mmap(nullptr, page + data_bytes + page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)`.
3. `mprotect(mapping + page, data_bytes, PROT_READ | PROT_WRITE)` for the data pages.
4. `base_ = top_ = mapping + page`; `limit_ = base_ + capacity_`. The top guard begins exactly
   at `limit_`, so the first cell past the end is the first protected byte, and the cell
   before `base_` is protected too.
5. If `mmap` or `mprotect` fails, throw `std::bad_alloc` (after unmapping).

Reserve-then-commit is already the shape of this code: the whole range starts `PROT_NONE`
and only the data part is made accessible. Step 2 then reserves a much larger range and
commits a growing prefix; Step 3 hooks the fault. The allocation is kept in one small helper
so Step 2 can reuse it.

Anonymous pages are zero-filled and untouched pages cost no physical memory, which is
convenient, though `new Cell[]` did not promise either.

The code is Linux/POSIX specific (`<sys/mman.h>`), as the interpreter already requires
GCC/Clang for computed goto.

### `Machine`

`CellStack operand_stack_;` becomes `CellStack operand_stack_{CellStack::DEFAULT_CAPACITY, Guard::Pages};`
(the default capacity constant becomes public) and `return_stack_` is unchanged.

## Steps

1. Add `Guard`, the guarded allocation helper, the destructor change and the flag to
   `CellStack`.
2. Switch `Machine::operand_stack_` to `Guard::Pages`.
3. Add `tests/test_cell_stack.cpp` (below).
4. Verify (below).

## Tests (`tests/test_cell_stack.cpp`)

Ordinary behaviour (both modes, so the guarded stack is shown not to have changed
semantics): push/pop/peek order, `size`, `empty`, `push_multiple`, `pop_multiple`,
`move_multiple` between a guarded and an unguarded stack and between two guarded ones, and
the **soft checks still throw** `runtime_error` ("Stack overflow" when full, "Stack
underflow" when empty) in the guarded stack. That last one shows the soft check fires before
the guard page is reached. (These tests assume `ENABLE_STACK_CHECKS`; guard them with
`if constexpr` so they survive turning it off.)

Guard behaviour, with a small guarded stack (for example one page of data, so the stack is
quick to fill):

- **No accessor is needed to find the guards.** For the top guard, fill the stack (the soft
  check allows exactly `capacity` pushes), then `&stack.peek() + 1` is the first guard cell.
  For the bottom, push one cell and `&stack.peek_at(0) - 1` is the cell below the base.
- A **positive control**: the last data cell and the first data cell can be read and written
  without a fault.
- **Faulting tests run in a forked child**, because a test cannot survive its own `SIGSEGV`:

  ```cpp
  // Runs `action` in a child; returns the signal that killed it, or 0 if it survived.
  int signal_from(const std::function<void()>& action);
  ```

  The child calls `setrlimit(RLIMIT_CORE, 0)` first so that the crash does not write a core
  file or invoke the system crash reporter, runs `action` with a `volatile` access so that
  the compiler cannot remove it, and ends with `_exit(0)` (never `exit` or a Catch2
  assertion: the child must not run the test framework's or `atexit` handlers). The parent
  `waitpid`s and reports `WIFSIGNALED ? WTERMSIG : 0`.
- Cases: write one cell past the top; read one cell past the top; read the cell below the
  base (the underflow case); write the cell below the base; each expects `SIGSEGV`. The same
  four accesses on an **unguarded** stack's neighbours are *not* tested (that would be
  undefined behaviour in the harness).
- The `Machine`'s own value stack is guarded: a test that fills `operand_stack_` through the
  machine and writes through `&peek() + 1` in a child dies with `SIGSEGV`.

## Verification

1. **Build and all tests**, with the new file, and no new compiler warnings.
2. **Zero cost on the hot path**: the stack member functions are unchanged and inline, so
   the disassembly of `Machine::threaded_impl` with the flags off should be identical to the
   previous commit. Compare as in Step 3 of
   `docs/tasks/done/2026-10-02-debug-log/plan-step3.md`.
3. **Real programs still run**: run `poplocal`, `triangle` and `helloworld` copies from the
   scratch directory, as before. (Not `nfib`: it takes ~110 s.)
4. **A real overflow is caught**: temporarily run a hand-built bundle that recurses on the
   value stack, or use a small-capacity `Machine` in a test child, with the soft checks
   disabled (`ENABLE_STACK_CHECKS = false` in a scratch build), and confirm the process dies
   with `SIGSEGV` at the guard rather than corrupting memory. This also confirms that the
   guard pages, not the soft checks, are what is catching it.
5. Check `/proc/<pid>/maps` of a running machine (or in a test) shows the data pages as
   `rw-p` between two `---p` pages.

## Looking ahead (not part of this step)

- **Step 2** keeps `base_` fixed and reserves a large `PROT_NONE` range, making a growing
  prefix `PROT_READ|PROT_WRITE`. The "top guard" becomes simply the first uncommitted page,
  and `limit_` moves as pages are committed. Because the range never moves, `top_`, `base_`
  and any raw `Cell*` into the stack stay valid, which is what Step 3 needs.
- **Step 3** adds a `SIGSEGV` handler that, if the faulting address is in the top guard,
  commits more pages and returns so the CPU retries the store. The software `push` check
  would then be what we remove for provably safe pushes; the question of two kinds of push
  instruction (question 6 of the Q&A) belongs there.
- **Fork-based tests** are the pattern for Step 3's tests too, for the unrecoverable cases
  (reserve exhausted).

## Open questions (with my suggestions)

1. **`PROT_NONE` for the guards** (neither readable nor writable) rather than only
   write-protected as the task says? The underflow case is a read of `base_ - 1`, which a
   write-protect-only page would not catch. [`PROT_NONE`.]
2. **One class with a `Guard` flag** or a separate guarded class? A separate class makes
   `move_multiple` between the two stack types need to be templated. [One class with a flag.]
3. **Capacity rounded up to whole pages**, silently, so the data region is page aligned and
   the guard begins exactly at `limit_`. [Yes; no change for the default size.]
4. **Failure to map**: throw `std::bad_alloc`. [Yes.]
5. **Core files in the test children**: suppress with `RLIMIT_CORE = 0`, so a failing test
   run does not litter the tree or hang in a crash reporter. [Yes.]
