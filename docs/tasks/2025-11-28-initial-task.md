# Initial development

## Part 1

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

## Part 2

... to be elaborated after Part 1 ...