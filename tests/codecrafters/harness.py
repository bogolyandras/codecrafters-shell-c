"""What a CodeCrafters stage test does, rebuilt: start the shell on a pseudo terminal, type into
it, and compare the rows of the screen with what the stage expects.

The structure follows the tester's test cases (command and response, autocomplete, multiple
completions, jobs, history ...): each method here is one of them. A pointer to the next row to
check moves forward as lines are accepted, and the prompt is checked without being consumed.
"""

import fcntl
import os
import pty
import random
import re
import select
import shutil
import signal
import struct
import tempfile
import termios
import threading
import time

import tools
from vt import Screen

PROMPT = "$ "
FIRST_PROMPT_TIMEOUT = 5.0  # The tester is lenient while the shell starts
TIMEOUT = 2.0

WORDS = ["apple", "banana", "cherry", "grape", "lemon", "mango", "orange", "peach", "pear", "plum",
         "melon", "kiwi", "fig", "lime", "berry", "guava", "olive", "papaya", "quince", "raisin"]
SMALL_WORDS = ["ant", "bee", "cow", "dog", "fox", "owl", "pig", "rat"]
LARGE_WORDS = ["hello", "world", "test", "example", "shell", "script"]
NAMES = ["Alice", "David", "Emily", "James", "Maria"]


class StageFailure(Exception):
    pass


class Line:
    """An expected row: this text, or a row that one of the patterns matches."""

    def __init__(self, text, patterns=()):
        self.text = text
        self.patterns = [re.compile(p) if isinstance(p, str) else p for p in patterns]

    def matches(self, got):
        if got == self.text:
            return True
        return any(p.search(got) for p in self.patterns)


def line(item):
    return item if isinstance(item, Line) else Line(item)


class Session:
    """The shell process on a pty, with its output fed to a Screen by a reader thread."""

    def __init__(self, executable, env, cwd, rows=50, columns=240):
        self.screen = Screen(columns)
        self.eof = False
        self._status = None
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            try:
                # Python ignores SIGPIPE, and an ignored signal survives exec: restore the defaults
                # so that the shell and what it starts die from a broken pipe like they do normally
                for number in (signal.SIGPIPE, signal.SIGINT, signal.SIGQUIT, signal.SIGTERM):
                    signal.signal(number, signal.SIG_DFL)
                os.chdir(cwd)
                os.execve(executable, [executable], env)
            finally:
                os._exit(127)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))
        self._reader = threading.Thread(target=self._read, daemon=True)
        self._reader.start()

    def _read(self):
        while True:
            try:
                data = os.read(self.fd, 65536)
            except OSError:
                break
            if not data:
                break
            self.screen.feed(data)
        self.eof = True

    def send(self, text):
        os.write(self.fd, text.encode())

    def wait_until(self, predicate, timeout):
        deadline = time.time() + timeout
        while True:
            if predicate():
                return True
            if time.time() > deadline:
                return False
            if self.eof:
                return predicate()
            time.sleep(0.002)

    def exit_status(self, timeout=TIMEOUT):
        """The exit code once the process has ended, or None if it is still running."""
        deadline = time.time() + timeout
        while self._status is None:
            try:
                pid, status = os.waitpid(self.pid, os.WNOHANG)
            except ChildProcessError:
                return None
            if pid:
                self._status = status
                break
            if time.time() > deadline:
                return None
            time.sleep(0.005)
        if os.WIFEXITED(self._status):
            return os.WEXITSTATUS(self._status)
        return 128 + os.WTERMSIG(self._status)

    def kill(self):
        for target in (-self.pid, self.pid):
            try:
                os.kill(target, signal.SIGKILL)
            except OSError:
                pass
        try:
            os.waitpid(self.pid, 0)
        except OSError:
            pass
        try:
            os.close(self.fd)
        except OSError:
            pass


class Stage:
    def __init__(self, shell_path, seed):
        self.shell_path = os.path.abspath(shell_path)
        self.rng = random.Random(seed)
        self.base = os.path.realpath(tempfile.mkdtemp(prefix="cc-stage-"))
        self.tools_cache = os.path.join(self.base, ".tools")
        os.mkdir(self.tools_cache)
        self.env = dict(os.environ)
        self.env["PS1"] = PROMPT
        self.env.pop("HISTFILE", None)
        # PATH holds only what the stages use. The tester runs in a bare container; on a machine
        # with many tools, prefixes such as `exi` would match more than the builtin
        self.original_path = self._make_clean_bin()
        self.env["PATH"] = self.original_path
        self.cwd = self.base
        self.session = None
        self.row = 0
        self._bells_seen = 0
        self._used_names = set()
        self.step = 0

    CLEAN_BIN_TOOLS = ("cat", "cp", "mkdir", "ls", "sleep", "du", "stat", "wc", "head", "tail", "grep", "sh",
                       "rm", "echo", "tree", "tr", "sort", "env", "true", "false", "mv", "touch")

    def _make_clean_bin(self):
        directory = os.path.join(self.base, ".bin")
        os.mkdir(directory)
        system_path = os.environ.get("PATH", "/usr/bin:/bin")
        for tool in self.CLEAN_BIN_TOOLS:
            found = shutil.which(tool, path=system_path)
            if found:
                os.symlink(found, os.path.join(directory, tool))
        return directory

    # ---- randomness and files --------------------------------------------------------

    def words(self, count):
        return self.rng.sample(WORDS, count)

    def small_words(self, count):
        return self.rng.sample(SMALL_WORDS, count)

    def int_between(self, low, high):
        """[low, high), like the tester's RandomInt."""
        return self.rng.randrange(low, high)

    def ints(self, low, high, count):
        return self.rng.sample(range(low, high), count)

    def secret(self):
        return "".join(str(self.rng.randrange(10, 100)) for _ in range(5))

    def name(self):
        return self.rng.choice(NAMES)

    def random_dir(self):
        while True:
            path = os.path.join(self.base, *self.words(3))
            if not os.path.exists(path):
                os.makedirs(path)
                return path

    def short_dir(self):
        for word in self.rng.sample(SMALL_WORDS, len(SMALL_WORDS)):
            path = os.path.join(self.base, word)
            if not os.path.exists(path):
                os.makedirs(path)
                return path
        path = os.path.join(self.base, "dir%d" % self.int_between(100, 999))
        os.makedirs(path)
        return path

    def temp_file(self, suffix=".txt"):
        return os.path.join(self.base, self.rng.choice(WORDS) + str(self.int_between(10, 99)) + suffix)

    def write_files(self, paths, contents):
        for path, content in zip(paths, contents):
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as handle:
                handle.write(content)

    def add_to_path(self, directory):
        self.env["PATH"] = directory + ":" + self.env.get("PATH", "")

    def setup_commands(self, specs, short=True):
        """specs: (kind, file name, metadata). Returns the directory, which is put first on PATH."""
        directory = self.short_dir() if short else self.random_dir()
        self.add_to_path(directory)
        for kind, name, metadata in specs:
            target = os.path.join(directory, name)
            if kind == "signature_printer":
                tools.write_signature_printer(target, metadata, self.tools_cache)
            else:
                tools.write_script(target, kind)
        return directory

    def which_original(self, command):
        return shutil.which(command, path=self.original_path)

    # ---- the shell -------------------------------------------------------------------

    def start(self, check_prompt=True):
        self.session = Session(self.shell_path, self.env, self.cwd)
        if check_prompt:
            if not self.session.wait_until(lambda: self.screen.row(0) == PROMPT, FIRST_PROMPT_TIMEOUT):
                self.fail("Expected a prompt ('$ ')", "no prompt received")
            time.sleep(0.01)

    def restart(self):
        """Kills the shell and starts a new one, with the same environment and directory."""
        self.session.kill()
        self.row = 0
        self._bells_seen = 0
        self.start()

    @property
    def screen(self):
        return self.session.screen

    def fail(self, expected, got=""):
        raise StageFailure("%s\n  Received: %r\n%s" % (expected, got, self.screen.dump()))

    def send_text(self, text):
        self.session.send(text)

    def send_command(self, command):
        self.session.send(command + "\n")

    def close(self):
        if self.session is not None:
            self.session.kill()
        shutil.rmtree(self.base, ignore_errors=True)

    # ---- assertions ------------------------------------------------------------------

    def _check(self, expected, prompt, start):
        for offset, wanted in enumerate(expected):
            got = self.screen.row(start + offset)
            if not wanted.matches(got):
                return "row %d: expected %r" % (start + offset, wanted.text), got
        if prompt:
            got = self.screen.row(start + len(expected))
            if got.lower() != PROMPT.lower():
                return "row %d: expected the prompt %r" % (start + len(expected), PROMPT), got
        return None

    def expect_rows(self, expected, prompt=True, timeout=TIMEOUT):
        """The next rows are `expected`, and then the prompt. Consumes the rows, not the prompt."""
        wanted = [line(item) for item in expected]
        start = self.row
        if not self.session.wait_until(lambda: self._check(wanted, prompt, start) is None, timeout):
            problem, got = self._check(wanted, prompt, start)
            self.fail(problem, got)
        self.row += len(wanted)

    def expect_row_stay(self, text, patterns=(), timeout=TIMEOUT):
        """The current row is `text` (the cursor stays on it): used while a line is being edited."""
        wanted = [Line(text, patterns)]
        start = self.row
        if not self.session.wait_until(lambda: self._check(wanted, False, start) is None, timeout):
            problem, got = self._check(wanted, False, start)
            self.fail(problem, got)

    def expect_empty_row(self, timeout=0.3):
        """The row at the pointer is still empty (the quiet first TAB prints nothing below the
        line). Waits a moment, so that output that is on its way would be seen."""
        time.sleep(timeout)
        got = self.screen.row(self.row)
        if got != "":
            self.fail("row %d: expected an empty line" % self.row, got)

    def expect_bell(self, timeout=TIMEOUT):
        if not self.session.wait_until(lambda: self.screen.bells > self._bells_seen, timeout):
            self.fail("Expected the bell to ring, but it didn't")
        self._bells_seen += 1

    # ---- the tester's test cases -----------------------------------------------------

    def command_no_response(self, command, prompt=True):
        self.send_command(command)
        self.expect_rows(["$ " + command], prompt=prompt)

    def command_response(self, command, expected, patterns=()):
        self.send_command(command)
        self.expect_rows(["$ " + command, Line(expected, patterns)])

    def command_multiline(self, command, lines, prompt=True):
        self.send_command(command)
        self.expect_rows(["$ " + command] + list(lines), prompt=prompt)

    def invalid_command(self, command, prompt=True):
        patterns = [r"^(bash: )?%s: (command )?not found$" % re.escape(command),
                    r"^ash: %s: not found$" % re.escape(command),
                    r"^zsh: command not found: %s$" % re.escape(command),
                    r"^dash: [0-9]+: %s: not found$" % re.escape(command)]
        self.send_command(command)
        self.expect_rows(["$ " + command, Line("%s: command not found" % command, patterns)], prompt=prompt)

    def exit_command(self, allowed=(0,)):
        self.send_command("exit")
        self.expect_rows(["$ exit"], prompt=False)
        status = self.session.exit_status()
        if status is None:
            self.fail("Expected the program to exit, it is still running")
        if status not in allowed:
            self.fail("Expected exit code to be one of %s, got %d" % (list(allowed), status))
        after = self.screen.row(self.row)
        if after.strip() not in ("", "exit"):
            self.fail("Expected no output after exit, got %r" % after)

    def type_builtin(self, command, also_reserved=False):
        patterns = [r"^%s is a( special)? shell builtin$" % re.escape(command)]
        if also_reserved:
            patterns.append(r"^%s is a reserved word$" % re.escape(command))
        self.command_response("type " + command, "%s is a shell builtin" % command, patterns)

    def type_executable(self, command, expected_path):
        patterns = [r"^(%s is )?%s$" % (re.escape(command), re.escape(expected_path))]
        self.command_response("type " + command, "%s is %s" % (command, expected_path), patterns)

    def type_invalid(self, command):
        patterns = [r"^(bash: type: )?%s[:]? not found$" % re.escape(command)]
        self.command_response("type " + command, "%s: not found" % command, patterns)

    def cd_and_pwd(self, directory, response):
        os.makedirs(response, exist_ok=True)
        self.command_no_response("cd " + directory)
        self.command_response("pwd", response)

    # -- completion

    def autocomplete(self, raw, expected, previous="", bell=False):
        """Type `raw` after what is on the line, press TAB, and the whole line must be `expected`
        (with its trailing space, if completion adds one). The line is left unfinished."""
        self.send_text(raw)
        self.expect_row_stay("$ " + previous + raw)
        self.send_text("\t")
        time.sleep(0.01)
        self.expect_row_stay("$ " + expected)
        if bell:
            self.expect_bell()

    def multiple_completions(self, raw, options_line, patterns=(), tab_count=2, bell=True):
        self.send_text(raw)
        self.expect_rows(["$ " + raw], prompt=False)  # consumes the row: the options go below it
        for index in range(tab_count):
            time.sleep(0.005)
            self.send_text("\t")
            if index == 0 and bell:
                self.expect_empty_row()
                self.expect_bell()
        self.expect_rows([Line(options_line, patterns)], prompt=False)
        self.expect_row_stay("$ " + raw)

    def partial_completions(self, pairs, after_each=None):
        """pairs: (typed text, expected whole line). The typed text is appended after each TAB."""
        previous = ""
        for index, (typed, expected) in enumerate(pairs):
            if typed:
                self.send_text(typed)
                self.expect_row_stay("$ " + previous + typed)
            self.send_text("\t")
            self.expect_row_stay("$ " + expected)
            previous = expected
            if after_each is not None:
                after_each(index)

    def autocomplete_and_run(self, text, reflection, args, output, no_space=False):
        self.send_text(text)
        self.expect_row_stay("$ " + text)
        self.send_text("\t")
        self.expect_row_stay("$ " + reflection + ("" if no_space else " "))
        tail = " ".join(args)
        self.send_text(tail + "\n")
        self.expect_rows(["$ %s %s" % (reflection, tail)] + ([output] if output else []), prompt=False)

    # -- jobs

    def descendants(self):
        """Process ids below the shell, from /proc (Linux) or `ps` elsewhere."""
        children = {}
        if os.path.isdir("/proc"):
            for entry in os.listdir("/proc"):
                if entry.isdigit():
                    try:
                        with open("/proc/%s/stat" % entry) as handle:
                            fields = handle.read().rsplit(")", 1)[1].split()
                        children.setdefault(int(fields[1]), []).append(int(entry))
                    except (OSError, IndexError, ValueError):
                        pass
        else:
            import subprocess
            output = subprocess.run(["ps", "-A", "-o", "pid=,ppid="], capture_output=True, text=True).stdout
            for entry in output.splitlines():
                pid, ppid = entry.split()
                children.setdefault(int(ppid), []).append(int(pid))
        found, todo = [], [self.session.pid]
        while todo:
            for child in children.get(todo.pop(), []):
                found.append(child)
                todo.append(child)
        return found

    def background_command(self, command, job_number):
        self.send_command(command + " &")
        self.expect_rows(["$ %s &" % command], prompt=False)
        pattern = re.compile(r"^\[%d\] (\d+)$" % job_number)
        self.expect_rows([Line("[%d] <PID>" % job_number, [pattern])], prompt=True)
        received = pattern.match(self.screen.row(self.row - 1))
        pid = int(received.group(1))
        if pid not in self.descendants():
            self.fail("Could not find process with PID %d" % pid)
        argv0 = command.split()[0]
        exe_link = "/proc/%d/exe" % pid
        if os.path.exists(exe_link):
            found = shutil.which(argv0, path=self.env["PATH"])
            expected = {found, os.path.realpath(found)} if found else set()
            actual = os.path.realpath(exe_link)
            if actual not in expected:
                self.fail("Expected executable path for %s to be %r, got %r" % (argv0, found, actual))

    @staticmethod
    def job_entry(number, status, command, marker):
        """The line `jobs` prints, and a pattern for the shells that format it a little differently."""
        sign = {"unmarked": " ", "current": "+", "previous": "-"}[marker]
        text = "[%d]%s  %s                 %s" % (number, sign, status, command)
        if status == "Running":
            text += " &"
        pattern = r"^\[%d\]\s*%s\s+(?i:%s)\s+%s%s$" % (
            number, re.escape(sign), re.escape(status), re.escape(command),
            "( &)?" if status == "Running" else "")
        return Line(text, [pattern])

    def jobs(self, entries):
        self.send_command("jobs")
        self.expect_rows(["$ jobs"] + [self.job_entry(*entry) for entry in entries])

    def command_with_reaped_jobs(self, command, output, entries):
        self.send_command(command)
        self.expect_rows(["$ " + command, output] + [self.job_entry(*entry) for entry in entries])

    def write_to_fifo(self, path, content):
        def write():
            with open(path, "w") as handle:
                handle.write(content)
        thread = threading.Thread(target=write, daemon=True)
        thread.start()
        thread.join(TIMEOUT)
        if thread.is_alive():
            self.fail("Nobody is reading %s" % path)

    def make_fifo(self):
        path = os.path.join(self.base, "%s-%d" % (self.rng.choice(WORDS), self.int_between(1, 100)))
        while os.path.exists(path):
            path += "x"
        os.mkfifo(path, 0o644)
        return path

    # -- history and declare

    def history(self, previous, offset=0, last=0):
        command = "history" if not last else "history %d" % last
        self.send_command(command)
        self.expect_rows(["$ " + command], prompt=False)
        start = 0
        if 0 < last < len(previous):
            start = len(previous) - last + 1
        lines = []
        for index, entry in enumerate(previous[start:]):
            number = offset + start + index + 1
            lines.append(Line("    %d  %s" % (number, entry),
                              [r"^\s*%d\s+%s$" % (number, re.escape(entry)),
                               r"^\s*%d\s+%s$" % (number - 1, re.escape(entry))]))
        number = offset + len(previous) + 1
        lines.append(Line("    %d  %s" % (number, command),
                          [r"^\s*%d\s+%s$" % (number, re.escape(command)),
                           r"^\s*%d\s+%s$" % (number - 1, re.escape(command))]))
        self.expect_rows(lines, prompt=True)

    @staticmethod
    def history_file_has(path, commands):
        try:
            with open(path) as handle:
                content = handle.read()
        except OSError as error:
            raise StageFailure("failed to read %s: %s" % (path, error))
        if not content.endswith("\n"):
            raise StageFailure("file %s has no trailing newline: %r" % (path, content))
        lines = content.split("\n")[:-1]
        if lines != commands:
            raise StageFailure("history file %s\n  expected: %r\n  found:    %r" % (path, commands, lines))

    def declare_assign(self, variable, value):
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", variable):
            self.command_no_response("declare %s=%s" % (variable, value))
            return
        assignment = "%s=%s" % (variable, value)
        patterns = [r"^bash: declare: `%s': not a valid identifier$" % re.escape(assignment),
                    r"^declare: not an identifier: %s$" % re.escape(variable),
                    r"^declare: not valid in this context: %s$" % re.escape(variable)]
        self.command_response("declare " + assignment, "declare: `%s': not a valid identifier" % assignment,
                              patterns)

    def declare_print(self, variable, value):
        self.command_response("declare -p " + variable, 'declare -- %s="%s"' % (variable, value),
                              [r"^typeset %s=%s$" % (re.escape(variable), re.escape(value))])

    def declare_print_missing(self, variable):
        patterns = [r"^bash: declare: %s: not found$" % re.escape(variable),
                    r"^declare: no such variable: %s$" % re.escape(variable)]
        self.command_response("declare -p " + variable, "declare: %s: not found" % variable, patterns)
