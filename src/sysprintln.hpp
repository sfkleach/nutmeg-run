#include <cstdint>

#ifndef SYSPRINTLN_HPP
#define SYSPRINTLN_HPP



namespace nutmeg {

class Machine;

// Sys-function implementation for "println".
void sys_println(Machine& machine, uint64_t nargs);

} // namespace nutmeg

#endif // SYSPRINTLN_HPP
