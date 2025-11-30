# Task: Implement heap storage

Goal: To ensure that the function-object and string-object required for
our running hello-world example are constructed in the Nutmeg Machine heap
and not the C++ managed store.

## Background

The heap is a collection of Pools, each of which are linear blocks of Cells.
To begin with, we will be implementing a heap consisting of a single pool of
fixed size.

All heap objects are store in the pool itself and follow the detailed layout
described [here](heap-object-layout.md). For our initial implementation that 
means we only need to implement:

- Datakey-objects, including the keykey object.
- String-objects
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
