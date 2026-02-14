# How to add a new instruction

## Case-study: check.count.is.1

- Add a new opcode in `instruction.hpp`, extending `enum class Opcode`.

- Add a mapping from the textual name of the opcode `check.count.is.1` to the
  relevant opcode in `string_to_opcode_map` and to the reverse mapping
  `opcode_to_string`, in `instruction.cpp`.
  
- Note that there are potentially two internal opcodes associated with a single
  instruction. The second field is for lazy instructions that will replace
  themselves when executed.

- Add a method prototype for planting the new instruction in 
  `class ParseFunctionObject` in `parse_function_object.hpp`. In this case it 
  becomes `plant_check_count_is_1`.

- Implement that method in `parse_function_object.cpp`.

- Now implement the instruction itself in `Machine::threaded_impl` by:
  - Add a new label `L_CCHECK_COUNT_IS_1` for the code.
  - Add the code to be executed to that label (inside braces please).
  - And remember to add a mapping from the opcode to the label in `this->opcode_map`.

## Point to notes

- The interpret loop is implemented as a threaded interpreter, meaning that 
  the transfer to the next instruction is performed as the last action of each
  instruction rather than a master loop. This is why we see `goto *(pc++)->label_addr;`
  at the end of each instruction.
