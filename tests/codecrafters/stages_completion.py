"""Autocompletion, filename completion and programmable completion (stage_a*.go, stage_fa_*.go,
stage_pa*.go)."""

import os
import re

import tools
from registry import stage


def _options_patterns(names):
    return ["^" + r"\s+".join(re.escape(n) for n in names) + "$"]


def _file_command(t):
    return t.rng.choice(["cat", "du", "stat", "wc"])


def _file_and_dir_command(t):
    return t.rng.choice(["du", "stat"])


def _dir_command(t):
    return t.rng.choice(["du", "ls", "stat", "tree"])


# ---- autocompletion of command names --------------------------------------------------

@stage("qp2", "Builtin completion")
def builtin_completion(t):
    t.start()
    for typed, completion in (("ech", "echo "), ("exi", "exit ")):
        t.autocomplete(typed, completion)
        t.restart()


@stage("gm9", "Complete and run")
def complete_and_run(t):
    t.start()
    for args, response in ((["hello"], "hello"), (["foo", "bar"], "foo bar")):
        t.autocomplete_and_run("ech", "echo", args, response)
        t.restart()


@stage("qm8", "Missing completions")
def missing_completions(t):
    t.start()
    t.autocomplete("xyz", "xyz", bell=True)


@stage("gy5", "Executable completion")
def executable_completion(t):
    name = "custom_exe_%d" % t.int_between(1000, 9999)
    t.setup_commands([("signature_printer", name, t.secret())])
    t.start()
    t.autocomplete("custom", name + " ")


@stage("wh6", "Multiple completions")
def multiple_completions(t):
    prefix = "xyz_"
    names = sorted(prefix + word for word in t.small_words(3))
    t.setup_commands([("signature_printer", name, t.secret()) for name in names], short=True)
    t.start()
    t.multiple_completions(prefix, "  ".join(names), _options_patterns(names))


@stage("wt6", "Partial completions")
def partial_completions(t):
    prefix = "xyz_"
    initial = prefix
    names = []
    for word in t.small_words(3):
        name = prefix + word
        prefix = name + "_"
        names.append(name)
    t.setup_commands([("signature_printer", name, t.secret()) for name in names], short=True)
    t.start()
    pairs = []
    for index, name in enumerate(names):
        expected = name + (" " if index == len(names) - 1 else "")
        pairs.append((initial if index == 0 else "_", expected))
    t.partial_completions(pairs)


# ---- filename completion --------------------------------------------------------------

def _work_dir(t):
    t.cwd = t.random_dir()
    return t.cwd


@stage("zv2", "File completion")
def file_completion(t):
    directory = _work_dir(t)
    base_name = "%s-%d.txt" % (t.words(1)[0], t.int_between(1, 100))
    t.write_files([os.path.join(directory, base_name)], [""])
    command = _file_command(t)
    t.start()
    t.autocomplete("%s %s" % (command, base_name[: len(base_name) // 2]), "%s %s " % (command, base_name))


@stage("ue6", "Nested file completion")
def nested_file_completion(t):
    directory = _work_dir(t)
    names = t.words(3)
    relative = "%s/%s/%s.txt" % tuple(names)
    t.write_files([os.path.join(directory, relative)], [""])
    command = _file_command(t)
    t.start()
    t.autocomplete("%s %s/" % (command, os.path.dirname(relative)), "%s %s " % (command, relative))


@stage("lc6", "Directory completion")
def directory_completion(t):
    directory = _work_dir(t)
    names = t.small_words(2)
    os.makedirs(os.path.join(directory, *names))
    command = _dir_command(t)
    t.start()
    t.partial_completions([("%s " % command, "%s %s/" % (command, names[0])),
                           ("", "%s %s/%s/" % (command, names[0], names[1]))])


@stage("vs5", "Missing file completion")
def missing_file_completion(t):
    _work_dir(t)
    command = _file_command(t)
    typed = "%s missing_%d" % (command, t.int_between(1, 1000))
    t.start()
    t.autocomplete(typed, typed, bell=True)


@stage("no5", "Multiple matches with files and directories")
def files_and_directories(t):
    directory = _work_dir(t)
    prefix = "%s_" % t.rng.choice(["ant", "bee", "cow", "dog", "fox", "owl", "pig", "rat"])
    file_suffix = t.int_between(1, 10)
    dir_suffix = t.int_between(1, 10)
    while dir_suffix == file_suffix:
        dir_suffix = t.int_between(1, 10)
    file_name = "%s%d.txt" % (prefix, file_suffix)
    dir_name = "%s%d" % (prefix, dir_suffix)
    t.write_files([os.path.join(directory, file_name)], [""])
    os.makedirs(os.path.join(directory, dir_name))
    options = sorted([file_name, dir_name + "/"])
    command = _file_and_dir_command(t)
    t.start()
    typed = "%s %s" % (command, prefix)
    t.multiple_completions(typed, "  ".join(options), _options_patterns(options))
    if t.rng.randrange(2) == 0:
        t.autocomplete(str(file_suffix), "%s %s " % (command, file_name), previous=typed)
    else:
        t.autocomplete(str(dir_suffix), "%s %s/" % (command, dir_name), previous=typed)


@stage("jp8", "Longest common prefix in file names")
def common_prefix_files(t):
    directory = _work_dir(t)
    prefix = "xyz_"
    initial = prefix
    names = []
    for word in t.small_words(2):
        name = prefix + word
        prefix = name + "_"
        os.makedirs(os.path.join(directory, name))
        names.append(name)
    file_name = "%s%s.txt" % (prefix, t.small_words(3)[2])
    t.write_files([os.path.join(directory, file_name)], [""])
    names.append(file_name)
    command = _file_and_dir_command(t)
    t.start()
    pairs = []
    for index, name in enumerate(names):
        expected = "%s %s%s" % (command, name, " " if index == len(names) - 1 else "")
        pairs.append(("%s %s" % (command, initial) if index == 0 else "_", expected))
    t.partial_completions(pairs)


@stage("bf8", "Completion of several arguments")
def several_arguments(t):
    directory = _work_dir(t)
    prefix = "%s_" % t.small_words(1)[0]
    file_suffix, dir_suffix = t.ints(1, 10, 2)
    file_name = "%s%d.txt" % (prefix, file_suffix)
    dir_name = "%s%d" % (prefix, dir_suffix)
    t.write_files([os.path.join(directory, file_name)], [""])
    os.makedirs(os.path.join(directory, dir_name))
    options = sorted([file_name, dir_name + "/"])
    command = _file_and_dir_command(t)
    t.start()
    typed = "%s %s" % (command, prefix)
    t.multiple_completions(typed, "  ".join(options), _options_patterns(options))
    after_file = "%s %s " % (command, file_name)
    t.autocomplete(str(file_suffix), after_file, previous=typed)
    after_dir = "%s %s %s/" % (command, file_name, dir_name)
    t.autocomplete(dir_name, after_dir, previous=after_file)
    missing = " missing_entry-%d" % t.int_between(1, 1000)
    t.autocomplete(missing, after_dir + missing, previous=after_dir, bell=True)


# ---- programmable completion ----------------------------------------------------------

def _completer(t, directory, name, output, args=None, env=None, secret=None):
    path = os.path.join(directory, name)
    config = {"OutputLines": output}
    if args is not None:
        config["ExpectedArguments"] = {"Argv1": args[0], "Argv2": args[1], "Argv3": args[2]}
    if env is not None:
        config["ExpectedEnvVars"] = {"CompLine": env[0], "CompPoint": env[1]}
    tools.write_completer(path, config)
    return path


def _report(t, path):
    report = tools.completer_report(path)
    if report:
        print("completer reported:\n" + report)


@stage("ne7", "The complete builtin")
def complete_builtin(t):
    t.start()
    t.type_builtin("complete")


@stage("oi7", "complete -p without a specification")
def complete_print_missing(t):
    command = t.rng.choice(["git", "docker", "systemctl"])
    t.start()
    t.command_response("complete -p " + command, "complete: %s: no completion specification" % command,
                       [r"^bash: complete: %s: no completion specification$" % re.escape(command)])


@stage("wl6", "complete -C and complete -p")
def complete_register(t):
    git, docker = (os.path.join(t.base, "%s.py" % word) for word in t.words(2))
    t.start()
    t.command_no_response("complete  -C  %s  git" % git)
    t.command_no_response("complete  -C  %s  docker" % docker)
    t.command_response("complete -p git", "complete -C '%s' git" % git)
    t.command_response("complete -p docker", "complete -C '%s' docker" % docker)


@stage("pm5", "Run a completer")
def run_completer(t):
    directory = t.short_dir()
    command, completions = t.rng.choice([("git", ["clone", "add", "commit", "push"]),
                                         ("docker", ["run", "ps", "exec", "build"])])
    completion = t.rng.choice(completions)
    path = _completer(t, directory, "singleCompleter", [completion])
    t.start()
    t.command_no_response("complete -C %s %s" % (path, command))
    t.autocomplete(command + " ", "%s %s " % (command, completion))


@stage("qf1", "A completer without candidates")
def no_candidates(t):
    directory = t.short_dir()
    path = _completer(t, directory, "noCandidatesCompleter", [])
    command = t.rng.choice(["docker", "git", "systemctl"])
    t.start()
    t.command_no_response("complete -C %s %s" % (path, command))
    t.autocomplete(command + " xyz", command + " xyz", bell=True)


def _sub_command_stage(t, sub_command, choices, with_environment):
    directory = t.short_dir()
    partial, complete = t.rng.choice(choices)
    command = "git"
    line = "%s %s %s" % (command, sub_command, partial)
    path = _completer(t, directory, "git%sCompleter" % sub_command.capitalize(), [complete],
                      args=(command, partial, sub_command),
                      env=(line, str(len(line))) if with_environment else None)
    t.start()
    t.command_no_response("complete -C %s %s" % (path, command))
    t.autocomplete(line, "%s %s %s " % (command, sub_command, complete))
    _report(t, path)


@stage("zi0", "Completer arguments")
def completer_arguments(t):
    _sub_command_stage(t, "remote", [("set", "set-url"), ("get", "get-url"), ("ad", "add"), ("ren", "rename")], False)


@stage("nr7", "Completer environment")
def completer_environment(t):
    _sub_command_stage(t, "stash", [("pu", "push"), ("po", "pop"), ("lis", "list"), ("cle", "clear")], True)


@stage("ep2", "Several candidates from a completer")
def several_candidates(t):
    directory = t.short_dir()
    command = "git"
    partial, completions = t.rng.choice([("che", ["checkout", "cherry-pick"]), ("re", ["reset", "remote", "rebase"]),
                                         ("pu", ["push", "pull"]), ("sta", ["stash", "status"])])
    completions = sorted(completions)
    line = "%s %s" % (command, partial)
    path = _completer(t, directory, "multiCandidateEnvCompleter", completions,
                      args=(command, partial, command), env=(line, str(len(line))))
    t.start()
    t.command_no_response("complete -C %s %s" % (path, command))
    t.multiple_completions(line, "  ".join(completions), _options_patterns(completions))
    _report(t, path)


@stage("xz3", "Longest common prefix from a completer")
def completer_common_prefix(t):
    directory = t.short_dir()
    command = "git"
    branch_letter, branch_name = t.rng.choice([("c", "checkout"), ("r", "cherry-pick")])
    ambiguous = ["checkout", "cherry-pick"]
    path = _completer(t, directory, "lcpEnvCompleter", ambiguous,
                      args=(command, "c", command), env=("git c", str(len("git c"))))
    t.start()
    t.command_no_response("complete -C %s %s" % (path, command))

    def reconfigure(index):
        if index == 0:
            word = "che" + branch_letter
            line = "%s %s" % (command, word)
            _completer(t, directory, "lcpEnvCompleter", [branch_name],
                       args=(command, word, command), env=(line, str(len(line))))

    t.partial_completions([("%s c" % command, "%s che" % command),
                           (branch_letter, "%s %s " % (command, branch_name))], after_each=reconfigure)
    _report(t, path)


@stage("tz2", "Unregister a completer")
def unregister(t):
    directory = t.short_dir()
    command = "git"
    path = _completer(t, directory, "singleCompleter", [t.rng.choice(["clone", "add", "commit", "push"])],
                      args=(command, "", command), env=("git ", "4"))
    # With the completer gone the word is completed as a file name. The tester runs in a
    # directory with many entries, so TAB is ambiguous: bell and an unchanged line. Do the same
    t.write_files([os.path.join(t.cwd, name) for name in ("one.txt", "two.txt")], ["", ""])
    t.start()
    t.command_no_response("complete  -C  %s  %s" % (path, command))
    t.command_no_response("complete -r " + command)
    t.command_response("complete -p " + command, "complete: %s: no completion specification" % command,
                       [r"^bash: complete: %s: no completion specification$" % re.escape(command)])
    # The directory must have several entries, or the shell would complete a file name uniquely
    t.autocomplete(command + " ", command + " ", bell=True)
