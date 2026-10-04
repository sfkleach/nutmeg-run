## Questions and Answers

> 1. Which stacks? The doc says "value stack". The same CellStack class backs
>    the return stack, which has the same overflow risk (deep recursion). Should
>    the return stack get guard pages too, or only the value stack?

That will be a separate task.

> 2. What happens when the guard page is hit in Step 1? A write to a protected
>    page raises SIGSEGV. For "detected and prevented" we need a handler that
>    turns the fault into a clean error (Step 3 needs a handler anyway). Should
>    Step 1 install a handler that reports "stack overflow/underflow" and exits,
>    or just rely on the crash? Unit tests that deliberately trigger a fault
>    need either a forked child process or a signal handler that jumps back,
>    because a test can't normally survive its own SIGSEGV.

Step 1 can just rely on a crash, so the tests can fork processes and check
the status code.

> 3. Do the software checks go? The overview says they're the overhead to
>    remove, but ENABLE_STACK_CHECKS is currently on. Do you want them removed
>    once the guard pages exist, kept as a debug option, or only for the value
>    stack?

The software checks should remain for the moment, if only for the better
error message.

> 4. Underflow isn't only about the base. A pop reads below base_, so the bottom
>    guard page catches a single stray pop. peek_at, offset_from_top or
>    pop_multiple(n) with a large n could jump past a one-page guard. Which
>    accesses should the guard cover, and is the guard size one page?

The guard size is one page. pop_multiple will need to retain a soft-check. i.e.
pop_multiple and the other bulk operations (push_multiple, move_multiple,
discard_multiple, resize) keep their soft checks, since a large count can jump
past a one-page guard.


> 5. Retry in Step 3. A hardware fault is asynchronous to the C++ code: the
>    handler fires inside the faulting store. Retrying "the instruction that
>    failed" means either:
>   - growing the stack in the signal handler by remapping or extending in place
>     (mremap/mmap at a fixed address, so existing pointers stay valid), then
>     returning so the CPU retries the store; or
>   - checking in the "checked push" instructions.

>   The first approach needs the stack to grow in place without moving, so that
>   top_, base_ and any raw Cell* into it stay valid. That points to reserving a
>   large virtual address range up front (mmap with PROT_NONE) and committing it
>   on demand. I'd want to know whether you intend that. Moving the stack on
>   growth would invalidate pointers (such as sp caches in the interpreter),
>   which is much harder.

This remap sounds like the way to go.

> 6. Two kinds of push instruction. This ties into the planned instruction-set
>    work and the unfinished conditional instructions. I'd keep it out of Steps
>    1 and 2 and leave it as a design question for Step 3.
