# 0001 - Sticky Quotes: A Proposal for Shell Safety, 2025-11-29

## The Problem

Unix shells have a fundamental security flaw: wildcard expansion happens before commands see their arguments, making it impossible to distinguish filenames from options.

### Example of the Problem

```bash
# Create a malicious file
touch -- -l

# User innocently lists all files ending in 'l'
ls *l
# The shell expands this to: ls -l
# Now ls interprets the filename "-l" as an option flag!
```

This is not a theoretical issue. It affects every command that accepts both options and filenames:

```bash
rm *             # If a file named "-rf" exists, disaster
cp *.txt dest/   # If "-i" exists, cp prompts for every file
tar czf *.tar    # Files starting with "-" become options
```

### Why Current Solutions Don't Work

**Solution 1: "Use `--` separator"**
```bash
ls -- *l    # Requires users to remember this for every command
```
Problem: Users forget, and one mistake is all it takes.

**Solution 2: "Check if files exist"**
```bash
if [[ -f "$arg" && "$arg" == -* ]]; then
    echo "Warning: filename looks like option"
fi
```
Problem: Can't distinguish between user typing `-l` (option) vs wildcard expanding to `-l` (filename).

**Solution 3: "Don't use wildcards"**
```bash
for f in *.txt; do cp "$f" dest/; done
```
Problem: Verbose, and still vulnerable if you forget quotes.

## Root Cause

By the time a command receives `argv`, the shell has already:
1. Expanded wildcards
2. Removed quotes
3. Lost all context about what was a literal argument vs expanded filename

The command literally cannot tell the difference between:
- User typed: `ls -l` (wants long format)
- User typed: `ls *l` which expanded to `ls -l` (wants to list a file)

## The Solution: Sticky Wildcards

Introduce a mechanism where **wildcard expansion produces strings marked as data**, not potential options.

### Syntax

```bash
@(PATTERN)           # Sticky wildcard expansion
@(*.txt)             # Expands to sticky-marked filenames
@{command args}      # Sticky command substitution
@{basename $i .dir}  # Command output marked as sticky
@'literal'           # Sticky literal string
```

Note: The parentheses/braces indicate expansion will occur. Sticky literals use `@'...'` to mark plain strings as data.

### Behavior

1. **Sticky wildcards expand and mark results**
   ```bash
   ls @(*l)
   # Expands to: ls file.txt -l another.xml
   # But each filename has a sticky bit set internally
   ```

2. **Sticky command substitution**
   ```bash
   RESULT=@{basename $file .txt}
   # Command output is marked as sticky
   ```

3. **Contagious across substitutions**
   ```bash
   FILES=@(*.txt)
   echo $FILES        # Variable holds sticky-marked strings
   ```

4. **Stickiness preserved through concatenation**
   ```bash
   STEM=@{basename $file .txt}
   NEWFILE=$STEM.bak      # Result is sticky if $STEM is sticky
   OUTFILE=@'prefix'_$i   # Result is sticky (both parts are sticky)
   ```
   If any part of a concatenated string is sticky, the entire result is sticky.

5. **Passed to commands with metadata**
   The shell passes both the string value AND a "sticky bit" indicating this came from a protected expansion.

4. **Commands can check the sticky bit**
   ```c
   // Conceptual API
   for (int i = 1; i < argc; i++) {
       if (is_sticky(argc, argv, i)) {
           // This came from a sticky expansion
           // Treat it as data, never as an option
           // argv[i] is already the plain string (no @'...' wrapper)
           handle_filename(argv[i]);
       } else {
           // Normal argument - could be option or filename
           parse_argument(argv[i]);
       }
   }
   ```

### Example Usage

```bash
# Safe wildcard expansion
ls @(*l)
# Even if -l exists as a filename, ls knows it's data not an option

# Safe copying
cp @(*.txt) dest/
# All expanded filenames are marked as sticky, cannot be confused with -i

# Still works with options
cp -v @(*.txt) dest/
# -v is a normal argument (option), *.txt expands to sticky filenames

# Variable expansion preserves stickiness
FILES=@(*.txt)
cp $FILES dest/
# $FILES expands with sticky bit preserved

# Command substitution with sticky output
for file in @(*.dir); do
    BASE=@{basename $file .dir}
    echo "Processing: $BASE"  # $BASE is sticky-marked
done

# Concatenation preserves stickiness
STEM=@{basename myfile.txt .txt}
BACKUP=$STEM.bak              # $BACKUP is sticky (because $STEM is sticky)
OUTPUT=@'output'_$i.log       # Entire string is sticky
```

## Backward Compatibility

This is **fully backward compatible**:

1. **Old syntax still works**: Regular `*.txt` behaves exactly as before
2. **Opt-in safety**: Only use `@(...)` when you want protection
3. **Old commands see regular strings**: If a command doesn't check the sticky bit, it just sees normal string values
4. **No breaking changes**: All existing scripts work unchanged

## Implementation Path

### Phase 1: Shell Support (2025-2027)
- Bash/Zsh implement `@(...)` syntax for sticky wildcard expansion
- Track sticky bit in an auxiliary data structure (since POSIX argv is `char*[]`)
- Pass sticky bits via environment variable: `_STICKY_ARGS="1,3,5"` (indices that are sticky)

### Phase 2: Command Support (2027-2030)
- New commands check `_STICKY_ARGS` and refuse to treat sticky args as options
- Standard libraries provide `parse_args_with_sticky()` helpers
- Commands advertise sticky-quote support in `--version`

### Phase 3: Sticky-by-Default Option (2030+)
- Introduce `_STICKY_WILDCARDS=1` environment variable
- When enabled, all wildcards expand as sticky by default
- Shells warn when dangerous patterns detected without sticky mode
- Suggest: "Enable `set -o sticky-wildcards` for automatic protection"

### Phase 4: Default Behavior (2035+)
- Make `_STICKY_WILDCARDS=1` the default in new shell sessions
- Legacy mode for old scripts: `set +o sticky-wildcards`
- Document migration path for commands that need updating

## Technical Details

### Sticky-by-Default Option

An alternative, more aggressive approach: make **all wildcards sticky by default**.

```bash
# With _STICKY_WILDCARDS=1 (or set -o sticky-wildcards)
ls *l
# Equivalent to: ls @(*l)
# Expands with sticky bit automatically

# Opt-out when you want normal behavior
set +o sticky-wildcards
ls *l
```

This inverts the safety model for wildcards:
- **Default**: Wildcards are safe (sticky)
- **Opt-in**: Unsafe behavior requires explicit action

Note: Command substitution remains opt-in via `@{...}` syntax, as it's legitimately used to generate options (e.g., `gcc $(pkg-config --cflags gtk)`).

Users could control this per-session:
```bash
export _STICKY_WILDCARDS=1     # Enable sticky wildcards by default
set -o sticky-wildcards        # Shell builtin alternative
```

**Key Rule**: If any part of a concatenated string is sticky, the entire result is sticky. This ensures that operations like `$STEM.txt` or `prefix_$FILE` preserve data semantics.

**Pros**:
- Maximum safety: users protected by default
- No syntax changes needed for most cases
- Existing scripts work (slightly safer, actually)
- Concatenation naturally preserves safety

**Cons**:
- Commands that don't understand sticky bits might misbehave
- Harder migration: requires commands to be updated first
- More magical: behavior changes based on environment variable

**Recommendation**: Start with opt-in `@(...)` syntax (Phase 1-2), then introduce sticky-by-default as Phase 3 once enough commands support it.

### Environment Variable Format
```bash
_STICKY_ARGS="2,4,5"        # argv[2], argv[4], argv[5] are sticky
_STICKY_VALS="*.txt,file-*,-l"  # Optional: preserve original pattern
_STICKY_WILDCARDS=1         # Make all wildcards sticky by default (optional)
```

### Library Support
```c
#include <sticky_args.h>

bool is_sticky(int argc, char *argv[], int index);

// High-level API
typedef struct {
    char *value;
    bool is_sticky;
    bool is_option;  // Starts with - and not sticky
} parsed_arg;

parsed_arg* parse_args_safe(int argc, char *argv[]);
```

### Shell Implementation Pseudocode
```python
# Internal representation: strings carry a sticky flag
class ShellWord:
    def __init__(self, value, is_sticky=False):
        self.value = value
        self.is_sticky = is_sticky

def expand_glob(pattern, is_sticky):
    """Expand wildcards, marking results as sticky if requested."""
    files = glob.glob(pattern)
    # Wildcard expansion yields sticky strings
    return [ShellWord(f, is_sticky=is_sticky) for f in files]

def parse_command_line(tokens):
    """Parse tokens, handling @(...), @{...}, and @'...' syntax."""
    words = []
    for token in tokens:
        if token.startswith("@(") and token.endswith(")"):
            # Sticky wildcard syntax - expand with sticky flag
            pattern = token[2:-1]  # Strip @(...)
            words.extend(expand_glob(pattern, is_sticky=True))
        elif token.startswith("@{") and token.endswith("}"):
            # Sticky command substitution
            command = token[2:-1]  # Strip @{...}
            output = execute_command(command)
            words.append(ShellWord(output, is_sticky=True))
        elif token.startswith("@'") and token.endswith("'"):
            # Sticky literal string
            literal = token[2:-1]  # Strip @'...'
            words.append(ShellWord(literal, is_sticky=True))
        elif has_sticky_parts(token):
            # Token contains sticky variables/literals - concatenate with sticky flag
            result = expand_concatenation(token)
            words.append(ShellWord(result, is_sticky=True))
        elif '*' in token or '?' in token:
            # Normal wildcard - expand without sticky flag
            # (or with sticky flag if _STICKY_WILDCARDS=1)
            use_sticky = env.get('_STICKY_WILDCARDS') == '1'
            words.extend(expand_glob(token, is_sticky=use_sticky))
        elif token.startswith("$(") and token.endswith(")"):
            # Normal command substitution - not sticky
            # (users must explicitly use @{...} for sticky output)
            command = token[2:-1]  # Strip $(...)
            output = execute_command(command)
            words.append(ShellWord(output, is_sticky=False))
        else:
            # Literal argument
            words.append(ShellWord(token, is_sticky=False))
    return words

def build_argv(words):
    """Convert shell words to argv, tracking sticky indices."""
    argv = []
    sticky_indices = []
    
    for i, word in enumerate(words):
        argv.append(word.value)
        if word.is_sticky:
            sticky_indices.append(i)
    
    env["_STICKY_ARGS"] = ",".join(map(str, sticky_indices))
    return argv
```

## Benefits

1. **Security**: Prevents filename-as-option attacks
2. **Correctness**: Commands know what's data vs options
3. **Backward compatible**: Existing code unaffected
4. **Opt-in**: Use only when needed
5. **Composable**: Works with pipes, variables, command substitution
6. **Migration path**: Can be adopted gradually over decades

## Comparison to Alternatives

| Approach | Security | Usability | Backward Compat | Adoption Barrier |
|----------|----------|-----------|-----------------|------------------|
| Status quo | ❌ Broken | ✅ Familiar | ✅ N/A | N/A |
| Always use `--` | ⚠️ If remembered | ❌ Tedious | ✅ Yes | High (requires discipline) |
| Filename prefix `@file` | ✅ Safe | ⚠️ Unfamiliar | ❌ Breaks everything | Impossible |
| Sticky wildcards `@(...)` | ✅ Safe | ✅ Opt-in | ✅ Full | Low (gradual) |

## Call to Action

This proposal provides a **realistic path** to fixing a 50-year-old security flaw in Unix shells.

- **Shell developers**: Implement `@(...)` syntax and sticky bit tracking
- **Command developers**: Check `_STICKY_ARGS` in new commands
- **Standard bodies**: Consider adding sticky wildcards to POSIX shell spec
- **Educators**: Teach sticky wildcards as best practice for safety

## References

- Original wildcard expansion: Thompson shell (1971)
- `--` separator: Added to many tools, but not universally adopted
- Similar proposals: None found with backward-compatible approach

## Author's Note

This is a rational reconstruction approach: identify the root cause (loss of expansion context), design a minimal extension that fixes it (sticky metadata), and provide a migration path that doesn't break existing systems. The goal is not to replace Unix shells, but to evolve them toward correctness while preserving their utility.
