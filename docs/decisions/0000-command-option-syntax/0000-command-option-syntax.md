# 0000 - Command option syntax, 2025-11-29

## Issue

There's no universal consensus on the syntax of command-line options. This note
documents the approach to be taken with nutmeg-run (and similar commands such as
nutmeg-common, nutmeg-bundler etc) and the nutmeg command itself.

## Decision

- Subcommands (relevent to the `nutmeg` command but not `nutmeg-run`)
- Short options
    - `cmd -x`, when the option _may_ take no value
    - `cmd -o VALUE`, when the option _must_ take a value
    - `cmd -o=VALUE`, when the option _may_ take a value
    - `cmd -xy`, compacted short options, all which _may_ take no value
- GNU-style long options 
    - `cmd --long-option`, when the option _may_ take no value
    - `cmd --long-value VALUE`, when the option _must_ take one value
    - `cmd --long-value=VALUE`, when the option _may_ take one value
    - `cmd --help`, will be provided (but short-codes are optional)
- Mandatory positional arguments
    - `cmd ARG1 ARG2 ARG3`, are positional arguments that must come before 
      any named argument. There must be a fixed number of such arguments.
      Sanity checks for positional arguments starting with `-` are required
      e.g. if the argument is supposed to be a file to read, does it exist?
      If no check is possible a warning should be issued and a long-option to
      suppress that warning is provided, `--no-missed-arg-warning`.
- End of named options and trailing positional arguments
    - `cmd OPTIONS -- ARG1 ARG2 ARG3 ...`, the `--` marks the end of
      non-positional arguments and introduces an arbitrary number of
      further arguments.
- Repeated named options
    - Options _may_ be repeated multiple times e.g. `-v`, `-vv`. 
    - If an option that does not have a list value is repeated, a suppressable 
      warning will be issued `--no-dup-arg-warning`.

## Factors

- Subcommands are appropriate when the command is effectively a gateway to
  a set of signficantly different functions.

- Short-option with arguments are potentially confusing, so we do not allow
  option-compaction to include short-options that are taking values. This has
  two benefits:
  - It is clear which short-options are grouped with which value
  - It allows short options to take optional arguments without ambiguity

- Positional arguments might include a leading `-` or `--` and not properly
  distinguishing files starting with a `-` sign or even `--` is an underrated
  weakness in many system commands. For example, create a file `-l` with
  the command `touch -- -l` and then try `ls *l` and you will see the `ls` 
  command accept the wildcard exansion as an option! This is clearly a
  serious design flaw.

- To prevent this, we require that positional arguments, which might be
  completed with a wildcard expansion and substitute options, only appear in
  contexts that exclude named arguments. The risk here is that the user
  accidently omits a positional argument and the named argument is accepted. 
  
- To mitigate this risk, we require that when a positional arguments begins with
  a `-`, an early sanity check of that argument is made OR a suppressable 
  warning is issued.

- The same reasoning does not apply to trailing arguments since omitting the
  `--` is not a typical mistake.



## Consequences

- Note that a common idiom `cp FILES... DEST` is forbidden by these rules.
  This is by design, since `cp` has multiple weaknesses e.g. cp *.web /tmp
  will fail if there are no matching files. And `cp` can be tricked by files
  with leading `-` signs. The equivalent within these rules would be:
  `cp --to DEST -- FILES...`. Clumsier but, given the bad design of wildcard
  expansion, much safer.

- Options that must take a value may use both `--foo=VALUE` and `--foo VALUE`
  syntax. But options that take zero or one values must use `--foo=VALUE` when
  a value is supplied and `--foo` when it is not.
  