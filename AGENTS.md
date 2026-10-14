# AGENTS.md

Notes for anyone (human or agent) working on this shell. It is a CodeCrafters "build your own
shell" project written in C, built with CMake (a portable `core` and one platform module per system, with
globbed sources, so new files are picked up on the next build). Target toolchains: GCC and Clang (including
MinGW on Windows). MSVC is not a target. Windows (MinGW-w64 or clang with the MinGW runtime) is
supported: see the Windows section.

## Layout

Three modules. The core is portable and declares what it needs from the operating system; the
platform modules implement that, and exactly one of them is built:

| Directory | Targets | Contents |
|---|---|---|
| `src/core/` | `shell_core` (object library), `shell_core_api` (headers) | Everything portable: strings, containers, tokenizer, builtins, line editor, completion, `shell_run`, and the interfaces to the system (`core/platform.h`, `terminal.h`, `redirection.h`, `execution_handler.h`) |
| `src/posix/` | `shell_posix` (object library, no `main`), `shell` | The POSIX implementation of those interfaces, and `main` |
| `src/win32/` | `shell_win32` (object library, no `wmain`), `shell` | The Win32 implementation, and `wmain` |

Each module has `include/` (public headers) and, for the platform modules, `src/` (sources and
private headers) and `app/` (the entry point, `main_posix.c` / `main_win32.c`, alone in its
directory so the library globs never see it). The top-level `CMakeLists.txt` adds `src/core` and
then `src/win32` or `src/posix` with `if (WIN32)`. The object libraries have no `main`, so tests
can link `shell_core` plus the platform module.

The targets are used through namespaced aliases, `shell::core_api` and `shell::platform_config`
(the second one is defined by whichever platform module is built): a misspelt `shell::` name is a
configure error, where a plain name would silently turn into a linker flag. The project needs
CMake 3.21 (the first that knows C23) and requires the C23 standard from the compiler.

Includes are namespaced by module: `"core/pstring.h"`, `"core/datastructs/vector.h"`,
`"core/platform.h"`, `"core/shell_internal/tokenizer.h"`, `"core/shell_builtin/history.h"`.
`shell_internal` may include `shell_builtin` headers (history, builtin names, completers, variable
name rules) but not the other way round, except `jobs.h` which needs `execution_handler.h`. The only
unprefixed headers are the ones the platform module supplies to the core, and the private headers of
a platform module's own `src/` (`fd_raii.h`, `platform_win32.h`, `redirection_win32.h`).

### Operating system headers

**No source file tests for the operating system, and the core includes no operating system
header** (`unistd.h`, `fcntl.h`, `windows.h`, `io.h`, `termios.h`, ...). It is enforced twice:

- `src/core` is compiled with `cmake/forbidden_headers/` first on its include path. It holds a
  file of the main such names (`windows.h`, `unistd.h`, `termios.h`, `pwd.h`, `sys/wait.h`,
  `poll.h`) containing `#error`, so including one fails the build. Names that a standard header of
  some toolchain includes itself (`sys/stat.h`, `fcntl.h`, `io.h`, `direct.h`) cannot be shadowed
  and are left to the scan.
- `cmake/check_platform_includes.cmake` runs as part of every build (target
  `check_platform_includes`, which `shell_core` depends on, so it runs first even when only one
  target is built) and fails if `src/core` includes any of them, `src/posix` a Windows
  header or `src/win32` a POSIX-only one. This is what catches a wrong header in the module the
  compiler of the current system does not build.

When the core needs something the system provides, add a function to a `core/*.h` interface and
implement it in both platform modules, or put a constant in the per system `platform_config.h` /
`pstring_platform.h` (one pair in `src/posix/include` and `src/win32/include`, found through the
include path; they may include only standard C headers). Do not add an `#ifdef _WIN32`. The only
`if (WIN32)` is in the top-level `CMakeLists.txt`.

Sources of each module are globbed (`CONFIGURE_DEPENDS`), so a new `.c` file is picked up on the
next build. The binary is written to `<build dir>/shell`, where the CodeCrafters runner and CI
expect it.

CI (`.github/workflows/ci.yml`) builds with GCC and Clang using `-Wall -Wextra
-fsanitize=address,undefined`, runs a small piped smoke test, and has a Windows (MinGW-w64, UCRT64)
job that is allowed to fail until it has been green once.

| File | Role |
|---|---|
| `src/core/src/shell.c` | `shell_run`: start-up, the read-eval loop, running a line or a pipeline, clean-up |
| `src/posix/app/main_posix.c`, `src/win32/app/main_win32.c` | `main` / `wmain`, which only call `shell_run` |
| `src/core/src/shell_builtin/builtin_run.c` | The builtin commands themselves (`echo`, `exit`, `type`, `pwd`, `cd`, `history`, `jobs`, `complete`, `declare`), `is_builtin`, `resolve_command` |
| `src/core/src/shell_internal/shell_error.c` | `shell_print_error` and `shell_report_write_error`, the one place errors are formatted |
| `core/common.h` | `PATH_BUFFER_SIZE`, and `NEWLINE` and the path separators from `platform_config.h` |
| `core/str_vec.h`, `src/str_vec.c` | `StrVec`, a vector of owned strings |
| `core/shell_internal/tokenizer.h` | Quotes, backslashes, `~`, `$name` / `${name}`, `|`, `&`, and `>` `>>` `1>` `2>` `1>>` `2>>` parsing into commands |
| `core/shell_internal/tilde_expansion.h` | `get_home_dir`, for `~` and path completion |
| `core/shell_builtin/history.h` | The entered lines and the history files (see History) |
| `core/redirection.h`, `src/posix/src/redirection_posix.c`, `src/win32/src/redirection_win32.c` | `redirect_stream` / `restore_stream`: point stdout or stderr at a file and put it back. Callers see no descriptor |
| `core/shell_internal/line_editor.h` | Interactive line input and TAB completion (see below) |
| `core/terminal.h`, `terminal_posix.c`, `terminal_win32.c` | Terminal mode, key input and output |
| `core/platform.h`, `platform_posix.c`, `platform_win32.c` | Startup, directory listing, executable rules, default home directory, backslash rules |
| `src/posix/include`, `src/win32/include` | `platform_config.h` (separators, `NEWLINE`) and `pstring_platform.h` (`PChar`, `P()`, `p_*` aliases), one pair per system |
| `pstring_posix.c`, `pstring_win32.c` | `p_strerror`, `p_perror`, `p_getcwd`, `p_chdir`, UTF-8 conversion and (Windows) the stream functions |
| `core/shell_internal/completion.h` | The TAB completion candidates: command names, a registered external completer, or paths |
| `core/shell_builtin/jobs.h` | The table of background jobs (`command &`) and the `jobs` listing |
| `core/shell_builtin/builtins.h` | The list of builtin names, shared by `type` and completion |
| `core/shell_builtin/variables.h` | The table of variables made with `declare`, and the identifier rules (see Variables) |
| `core/shell_builtin/completers.h` | The table of completers registered with `complete -C` (see Programmable completion) |
| `core/shell_internal/path_env_handler.h` | PATH scan, `StrVec` of directories, `PathMap` name to full path |
| `core/execution_handler.h`, `execution_posix.c`, `execution_win32.c` | Run a program and wait: `fork`/`execv` or `CreateProcessW` |
| `core/pstring.h`, `src/pstring.c` | The platform string abstraction: `PChar`, `P("...")`, `p_*` functions (see Strings) |
| `core/raii.h` | `CLEANUP`, `AUTO_FREE`, `AUTO_FCLOSE`, `TAKE`, `DISOWN` (`AUTO_CLOSE_DIR` is in `platform.h`; `AUTO_CLOSE` and `TAKE_FD` for descriptors are each platform module's private `fd_raii.h`) |
| `core/datastructs/` | `Vector` and `HashMap` with type safe `VECTOR_DEFINE` / `HASHMAP_DEFINE` |

## Finding commands

A command name that is a builtin is run by the shell. Otherwise `resolve_command` (`builtin_run.c`)
decides: a name with a path separator in it (`./prog`, `/bin/echo`, `src/tool`) is a path, used as
it is if it is an executable file (`platform_is_executable_file`); any other name is looked up in
the PATH index (`find_executable`), which is built once at start-up. Both `type` and pipelines use
the same rule. A failure is `name: not found` in both cases, without telling a missing file from a
missing permission. On Windows a path needs its extension typed (`.\prog.exe`).

## History

`history.[ch]` keeps the entered lines in memory, oldest first, numbered from 1 for the lifetime
of the shell. `main` records every line that is not blank, **before** it runs (so `history`
lists itself) and whether or not it parses. Nothing is deduplicated.

- `history` lists all entries as `%5d  text`; `history N` lists only the last `N`, keeping their
  numbers; `history 0` lists nothing. A non-number is `numeric argument required`, a leading `-`
  is `invalid option`, more than one argument is `too many arguments`.
- Up and Down in the line editor recall entries. The line being typed is kept while an older
  entry is shown and comes back when going Down past the newest one. The editor only reads the
  history, it never adds to it.
- **Files** are UTF-8 text, one command per line. `history -r file` appends the file's lines
  (`history_load`: blank lines skipped, `\r\n` accepted); they come after the `history -r`
  command itself, so the numbers continue. `history -w file` replaces the file with the whole
  history. `history -a file` appends only the entries that are not in a file yet. A missing
  argument, too many arguments, or an unreadable or unwritable file is reported and changes
  nothing. In a pipeline these three do nothing, like `cd` they must not change the shell.
- **New versus saved.** Every entry knows whether it is "new": typed in this session and not
  written to any file yet. Entries loaded from a file are never new. A successful `-w` or `-a`
  marks everything it wrote as saved, so appending never repeats lines, whichever file it goes to.
- **`HISTFILE`.** If it is set and not empty, the history is loaded from it at start-up (a missing
  file is silently fine, other errors are reported) and the new entries are appended to it when
  the shell ends: `exit`, Ctrl-D at an empty prompt, or a fatal error in the main loop. It is
  done by a cleanup variable in `shell_run`, so it does not run on `exit()` or a fatal signal. The
  `exit` command itself is in the file, like in bash.
- It is a builtin, so it works in pipelines (`history | grep x`), where it sees the history as it
  was when the line started.
- There is no size limit and no `HISTSIZE` / `HISTFILESIZE` yet, and no `-c` (clear) or `-d`.

## Variables

`declare` makes shell variables (`variables.[ch]`, a table in declaration order, in memory for
the session only) and the tokenizer expands them, so `echo $name` and `echo ${name}` work.

- `declare name=value...` sets, `declare name` only declares (`declare -p name` then prints
  `declare -- name`, it expands to nothing and an environment variable of that name is hidden).
  A name must be an identifier: an ASCII letter or `_`, then letters, digits and `_`
  (`variable_name_is_valid`). A bad one prints ``declare: `67=x': not a valid identifier``, is
  skipped and makes the status 1; the other arguments are still processed.
- `declare -p [name...]` prints `declare -- name="value"` with `"`, `\`, `$` and a backtick
  escaped; without names it prints every variable alphabetically. A missing one is
  `declare: name: not found` (status 1). An environment variable that is not shadowed is shown as
  `declare -x`. Any other option is `invalid option` (status 2). There is no `-x`, `-r`, `-i`,
  arrays or `unset`.
- Variables are **not exported** to the programs that run: `declare` only changes the shell.
  Expansion looks at the shell's variables first and then at the environment, so `$HOME` works.
- In a pipeline `declare` validates its arguments and changes nothing, like `cd`.
- **Expansion** happens in `tokenize` (a lookup callback, `shell.c`'s `lookup_variable`), before
  anything runs, so a variable declared on a line is not seen by the same line (`declare a=1 | echo $a`).
  `$name` and `${name}` expand outside quotes and inside double quotes, not inside single quotes or
  after a backslash. An unset variable is empty, and a word that is only unquoted unset variables
  is dropped (`echo $none x` has one argument), like in bash. `$` followed by anything else
  (`$1`, `$?`, `$$`) is literal. `${` without a valid name and `}` is `bad substitution`.
- **Deviations from bash**, on purpose for now: the value is not word split (`declare v="a b"; ls
  $v` is one argument, bash makes two), not scanned again for quotes or operators, and there is no
  `${name:-default}` or other `${...}` form, `$?`, `$$`, positional parameters or `$(...)`.
  A tilde after `=` (`declare p=~/x`) is not expanded.

## Programmable completion

`complete` registers an external program that completes the arguments of a command, like
bash's `complete -C`. The table (`completers.[ch]`) is plain memory: it lives for the session and is
not saved anywhere (not in the history file either; the shell has no start-up file yet).

- `complete -C script command...` registers (replacing an earlier one), `complete -p [command...]`
  prints `complete -C 'script' command` (all of them alphabetically without a command, which is
  also what a bare `complete` does), `complete -r [command...]` unregisters (everything without a
  command). Whitespace in the typed line does not matter, the output is always normalised and the
  script is always single quoted. An unknown command is `complete: name: no completion
  specification` (status 1); misuse is status 2. Registering and removing do nothing in a pipeline.
- On TAB, a word that is **not** the first one of its command (the first word of each `|` stage is
  the command) goes to the completer registered for that command, if there is one: it is run as
  `script command word previous_word` (`previous_word` is the word before it, or the command
  itself) through `run_capture`. Its stdout lines are the candidates; the editor then does what it
  does for any list (one candidate: complete with a space, several: longest common prefix, a bell
  when that adds nothing, the list, sorted, on the second TAB). No output means a bell and an
  unchanged line, with no fallback to file names.
- Environment added for the script: `COMP_LINE` (the line typed so far), `COMP_POINT` (the cursor,
  as a byte index; always the length of the line, the editor only appends), `COMP_KEY` and
  `COMP_TYPE` (both `9`, TAB). `COMP_WORDS`/`COMP_CWORD` are bash arrays and are not provided.
- The script is started with `execv`, so it needs the execute bit and a shebang (the kernel runs
  the interpreter); there is no PATH search for it and no `sh` fallback for a file without a shebang.
  Relative script paths are relative to the directory at the time of the TAB.
- `run_capture`: stdin and stderr are the null device (a message would garble the line being
  edited), output is limited to 1 MiB, and on POSIX a script that is silent for 5 seconds is killed,
  because Ctrl-C does not work while the editor is raw. The Windows version has no timeout and was
  written but not built or run.
- **Open concern: the editor blocks while a completer runs.** On POSIX the 5 second timeout is
  the only safety net, and on Windows there is none (`ReadFile` on the pipe from `CreatePipe` has no
  timeout), so a stuck completer freezes the prompt there. Ideas, in rising order of effort:
  `PeekNamedPipe` + `Sleep` polling in `run_capture` (about 15 lines, busy-waits); a named pipe with
  overlapped I/O and `WaitForSingleObject(event, 5000)`; or running the completer on a helper thread
  so the editor keeps reading keys. The thread is the only one that would also let Ctrl-C or
  further typing cancel a slow completer, but it needs care: the editor and `run_capture` share the
  terminal and the line buffer, and a result arriving after the user typed on has to be dropped.
- Candidates from a completer are listed whole (`LineCompleter` sets `*whole_names`), not cut to
  the last path component like file names are.

## Background jobs

A line that ends with an unquoted `&` runs its program in the background (`Command.background`,
set by the tokenizer, which rejects `&` anywhere else). The shell prints `[N] PID` and goes on;
the program's output reaches the terminal as usual, its input is the null device.

- `jobs.[ch]` holds the `JobTable`, oldest first. A new job gets the highest number in the table
  plus one, so numbers restart at 1 once the table is empty (like bash).
- `start_background` / `poll_background` (`execution_handler.h`) start a program without waiting
  and check without blocking. `BackgroundProcess` is an **opaque** type: the core only holds a
  pointer and uses `background_process_id`, `poll_background` and `background_process_free`; each
  platform module defines the struct (a `pid_t` on POSIX, a handle and the id on Windows). The
  `JobTable` owns the processes it holds and frees them when a job is forgotten or at exit. On POSIX the child gets its own process group, so Ctrl-C of a
  foreground command does not reach it.
- Before every prompt the shell polls the jobs and prints each finished one once
  (`[1]-  Done                    sleep 1`, or `Exit N`, no `&`), then forgets it.
- `jobs` lists all jobs: `[N]` then `+` (newest), `-` (the one before) or a blank, two spaces,
  the status padded to 24, the command as typed with ` &`. It also forgets the finished ones.
  In a pipeline it only prints, since it cannot wait for anything.
- **Not done:** builtins and pipelines cannot be backgrounded (an error is printed), no `fg`,
  `bg`, `wait`, `kill %n`, `jobs -l`, and a signal death is `Exit 128+n`, not `Terminated`.
- The Windows side (`CreateProcessW`, `WaitForSingleObject(h, 0)`) was written but not built or run.

## Pipelines

`tokenize` returns a `CommandVec`: one `Command` (words plus redirections) per `|`-separated
part, and exactly one for a line without `|`. A single command runs as before. Several go to
`run_pipeline` (`execution_handler.h`), which connects each stage's output to the next one's input.

- **Builtins are stages too.** They are called through the `BuiltinRunner` callback with
  `in_pipeline` set. On POSIX every stage, builtin or program, runs in its own forked child, so
  the builtin's state changes die with it. On Windows there is no `fork`: the programs are
  started first, then the builtins run in the shell process with descriptors 0, 1 and 2 pointed
  at the pipes and restored afterwards.
- **A builtin in a pipeline must not change the shell.** `cd` only validates its argument and
  `exit` does nothing, like in a subshell; `history -r/-w/-a`, `declare name=value` and
  `complete -C/-r` only check their arguments. Write failures are silent there, because the
  reader having finished is the normal way for a pipeline to end.
- **Builtins do not read.** Their input pipe is closed at once, so a writer upstream ends with a
  broken pipe instead of blocking: `ls | type exit` prints `exit is a shell builtin`.
- Redirections of a stage win over its pipes, like in other shells (`a > f | b` sends `a`'s
  output to `f` and `b` gets nothing).
- Every command must be found before any starts; an unknown command stops the whole pipeline.
- The status of a pipeline is the status of its last stage. There is no `pipefail` and no `$?` yet.
- Completion treats the word after a `|` as a command name (quotes are not considered).

## Strings

All shell text is `PChar`, from `pstring.h`: `char` (UTF-8 bytes) on POSIX and `wchar_t`
(UTF-16 code units) on Windows. Do not write `char *` for text, and do not call `strlen`,
`strcmp`, `printf` and friends directly.

- Literals: `P("text")`. Narrow literals like `NEWLINE` can be appended inside it:
  `P("cd: too many operands" NEWLINE)`.
- Format strings: `%` followed by `PS` for a `PChar *` argument, for example
  `p_fprintf(stderr, P("%" PS ": not found" NEWLINE), name)`. Never use a bare `%s` or `%ls`.
- Functions: `p_strlen`, `p_strcmp`, `p_strncmp`, `p_strchr`, `p_strrchr`, `p_memcpy`,
  `p_memset`, `p_snprintf`, `p_printf`, `p_fprintf`, `p_fputs`, `p_fgets`, `p_getchar`,
  `p_strdup`, `p_strerror`, `p_perror`, `p_fopen`.
- **Sizes and lengths count PChar units, not bytes.** Multiply by `sizeof(PChar)` when
  allocating, and pass `sizeof buffer / sizeof buffer[0]` as a size. Use `PATH_MAX` as is.
- `p_unit(c)` gives an unsigned value for comparisons (a plain `char` is signed).
  `p_is_continuation(c)` tells whether a unit continues a multi unit character (UTF-8
  continuation byte, or a UTF-16 low surrogate), for stepping back over whole characters.
- `p_from_utf8` / `p_to_utf8` convert at the edges (files, narrow OS APIs); they are plain
  copies on POSIX.
- Text streams carry UTF-8 on every platform. On Windows `p_printf`, `p_fprintf`, `p_fputs`,
  `p_fgets` and `p_getchar` are functions that convert with the project's own codec (or write
  wide text straight to a console). Do not use the C runtime's wide stdio or `_O_U8TEXT`: it
  converts one UTF-16 unit at a time and mangles characters outside the BMP.
- OS calls that take a path or a name go through `p_getenv`, `p_fopen`, `p_getcwd`, `p_chdir`
  (wide variants on Windows). Directory listing, executables, process creation and the terminal
  are behind `platform.h`, `execution_handler.h` and `terminal.h`; do not call `opendir`,
  `stat`, `fork` and friends from shell logic.

## Conventions

- **Return values**: `int` functions return `0` on success and `-1` with `errno` set on failure.
  Pointer functions return `NULL` with `errno` set. `vector_*` and `hashmap_*` follow this too.
- **Tri-state exceptions** to the `0` / `-1` rule, modelled on `read(2)`:
  `line_read` (`LINE_OK` is 1, `LINE_EOF` is 0, `LINE_ERROR` and `LINE_TOO_LONG` are negative),
  `terminal_read_char` (1 character, 0 end of input, -1 error), `do_execution` and `run_capture`
  (the child's exit status, or -1 if forking or waiting failed), `poll_background` (0 ended,
  1 still running, -1 error), and boolean predicates such as `terminal_is_interactive`.
- **Who prints**: library code only returns errors. `shell.c` and `builtin_run.c` report them, on stderr, as
  `name: reason`. The exceptions are the child after a failed `execv`, which is the only place
  that knows the reason, `restore_fd`, a cleanup function that cannot return an error, and
  `jobs_print`, which exists to write the job list.
- **Ownership**: `StrVec` of tokens owns its strings (`StrVec_clear_owned`, `StrVec_free_owned`).
  Use `AUTO_FREE` + `DISOWN` when handing a heap string to a container, and `TAKE` when
  returning one.
- **RAII**: `__attribute__((cleanup))` via `raii.h` (`AUTO_FREE`, `AUTO_CLOSEDIR`, `AUTO_CLOSE`
  for descriptors with `TAKE_FD`). Cleanups do **not** run on `exit()`,
  `_exit()`, `abort()`, `exec*()`, `longjmp` or fatal signals. Always initialize variables at
  their declaration.

## Line editor and TAB completion: caveats

readline is deliberately **not** used (licence, portability, and the project goal), and
`CMakeLists.txt` does not link it. Do not add it back.

### How it works

- `line_read()` checks `terminal_is_interactive()`. When stdin is **not** a terminal (pipes,
  scripts, the CodeCrafters test harness through redirection) it falls back to plain `fgets`.
  Keep this fallback: output must stay identical in both modes.
- On a terminal it switches to raw-ish mode (`ICANON`, `ECHO`, `ISIG`, `IEXTEN` cleared,
  `VMIN=1`, `VTIME=0`) only **while reading one line**, and restores the saved mode before
  returning. Children therefore always run with the normal cooked terminal.
- The first word is completed as a command name (builtins plus PATH executables), unless it
  contains a `/`. Every other word goes to the external completer registered for its command
  (see Programmable completion), and without one it is completed as a path, in the current
  directory or in the directory the word names (`src/ma`, `~/Doc`). First TAB on an ambiguous word completes the
  common prefix or rings the bell; a second TAB in a row lists the candidates and redraws the
  prompt. A sole directory match is completed with a trailing `/` and no space.

### Things that are easy to break

1. **Never leave the terminal raw.** The raw section is wrapped in a `CLEANUP` guard, and
   `SIGTERM`/`SIGHUP` handlers restore the saved mode and re-raise. Anything new that can end
   the process while in raw mode (new signals, `exit()` inside the editor) must restore first.
2. **Do not mix stdio and `read(0)`.** In interactive mode input is read with `read()` byte by
   byte. Calling `fgets`/`getchar` on stdin while the editor is active, or reading stdin through
   stdio and then switching, loses buffered bytes. Output from the editor uses `write(1)`.
3. **`ISIG` is off in raw mode.** Ctrl-C arrives as `0x03` and cancels the current line; Ctrl-Z
   (`0x1a`) is ignored. While a **child** runs, the terminal is cooked and `SIGINT` goes to the
   whole foreground group, which currently **also kills the shell** (the shell does not ignore
   `SIGINT`). Fixing it means ignoring `SIGINT` in the shell and resetting it to `SIG_DFL` in the
   child before `execv`, because `SIG_IGN` is inherited across `exec`.
4. **Redirections are applied after the line is read**, so the editor always talks to the real
   terminal. Do not move redirection setup before `line_read`.
5. **No cursor movement.** Editing is append-only at the end of the line. Left/Right, Home/End and
   other escape sequences are read and **ignored**; Up and Down walk through the history (see
   below). There is no Ctrl-R, no Ctrl-W, no kill ring, no vi mode, no `~/.inputrc`.
6. **Width assumptions.** Backspace removes whole UTF-8 characters but assumes each is one column
   wide, so double width characters and combining marks make the display drift. Lines longer
   than the terminal width are redrawn without wrap handling, and `SIGWINCH` is not handled.
7. **Word boundaries are naive.** The word under completion starts after the last blank, so
   quotes are not considered: `echo "a b<TAB>` completes `b`, and `cat 'My Do<TAB>` does not
   work. Unquoted names with spaces do work, they are inserted with backslash escapes
   (`My\ Documents/`). Only `~/` is expanded, not `~user/`.
8. **Fixed buffer.** The line buffer is 1024 `PChar` units (bytes on POSIX). In interactive mode extra characters are
   rejected with the bell; in non-interactive mode the rest of the line is discarded and
   `LINE_TOO_LONG` is reported.
9. **Candidate list is a snapshot.** The PATH executables are indexed once at startup.
   Executables installed or removed afterwards are not seen until the shell restarts.
10. **Many candidates are printed in one block**, with no pager and no "display all N
    possibilities?" prompt.
11. **Path completion reads the directory on every TAB** and stats symlinks and unknown entry
    types to find directories. Huge directories are slow, and matches are not cached.
12. **Hidden files** are only offered when the typed name already starts with `.`, and `.` and
    `..` are never offered.

### Windows

The editor uses `terminal_win32.c`: console mode without line input, echo and processed input,
`ReadConsoleInputW` for key events (Up and Down are turned into the escape sequences a POSIX
terminal sends, other keys without a character are dropped), `WriteConsoleW` for output. When stdin is not a console (a pipe, a file,
mintty in Git Bash) the plain `fgets` fallback runs.

### Testing it

Piped input only exercises the fallback. To test the editor you need a pseudo terminal, for
example a small Python script using `pty.fork()` that sends `"ech\t"`, `"xdg-d\t\t"`, `"\x7f"`,
`"\x03"` and `"\x04"` and checks the echoed output (name the script something other than
`pty.py`, which shadows the standard module). Build with
`-fsanitize=address,undefined` and check that no leak report appears on exit.

## Windows

Built with `-municode` and the entry point `wmain` (see `src/win32/app/main_win32.c` and
`src/win32/CMakeLists.txt`). The `src/win32/` module is only compiled there.

### Decisions

- **Backslash.** On POSIX a backslash escapes any character. On Windows it is a path separator
  (`cd C:\Users\me`), so an unquoted one only escapes a blank, a quote, `>`, `|`, `&` or `~`
  (`tokenizer_is_escapable`), and inside double quotes only `"`. Both `/` and `\` separate path
  components: the core asks `platform_is_path_separator` wherever it needs to know, in completion
  (is the first word a path, the directory part), in the candidate list display and in `~` expansion
  (`~\Documents`). Do not write `'/'` in core code for this. The one fixed `/` is the trailing
  directory marker that completion puts on a candidate and the editor reads back.
- **Executables.** There is no execute permission: a regular file is runnable if its extension is
  `.com .exe .bat .cmd` (a fixed list, `PATHEXT` is not read). Names are lower cased and the
  extension may be left out (`find_executable`), so `type notepad` finds `notepad.exe`.
  Completion of a command name ignores case.
- **Processes.** `CreateProcessW`, with the command line quoted by `quote_argument`. The
  redirected C runtime descriptors 0, 1 and 2 are passed as the standard handles. Batch files run
  through the command interpreter because no application name is given.
- **Ctrl-C.** The shell ignores it while a command runs (`SetConsoleCtrlHandler`), so only the
  command is interrupted. At the prompt the editor reads it as a key.
- **Exit status** values above 255 are reported as 255.
- **Redirected files** are opened in text mode, so the shell's own output gets `\r\n` line
  endings. Programs that are launched write their own bytes. The files are opened with
  `CreateFileW` and wrapped in a C runtime descriptor (`_open_osfhandle`), which is what the
  redirection of the shell's own output (`_dup2` onto 1 and 2) still needs. Moving all output to
  handles (`SetStdHandle`, `WriteFile`) is not done: it needs the builtins to write through a
  platform writer instead of the C runtime's `stdout`.
- **Not done:** `PATHEXT`, wildcard expansion, case-insensitive file name completion, and leaving
  the extension out when running a command given as a path (`.\prog`, see Finding commands).

### Building and testing without a Windows machine

Zig bundles the mingw-w64 headers and Wine runs the result, which is how the port was developed:

    python3 -m venv venv && venv/bin/pip install ziglang
    venv/bin/python -m ziglang cc -target x86_64-windows-gnu -municode -DUNICODE -D_UNICODE \
        -Isrc/core/include -Isrc/win32/include -Wall -Wextra -std=gnu11 \
        $(find src/core/src src/win32/src src/win32/app -name '*.c') -o shell.exe
    printf 'echo hi > out.txt\ncmd /c type out.txt\nexit\n' | wine shell.exe

Wine can also give the editor a console: run `wine shell.exe` inside a pseudo terminal (the same
Python harness as above, with a longer first wait while wineserver starts). Its output contains
cursor escape sequences added by Wine's console bridge.
