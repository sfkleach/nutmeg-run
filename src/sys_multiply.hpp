#include <cstdint>

#ifndef SYS_MULTIPLY_HPP
#define SYS_MULTIPLY_HPP



namespace nutmeg {

class Machine;

// Sys-function implementation for "multiply".
void sys_multiply(Machine& machine, uint64_t nargs);

} // namespace nutmeg

#endif // SYS_MULTIPLY_HPP