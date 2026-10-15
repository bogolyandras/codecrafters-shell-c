# Tests

Run everything with `ctest --test-dir <build dir> --output-on-failure`.
There are three kinds of tests: 7 case files, 1 pty editor test and 76 CodeCrafters stages.

## 1. Case files (`cases/`)

The shell is fed the `.in` file on standard input. Every line of the `.expect` file must appear in
the output (`!text` = must not appear, `file:NAME:text` = a file the case created contains the text).
The shell must exit with status 0, so a sanitizer report fails the case. Runner: `run_case.cmake`.
Each case runs in its own empty directory.

### `cases/common/` (all platforms, builtins and syntax only)

| Test | What it does |
|---|---|
| `common.basics` | `echo` with plain, single and double quotes and backslash-escaped spaces, `type` of a builtin and of an unknown command, `pwd`, `cd /` and `cd` to home. |
| `common.history_complete` | `history 2` (last N entries), then the `complete` builtin: register with `-C`, print with `-p`, remove with `-r`, whitespace normalisation, and the error for a bad option or missing argument. |
| `common.pipeline_builtins` | Builtin-only pipelines: a builtin as writer and as reader (it does not read), three stages, `>` and `2>` on a stage winning over its pipe, `declare` and `cd` not changing the shell, and twenty pipelines in a row to catch leaked handles. |
| `common.redirect` | `>`, `>>`, `1>`, `1>>`, a quoted `>` that is not a redirect, and several redirects on one command (the last one wins). Checks the file contents. |
| `common.syntax_errors` | Unterminated quote, `>` without a file, `\| \|`, a leading `\|`, `&` in the middle of a line. Each must print an error and the shell must carry on with the next line. |
| `common.variables` | `declare` / `declare -p`, invalid names, `$name` and `${name}` expansion, quoting rules (`'...'`, `"..."`, `\$`), unset variables, and `${}` as a bad substitution. |

### `cases/posix/` (needs real programs such as `cat`, `sleep`)

| Test | What it does |
|---|---|
| `posix.programs` | Runs `/bin/echo` by path, a missing `./program`, a directory as a command, pipelines (`echo \| cat`, `\| cat > file`), `type` of a path and of `cat`, a background `sleep &` with `jobs`, and builtins (`declare`) inside a pipeline. |

## 2. Line editor on a pseudo terminal (`pty_editor.py`, `posix.pty_editor`)

One ctest entry, 15 checks. Python starts the shell on a pty and types keys, because piped input
never reaches the line editor. Run it directly with `python3 -I tests/legacy/pty_editor.py <shell>`.

| Check group | What it does |
|---|---|
| Command completion | TAB on a prefix completes the command name (`ec` -> `echo `). |
| File completion | A single match gets a trailing space; an ambiguous one rings the bell, then lists the candidates on the second TAB (only the last path component is shown); a directory gets `/` and no space. |
| Ctrl-C | Cancels the current line (`^C`). |
| History keys | Up recalls the previous line, Up again the one before it. |
| External completer | After `complete -C`, TAB asks the script: common prefix, a single candidate with a space, no candidates (bell, line unchanged), the sorted list on an empty word, and the argv / `COMP_LINE` / `COMP_POINT` the script receives. |
| Unregistering | After `complete -r` the script is no longer asked. |
| Clean exit | The shell ends with status 0 (also fails on sanitizer reports). |

## 3. CodeCrafters stages (`codecrafters/`, `ctest -R codecrafters.`)

76 tests, one per course stage, named by slug (`codecrafters.oo8` ...). Each starts the shell on a
pty with `PS1="$ "`, types into it, and compares the screen rows with what the stage expects, like the
official tester does. Single stage: `python3 -I tests/legacy/codecrafters/run.py <shell> --stage iz3 --seed 1`;
`--list` prints all of them. Helper programs (`cat`, `ls`, `wc`, a completer, ...) are in `tools.py`.

| Group | Stages (slug: what is tested) |
|---|---|
| **Base** | `oo8` the prompt is printed; `cz2` an unknown command prints `not found`; `ff0` the REPL keeps reading lines; `pn5` `exit`; `iz3` `echo`; `ez5` `type` of a builtin; `mg5` `type` of an executable in PATH; `ip1` run an external program. |
| **Navigation** | `ei0` `pwd`; `ra6` `cd` with an absolute path; `gq9` `cd` with a relative path; `gp4` `cd ~`. |
| **Quoting** | `ni6` single quotes; `tg6` double quotes; `yt5` backslash outside quotes; `le5` backslash inside single quotes; `gu3` backslash inside double quotes; `qj0` running an executable whose name is quoted. |
| **Redirection** | `jv1` `>`; `vz4` `2>`; `el9` `>>`; `un3` `2>>`. |
| **Completion** | `qp2` complete a builtin; `gm9` complete then run it; `qm8` no match rings the bell; `gy5` complete an executable from PATH; `wh6` several matches are listed on the second TAB; `wt6` partial completion to the common prefix. |
| **File completion** | `zv2` a file name; `ue6` nested path; `lc6` a directory gets `/`; `vs5` missing file, bell; `no5` mixed files and directories; `jp8` longest common prefix of file names; `bf8` completing several arguments in one line. |
| **Programmable completion** | `ne7` the `complete` builtin; `oi7` `complete -p` with nothing registered; `wl6` `complete -C` and `-p`; `pm5` run a completer on TAB; `qf1` a completer with no candidates; `zi0` the arguments it receives; `nr7` its environment (`COMP_LINE`, `COMP_POINT`); `ep2` several candidates; `xz3` longest common prefix from candidates; `tz2` `complete -r`. |
| **Background jobs** | `af3` the `jobs` builtin; `at7` run with `&` and print `[N] PID`; `si2` output of a background job; `jd6` list one running job; `dk5` list several; `ma9` report a finished job as `Done`; `rq2` `+` / `-` markers; `bv8` finished jobs are reaped; `fy4` job numbers are reused. |
| **Pipelines** | `br6` two commands joined by `\|`; `ny9` builtins as pipeline stages; `xk3` pipelines with more than two commands. |
| **History** | `bq4` the `history` builtin; `yf5` list the entries; `ag6` `history N` limit; `rh7` Up arrow recalls; `vq0` Up and Down navigation; `dm2` run a recalled command. |
| **History persistence** | `za2` `history -r`; `in3` `history -w`; `sx3` `history -a`; `zp4` load `HISTFILE` at start-up; `kz7` save to `HISTFILE` on exit; `jv2` append to a loaded history on exit. |
| **Parameter expansion** | `ji0` the `declare` builtin; `oa2` `declare -p` of a missing variable; `kv5` assign and print; `db8` validate variable names; `ge9` expand `$name`; `br2` expand `${name}`; `my0` expand a missing variable to nothing. |

## Not covered

The Windows line editor (needs a real console), `-Werror`, static analysis and fuzzing of the tokenizer.
