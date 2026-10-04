#include "guard_fault.hpp"
#include "cell_stack.hpp"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <unistd.h>

namespace nutmeg {

namespace {

struct sigaction previous_action;

// A constant message: nothing may be formatted or allocated in the handler.
constexpr char EXHAUSTED_MESSAGE[] =
    "Error: value stack overflow: the stack has reached its size limit\n";

void handle_fault(int signal_number, siginfo_t* info, void*) {
    const int saved_errno = errno;
    const CellStack::GuardFault outcome = CellStack::repair_guard_fault(info->si_addr);
    if (outcome == CellStack::GuardFault::Repaired) {
        // Returning re-executes the faulting store, which now succeeds.
        errno = saved_errno;
        return;
    }
    if (outcome == CellStack::GuardFault::Exhausted) {
        ssize_t ignored = write(STDERR_FILENO, EXHAUSTED_MESSAGE, sizeof(EXHAUSTED_MESSAGE) - 1);
        (void)ignored;
    }
    // Not ours, or not repairable: put back whoever was there before and return, so that the
    // store faults again and is dealt with by them (by default, by killing the process).
    sigaction(signal_number, &previous_action, nullptr);
    errno = saved_errno;
}

} // namespace

void install_guard_fault_handler() {
    // Do nothing if our handler is already the current one. (A program installs it once; the
    // check, rather than a flag, also lets it be reinstated if something else, such as the test
    // framework at the start of each test case, has replaced it since.)
    struct sigaction current;
    if (sigaction(SIGSEGV, nullptr, &current) == 0 && (current.sa_flags & SA_SIGINFO) != 0 &&
        current.sa_sigaction == handle_fault) {
        return;
    }
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_sigaction = handle_fault;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, &previous_action);
}

void guard_fault_forget_previous_handler() {
    std::memset(&previous_action, 0, sizeof(previous_action));
    previous_action.sa_handler = SIG_DFL;
}

} // namespace nutmeg
