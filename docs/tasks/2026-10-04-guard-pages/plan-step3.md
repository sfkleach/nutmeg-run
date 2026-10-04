# Plan for Step 3: catch overflow of the value stack and grow it

## Goal

When a store runs into the value stack's top guard page, grow the stack and carry on, so that
pushes no longer need a software overflow check. The task text:

> When there is an attempt to write into the guard page, we should try to grow the value stack.
> If we succeed we should retry the instruction that failed. [...] We are prepared to generate
> different push instructions that check for stack overflow and those that don't.

Starting point (after Step 2): a guarded `CellStack` reserves 64 MiB, commits a prefix, and
`try_grow()` is a noexcept, allocation-free function that commits more. The software checks call
it, so the stack already grows in practice. The first uncommitted page is the top guard.

## Review: what "retry the instruction" means

The retry is done by the **CPU, not by the interpreter**. When a store faults, the kernel
delivers `SIGSEGV` to a handler; if the handler returns having made the page writable, the CPU
re-executes **just the faulting machine instruction**, with the registers it had. So:

- It does not matter that the C++ statement `*top_++ = value` has other effects: whichever order
  the compiler emitted them in, only the one store is re-run, to the same address, which is now
  committed. Nothing in a `push` can be done twice.
- Bulk writes (`std::fill_n`, `memcpy`) are made of idempotent stores, so restarting part of one
  is also safe.
- So there is **no need for the interpreter to be able to restart a VM instruction**, which was
  the worry in my Step 2 notes ("retrying is only safe if the instruction has not changed
  machine state"). I got that wrong; this is the useful consequence: a push never needs to be
  "proved safe" by the compiler, because every push that runs into the guard is repaired the
  same way.

What the handler can **not** do is throw a C++ exception, so the case where growth fails (the 64
MiB reserve is exhausted) cannot produce today's `runtime_error("Stack overflow")`. It has to be a
fatal error with a message (see Design, "When growth fails").

### Which accesses the guard handles

The first byte that touches an uncommitted page is what faults. For a push that is the cell at
`limit_`, so the faulting address is always in `[limit_, limit_ + page)`. A sequential bulk write
also first faults at `limit_`. A write that skips the first uncommitted page (a huge offset, as
`peek_at` or `offset_from_top` with a big index could produce) faults *beyond* that window and is
**not** repaired: it is a real bug and should kill the process. The multi-cell operations keep
their software checks, as decided in the Q&A.

### "Different push instructions"

The task allows for the compiler generating checked and unchecked pushes. Given the above, it
should not need to: a single-cell push needs no proof of room, because the hardware catches it
and growing fixes it. The only thing a checked push buys is the *nice error* when the reserve is
exhausted, and that applies equally to every push. So the plan makes the choice **in the runtime,
once** (a compile-time constant), not in the instruction set, and requires **no compiler
change**. See Open Questions, 1.

## Design

### 1. A fault handler (`src/guard_fault.hpp/.cpp`)

`SIGSEGV` is handled by one `sigaction` handler with `SA_SIGINFO`, installed once, the first time a
guarded stack is created. It uses only async-signal-safe operations (no allocation, no
exceptions, no stdio, no locks):

1. Save `errno` (the handler may change it).
2. Find the guarded stack whose top guard contains `si_addr` (below).
3. If found: `try_grow()`. On success, restore `errno` and **return**, so the store is retried.
4. Otherwise (not ours, or growth failed): restore the *previous* disposition and return, so
   that the retried instruction faults again and is handled by whoever was there before (Catch2's
   handler under test, the default action otherwise). If growth failed, first `write(2, ...)` a
   one-line message ("value stack overflow: the 64 MiB limit is exhausted"). The message is a
   constant string, with no formatting.

The previous action is saved by `sigaction` at installation. `sigaltstack` is not needed: the
value stack is separate memory, so the thread's own stack is not in trouble when it faults.

### 2. Finding the stack that faulted

A process can have several guarded stacks (several `Machine`s, mostly in tests), so the handler
needs a registry it can search without allocating. A guarded `CellStack` links itself into a
global **intrusive list** in its constructor and unlinks in its destructor. The handler walks the
list and asks each stack:

```cpp
// If `address` lies in this stack's top guard (the page at limit_), grows it. noexcept.
bool CellStack::handle_guard_fault(void* address) noexcept;
```

This relies on the machine being single-threaded (the registry is not locked; the signal arrives
on the faulting thread, which cannot be changing the list at that moment). That is the case
today and is stated in a comment, since a threaded machine would need a lock-free registry.

### 3. Unchecked pushes

`CellStack` gains `push_unchecked(Cell)`: just `*top_++ = value`, with no comparison. A
compile-time constant in `stack_config.hpp`,

```cpp
inline constexpr bool VALUE_STACK_HARDWARE_GROWTH = true;
```

selects, for **guarded** stacks, whether `push` is the unchecked or the checked version. The
interpreter does not change: it keeps calling `operand_stack_.push(...)` (three sites in
`machine.cpp`: `PUSH_VALUE`, `PUSH_LOCAL`, `PUSH_GLOBAL`, plus `Machine::push`, used by the
built-in functions). Unguarded stacks (the return stack) always keep the check, since nothing
catches their overflow. With the constant `false`, behaviour is exactly Step 2's.

`pop`, `peek` and the multi-cell operations keep their soft checks, as agreed. The bottom guard
remains a pure backstop (an underflow is a bug, not something to repair).

### 4. Stale `limit_` after a repair

When the handler repairs a store it changes `limit_` and `capacity_` behind the code that was
running. If a function had already loaded `limit_` into a register, it keeps the old value until it
re-reads it. The only readers are the soft checks of the multi-cell operations. They must treat
"a stale `limit_` says there is no room" as harmless: `make_room(needed)` now begins with
`if (needed <= capacity_) return;` (it reads the member afresh, because it is not inlined), so a
stale check cannot cause a spurious growth or, at the full reserve, a spurious "Stack overflow".
This is a one-line change to the Step 2 code, with a test.

### 5. When growth fails

The reserve (64 MiB) is exhausted, or `mprotect` fails (out of memory). The handler writes the
message and restores the default action, and the process dies with `SIGSEGV`. That is a change from
the Step 2 behaviour (a catchable `std::runtime_error("Stack overflow")` that `main` reports as
`Error: Stack overflow`). Open Questions, 2.

### 6. Events

`try_grow` cannot write to the events log from the handler (file output is not signal safe). A
`stack.grow` event is left to a later task; the stack's `capacity()` is visible to tests.

## Steps

1. `handle_guard_fault`, the list registration in `CellStack`, `push_unchecked`, the
   `VALUE_STACK_HARDWARE_GROWTH` constant, and the `make_room` early return.
2. `guard_fault.hpp/.cpp`: installation, the handler, the previous-action chaining, and a
   test-only `guard_fault_forget_previous_handler()` (so a forked test child can have the default
   action as its "previous").
3. Tests (below).
4. Verify (below).

## Tests (in `tests/test_cell_stack.cpp`, or `tests/test_guard_fault.cpp`)

Most can run **in the test process**, because the handler repairs the fault instead of dying:

- **A store into the guard is repaired:** a guarded stack with a small reserve, filled to its
  capacity; write through `&peek() + 1`. The write succeeds, `capacity()` has grown by the policy,
  the value reads back, and `base_` and the earlier contents are unchanged.
- **Unchecked pushes grow the stack:** `push_unchecked` many times past several growths; all
  values intact, capacity follows the policy, addresses stable.
- **`ENABLE_STACK_CHECKS`-independent:** the checks are `if constexpr`, so the same tests hold
  when the soft checks are off (verified with a scratch build, below).
- **A stale `limit_` is harmless:** `make_room(n)` with `n <= capacity()` returns without
  growing, even at a full reserve.
- **Machine:** build a function from JSON with more `push.int` instructions than the initial
  capacity (say 70,000, generated in the test), execute it, and check the stack size, a few
  values, and that the stack grew. This is the real "retry the instruction" case.

Cases that kill the process run in a forked child (as in Step 1), now with a helper that
**keeps** the handler rather than resetting `SIGSEGV` to the default:

- **Reserve exhausted:** `push_unchecked` past the full reserve. The child dies with `SIGSEGV`,
  and its stderr (captured through a pipe) contains the overflow message.
- **A fault outside the top guard is not repaired:** a write below the base, and a write more than
  a page beyond `limit_` (skipping the guard), each die with `SIGSEGV` and no message, with the
  previous action restored (`guard_fault_forget_previous_handler()` first, so "previous" is the
  default).
- **Another stack's guard:** with two guarded stacks, a fault in the second is repaired for the
  second and does not grow the first.
- **Destroyed stacks are unregistered:** after a stack is destroyed, a fault at its old guard
  address is not repaired (it dies).

## Verification

1. **Build and all tests**, reading the whole output of the fork tests.
2. **The check is gone from the push:** the disassembly of `CellStack::push` (with the constant
   `true`) has no compare and branch (and no `make_room` call), just the store and the
   increment; the interpreter's handlers are unchanged.
3. **Real programs:** `poplocal`, `triangle` and `helloworld`.
4. **With the software checks off** (`ENABLE_STACK_CHECKS = false`, in a scratch build), the
   stack-growing tests still pass, which shows the handler, not the checks, is doing the work.
5. **Cost of the check:** measure the difference with the constant on and off for a
   push-heavy workload. The project builds with `-O0`, where a push is a function call and the
   check is a small fraction of it, so I would measure it at `-O2` as well and report both
   honestly: the point of the task is to be able to drop the check, not a guaranteed speed-up
   of `-O0` runs. (Not `nfib`, which takes ~110 s.)
6. **The handler is installed once** and chained: under `strace -e trace=rt_sigaction`, a
   run installs it once. The test suite's own (Catch2) handler still works after it
   (a failing assertion that crashes is still reported).
7. **A deliberate runaway:** a hand-built bundle that pushes without bound (a function that
   recurses on a value-stack pushing instruction) dies with the overflow message after the
   64 MiB limit, not before and not by corrupting memory.

## Open questions (with my suggestions)

1. **No new push instructions.** Since the hardware repairs every single-cell push, the compiler
   need not distinguish checked and unchecked pushes. Agreed to drop that part of the task
   text? [Yes. Choose checked or unchecked once, in the runtime, by the compile-time constant.
   If you later want an exact error for a runaway program, that is the use for a checked push,
   and it can still be added without touching this design.]
2. **Behaviour when the 64 MiB limit is hit.** With unchecked pushes it is a fatal message and
   `SIGSEGV`, not a catchable `Error: Stack overflow` with exit status 1. Acceptable?
   [Yes, with `VALUE_STACK_HARDWARE_GROWTH = true` by default, as the task is about removing the
   cost of the checks. The alternative, keeping the checked push, is one constant away. The
   process exit status for the overflow is a signal death; `main` cannot catch it.]
3. **Default of the constant.** `true` (checks out of the push) or `false` (Step 2's
   behaviour, handler installed but only a backstop) until you have seen the measurements?
   [`true`; verification step 5 gives you the data to flip it.]
4. **Single-threaded registry.** An unlocked intrusive list of guarded stacks is correct only
   for a single-threaded machine. [Yes, documented in the code. A lock-free registry is for
   whenever threads arrive.]
5. **Handler only for `Guard::Pages` stacks.** The return stack is not guarded (a separate task)
   and is unaffected, and its overflow keeps the soft check. [Yes.]
6. **Test-only hook** `guard_fault_forget_previous_handler()` in the production header, so that
   forked test children can have the default action as the "previous" one. [Yes, clearly named.]

## Notes from the implementation

Where the implementation differs from the plan above:

- **Choosing unchecked pushes happens in `Machine`, not in `CellStack::push`.** A single `CellStack`
  class serves both stacks, so `push` cannot be checked for one and unchecked for the other without a
  run-time branch on every push. Instead `CellStack` has both `push` (checked) and `push_unchecked`,
  and `Machine::push` picks one with `if constexpr (VALUE_STACK_HARDWARE_GROWTH)`. `PUSH_LOCAL` now
  calls `push(...)` rather than `operand_stack_.push(...)`, so that it follows the same choice.
  `PUSH_VALUE` and `PUSH_GLOBAL` already did, and so do the built-in functions.
- **The registry is inside `CellStack`** (a private intrusive list, with `repair_fault` and
  `repair_guard_fault` and a three-valued `GuardFault` result: `NotAGuard`, `Repaired`, `Exhausted`),
  so `guard_fault.cpp` is only the signal glue.
- **The handler is reinstated rather than installed "once".** `install_guard_fault_handler()` checks
  whether our handler is the current one and installs it (saving what it replaced) if not. The test
  framework reinstalls its own `SIGSEGV` handler at the start of every test case, which would
  otherwise displace ours for all but the first test. A real program installs it once (confirmed with
  `strace`: one query and one install).
- **The reserve-exhausted test needed a child process** in two places (the machine test as well),
  since an unchecked push past the limit is fatal. Test helpers moved to `tests/fork_helpers.hpp`
  (`run_in_child` with `Handlers::Default` or `Handlers::Keep`, capturing the child's stderr).
- **The stale-`limit_` early return** is tested through a friend (`CellStackTestAccess`) that reaches
  the private `make_room`.

Measured (`push` against `push_unchecked`, 50 million pushes in a loop, a guarded stack):
`-O0` unchecked takes about 80% of the time of checked; `-O2` the difference is within noise
(91% to 99%). The project builds at `-O0`, so the gain on a push-bound workload is real but
modest, and it is smaller still on whole programs.
