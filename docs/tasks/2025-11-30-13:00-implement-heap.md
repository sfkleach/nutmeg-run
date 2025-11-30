# Task: Implement heap storage

Goal: To ensure that the function-object and string-object required for
our running hello-world example are constructed in the Nutmeg Machine heap
and not the C++ managed store.

## Background

The heap is a collection of Pools, each of which are linear blocks of Cells.
To begin with, we will be implementing a heap consisting of a single pool of
fixed size (1MB - suitable for toy problems).

All heap objects are store in the pool itself and follow the detailed layout
described [here](heap-object-layout.md). For our initial implementation that 
means we only need to implement:

- Datakey-objects, including the keykey object.
- String-objects (which are binarray-objects with BitWidth=8)
- Function-objects

Before loading the program we should 

- Initialise the heap with:
    - The DatakeyDatakey object
    - The StringDatakey object
    - The FunctionDatakey object
- Initialise the dictionary with 
    - variable DatakeyDatakey
    - variable StringDatakey
    - variable FunctionDatakey

It is safe to assume that these objects will never be relocated, regardless of
what garbage collector is implemented. IMPORTANT: DO NOT implement a garbage
collector at this stage. We will need to but that is not for this task.

The allocators for strings and functions will need to know about these 
datakeys so that they can fill those fields in correctly.

## Implementation Details

### DatakeyDatakey Bootstrap
The DatakeyDatakey is the first object allocated at the start of the pool. 
Since its address is known (pool start), its self-referential datakey field 
can be handled specially during initialization.

### String-objects
String-objects are implemented as binarray-objects with:
- Flavour: binarray
- BitWidth: 8 (for UTF-8)
- Datakey: points to StringDatakey

What identifies an object as a string is that its datakey is the StringDatakey.

### Function-objects
The entire FunctionObject is in-lined in the pool. Each instruction word is 
a 64-bit value (either an address-label or an operand). This design prioritizes 
simplicity of storage and garbage collection overheads.

### T-block (Deferred)
The T-block records which instruction words contain tagged pointers. This is 
needed for garbage collection but should be OMITTED from this implementation. 
We will add T-block support when implementing the garbage collector.

### Global Dictionary Keys
Variable names (dictionary keys) are NOT allocated in the heap. Nutmeg is 
designed so that at runtime, names can be completely discarded. This will be 
made possible in a later iteration, so for now keys remain as C++ strings in 
the std::unordered_map.
