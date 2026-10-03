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
