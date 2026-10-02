# Layout of the call/return stack#

```
Top of call-stack

Frame (Current)
    [return_address], raw Cell * pointer to the next instruction
    [current func-obj], raw pointer, Cell *
    [local_nlocals-1] 
    ...
    [local_0]
Frame (Previous)
    [return_address]
    [previous func-obj]
    [local_nlocals-1] 
    ...
    [local_0]
Frame (Older)
    [return_address]
    [even older func-obj]
    [local_nlocals-1] 
    ...
    [local_0]

Bottom of call-stack
```