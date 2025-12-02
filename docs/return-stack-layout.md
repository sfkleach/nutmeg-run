# Layout of the call/return stack#

```
Top of call-stack

Frame (Current)
    [return_address], raw Cell * pointer to the next instruction
    [current func-obj], raw pointer, Cell *
    [local_0] 
    ...
    [local_nlocals-1]
Frame (Previous)
    [return_address]
    [previous func-obj]
    [local_0] 
    ...
    [local_nlocals-1]
Frame (Older)
    [return_address]
    [even older func-obj]
    [local_0]
    ...
    [local_nlocals-1]

Bottom of call-stack
```