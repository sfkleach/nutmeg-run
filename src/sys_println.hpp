#include <cstdint>

#ifndef SYS_PRINTLN_HPP
#define SYS_PRINTLN_HPP



namespace nutmeg {

class Machine;

// Sys-function implementation for "println".
void sys_println(Machine& machine, uint64_t nargs);

} // namespace nutmeg

#endif // SYS_PRINTLN_HPP
