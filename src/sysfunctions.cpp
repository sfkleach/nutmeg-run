#include "sysfunctions.hpp"
#include "sys_println.hpp"
#include "sys_maths.hpp"
#include "machine.hpp"

namespace nutmeg {

// Global sys-functions table mapping names to function pointers.
const std::unordered_map<std::string, SysFunction> sysfunctions_table = {
    {"println", sys_println},
    {"+", sys_add},
    {"-", sys_subtract},
    {"*", sys_multiply},
    {"/", sys_divide},
    {"negate", sys_negate},
};

} // namespace nutmeg
