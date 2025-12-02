#ifndef SYSFUNCTIONS_HPP
#define SYSFUNCTIONS_HPP

#include <unordered_map>
#include <string>

namespace nutmeg {

class Machine;

// Type for sys-function implementations.
// Takes a reference to a Machine and returns no results.
using SysFunction = void (*)(Machine&);

// Sys-function implementations.
void sys_println(Machine& machine);

// Global sys-functions table.
extern const std::unordered_map<std::string, SysFunction> sysfunctions_table;

} // namespace nutmeg

#endif // SYSFUNCTIONS_HPP
