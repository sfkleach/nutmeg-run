# Plan for Step 2: a facility for growing the value stack

## Goal

The value stack can be made bigger while the program runs, without moving. It never
shrinks. This step provides and tests the mechanism and makes the existing software overflow
checks use it; Step 3 adds the second trigger, a fault in the top guard page, so that the
checks can be left out of pushes known to have room. Same scope as Step 1: the value stack only (`Guard::Pages`); the
return stack stays as it is.

Decisions carried over from Step 1 and `q-and-a.md`:

- Growth is **in place**: reserve a large range of address space up front and commit it on
  demand, so `base_`, `top_` and every raw `Cell*` into the stack remain valid. Moving the
  stack on growth is ruled out.
- The software checks stay, and the multi-cell operations keep theirs.
- The stack is never shrunk.

## Review of the current code (after Step 1)

`CellStack` with `Guard::Pages` maps `[guard][data][guard]` in one `mmap`, where the data
pages are `PROT_READ|PROT_WRITE` and the two guards are `PROT_NONE`. `base_` is the first data
cell, `limit_` is one past the last, and `capacity_` is the data size in cells (rounded up to a
whole page). Everything that checks bounds uses `limit_` (`push`, `push_multiple`,
`move_multiple`'s target) or `capacity_` (`resize`) or `base_`.

That is already reserve-and-commit with nothing in reserve. Step 2 puts address space after
the committed data:

```
| lower guard | committed data pages (rw) | uncommitted pages (PROT_NONE) ...... |
^ mapping     ^ base_                     ^ limit_                               ^ end of reservation
```

The **top guard is now the first uncommitted page**, and everything above it up to the end of
the reservation is also `PROT_NONE`. A push past `limit_` therefore still faults exactly as in
Step 1, and growing is just `mprotect` of the pages above `limit_` to read/write and moving
`limit_` up.

## Design

### Reservation and commit

`CellStack` gains a `reserve` (maximum size in cells) alongside `capacity` (the initial,
committed size):

```cpp
explicit CellStack(size_t capacity = DEFAULT_CAPACITY, Guard guard = Guard::None,
                   size_t reserve = VALUE_STACK_RESERVE_BYTES / sizeof(Cell));
```

- `reserve` is used only with `Guard::Pages`; it is raised to at least `capacity` and rounded up
  to whole pages.
- The mapping is `lower guard page + reserve bytes`, created with `PROT_NONE` and
  `MAP_NORESERVE`, then the first `capacity` bytes of data are made read/write, as now.
- The default reserve is `VALUE_STACK_RESERVE_BYTES` (64 MiB) from the new header
  `src/stack_config.hpp` (see Open Questions, 1). A `PROT_NONE` reservation uses no memory and
  is not counted against overcommit.
- New members: `reserve_cells_`. `capacity_` now means **committed** cells, so it grows.
  `mapping_bytes_` is the whole reservation, so the destructor's `munmap` is unchanged.
- `Guard::None` stacks have `reserve == capacity` and cannot grow.

### Growing

```cpp
// Commits more of the reserved range. Returns false if the reserve is exhausted or the OS
// refuses. Noexcept and allocation-free, so that Step 3 can call it from a signal handler.
bool try_grow(size_t min_cells) noexcept;

// As try_grow, but throws std::runtime_error("Stack overflow: reserve exhausted").
void grow(size_t min_cells = 0);
```

`try_grow(n)` makes the committed size at least `n` cells (and always strictly larger than now
when `n` is 0 or too small). With `m` the current committed size in bytes, the target is the
**coefficient policy**
`m' = m + A*(m >> 0) + B*(m >> 1) + C`, with `A`, `B` and `C` constants in `stack_config.hpp`
(default `A = 1`, `B = 0`, `C = 0`, which is `m + m`, i.e. doubling). The growth factor is
`1 + A + B/2` and it needs only shifts, multiplications by small integers and adds. For example
`A = 0, B = 1` gives 1.5x, `A = 1, B = 1` gives 2.5x, and `C` adds a fixed number of bytes. The
scheme extends by adding terms `(m >> N) * coefficient[N]`, so the coefficients are held as an
array (`{A, B}` today) and `next_committed_bytes` loops over it; a third entry would allow
1.25x (`{0, 0, 1}`). The new size is
`max(m', bytes needed for n cells, m + one page)`, then clamped to the reserve and rounded up
to whole pages. (The "one page" floor guarantees progress even for an unhelpful policy such as
`G = 1`, `K = 0`.) It then does one
`mprotect(base_ + capacity_, new - capacity_, PROT_READ|PROT_WRITE)` and sets `capacity_` and
`limit_`. If nothing more can be committed (`capacity_ == reserve_cells_`) it returns `false`.
The cells are zero-filled by the OS and the contents of the existing stack are untouched.

Why a noexcept core: only `mprotect` and plain stores, which are async-signal-safe (unlike
`new` or exceptions). Writing it that way now avoids rewriting it in Step 3. `grow` is the
convenient throwing wrapper for ordinary code and tests.

Two accessors are needed by the tests and by Step 3: `capacity()` (committed cells) and
`reserve()` (maximum cells).

### The soft checks grow the stack

Every software overflow check has the shape "if it would pass `limit_`, throw". Each becomes
"if it would pass `limit_`, grow, and throw only if that fails". There are four of them:

| Check                                   | Needs room for              |
|-----------------------------------------|-----------------------------|
| `push`: `top_ >= limit_`                | 1 cell                      |
| `push_multiple`: `top_ + count > limit_` | `count` cells              |
| `move_multiple`: `target.top_ + count > target.limit_` | `count` cells in the *target* |
| `resize`: `new_size > capacity_`        | `new_size` cells in total   |

The slow path is one small private function, so the inline code stays as small as it is today:

```cpp
// Makes room for `needed` more cells, or throws std::runtime_error("Stack overflow").
[[gnu::cold, gnu::noinline]] void make_room(size_t needed_total_cells);
```

called as `if (top_ >= limit_) make_room(size() + 1);` with the failure message unchanged
("Stack overflow", "Target stack overflow during move"). For `move_multiple` the *target's*
`make_room` is used, so a guarded target grows and an unguarded one (the return stack) throws
exactly as now. An unguarded stack's `try_grow` returns `false`, so its behaviour is the same
as before.

These checks exist only when `ENABLE_STACK_CHECKS` is on. With the checks off there is no
growth until Step 3's fault handler provides it. That is intended: Step 3 is what makes the
checks removable.

### What does not change

- The common path of `push` and the rest is unchanged: the test is still `top_ >= limit_`
  (`limit_` now moves), and nothing is added when there is room. Only what happens when the
  test fails changes (below).
- **The soft overflow checks now grow the stack** (decided, Open Questions 3). Where they used
  to throw "Stack overflow" they first try to commit more, and throw only if the reserve is
  exhausted (or the stack is unguarded). So the value stack is genuinely growable as soon as
  this step is done, without any signal handling; Step 3's fault handler later makes the
  checks removable.
- `resize()` currently throws if `new_size > capacity_`; it now grows to `new_size` first, as
  for the pushes.

### `Machine`

`operand_stack_{DEFAULT_CAPACITY, Guard::Pages}` becomes
`operand_stack_{VALUE_STACK_INITIAL_BYTES / sizeof(Cell), Guard::Pages, VALUE_STACK_RESERVE_BYTES / sizeof(Cell)}`,
taking both sizes from `stack_config.hpp`. The `Machine` gets no new
methods: nothing calls `grow` yet outside the tests.

## Steps

1. Add `src/stack_config.hpp`. Reservation: the `reserve` parameter, `reserve_cells_`, the larger mapping, and
   `capacity()`/`reserve()`.
2. `try_grow` and `grow`.
3. Pass the reserve from `Machine`.
4. Tests (below).
5. Verify (below).

## Tests (added to `tests/test_cell_stack.cpp`)

Use a small reservation (for example a one-page start with a reserve of eight pages) so the
tests are quick and can reach the end of the reserve:

- **Initial state:** `capacity()` is the rounded initial size, `reserve()` the rounded reserve.
- **Growth, values preserved, addresses stable:** fill the stack, remember `&peek_at(0)`, the
  address of the last element and the cells' values, call `grow()`, and check the capacity has
  increased, the addresses are unchanged, the old values are intact, and the stack can now
  take more pushes (the soft check allows them) up to the new capacity.
- **Policy:** the committed size after each growth equals the formula
  (`m + A*m + B*(m >> 1) + C`, page-rounded, clamped to the reserve) computed in the test from
  the constants, so the test follows whatever the constants are; `grow(n)` with a large `n`
  honours `n`; a request beyond the reserve is clamped to it. The policy is a pure function
  (`next_committed_bytes(committed, needed, reserve)`, a `constexpr` free function in
  `cell_stack.hpp`), so it is also tested directly with a few explicit policies and edge values
  (including a policy that would not progress, and the largest values, to show nothing
  overflows), independently of the constants.
- **Automatic growth:** with a one-page start, push more cells than fit; `push` grows the stack
  and the values are all there afterwards (the same for `push_multiple`, a `move_multiple`
  *into* a guarded stack, and `resize` up). Addresses stay stable.
- **Reserve exhausted:** once `capacity() == reserve()`, `try_grow` returns `false` and `grow`
  throws `runtime_error`; the next push throws "Stack overflow" (a `runtime_error`, message
  unchanged), and so does `push_multiple` / `resize` past the reserve, leaving the stack
  intact.
- **Unguarded stacks still throw** "Stack overflow" when full, as before, including as the
  target of `move_multiple` (the case used by the machine for the return stack).
- **Guards still work after growth:** in a forked child, a write one cell past the *new* top
  faults (`SIGSEGV`), and so does the cell below the base. And one past the **end of the
  reserve** faults too (the last page of the reservation is `PROT_NONE`).
- **Old top is no longer a fault:** the cell that was the first guard cell before the growth is
  now writable (no signal).
- **Move/bulk operations after growth:** `push_multiple`, `move_multiple` into a grown stack
  and out of one, and `resize` up to the new capacity, behave correctly.
- **Unguarded stacks** (`Guard::None`) cannot grow: `try_grow` returns `false`.
- **Configuration:** the `static_assert`s in `stack_config.hpp` hold; a default `Machine`'s value
  stack has `reserve() * sizeof(Cell) == VALUE_STACK_RESERVE_BYTES`; its capacity
  starts at the default; `Machine` construction does not touch the reserve (see verification).

## Verification

1. **Build and all tests**, reading the *whole* output of fork-based tests this time (the
   Step 1 lesson): no stray "FAILED" or "fatal error" text.
2. **Hot path unchanged:** the overflow branch changes (an inline `throw` becomes a call to the
   cold `make_room`), so the disassembly of `threaded_impl` will *not* be identical to the
   previous commit as it was in Step 1. Compare the two with the overflow paths in mind: the
   sequence leading to the `top_ >= limit_` test (the load, the compare and the branch) and the
   store/increment after it should be the same in each handler, and the differences should be
   confined to the body of that branch. If a handler gets a different fast path, investigate.
3. **Real programs still run:** `poplocal`, `triangle` and `helloworld` from the scratch
   directory.
4. **The reservation is not paid for:** start a `Machine` and compare `VmSize` and `VmRSS` in
   `/proc/self/status` (from a small test or program) with before: `VmSize` rises by about the
   reserve (64 MiB), `VmRSS` by almost nothing. This is the claim that justifies reserving a lot.
5. **Layout:** `/proc/self/maps` shows the guard, the read/write prefix, and one `---p`
   region for the uncommitted rest; after `grow`, the read/write region has lengthened and the
   address of its start has not changed.
6. **Growth under a real program shape:** a `Machine` test (and, if it can be done without a
   new compiler bundle, a hand-built bundle) whose value stack goes beyond the initial 64K cells
   and comes back, checking the result and that `capacity()` has increased.

## Looking ahead (Step 3)

- Growth already works through the soft checks. What Step 3 adds is the fault handler, which
  makes it possible to leave the checks out of pushes proved to have room. The signal handler
  checks that the faulting address lies in `[limit_, limit_ + page)`, calls `try_grow`, and
  returns so the faulting store is retried. A fault anywhere else (below
  the base, or past the end of the reserve) must fall through to the default action. `try_grow`
  is written to be callable there.
- **`limit_` and the compiler.** The software check reads `limit_`; once a handler can change
  it behind the interpreter's back, the check must not be cached in a register across a push.
  Step 3 will decide whether the soft check goes for the instructions that are proved safe or
  whether `limit_` needs to be `volatile`-like. Nothing in Step 2 depends on this.
- Retrying the instruction is only safe if the instruction has not yet changed machine state
  when its store faults. That matters for `PUSH_*` (store, then increment `top_`) and is the
  "different push instructions" design question, so it is left to Step 3.
- The value stack lives in its own mapping, so a fault there does not disturb the thread's
  own stack and the handler runs normally. (A `sigaltstack` is a concern for the return-stack
  task, not this one.)

## Open questions (with my suggestions)

1. **How big a reserve?** *Decided:* the hard limit is **64 MiB** (8M cells), a compile-time
   constant in its own header, `src/stack_config.hpp`, so it is easy to find and change:

   ```cpp
   inline constexpr size_t VALUE_STACK_INITIAL_BYTES = 512 * 1024;       // Committed at start.
   inline constexpr size_t VALUE_STACK_RESERVE_BYTES = 64 * 1024 * 1024;  // Hard limit.

   // Growth policy: committed' = committed + sum_N(COEFFICIENTS[N] * (committed >> N)) + ADD_BYTES,
   // so the default {1, 0} and 0 doubles. See CellStack::try_grow.
   inline constexpr std::array<size_t, 2> VALUE_STACK_GROWTH_COEFFICIENTS = {1, 0};  // A, B
   inline constexpr size_t VALUE_STACK_GROWTH_ADD_BYTES = 0;                         // C
   ```

   (The initial size is today's `CellStack::DEFAULT_CAPACITY`, 64K cells; I would move it
   there too so the two sit side by side. Both are in bytes, and `static_assert`s check that the
   reserve is at least the initial size, that both are multiples of `sizeof(Cell)`, that
   the policy cannot overflow: `VALUE_STACK_RESERVE_BYTES` times the sum of the coefficients,
   plus `ADD_BYTES`, fits in a `size_t`.) At
   64 MiB a reservation is also unlikely to trouble `ulimit -v` or sanitizer builds. [Name of
   the header is my choice; rename if you prefer.]
2. **Growth policy.** *Decided:* the coefficient policy
   `m' = m + A*(m >> 0) + B*(m >> 1) + C`, extensible to further `(m >> N)` terms, defaulting
   to doubling (`A = 1, B = 0, C = 0`), with the constants in `stack_config.hpp`. Only shifts,
   small multiplications and adds, so `try_grow` stays usable from a signal handler in Step 3 (no
   floating point or division) and overflow-free (the committed size is at most the 64 MiB
   reserve, and a `static_assert` covers the coefficients). Committing is cheap (the pages are
   not backed by memory until touched), so the policy is a tuning knob rather than a correctness
   matter. Held as an array so that more terms need no code change. If this feels like more
   than the task needs, the array can shrink to two plain constants without affecting anything
   else.
3. **Should the soft overflow check call `try_grow` now?** *Decided: yes,* since that is where
   we are going anyway. The soft checks grow the stack; Step 3 adds the hardware path and lets
   the proved-safe pushes drop their checks. (See "The soft checks grow the stack" above.)
4. **`try_grow` noexcept and signal-safe**, with a throwing `grow` wrapper, so the same core is
   reused by Step 3? [Yes.]
5. **Never shrinking** means a one-off deep recursion keeps its memory for the life of the
   process. Fine for now, per the task. [Yes; no `shrink` or `madvise` in this task.]

## Notes from the implementation

Where the implementation differs from the plan above:

- **A trailing guard page.** The mapping is `lower guard + reserve + upper guard`, not just
  `lower guard + reserve`. Without the upper guard, a stack grown to its full reserve would have
  no protected page after it. (Short of that, the first uncommitted page is the guard.)
- **`try_grow(n)` with `n` beyond the reserve** returns `false` and changes nothing, rather than
  clamping to the reserve and returning `true` with fewer cells than asked for.
- **`DEFAULT_CAPACITY` stays** in `CellStack` (the unguarded return stack still uses it);
  `Machine` takes the value stack's initial and reserve sizes from `stack_config.hpp`.
  `CellStack::DEFAULT_RESERVE` is derived from `VALUE_STACK_RESERVE_BYTES`.
- **Disassembly:** `threaded_impl` is identical to the previous commit at `-O0`, because it only
  calls the stack functions. The change is inside `CellStack::push` etc.: the same load, compare
  and branch, then the same store and increment; only the overflow branch differs (a call to
  `make_room` instead of an inline `throw`), and `push` is smaller.
- **Measured** (`CellStack` with the default 512 KiB / 64 MiB): `VmSize` rises by 65.5 MB on
  construction, `VmRSS` by 68 kB; after growing to 2 MiB and using 1.5 MB, `VmRSS` rises by
  about the amount used; `base_` does not move. `/proc/self/maps` shows `---p` (4 KiB), then
  the `rw-p` committed region.
