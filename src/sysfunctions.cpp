#include "sysfunctions.hpp"
#include "sysprintln.hpp"
#include "machine.hpp"

namespace nutmeg {

// Global sys-functions table mapping names to function pointers.
const std::unordered_map<std::string, SysFunction> sysfunctions_table = {
    {"println", sys_println}
};

} // namespace nutmeg
