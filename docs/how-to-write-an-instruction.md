# How to Write an Instruction

This document covers the implementation details and best practices for writing the actual execution code for a new instruction in the threaded interpreter.

## Stack Operations

The machine maintains an operand stack (`operand_stack_`) of type `CellStack` where values are pushed and popped during execution.

### Popping values from the stack

The `pop()` method both removes and returns the top value:

```cpp
Cell value = pop();
```

You can also access the stack directly:

```cpp
Cell value = operand_stack_.pop();
```

### Pushing values onto the stack

```cpp
push(cell);
```

Or directly:

```cpp
operand_stack_.push(cell);
```

### Peeking at the top value without removing it

```cpp
Cell& value = peek();
```

Or directly:

```cpp
Cell& value = operand_stack_.peek();
```

### Getting the current stack size

```cpp
size_t size = operand_stack_.size();
```

### Checking if the stack is empty

```cpp
if (operand_stack_.empty()) {
    // Handle empty stack
}
```

### Stack vs Return Stack

The operand stack is for computation values. The return stack (`return_stack_`) is for function call frames and should not be directly manipulated by most instructions. Use `LAUNCH` and `RETURN` instructions for call frame management.

## Reading Immediate Operands

Many instructions need to read immediate operands from the code stream. These are values encoded directly after the instruction's opcode.

### Pattern for reading an integer operand

```cpp
L_MY_INSTRUCTION: {
    int64_t operand = (pc++)->i64;
    // Use operand...
    
    goto *(pc++)->label_addr;
}
```

**Important:** The `pc++` advances the program counter. Make sure you advance it for each operand you read, and once more at the end to fetch the next instruction's address.

### Pattern for reading a Cell operand

```cpp
L_MY_INSTRUCTION: {
    Cell operand = *(pc++);
    // Use operand...
    
    goto *(pc++)->label_addr;
}
```

### Planting immediate operands

When you implement the planting method in `parse_function_object.cpp`, you need to push these operands into the code stream:

```cpp
void ParseFunctionObject::plant_my_instruction(FunctionObject& func, const Instruction& inst) {
    // plant_instruction has already pushed the label address for the handler,
    // so only the operands are planted here.
    if (!inst.ivalue.has_value()) {
        throw std::runtime_error("MY_INSTRUCTION requires an ivalue field");
    }
    Cell operand;
    operand.i64 = inst.ivalue.value();
    func.code.push_back(operand);
}
```

## Error Handling

Instructions should throw exceptions when they encounter runtime errors. Use `std::runtime_error` with descriptive messages.

### Basic error throwing

```cpp
if (some_error_condition) {
    throw std::runtime_error("Description of what went wrong");
}
```

### Using fmt::format for detailed error messages

```cpp
if (current_size != expected_size) {
    throw std::runtime_error(
        fmt::format("Stack size mismatch: expected {}, got {}", expected_size, current_size)
    );
}
```

### When to throw vs when to return error codes

- **Throw exceptions** for runtime errors that indicate program bugs or invalid bytecode (the usual case).
- **Return error codes** only at the top level (like in `main()`) to interface with the OS.

### What happens to machine state on exceptions

Exceptions will unwind the stack back to the `try/catch` block in `main()`. The machine state may be left in an inconsistent state, so exceptions generally terminate the program.

## Debugging Support

Use the `DEBUG_INSTRUCTIONS` compile-time constant to add trace output that can be enabled/disabled.

### Pattern for debug output

```cpp
L_MY_INSTRUCTION: {
    int64_t operand = (pc++)->i64;
    
    if constexpr (DEBUG_INSTRUCTIONS) {
        fmt::print("MY_INSTRUCTION operand = {}\n", operand);
    }
    
    // Instruction implementation...
    
    goto *(pc++)->label_addr;
}
```

The `if constexpr` means the debug code is completely eliminated when `DEBUG_INSTRUCTIONS` is false, so there's zero runtime overhead.

### What to print

- Instruction name
- Key operand values
- Stack sizes before/after
- Important computed values

## Memory Allocation

Instructions that create heap objects must use the machine's allocation methods to ensure proper garbage collection tracking.

### Allocating a string

```cpp
Cell str_cell = machine.allocate_string("hello");
operand_stack_.push(str_cell);
```

### Allocating a function object

```cpp
Cell* func_ptr = machine.allocate_function(code, nlocals, nparams);
Cell func_cell = make_tagged_ptr(func_ptr);
operand_stack_.push(func_cell);
```

### Important: Tagging pointers

Heap-allocated objects are stored as tagged pointers. Use `make_tagged_ptr()` to create the tagged value.

```cpp
Cell* ptr = machine.allocate_something(...);
Cell tagged = make_tagged_ptr(ptr);
```

And `as_detagged_ptr()` to retrieve the pointer:

```cpp
Cell tagged = operand_stack_.peek();
Cell* ptr = as_detagged_ptr(tagged);
```

## Threading the Interpreter

The interpreter uses computed goto for efficient dispatch. This is why every instruction ends with `goto *(pc++)->label_addr;`.

### The standard instruction template

```cpp
L_MY_INSTRUCTION: {
    // 1. Read immediate operands (if any)
    int64_t operand = (pc++)->i64;
    
    // 2. Pop input values from stack (if needed)
    Cell input = pop();
    
    // 3. Perform computation
    Cell result = compute_something(input, operand);
    
    // 4. Push result onto stack (if any)
    operand_stack_.push(result);
    
    // 5. Debug output (if enabled)
    if constexpr (DEBUG_INSTRUCTIONS) {
        fmt::print("MY_INSTRUCTION completed\n");
    }
    
    // 6. Jump to next instruction
    goto *(pc++)->label_addr;
}
```

### Critical: The final goto

Every instruction **must** end with `goto *(pc++)->label_addr;`. This:
1. Reads the next instruction's address from the code stream
2. Advances the program counter
3. Jumps to that address

Forgetting this will cause the interpreter to fall through to the next label,
which is never correct (except when forcing code sharing as a "trick").

## Potential Pitfalls

### 1. Forgetting to update opcode_map

After adding your label, you must register it in `this->opcode_map` in the `Machine::threaded_impl` constructor:

```cpp
this->opcode_map = {
    // ... existing mappings ...
    {Opcode::MY_INSTRUCTION, &&L_MY_INSTRUCTION},
};
```

Without this, the planting code can't find your instruction's address.

### 2. Forgetting the final goto

**Wrong:**
```cpp
L_MY_INSTRUCTION: {
    do_something();
    // Missing goto!
}
```

**Correct:**
```cpp
L_MY_INSTRUCTION: {
    do_something();
    goto *(pc++)->label_addr;
}
```

### 3. Off-by-one errors with pc++

Each `pc++` advances the program counter by one `Cell`. Make sure you advance it:
- Once for each immediate operand you read
- Once at the end for the next instruction address

**Wrong:**
```cpp
L_MY_INSTRUCTION: {
    int64_t a = (pc++)->i64;
    int64_t b = (pc++)->i64;
    goto *pc->label_addr;  // Wrong! Forgot the final pc++
}
```

**Correct:**
```cpp
L_MY_INSTRUCTION: {
    int64_t a = (pc++)->i64;
    int64_t b = (pc++)->i64;
    goto *(pc++)->label_addr;  // Correct!
}
```

### 4. Not handling both opcodes for lazy variants

Some instructions have both regular and lazy variants (e.g., `PUSH_GLOBAL` and `PUSH_GLOBAL_LAZY`). The mapping in `instruction.cpp::string_to_opcode_map` returns a pair. Make sure both are mapped to their labels:

```cpp
this->opcode_map = {
    {Opcode::MY_INSTRUCTION, &&L_MY_INSTRUCTION},
    {Opcode::MY_INSTRUCTION_LAZY, &&L_MY_INSTRUCTION_LAZY},
};
```

### 5. Stack underflow

Always check that the stack has enough values before popping:

**Defensive (but verbose):**
```cpp
if (operand_stack_.size() < 2) {
    throw std::runtime_error("Stack underflow in MY_INSTRUCTION");
}
Cell b = pop();
Cell a = pop();
```

**Simple (assumes correct bytecode):**
```cpp
Cell b = pop();
Cell a = pop();
```

Most instructions assume the bytecode is valid and will naturally crash if the stack underflows, which helps catch bugs during development.

### 6. Type confusion

Cells are untyped unions. Make sure you're extracting the right type:

```cpp
Cell cell = pop();
int64_t value = as_detagged_int(cell);  // Assumes cell contains a tagged integer
```

If the cell contains a different type, you'll get garbage data or a crash. The parser should ensure type safety at compile time, so runtime type checks are usually unnecessary.

### 7. Mutating immutable data

Nutmeg values are generally immutable. Don't try to modify the contents of heap objects in-place. Create new objects instead.

## Testing Instructions

Every new instruction should have test coverage in `tests/test_threaded.cpp` using the Catch2 framework.

### Basic test structure

```cpp
TEST_CASE("MY_INSTRUCTION works correctly", "[threaded]") {
    Machine machine;
    const auto& opcode_map = machine.get_opcode_map();
    
    FunctionObject func;
    func.nlocals = 0;
    func.nparams = 0;
    
    // Build the instruction sequence
    Cell w1, w2, w3;
    w1.label_addr = opcode_map.at(Opcode::MY_INSTRUCTION);
    w2.i64 = 42;  // Immediate operand
    w3.label_addr = opcode_map.at(Opcode::HALT);
    func.code = {w1, w2, w3};
    
    Cell* func_ptr = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_ptr);
    
    // Assert expected results
    REQUIRE(machine.stack_size() == 1);
    REQUIRE(as_detagged_int(machine.pop()) == 42);
}
```

### Test both success and failure cases

Test that the instruction works correctly with valid inputs, and test that it throws appropriate exceptions with invalid inputs.

```cpp
TEST_CASE("MY_INSTRUCTION throws on invalid input", "[threaded]") {
    Machine machine;
    // ... setup code that will cause error ...
    
    REQUIRE_THROWS_AS(machine.execute(func_ptr), std::runtime_error);
}
```

### Test edge cases

- Empty stack
- Maximum/minimum integer values
- Null pointers
- Empty strings
- Boundary conditions specific to your instruction

## Example: Complete Implementation

Here's a complete example of implementing a `DUPLICATE` instruction that duplicates the top stack value.

### In instruction.hpp (enum)

```cpp
enum class Opcode {
    // ... existing opcodes ...
    DUPLICATE,
};
```

### In instruction.cpp (mappings)

```cpp
static const std::unordered_map<std::string, std::pair<Opcode, Opcode>> string_to_opcode_map = {
    // ... existing mappings ...
    {"duplicate", {Opcode::DUPLICATE, Opcode::DUPLICATE}},
};

const char* opcode_to_string(Opcode opcode) {
    switch (opcode) {
        // ... existing cases ...
        case Opcode::DUPLICATE: return "DUPLICATE";
        default: return "UNKNOWN";
    }
}
```

### In parse_function_object.hpp (declaration)

```cpp
class ParseFunctionObject {
    // ... existing methods ...
    void plant_duplicate(FunctionObject& func, const Instruction& inst);
};
```

### In parse_function_object.cpp (implementation)

```cpp
void ParseFunctionObject::plant_duplicate(FunctionObject& func, const Instruction& inst) {
    // DUPLICATE has no operands; plant_instruction has already pushed its
    // label address, so there is nothing more to plant.
}
```

### In machine.cpp (threaded implementation)

```cpp
// In Machine::threaded_impl constructor:
this->opcode_map = {
    // ... existing mappings ...
    {Opcode::DUPLICATE, &&L_DUPLICATE},
};

// In the main dispatch loop:
L_DUPLICATE: {
    if constexpr (DEBUG_INSTRUCTIONS) {
        fmt::print("DUPLICATE, stack size = {}\n", operand_stack_.size());
    }
    
    // Check we have at least one value to duplicate. (Defensive check
    // appropriate because duplicating an empty stack is a clear error).
    if (operand_stack_.empty()) {
        throw std::runtime_error("Cannot duplicate: stack is empty");
    }
    
    Cell value = operand_stack_.peek();
    operand_stack_.push(value);
    
    goto *(pc++)->label_addr;
}
```

### In tests/test_threaded.cpp (test)

```cpp
TEST_CASE("DUPLICATE instruction duplicates top of stack", "[threaded]") {
    Machine machine;
    const auto& opcode_map = machine.get_opcode_map();
    
    FunctionObject func;
    func.nlocals = 0;
    func.nparams = 0;
    
    // PUSH_INT 42, DUPLICATE, HALT
    Cell w1, w2, w3, w4;
    w1.label_addr = opcode_map.at(Opcode::PUSH_INT);
    w2 = make_tagged_int(42);
    w3.label_addr = opcode_map.at(Opcode::DUPLICATE);
    w4.label_addr = opcode_map.at(Opcode::HALT);
    func.code = {w1, w2, w3, w4};
    
    Cell* func_ptr = machine.allocate_function(func.code, func.nlocals, func.nparams);
    machine.execute(func_ptr);
    
    REQUIRE(machine.stack_size() == 2);
    REQUIRE(as_detagged_int(machine.pop()) == 42);
    REQUIRE(as_detagged_int(machine.pop()) == 42);
}

TEST_CASE("DUPLICATE throws on empty stack", "[threaded]") {
    Machine machine;
    const auto& opcode_map = machine.get_opcode_map();
    
    FunctionObject func;
    func.nlocals = 0;
    func.nparams = 0;
    
    // DUPLICATE, HALT (with no value to duplicate)
    Cell w1, w2;
    w1.label_addr = opcode_map.at(Opcode::DUPLICATE);
    w2.label_addr = opcode_map.at(Opcode::HALT);
    func.code = {w1, w2};
    
    Cell* func_ptr = machine.allocate_function(func.code, func.nlocals, func.nparams);
    
    REQUIRE_THROWS_AS(machine.execute(func_ptr), std::runtime_error);
}
```

This complete example shows all the pieces working together to implement a functioning instruction with proper error handling, debugging support, and test coverage.
