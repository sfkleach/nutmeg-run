#ifndef FORK_HELPERS_HPP
#define FORK_HELPERS_HPP

// Helpers for tests that provoke a crash. A test cannot survive its own SIGSEGV, so the
// faulting action is run in a forked child and the parent inspects how it died.

#include <catch2/catch_test_macros.hpp>
#include <csignal>
#include <functional>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../src/guard_fault.hpp"

namespace nutmeg_test {

struct ChildResult {
    int signal = 0;          // The signal that killed the child, or 0 if it survived.
    std::string error_text;  // Everything the child wrote to stderr.
};

enum class Handlers {
    Default,  // SIGSEGV and friends are set to the default action: the child just dies.
    Keep,     // The guard-fault handler stays installed, and the default is what it falls back to.
};

// Runs `action` in a child process. The child must not run the test framework or atexit
// handlers, so it ends with _exit.
inline ChildResult run_in_child(const std::function<void()>& action,
                                Handlers handlers = Handlers::Default) {
    int pipe_fds[2];
    REQUIRE(pipe(pipe_fds) == 0);
    pid_t pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDERR_FILENO);
        if (handlers == Handlers::Default) {
            // Catch2 installs fatal-signal handlers that would print a bogus failure report
            // from the child; restore the default action so the child just dies.
            for (int sig : {SIGSEGV, SIGBUS, SIGABRT}) {
                signal(sig, SIG_DFL);
            }
        } else {
            // Keep the guard-fault handler, but have it fall back to the default action rather
            // than to Catch2's handler.
            nutmeg::install_guard_fault_handler();
            nutmeg::guard_fault_forget_previous_handler();
            for (int sig : {SIGBUS, SIGABRT}) {
                signal(sig, SIG_DFL);
            }
        }
        struct rlimit no_core = {0, 0};
        setrlimit(RLIMIT_CORE, &no_core);  // No core file or crash reporter for a deliberate crash.
        action();
        _exit(0);
    }
    close(pipe_fds[1]);
    ChildResult result;
    char buffer[256];
    ssize_t count;
    while ((count = read(pipe_fds[0], buffer, sizeof(buffer))) > 0) {
        result.error_text.append(buffer, static_cast<size_t>(count));
    }
    close(pipe_fds[0]);
    int status = 0;
    REQUIRE(waitpid(pid, &status, 0) == pid);
    result.signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    return result;
}

// Returns the signal that killed a child running `action`, or 0 if it survived.
inline int signal_from(const std::function<void()>& action) {
    return run_in_child(action).signal;
}

} // namespace nutmeg_test

#endif // FORK_HELPERS_HPP
