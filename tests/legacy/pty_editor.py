"""Tests of the line editor and TAB completion through a pseudo terminal (POSIX only).

Piped input never reaches the editor (the shell falls back to plain fgets), so these cases start
the shell on a pty and type into it. Run as: python3 -I pty_editor.py <path to the shell>
The exit status is 0 when every case passed.
"""

import os
import pty
import select
import shutil
import sys
import tempfile
import time

BELL = b"\x07"
CTRL_U = "\x15"
CTRL_C = "\x03"
UP = "\x1b[A"


class Session:
    """The shell on a pseudo terminal, started in its own working directory."""

    def __init__(self, shell, cwd):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(cwd)
            os.execv(shell, [shell])
        self.read_for(0.4)  # The first prompt

    def read_for(self, seconds):
        out = b""
        end = time.time() + seconds
        while time.time() < end:
            ready, _, _ = select.select([self.fd], [], [], 0.05)
            if ready:
                try:
                    data = os.read(self.fd, 4096)
                except OSError:
                    break
                if not data:
                    break
                out += data
        return out

    def type(self, text, wait=0.35):
        os.write(self.fd, text.encode())
        return self.read_for(wait)

    def close(self):
        try:
            os.write(self.fd, b"\x15exit\n")
            self.read_for(0.2)
        except OSError:
            pass
        _, status = os.waitpid(self.pid, 0)
        return status


failures = []


def check(name, condition, detail=""):
    print(("ok    " if condition else "FAIL  ") + name + ("" if condition else "  " + detail))
    if not condition:
        failures.append(name)


def main():
    shell = os.path.abspath(sys.argv[1])
    work = tempfile.mkdtemp(prefix="pty-editor-")
    try:
        os.mkdir(os.path.join(work, "subdir"))
        for name in ("subdir/ab1", "subdir/ab2", "alpha_one.txt", "alpha_two.txt", "only_file.txt"):
            open(os.path.join(work, name), "w").close()

        # A completer for the `git` command: prints candidates starting with the word, and logs its arguments
        completer = os.path.join(work, "completer.sh")
        with open(completer, "w") as f:
            f.write("#!/bin/sh\n"
                    'echo "$1|$2|$3|$COMP_LINE|$COMP_POINT" >> "$(dirname "$0")/completer.log"\n'
                    "printf 'checkout\\ncherry-pick\\ncommit\\nstatus\\n' | grep \"^$2\"\n")
        os.chmod(completer, 0o755)

        s = Session(shell, work)

        out = s.type("ech\t")
        check("command name is completed", b"echo " in out, repr(out))
        s.type(CTRL_U)

        out = s.type("ls only_f\t")
        check("a single file is completed with a space", b"only_file.txt " in out, repr(out))
        s.type(CTRL_U)

        out = s.type("ls alpha_\t") + s.type("\t")
        check("an ambiguous word rings the bell, then lists", BELL in out and b"alpha_one.txt" in out
              and b"alpha_two.txt" in out, repr(out))
        s.type(CTRL_U)

        out = s.type("ls subdir/ab\t") + s.type("\t")
        check("the list shows the last path component", b"ab1  ab2" in out and b"subdir/ab1  " not in out,
              repr(out))
        s.type(CTRL_U)

        out = s.type("ls sub\t")
        check("a directory is completed with a slash and no space", b"subdir/" in out and b"subdir/ " not in out,
              repr(out))
        s.type(CTRL_U)

        out = s.type("echo typed")
        out += s.type(CTRL_C)
        check("Ctrl-C cancels the line", b"^C" in out, repr(out))

        s.type("echo first\n")
        s.type("echo second\n")
        out = s.type(UP)
        check("Up recalls the previous line", b"echo second" in out, repr(out))
        out = s.type(UP)
        check("Up again recalls the one before", b"echo first" in out, repr(out))
        s.type(CTRL_U)

        # The external completer
        s.type("complete -C %s git\n" % completer)

        out = s.type("git ch\t")
        check("a completer is asked and the common prefix is used", b"git che" in out, repr(out))
        s.type(CTRL_U)

        out = s.type("git s\t")
        check("a single candidate is completed with a space", b"git status " in out, repr(out))
        s.type(CTRL_U)

        out = s.type("git z\t")
        check("no candidates rings the bell and changes nothing", BELL in out and b"git zz" not in out,
              repr(out))
        s.type(CTRL_U)

        out = s.type("git \t") + s.type("\t")
        check("empty word: bell, then the sorted list",
              BELL in out and b"checkout  cherry-pick  commit  status" in out, repr(out))
        s.type(CTRL_U)

        log_path = os.path.join(work, "completer.log")
        log = open(log_path).read() if os.path.exists(log_path) else ""
        check("the completer gets command, word, previous word and COMP_LINE/COMP_POINT",
              "git|ch|git|git ch|6" in log and "git|s|git|git s|5" in log, repr(log))

        s.type("complete -r git\n")
        out = s.type("git s\t")
        check("after complete -r the completer is no longer used", b"git status" not in out, repr(out))
        s.type(CTRL_U)

        status = s.close()
        check("the shell exits cleanly", os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0,
              "status %r" % status)
    finally:
        shutil.rmtree(work, ignore_errors=True)

    if failures:
        print("%d failed: %s" % (len(failures), ", ".join(failures)))
        return 1
    print("all passed")
    return 0


sys.exit(main())
