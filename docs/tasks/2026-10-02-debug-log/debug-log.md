# Generate a log of instructions executed to support debugging

## Background

When developing the nutmeg-runner, it is very helpful to be able to stream out
an instruction-by-instruction log of the abstract machine to a file for post-hoc
debugging.

For obvious performance reasons, this is a feature that is conditionally
compiled in only when we are trying to debug the code.

We will proceed step by step. Progress:

- [x] Step 1
- [x] Step 2
- [x] Step 3
- [x] Step 4
- [x] Step 5
- [x] Step 6
- [x] Step 7
- [ ] Step 8


## Step 1: Create the log file conditionally and generate a stream of instructions

Based on a conditional flag, the nutmeg runner should create a log file when it
starts. Each executed instruction should add an entry to the file.

- The log file name must lead with ISO date-time so they sort nicely.
- The format of the log file should be line-by-line JSON (jsonl??)
- The instruction log should have the format `{"opcode": NAME}` i.e. starting off slowly.

## Step 2: Adjust the log file name

We will be adding several log files before the end of this task. We want the
log files to be distinguished by their function and datetime.

- The new format is `{LOG_TYPE}.{YYYY-MM-DD-HH.MM.SS}.jsonl`, using local time
- The LOG_TYPE for the instructions log is `run`

## Step 3: Add before-and-after stack length

Augment the instruction-log-entry format. It should capture the stack length
at the entry point of the instruction and the exit point and report both.
This must incur no run-time penalty when the logging is disabled.

The new format is:

```json
{"opcode": NAME, "onEntry": {"stacklength": M}, "onExit": {"stacklength": N}}
```

## Step 4: Opargs

Instructions typically have arguments which are inlined. We want to add these
to the log-entry, using docs/specs/tagging-scheme.md to interpret their meaning.

- Some arguments are _raw_ and should be reported as a bit-pattern.
- Bit patterns are rendered in both decimal and hex as "0dNNNN,0xNNNN", using no leading zeros
- Some arguments are _tagged_ and non-pointer values should be turned into descriptions
- Pointer values should simply be left as their hex addresses in the form &0xNNNN, skipping leading zeros

## Step 5: Improve the opargs formatting

In this step we improve the formatting of some oparg values.

- Instructions whose opargs represent local variables supply these as raw
  offsets. Please report this as "local DDD", where DDD is the decimal offsets.

- Strings are given as tagged pointers. Please format this as a JSON string. 
  However if the string is longer than 16 characters, reduce it to 13 characters
  and append "...".

- Other reference (tagged pointer) values should be prefixed with 
  the name of the type (datakey) and the rest of the contents as as we
  print them already.

## Step 6: Printing functions correctly

Some instructions call functions and their opargs are tagged pointers to
the functions. These opargs should be printed as "fn NAME", where NAME is
the name of the function, if it is available.

## Step 7: Instruction number

Each instruction executed is numbered in the log entry. This is to simplify
cross-checking instruction streams against events. Add a field to hold a 
number indicating this - I expect it to exactly correlate with the line number
in the JSONL file.

## Step 8: Lifecycle events

As any program runs there are a series of notable landmark events. We want
a flag in `trace.hpp` to turn on the logging of these events `ENABLE_EVENTS_LOG`.

- We want the file name to be `events.{YYYY-MM-DD-HH.MM.SS}.jsonl`
- This feature should have zero runtime performance when disabled.
- It should log 
  - loading the code from the bundle.
  - finding the entry point.
  - launching the program
  - memory allocations


