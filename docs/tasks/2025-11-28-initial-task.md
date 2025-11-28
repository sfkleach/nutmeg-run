# Initial development

## Background

The command line arguments are as follows:

```sh
nutmeg-run [OPTIONS] BUNDLE_FILE [ARGUMENTS...]
```

Where `OPTIONS` have not been decided yet and `ARGUMENTS` are passed to the
`main` entry point of the Nutmeg program.

`BUNDLE_FILE` is a Nutmeg bundle file created by the `nutmeg-bundler`
program, which is a SQLITE3 database file with the following schema:

```sql
CREATE TABLE IF NOT EXISTS "EntryPoints" (
  "IdName"	TEXT,
  PRIMARY KEY("IdName")
);
CREATE TABLE IF NOT EXISTS "DependsOn" (
  "IdName"	TEXT NOT NULL,
  "Needs"	TEXT NOT NULL,
  PRIMARY KEY("IdName","Needs")
);
CREATE TABLE IF NOT EXISTS "Bindings" (
  "IdName"	TEXT,
  "Lazy"  BOOLEAN,
  "Value"	TEXT,
  "FileName"	TEXT,
  PRIMARY KEY("IdName")
);
CREATE TABLE IF NOT EXISTS "SourceFiles" ( "FileName" TEXT NOT NULL, "Contents" TEXT NOT NULL, PRIMARY KEY("FileName") );
CREATE TABLE IF NOT EXISTS "Annotations" ( "IdName" TEXT, "AnnotationKey" TEXT NOT NULL, "AnnotationValue" TEXT NOT NULL, PRIMARY KEY("IdName", "AnnotationKey") );
```

The code resides in the `Value` part of the `Bindings` table in JSON format. 
It is generated from the following Golang serialised data:

```go
// Instruction represents a single instruction in the function body.
// This uses an adjacently tagged union format with a Type field and type-specific fields.
type Instruction struct {
	Type string `json:"type"`

	// Fields for different instruction types.
	// Only the relevant fields for each type will be populated.

	// PushInt, PopLocal, PushLocal.
	Index *int `json:"index,omitempty"`

	// PushString, PushGlobal.
	Value *string `json:"value,omitempty"`

	// SyscallCounted, CallGlobalCounted.
	Name  *string `json:"name,omitempty"`
	NArgs *int    `json:"nargs,omitempty"`
}

// FunctionObject represents a compiled function with its metadata and instructions.
type FunctionObject struct {
	NLocals      int           `json:"nlocals"`
	NParams      int           `json:"nparams"`
	Instructions []Instruction `json:"instructions"`
}
```

## Next Steps

1. Set up SQLite3 integration
  - Add SQLite3 dependency to conanfile.txt
  - Update CMake configuration to link against SQLite3

2. Implement bundle file reader
  - Create classes to represent the database schema (EntryPoints, Bindings, DependsOn, etc.)
  - Write SQLite3 query functions to read from the bundle file
  - Parse the JSON-formatted Value field into FunctionObject structures

3. Design the virtual machine core
  - Define data structures for:
      - The instruction set (matching the Go Instruction types)
      - FunctionObject representation in C++
      - Runtime stack and local variables
      - Value representation (integers, strings, functions, etc.)

4. Implement the instruction interpreter
  - Create the main execution loop
  - Implement handlers for each instruction type:
      - PushInt, PushString
      - PopLocal, PushLocal
      - PushGlobal, CallGlobalCounted
      - SyscallCounted

5. Create the main entry point
  - Parse command-line arguments (BUNDLE_FILE and ARGUMENTS)
  - Open the bundle database
  - Locate the main entry point
  - Initialize the VM and start execution

6. Add syscall infrastructure
  - Implement basic syscalls that Nutmeg programs might need
  - Create a registry for syscall functions