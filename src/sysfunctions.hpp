#ifndef SYSFUNCTIONS_HPP
#define SYSFUNCTIONS_HPP

#include <unordered_map>
#include <string>
#include <cstdint>

namespace nutmeg {

class Machine;

// Type for sys-function implementations.
// Takes a reference to a Machine and the number of arguments, and returns no results.
using SysFunction = void (*)(Machine&, uint64_t);

// Sys-function implementations.
void sys_println(Machine& machine, uint64_t nargs);

// Global sys-functions table.
extern const std::unordered_map<std::string, SysFunction> sysfunctions_table;

} // namespace nutmeg

#endif // SYSFUNCTIONS_HPP
