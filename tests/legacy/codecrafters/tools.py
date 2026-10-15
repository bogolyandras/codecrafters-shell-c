"""The programs CodeCrafters' tester puts on PATH, written again.

The tester copies small compiled programs next to the shell under test: cat, ls, wc, head, tail
and grep with fixed, simple behaviour (so that the output does not depend on the system's
coreutils), a "signature printer" that prints its arguments and a secret code, and a "completer"
that prints candidates after checking its arguments and environment.

Here the first group are Python scripts with a shebang line, and the signature printer is a C
program, because it must print the name it was started under (argv[0]) and a script cannot see
that: the kernel replaces it with the script path.
"""

import os
import shutil
import subprocess
import sys

SHEBANG = "#!%s\n" % sys.executable

_CAT = r'''
import os, shutil, sys
args = sys.argv[1:]
if not args:
    shutil.copyfileobj(sys.stdin.buffer, sys.stdout.buffer)
    sys.stdout.flush()
    sys.exit(0)
status = 0
for name in args:
    if not os.path.exists(name):
        sys.stderr.write("cat: %s: No such file or directory\n" % name)
        status = 1
        continue
    try:
        with open(name, "rb") as handle:
            while True:
                chunk = handle.read(65536)
                if not chunk:
                    break
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
    except OSError as error:
        sys.stderr.write("cat: %s: %s\n" % (name, error.strerror))
        status = 1
sys.exit(status)
'''

_LS = r'''
import os, sys
args = [a for a in sys.argv[1:] if a != "-1"]
if not args:
    args = ["."]
args.sort()
status = 0
existing = []
for name in args:
    if not os.path.exists(name):
        sys.stderr.write("ls: %s: No such file or directory\n" % name)
        status = 1
    else:
        existing.append(name)
for index, name in enumerate(existing):
    if len(existing) > 1:
        print("%s:" % name)
    for entry in sorted(os.listdir(name)):
        print(entry)
    if len(existing) > 1 and index < len(existing) - 1:
        print()
sys.exit(status)
'''

_WC = r'''
import sys
flags = [a for a in sys.argv[1:] if a.startswith("-") and a != "-"]
files = [a for a in sys.argv[1:] if not (a.startswith("-") and a != "-")]
lines = "-l" in flags or not flags
words = "-w" in flags or not flags
chars = "-c" in flags or not flags

def counts(data):
    return (data.count(b"\n"), len(data.split()), len(data))

def show(values, name):
    fields = []
    if lines: fields.append("%8d" % values[0])
    if words: fields.append("%8d" % values[1])
    if chars: fields.append("%8d" % values[2])
    text = "".join(fields)
    if name != "-":
        text = text + " " + name if text else name
    if text:
        print(text)

if not files:
    show(counts(sys.stdin.buffer.read()), "-")
    sys.exit(0)
status = 0
total = [0, 0, 0]
for name in files:
    try:
        data = open(name, "rb").read()
    except OSError:
        sys.stderr.write("wc: %s: open: No such file or directory\n" % name)
        status = 1
        continue
    values = counts(data)
    show(values, name)
    total = [a + b for a, b in zip(total, values)]
if len(files) > 1:
    show(total, "total")
sys.exit(status)
'''

_HEAD = r'''
import sys
count = 10
files = []
args = sys.argv[1:]
i = 0
while i < len(args):
    if args[i] == "-n" and i + 1 < len(args):
        count = int(args[i + 1]); i += 2
    elif args[i].startswith("-n"):
        count = int(args[i][2:]); i += 1
    else:
        files.append(args[i]); i += 1
stream = open(files[0], "rb") if files else sys.stdin.buffer
written = 0
while written < count:
    line = stream.readline()
    if not line:
        break
    sys.stdout.buffer.write(line)
    sys.stdout.buffer.flush()
    written += 1
'''

_TAIL = r'''
import os, select, sys, time
count = 10
follow = False
files = []
args = sys.argv[1:]
i = 0
while i < len(args):
    if args[i] == "-n" and i + 1 < len(args):
        count = int(args[i + 1]); i += 2
    elif args[i].startswith("-n"):
        count = int(args[i][2:]); i += 1
    elif args[i] == "-f":
        follow = True; i += 1
    else:
        files.append(args[i]); i += 1

def emit(data):
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()

if not files:
    data = sys.stdin.buffer.read().split(b"\n")
    if data and data[-1] == b"":
        data.pop()
    emit(b"".join(line + b"\n" for line in data[-count:]) if count else b"")
    sys.exit(0)

handle = open(files[0], "rb")
content = handle.read().split(b"\n")
if content and content[-1] == b"":
    content.pop()
emit(b"".join(line + b"\n" for line in content[-count:]) if count else b"")
if follow:
    poller = select.poll()
    poller.register(1, select.POLLOUT)
    while True:
        chunk = handle.read()
        if chunk:
            try:
                emit(chunk)
            except BrokenPipeError:
                break
        else:
            # A reader that has gone away shows as an error on the pipe: stop, like GNU tail
            events = poller.poll(0)
            if any(event & (select.POLLERR | select.POLLHUP) for _, event in events):
                break
            time.sleep(0.05)
'''

_GREP = r'''
import sys
if len(sys.argv) < 2:
    sys.stderr.write("Usage: grep PATTERN\n")
    sys.exit(2)
pattern = sys.argv[1]
found = False
for line in sys.stdin:
    if pattern in line.rstrip("\n"):
        sys.stdout.write(line if line.endswith("\n") else line + "\n")
        found = True
sys.exit(0 if found else 1)
'''

_COMPLETER = r'''
import json, os, sys
script = sys.argv[0]
with open(script + ".config.json") as handle:
    config = json.load(handle)

problems = []
expected = config.get("ExpectedArguments")
if expected is not None:
    if len(sys.argv) != 4:
        problems.append("Expected argv[1] through argv[3] only, found argv up to %d" % (len(sys.argv) - 1))
    else:
        for index, key in ((1, "Argv1"), (2, "Argv2"), (3, "Argv3")):
            if sys.argv[index] != expected[key]:
                problems.append("Expected argv[%d] to be %r, found %r" % (index, expected[key], sys.argv[index]))
environment = config.get("ExpectedEnvVars")
if environment is not None:
    for variable, key in (("COMP_LINE", "CompLine"), ("COMP_POINT", "CompPoint")):
        if os.environ.get(variable) != environment[key]:
            problems.append("Expected %s to be %r, found %r" % (variable, environment[key], os.environ.get(variable)))
if problems:
    with open(script + ".report.txt", "w") as report:
        report.write("\n".join(problems) + "\n")
    sys.exit(1)
for line in config.get("OutputLines", []):
    print(line)
'''

_SIGNATURE_PRINTER_C = r'''
#include <stdio.h>
static const char secret[] = "<<RANDOM>>";
int main(int argc, char **argv) {
    printf("Program was passed %d args (including program name).\n", argc);
    printf("Arg #0 (program name): %s\n", argv[0]);
    for (int i = 1; i < argc; i++) {
        printf("Arg #%d: %s\n", i, argv[i]);
    }
    printf("Program Signature: %s\n", secret);
    return 0;
}
'''

SCRIPTS = {"cat": _CAT, "ls": _LS, "wc": _WC, "head": _HEAD, "tail": _TAIL, "grep": _GREP}
PLACEHOLDER = b"<<RANDOM>>"


class ToolBuildError(RuntimeError):
    pass


def write_script(path, name):
    with open(path, "w") as handle:
        handle.write(SHEBANG + SCRIPTS[name])
    os.chmod(path, 0o755)


def write_completer(path, config):
    import json

    with open(path, "w") as handle:
        handle.write(SHEBANG + _COMPLETER)
    os.chmod(path, 0o755)
    with open(path + ".config.json", "w") as handle:
        json.dump(config, handle)
    report = path + ".report.txt"
    if os.path.exists(report):
        os.remove(report)


def completer_report(path):
    try:
        with open(path + ".report.txt") as handle:
            return handle.read()
    except OSError:
        return ""


_signature_printer_bytes = None  # The compiled program, kept in memory: stage directories are removed


def write_signature_printer(path, secret, cache_dir):
    """Compiles the printer once, then each executable is a copy with its secret patched in."""
    global _signature_printer_bytes
    if len(secret) != len(PLACEHOLDER):
        raise ToolBuildError("the secret must be %d characters" % len(PLACEHOLDER))

    if _signature_printer_bytes is None:
        compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            raise ToolBuildError("no C compiler found: needed for the signature printer")
        source = os.path.join(cache_dir, "signature_printer.c")
        binary = os.path.join(cache_dir, "signature_printer")
        with open(source, "w") as handle:
            handle.write(_SIGNATURE_PRINTER_C)
        result = subprocess.run([compiler, "-O1", "-o", binary, source], capture_output=True, text=True)
        if result.returncode != 0:
            raise ToolBuildError("compiling the signature printer failed:\n" + result.stderr)
        with open(binary, "rb") as handle:
            _signature_printer_bytes = handle.read()
        if PLACEHOLDER not in _signature_printer_bytes:
            raise ToolBuildError("the placeholder is not in the compiled signature printer")

    with open(path, "wb") as handle:
        handle.write(_signature_printer_bytes.replace(PLACEHOLDER, secret.encode()))
    os.chmod(path, 0o755)
