"""Base stages, navigation, quoting and redirection. Slugs and scenarios follow the tester's
stage files (stage1.go ... stage12.go, stage_q*.go, stage_r*.go)."""

import os

from harness import LARGE_WORDS, Line
from registry import stage


def invalid_names(t, count):
    return ["invalid_%s_command" % word for word in t.words(count)]


# ---- base stages ----------------------------------------------------------------------

@stage("oo8", "Print a prompt")
def prompt(t):
    # Stage 1 sets HOME to a random directory so that a shell that depends on it fails early
    t.env["HOME"] = t.random_dir()
    t.start()


@stage("cz2", "Handle invalid commands")
def invalid_command(t):
    t.start()
    t.invalid_command(invalid_names(t, 1)[0], prompt=False)


@stage("ff0", "Implement a REPL")
def repl(t):
    t.start()
    for index in range(t.int_between(3, 6)):
        t.invalid_command("invalid_command_%d" % (index + 1))


@stage("pn5", "Implement exit")
def exit_builtin(t):
    t.start()
    t.invalid_command(invalid_names(t, 1)[0])
    t.exit_command(allowed=(0, 127))


@stage("iz3", "Implement echo")
def echo(t):
    t.start()
    for _ in range(t.int_between(2, 4)):
        words = " ".join(t.words(t.int_between(2, 4)))
        t.command_response("echo " + words, words)


@stage("ez5", "Implement type: builtins")
def type_builtins(t):
    t.start()
    for builtin in ("echo", "exit", "type"):
        t.type_builtin(builtin)
    for command in invalid_names(t, 2):
        t.type_invalid(command)


def _make_not_executable(t, name):
    directory = t.setup_commands([("signature_printer", name, t.secret())])
    path = os.path.join(directory, name)
    os.chmod(path, os.stat(path).st_mode & ~0o111)
    return path


@stage("mg5", "Implement type: executables")
def type_executables(t):
    name = "my_exe"
    # PATH becomes dir1:dir2:dir3. Only the middle one is executable: a shell that does not check
    # permissions finds the first, one that walks PATH backwards finds the last
    third = _make_not_executable(t, name)
    directory = t.setup_commands([("signature_printer", name, t.secret())])
    executable = os.path.join(directory, name)
    first = _make_not_executable(t, name)
    assert t.env["PATH"].split(":")[0] == os.path.dirname(first)
    assert third != executable != first

    t.start()
    for command in ("cat", "cp", "mkdir", name):
        t.type_executable(command, executable if command == name else t.which_original(command))
    for command in invalid_names(t, 2):
        t.type_invalid(command)


@stage("ip1", "Run a program")
def run_program(t):
    codes = [t.secret(), t.secret()]
    names = ["custom_exe_%d" % n for n in t.ints(1000, 9999, 2)]
    t.setup_commands([("signature_printer", names[0], codes[0]), ("signature_printer", names[1], codes[1])])
    t.start()
    for index, count in enumerate(t.ints(1, 4, 2)):
        command = [names[index]] + [t.name() for _ in range(count)]
        expected = ["Program was passed %d args (including program name)." % len(command),
                    "Arg #0 (program name): %s" % command[0]]
        expected += ["Arg #%d: %s" % (position, arg) for position, arg in enumerate(command[1:], 1)]
        expected.append("Program Signature: %s" % codes[index])
        t.command_multiline(" ".join(command), expected)


# ---- navigation -----------------------------------------------------------------------

@stage("ei0", "The pwd builtin")
def pwd(t):
    # The tester renames the system pwd so that only a builtin works. Here a decoy first on PATH
    # prints something else, which has the same effect without touching /bin
    decoy_dir = t.short_dir()
    decoy = os.path.join(decoy_dir, "pwd")
    with open(decoy, "w") as handle:
        handle.write("#!/bin/sh\necho decoy pwd, not the builtin\n")
    os.chmod(decoy, 0o755)
    t.add_to_path(decoy_dir)

    t.start()
    t.type_builtin("pwd")
    t.command_response("pwd", t.cwd)


@stage("ra6", "The cd builtin: absolute paths")
def cd_absolute(t):
    t.start()
    directory = t.random_dir()
    t.cd_and_pwd(directory, directory)
    missing = "/non-existing-directory"
    patterns = [r"^(can't cd to %s|((bash: )?cd: )?%s: No such file or directory)$" % (missing, missing),
                r"^ash: cd: can't cd to %s: No such file or directory$" % missing,
                r"^cd: no such file or directory: %s$" % missing,
                r"^dash: [0-9]+: cd: can't cd to %s$" % missing]
    t.command_response("cd " + missing, "cd: %s: No such file or directory" % missing, patterns)


@stage("gq9", "The cd builtin: relative paths")
def cd_relative(t):
    t.start()
    directory = t.random_dir()
    parts = directory.split("/")
    first = "/" + os.path.join(*parts[:-2])
    t.cd_and_pwd(first, first)
    t.cd_and_pwd("./" + "/".join(parts[-2:]), directory)
    t.cd_and_pwd("../../../", "/" + os.path.join(*parts[:-3]))


@stage("gp4", "The cd builtin: home directory")
def cd_home(t):
    home = t.random_dir()
    t.env["HOME"] = home
    directory = t.random_dir()
    t.start()
    t.cd_and_pwd(directory, directory)
    t.cd_and_pwd("~", home)


# ---- quoting --------------------------------------------------------------------------

def _file_contents(t):
    return ["%s." % " ".join(t.words(2)), "%s." % " ".join(t.words(2)), "%s.\n" % " ".join(t.words(2))]


def _cat_output(contents):
    return contents[0] + contents[1] + contents[2].rstrip("\n")


@stage("ni6", "Single quotes")
def single_quotes(t):
    t.setup_commands([("cat", "cat", "")], short=False)
    directory = t.short_dir()
    numbers = t.ints(1, 100, 3)
    paths = [os.path.join(directory, "f   %d" % n) for n in numbers]
    contents = _file_contents(t)
    t.start()
    t.write_files(paths, contents)
    pick = t.rng.sample(LARGE_WORDS, 6)
    t.command_response("echo '%s %s'" % (pick[0], pick[1]), "%s %s" % (pick[0], pick[1]))
    t.command_response("echo %s     %s" % (pick[1], pick[4]), "%s %s" % (pick[1], pick[4]))
    t.command_response("echo '%s     %s' '%s''%s' %s''%s" % (pick[2], pick[3], pick[4], pick[0], pick[1], pick[5]),
                       "%s     %s %s%s %s%s" % (pick[2], pick[3], pick[4], pick[0], pick[1], pick[5]))
    t.command_response("cat '%s' '%s' '%s'" % tuple(paths), _cat_output(contents))


@stage("tg6", "Double quotes")
def double_quotes(t):
    t.setup_commands([("cat", "cat", "")], short=False)
    directory = t.short_dir()
    numbers = t.ints(1, 100, 3)
    paths = [os.path.join(directory, "f %d" % numbers[0]), os.path.join(directory, "f   %d" % numbers[1]),
             os.path.join(directory, "f's%d" % numbers[2])]
    contents = _file_contents(t)
    t.start()
    pick = t.rng.sample(LARGE_WORDS, 5)
    t.command_response('echo "%s %s"' % (pick[0], pick[1]), "%s %s" % (pick[0], pick[1]))
    t.command_response('echo "%s  %s"  "%s""%s"' % (pick[1], pick[2], pick[3], pick[0]),
                       "%s  %s %s%s" % (pick[1], pick[2], pick[3], pick[0]))
    t.command_response('echo "%s"  "%s\'s"  %s""%s' % (pick[3], pick[4], pick[1], pick[2]),
                       "%s %s's %s%s" % (pick[3], pick[4], pick[1], pick[2]))
    t.write_files(paths, contents)
    t.command_response('cat "%s" "%s" "%s"' % tuple(paths), _cat_output(contents))


@stage("yt5", "Backslash outside quotes")
def backslash_outside(t):
    t.setup_commands([("cat", "cat", "")], short=False)
    directory = t.short_dir()
    numbers = t.ints(1, 100, 3)
    actual = [os.path.join(directory, "_ignored_%d" % numbers[0]), os.path.join(directory, "ignore_%d" % numbers[1]),
              os.path.join(directory, "just_one_\\_%d" % numbers[2])]
    typed = [os.path.join(directory, "\\_ignored_%d" % numbers[0]), os.path.join(directory, "ignore_\\%d" % numbers[1]),
             os.path.join(directory, "just_one_\\\\_%d" % numbers[2])]
    contents = _file_contents(t)
    t.start()
    t.write_files(actual, contents)
    pick = t.rng.sample(LARGE_WORDS, 5)
    t.command_response("echo %s\\ \\ \\ \\ \\ \\ %s" % (pick[0], pick[1]), "%s      %s" % (pick[0], pick[1]))
    t.command_response("echo \\'\\\"%s %s\\\"\\'" % (pick[1], pick[2]), "'\"%s %s\"'" % (pick[1], pick[2]))
    t.command_response("echo %s\\n%s" % (pick[2], pick[3]), "%sn%s" % (pick[2], pick[3]))
    t.command_response("cat %s %s %s" % tuple(typed), _cat_output(contents))


@stage("le5", "Backslash inside single quotes")
def backslash_in_single(t):
    t.setup_commands([("cat", "cat", "")], short=False)
    directory = t.short_dir()
    numbers = t.ints(1, 100, 3)
    pick = t.rng.sample(LARGE_WORDS, 6)
    actual = [os.path.join(directory, "no slash %d" % numbers[0]), os.path.join(directory, "one slash \\%d" % numbers[1]),
              os.path.join(directory, "two slashes \\%d\\" % numbers[2])]
    typed = ["'%s'" % p for p in
             (os.path.join(directory, "no slash %d" % numbers[0]), os.path.join(directory, "one slash \\%d" % numbers[1]),
              os.path.join(directory, "two slashes \\%d\\" % numbers[2]))]
    contents = _file_contents(t)
    t.start()
    t.write_files(actual, contents)
    t.command_response("echo '%s\\\\n%s'" % (pick[0], pick[1]), "%s\\\\n%s" % (pick[0], pick[1]))
    t.command_response("echo '%s\\\"%s%s\\\"%s'" % (pick[2], pick[3], pick[4], pick[0]),
                       "%s\\\"%s%s\\\"%s" % (pick[2], pick[3], pick[4], pick[0]))
    t.command_response("echo '%s\\\\n%s'" % (pick[4], pick[1]), "%s\\\\n%s" % (pick[4], pick[1]))
    t.command_response("cat %s %s %s" % tuple(typed), _cat_output(contents))


@stage("gu3", "Backslash inside double quotes")
def backslash_in_double(t):
    t.setup_commands([("cat", "cat", "")], short=False)
    directory = t.short_dir()
    numbers = t.ints(1, 100, 3)
    actual = [os.path.join(directory, "number %d" % numbers[0]), os.path.join(directory, 'doublequote " %d' % numbers[1]),
              os.path.join(directory, "backslash \\ %d" % numbers[2])]
    typed = ['"%s"' % os.path.join(directory, "number %d" % numbers[0]),
             '"%s"' % os.path.join(directory, 'doublequote \\" %d' % numbers[1]),
             '"%s"' % os.path.join(directory, "backslash \\\\ %d" % numbers[2])]
    contents = _file_contents(t)
    t.start()
    t.write_files(actual, contents)
    pick = t.rng.sample(LARGE_WORDS, 5)
    t.command_response("echo \"%s'%s'\\\\'%s\"" % (pick[0], pick[1], pick[2]), "%s'%s'\\'%s" % (pick[0], pick[1], pick[2]))
    t.command_response("echo \"%s\\\"insidequotes\"%s\\\"" % (pick[0], pick[1]), "%s\"insidequotes%s\"" % (pick[0], pick[1]))
    t.command_response("echo \"mixed\\\"quote'%s'\\\\\"" % pick[4], "mixed\"quote'%s'\\" % pick[4])
    t.command_response("cat %s %s %s" % tuple(typed), _cat_output(contents))


@stage("qj0", "Executing a quoted executable")
def quoted_executable(t):
    # The tester builds the files through `sh -c "cp ... <name>"`, so the quotes in these names are
    # removed there: the files are called exe  with  space, exe with "quotes", and so on
    typed = ["'exe  with  space'", "'exe with \"quotes\"'", "\"exe with 'single quotes'\"", "\"exe with \\\\ backslash\""]
    actual = ["exe  with  space", 'exe with "quotes"', "exe with 'single quotes'", "exe with \\ backslash"]
    directory = t.setup_commands([("cat", "cat", "")] + [("cat", name, "") for name in actual])
    contents = ["%s." % " ".join(t.words(2)) for _ in range(4)]
    t.write_files([os.path.join(directory, "f%d" % (i + 1)) for i in range(4)], [c + "\n" for c in contents])
    t.start()
    for index in range(4):
        t.command_response("%s %s/f%d" % (typed[index], directory, index + 1), contents[index])


# ---- redirection ----------------------------------------------------------------------

def _redirection_setup(t, directories=2):
    t.setup_commands([("ls", "ls", ""), ("cat", "cat", "")], short=False)
    return [t.short_dir() for _ in range(directories)]


@stage("jv1", "Redirect standard output")
def redirect_stdout(t):
    stage_dir, ls_dir = _redirection_setup(t)
    words = sorted(t.words(3))
    paths = [os.path.join(ls_dir, word) for word in words]
    t.write_files(paths, [word + "\n" for word in words])
    out = [os.path.join(stage_dir, "%s.md" % word) for word in sorted(t.small_words(3))]
    t.start()

    t.command_no_response("ls -1 %s > %s" % (ls_dir, out[0]))
    t.command_multiline("cat " + out[0], words)

    message = "Hello " + t.name()
    t.command_no_response("echo %s 1> %s" % (message, out[1]))
    t.command_response("cat " + out[1], message)

    t.command_response("cat %s nonexistent 1> %s" % (paths[1], out[2]), "cat: nonexistent: No such file or directory")
    t.command_response("cat " + out[2], words[1])


def _expect_empty_file(t, path):
    if not t.session.wait_until(lambda: os.path.exists(path) and os.path.getsize(path) == 0, 2.0):
        t.fail("Expected %s to exist and be empty" % path)


@stage("vz4", "Redirect standard error")
def redirect_stderr(t):
    stage_dir, ls_dir = _redirection_setup(t)
    word = t.words(1)[0]
    path = os.path.join(ls_dir, word)
    t.write_files([path], [word + "\n"])
    out = [os.path.join(stage_dir, "%s.md" % w) for w in sorted(t.small_words(3))]
    t.start()

    t.command_no_response("ls -1 nonexistent 2> " + out[0])
    t.command_response("cat " + out[0], "ls: nonexistent: No such file or directory")

    message = "%s file cannot be found" % t.name()
    t.command_response("echo %s 2> %s" % (message, out[1]), message)
    _expect_empty_file(t, out[1])

    t.command_response("cat %s nonexistent 2> %s" % (path, out[2]), word)
    t.command_response("cat " + out[2], "cat: nonexistent: No such file or directory")


@stage("el9", "Append standard output")
def append_stdout(t):
    stage_dir, ls_dir = _redirection_setup(t)
    words = sorted(t.words(3))
    t.write_files([os.path.join(ls_dir, word) for word in words], [word + "\n" for word in words])
    out = [os.path.join(stage_dir, "%s.md" % w) for w in sorted(t.small_words(3))]
    t.start()

    t.command_no_response("ls -1 %s >> %s" % (ls_dir, out[0]))
    t.command_multiline("cat " + out[0], words)

    first, second = "Hello " + t.name(), "Hello " + t.name()
    t.command_no_response("echo %s 1>> %s" % (first, out[1]))
    t.command_no_response("echo %s 1>> %s" % (second, out[1]))
    t.command_multiline("cat " + out[1], [first, second])

    t.command_no_response("echo List of files: > " + out[2])
    t.command_no_response("ls -1 %s >> %s" % (ls_dir, out[2]))
    t.command_multiline("cat " + out[2], ["List of files:"] + words)


@stage("un3", "Append standard error")
def append_stderr(t):
    (stage_dir,) = _redirection_setup(t, 1)
    out = [os.path.join(stage_dir, "%s.md" % w) for w in sorted(t.small_words(3))]
    t.start()

    t.command_response("ls -1 nonexistent >> " + out[0], "ls: nonexistent: No such file or directory")
    _expect_empty_file(t, out[0])

    t.command_no_response("ls -1 nonexistent 2>> " + out[1])
    t.command_response("cat " + out[1], "ls: nonexistent: No such file or directory")

    message = "%s says Error" % t.name()
    t.command_response("echo %s 2>> %s" % (message, out[2]), message)
    t.command_no_response("cat nonexistent 2>> " + out[2])
    t.command_no_response("ls -1 nonexistent 2>> " + out[2])
    t.command_multiline("cat " + out[2], ["cat: nonexistent: No such file or directory",
                                           "ls: nonexistent: No such file or directory"])
