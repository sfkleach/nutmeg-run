#ifndef TRACE_HPP
#define TRACE_HPP

namespace nutmeg {
    // TODO warnings for incomplete features.
    inline constexpr bool TODO_WARNINGS = false;
    inline constexpr bool TRACE_TIMES = true;

    // Master flag - set to false to disable all debug tracing at compile time.
    inline constexpr bool ENABLE_TRACING = false;

    // Instruction execution tracing.
    inline constexpr bool DEBUG_INSTRUCTIONS = ENABLE_TRACING && true;
    inline constexpr bool DEBUG_INSTRUCTIONS_DETAIL = ENABLE_TRACING && false;

    // Code generation and planting tracing.
    inline constexpr bool TRACE_PLANT_INSTRUCTIONS = ENABLE_TRACING && true;
    inline constexpr bool TRACE_CODEGEN = ENABLE_TRACING && false;
    inline constexpr bool TRACE_CODEGEN_DETAILED = ENABLE_TRACING && false;

    // Runtime checks and validation.
    inline constexpr bool EXTRA_CHECKS = ENABLE_TRACING && true;

    // VM execution tracing.
    inline constexpr bool TRACE_EXECUTION = ENABLE_TRACING && true;

    // Main program tracing.
    inline constexpr bool TRACE_MAIN = ENABLE_TRACING && false;

    inline constexpr bool DEBUG = ENABLE_TRACING && false;

    inline constexpr bool TRACE_BUNDLE_READER = ENABLE_TRACING && false;
}

#endif // TRACE_HPP
