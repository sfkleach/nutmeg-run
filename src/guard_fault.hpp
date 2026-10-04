#ifndef GUARD_FAULT_HPP
#define GUARD_FAULT_HPP

namespace nutmeg {

// Installs the SIGSEGV handler that repairs stores into the top guard page of a guarded
// CellStack by growing the stack, so that the faulting store is retried and succeeds. A fault
// anywhere else, or one that cannot be repaired because the stack's reserve is exhausted, is
// passed to whatever handler was installed before (the default action, normally, which kills the
// process). Called by CellStack when it creates a guarded stack; it does nothing if the handler
// is already the current one.
//
// The handler uses only async-signal-safe operations. The registry of guarded stacks that it
// searches is not locked, which is right for the single-threaded machine.
void install_guard_fault_handler();

// For tests: forget the handler that was installed before ours, so that a fault that is not
// repaired gets the default action (a forked test child uses this so that it dies quietly).
void guard_fault_forget_previous_handler();

} // namespace nutmeg

#endif // GUARD_FAULT_HPP
