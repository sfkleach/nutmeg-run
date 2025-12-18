#include <cstdint>

#ifndef SYS_MATHS_HPP
#define SYS_MATHS_HPP



namespace nutmeg {

class Machine;

// Sys-function implementations for arithmetic operations.
void sys_add(Machine& machine, uint64_t nargs);
void sys_subtract(Machine& machine, uint64_t nargs);
void sys_multiply(Machine& machine, uint64_t nargs);
void sys_divide(Machine& machine, uint64_t nargs);

void sys_negate(Machine& machine, uint64_t nargs);

} // namespace nutmeg

#endif // SYS_MATHS_HPP