# Task: Add guard pages to the value-stack

## Overview

There is a lot of overhead associated with checking that pops and pushes to
the abstract machine's value stack. We can reduce this by adding guard pages 
to the top-and-bottom. If nothing else, the guard pages will protect against
overflow/underflow issues when the compiler generates faulty code.

The work has three stages: adding the guard pages, moving the stack pages,
and then catching overflow and attempting to grow the value stack.


## Step 1: Add guard pages at the base & top of the value stack
- [x] Status

The value stack should be topped and tailed by a pair of guard pages which are
write protected. This means that any attempt to under/overflow the abstract 
machine stack is detected and prevented.

Add unit tests showing this works.

## Step 2: Add a facility for growing the value stack
- [ ] Status

The value stack can potentially grow very large in some applications. So it
is important that we can expand the value stack (although we won't be shrinking
it). 

For this step arrange that the value stack can be grown. We will require 
tests showing this works.

## Step 3: Catch overflow and grow the stack
- [ ] Status

When there is an attempt to write into the guard page, we should try to 
grow the value stack. If we succeed we should retry the instruction that 
failed.

This is potentially complicated. We are prepared to generate different 
push instructions that check for stack overflow and those that don't (because
we can prove there is room).


