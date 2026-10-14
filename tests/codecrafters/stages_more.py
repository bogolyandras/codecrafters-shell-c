"""Background jobs, pipelines, history, history persistence and parameter expansion
(stage_bg*.go, stage_p*.go, stage_h*.go, stage_hp*.go, stage_pex*.go)."""

import os
import time

from harness import Line
from registry import stage

UP = "\x1b[A"
DOWN = "\x1b[B"


# ---- background jobs ------------------------------------------------------------------

@stage("af3", "The jobs builtin")
def jobs_builtin(t):
    t.start()
    t.type_builtin("jobs")
    t.jobs([])


@stage("at7", "Run in the background")
def background(t):
    t.start()
    t.background_command("sleep 500", 1)


@stage("si2", "Background output")
def background_output(t):
    fifo1, fifo2 = t.make_fifo(), t.make_fifo()
    t.start()
    t.background_command("cat " + fifo1, 1)
    t.command_no_response("cat " + fifo2, prompt=False)
    t.write_to_fifo(fifo1, "Hello from FIFO #1\n")
    t.expect_rows(["Hello from FIFO #1"], prompt=False)
    t.write_to_fifo(fifo2, "Hello from FIFO #2\n")
    t.expect_rows(["Hello from FIFO #2"], prompt=False)


@stage("jd6", "List a running job")
def list_job(t):
    t.start()
    t.background_command("sleep 100", 1)
    t.jobs([(1, "Running", "sleep 100", "current")])


@stage("dk5", "List several jobs")
def list_jobs(t):
    t.start()
    commands = ["sleep 100", "sleep 200", "sleep 300"]
    launched = []
    for number, command in enumerate(commands, 1):
        t.background_command(command, number)
        launched.append((number, command))
        entries = []
        for index, (job_number, job_command) in enumerate(launched):
            marker = "unmarked"
            if index == len(launched) - 1:
                marker = "current"
            elif index == len(launched) - 2:
                marker = "previous"
            entries.append((job_number, "Running", job_command, marker))
        t.jobs(entries)


@stage("ma9", "Report a finished job")
def finished_job(t):
    fifo = t.make_fifo()
    command = "cat " + fifo
    t.start()
    t.background_command(command, 1)
    t.jobs([(1, "Running", command, "current")])
    t.write_to_fifo(fifo, "")
    time.sleep(0.05)
    t.jobs([(1, "Done", command, "current")])
    t.jobs([])


@stage("rq2", "Several jobs and markers")
def job_markers(t):
    fifo1, fifo2 = t.make_fifo(), t.make_fifo()
    sleep = "sleep 500"
    cat1, cat2 = "cat " + fifo1, "cat " + fifo2
    t.start()
    t.background_command(sleep, 1)
    t.background_command(cat1, 2)
    t.background_command(cat2, 3)
    t.write_to_fifo(fifo1, "")
    time.sleep(0.05)
    t.jobs([(1, "Running", sleep, "unmarked"), (2, "Done", cat1, "previous"), (3, "Running", cat2, "current")])
    t.write_to_fifo(fifo2, "")
    time.sleep(0.05)
    t.jobs([(1, "Running", sleep, "previous"), (3, "Done", cat2, "current")])
    t.jobs([(1, "Running", sleep, "current")])


@stage("bv8", "Reap finished jobs")
def reap_jobs(t):
    fifo = t.make_fifo()
    sleep, cat = "sleep 500", "cat " + fifo
    t.start()
    t.background_command(sleep, 1)
    t.background_command(cat, 2)
    t.jobs([(1, "Running", sleep, "previous"), (2, "Running", cat, "current")])
    t.write_to_fifo(fifo, "")
    time.sleep(0.05)
    word = t.words(1)[0]
    t.command_with_reaped_jobs("echo " + word, word, [(2, "Done", cat, "current")])
    t.jobs([(1, "Running", sleep, "current")])


@stage("fy4", "Job numbers are recycled")
def recycle_numbers(t):
    # Part one: the numbers start again at 1 when no job is left
    fifo = t.make_fifo()
    t.start()
    cat = "cat " + fifo
    t.background_command(cat, 1)
    t.write_to_fifo(fifo, "")
    time.sleep(0.05)
    first, second = t.words(2)
    t.command_with_reaped_jobs("echo " + first, first, [(1, "Done", cat, "current")])
    t.command_response("echo " + second, second)
    t.jobs([])
    t.background_command("sleep 10", 1)
    t.jobs([(1, "Running", "sleep 10", "current")])

    # Part two: the lowest free number is not reused while a higher one is in use
    t.restart()
    fifo = t.make_fifo()
    sleep, cat = "sleep 100", "cat " + fifo
    t.background_command(sleep, 1)
    t.background_command(cat, 2)
    t.write_to_fifo(fifo, "")
    time.sleep(0.05)
    word = t.words(1)[0]
    t.command_with_reaped_jobs("echo " + word, word, [(2, "Done", cat, "current")])
    t.background_command("sleep 50", 2)
    t.jobs([(1, "Running", sleep, "previous"), (2, "Running", "sleep 50", "current")])


# ---- pipelines ------------------------------------------------------------------------

@stage("br6", "Two commands")
def pipeline_two(t):
    t.setup_commands([("cat", "cat", ""), ("head", "head", ""), ("tail", "tail", ""), ("wc", "wc", "")], short=False)
    directory = t.short_dir()
    path = os.path.join(directory, "file-%d" % t.int_between(1, 100))
    words = t.words(10)
    content = "".join("%s %s\n" % (words[i], words[i + 1]) for i in range(0, 10, 2))
    t.write_files([path], [content])
    t.start()
    lines, count_words, size = content.count("\n"), content.replace("\n", " ").count(" "), len(content)
    t.command_response("cat %s | wc" % path, "%8d%8d%8d" % (lines, count_words, size))

    # The shell has to stream: head sees the lines as they are appended
    directory = t.short_dir()
    path = os.path.join(directory, "file-%d" % t.int_between(100, 200))
    words = t.words(6)
    content = "1. %s %s\n2. %s %s\n3. %s %s\n" % tuple(words)
    t.write_files([path], [content])
    t.send_command("tail -f %s | head -n 5" % path)
    t.expect_rows(["$ tail -f %s | head -n 5" % path] + content.strip("\n").split("\n"), prompt=False)
    for number in (4, 5):
        extra = "%d. %s" % (number, " ".join(t.words(2)))
        with open(path, "a") as handle:
            handle.write(extra + "\n")
        t.expect_rows([extra], prompt=False)


@stage("ny9", "Builtins in pipelines")
def pipeline_builtin(t):
    t.setup_commands([("wc", "wc", "")], short=False)
    t.start()
    data = "%s-%s" % tuple(t.words(2))
    t.command_response("echo %s | wc" % data, "%8d%8d%8d" % (1, 1, len(data) + 1))
    t.command_response("ls | type exit", "exit is a shell builtin", [r"exit is a special shell builtin"])


@stage("xk3", "Multi-command pipelines")
def pipeline_three(t):
    t.setup_commands([("cat", "cat", ""), ("grep", "grep", ""), ("head", "head", ""), ("ls", "ls", ""),
                      ("tail", "tail", ""), ("wc", "wc", "")], short=False)
    directory = t.short_dir()
    path = os.path.join(directory, "file-%d" % t.int_between(1, 100))
    words = t.words(5)
    first_three = "%s\n%s\n%s\n" % tuple(words[:3])
    t.write_files([path], [first_three + "%s\n%s\n" % tuple(words[3:])])
    t.start()
    t.command_response("cat %s | head -n 3 | wc" % path, "%8d%8d%8d" % (3, 3, len(first_three)))

    directory = t.short_dir()
    numbers = t.ints(1, 100, 6)
    t.write_files([os.path.join(directory, "f-%d" % n) for n in numbers], t.words(6))
    ordered = sorted(numbers, key=str)
    wanted = ordered[1:4][2]
    t.command_response('ls %s | tail -n 5 | head -n 3 | grep "f-%d"' % (directory, wanted), "f-%d" % wanted,
                       [r"^f-%d$" % wanted])


# ---- history --------------------------------------------------------------------------

def _history_env(t):
    t.env["HISTFILE"] = "/dev/null"


@stage("bq4", "The history builtin")
def history_builtin(t):
    _history_env(t)
    t.start()
    t.type_builtin("history")


@stage("yf5", "List the history")
def history_list(t):
    _history_env(t)
    t.start()
    commands = []
    for _ in range(3):
        words = " ".join(t.words(2))
        t.command_response("echo " + words, words)
        commands.append("echo " + words)
    t.history(commands)


@stage("ag6", "Limit the history")
def history_limit(t):
    _history_env(t)
    t.start()
    commands = []
    for _ in range(3):
        words = " ".join(t.words(2))
        t.command_response("echo " + words, words)
        commands.append("echo " + words)
    t.history(commands, offset=0, last=2)
    more = []
    for _ in range(6):
        words = " ".join(t.words(2))
        t.command_response("echo " + words, words)
        more.append("echo " + words)
    t.history(more, offset=4, last=t.int_between(3, 5))


def _history_with_invalid(t):
    t.start()
    first, second, third = (" ".join(t.words(2)) for _ in range(3))
    invalid = "invalid_%s_command" % t.words(1)[0]
    t.command_response("echo " + first, first)
    t.command_response("echo " + second, second)
    t.invalid_command(invalid)
    t.command_response("echo " + third, third)
    return first, second, third, invalid


def _recall(t, expected):
    t.send_text(UP)
    t.expect_row_stay("$ " + expected)


@stage("rh7", "Recall with the up arrow")
def history_up(t):
    _history_env(t)
    _, second, third, invalid = _history_with_invalid(t)
    for command in ("echo " + third, invalid, "echo " + second):
        _recall(t, command)


@stage("vq0", "Navigate with the arrows")
def history_navigate(t):
    _history_env(t)
    _, _, third, invalid = _history_with_invalid(t)
    for command in ("echo " + third, invalid):
        _recall(t, command)
    t.send_text(DOWN)
    t.expect_row_stay("$ echo " + third)


@stage("dm2", "Run a recalled command")
def history_run(t):
    _history_env(t)
    _, _, third, invalid = _history_with_invalid(t)
    for command in ("echo " + third, invalid):
        _recall(t, command)
    t.send_text(DOWN)
    t.expect_row_stay("$ echo " + third)
    t.send_text("\n")
    t.expect_rows(["$ echo " + third, third], prompt=True)


# ---- history persistence --------------------------------------------------------------

def _echo_commands(t, count, words=(2, 4)):
    return ["echo " + " ".join(t.words(t.int_between(*words))) for _ in range(count)]


def _run_echo(t, command):
    t.command_response(command, command[len("echo "):])


@stage("za2", "Read the history from a file")
def history_read(t):
    _history_env(t)
    path = t.temp_file()
    commands = _echo_commands(t, t.int_between(2, 4))
    t.write_files([path], ["\n".join(commands) + "\n"])
    t.start()
    t.command_no_response("history -r " + path)
    t.history(["history -r " + path] + commands)


@stage("in3", "Write the history to a file")
def history_write(t):
    _history_env(t)
    path = t.temp_file()
    t.start()
    commands = _echo_commands(t, 3, (2, 3))
    for command in commands:
        _run_echo(t, command)
    t.command_no_response("history -w " + path)
    t.history_file_has(path, commands + ["history -w " + path])


@stage("sx3", "Append the history to a file")
def history_append(t):
    _history_env(t)
    path = t.temp_file()
    initial = _echo_commands(t, t.int_between(2, 4))
    t.write_files([path], ["\n".join(initial) + "\n"])
    t.start()
    commands = _echo_commands(t, t.int_between(2, 4))
    for command in commands:
        _run_echo(t, command)
    append = "history -a " + path
    t.command_no_response(append)
    expected = initial + commands + [append]
    t.history_file_has(path, expected)
    extra = _echo_commands(t, 1)[0]
    _run_echo(t, extra)
    t.command_no_response(append)
    t.history_file_has(path, expected + [extra, append])


@stage("zp4", "Load the history at start-up")
def history_load(t):
    path = t.temp_file()
    commands = _echo_commands(t, t.int_between(2, 5))
    t.write_files([path], ["\n".join(commands) + "\n"])
    t.env["HISTFILE"] = path
    t.start()
    t.history(commands)


@stage("kz7", "Save the history on exit")
def history_save(t):
    path = t.temp_file()
    t.write_files([path], [""])
    t.env["HISTFILE"] = path
    t.start()
    commands = _echo_commands(t, t.int_between(2, 4))
    for command in commands:
        _run_echo(t, command)
    t.history(commands)
    t.exit_command()
    t.history_file_has(path, commands + ["history", "exit"])


@stage("jv2", "Append to a loaded history on exit")
def history_save_loaded(t):
    path = t.temp_file()
    initial = _echo_commands(t, t.int_between(2, 4))
    t.write_files([path], ["\n".join(initial) + "\n"])
    t.env["HISTFILE"] = path
    t.start()
    commands = _echo_commands(t, t.int_between(2, 4))
    for command in commands:
        _run_echo(t, command)
    t.history(initial + commands)
    t.exit_command()
    t.history_file_has(path, initial + commands + ["history", "exit"])


# ---- parameter expansion --------------------------------------------------------------

@stage("ji0", "The declare builtin")
def declare_builtin(t):
    t.start()
    t.type_builtin("declare", also_reserved=True)


@stage("oa2", "declare -p of a missing variable")
def declare_missing(t):
    t.start()
    t.declare_print_missing("missing_variable_%d" % t.int_between(1, 100))


@stage("kv5", "Assign and print a variable")
def declare_assign_print(t):
    name, value, new_value = t.words(3)
    t.start()
    t.declare_assign(name, value)
    t.declare_print(name, value)
    t.declare_print_missing("missing_" + t.words(1)[0])
    t.declare_assign(name, new_value)
    t.declare_print(name, new_value)


@stage("db8", "Validate variable names")
def declare_validate(t):
    words = t.words(6)
    valid, value = "_" + words[0], words[1]
    digit_first = "%d%s" % (t.int_between(2, 9), words[2])
    hyphen = words[4] + "-" + words[5]
    t.start()
    t.declare_assign(valid, value)
    t.declare_assign(digit_first, words[3])
    t.declare_assign(hyphen, value)
    t.declare_print(valid, value)


def _expansion_stage(t, names_and_values, argument_builder, expected_arguments):
    secret = t.secret()
    executable = "custom_exe_%d" % t.int_between(1000, 9999)
    t.setup_commands([("signature_printer", executable, secret)])
    t.start()
    for name, value in names_and_values:
        t.declare_assign(name, value)
    command = "%s %s" % (executable, argument_builder)
    expected = ["Program was passed %d args (including program name)." % (len(expected_arguments) + 1),
                "Arg #0 (program name): %s" % executable]
    expected += ["Arg #%d: %s" % (i, arg) for i, arg in enumerate(expected_arguments, 1)]
    expected.append("Program Signature: %s" % secret)
    t.command_multiline(command, expected)


def _capitalised(word, suffix):
    return "%s_%d" % (word[:1].upper() + word[1:], suffix)


@stage("ge9", "Expand $name")
def expand_plain(t):
    words = t.words(5)
    suffixes = t.ints(0, 10, 2)
    first, second = _capitalised(words[0], suffixes[0]), _capitalised(words[1], suffixes[1])
    _expansion_stage(t, [(first, words[2]), (second, words[3])],
                     "$%s %s_$%s" % (first, words[4], second), [words[2], "%s_%s" % (words[4], words[3])])


@stage("br2", "Expand ${name}")
def expand_braced(t):
    words = t.words(7)
    suffixes = t.ints(1, 10, 2)
    first, second = _capitalised(words[0], suffixes[0]), _capitalised(words[1], suffixes[1])
    _expansion_stage(t, [(first, words[2]), (second, words[3])],
                     "%s_${%s}_%s ${%s}_%s" % (words[4], first, words[5], second, words[6]),
                     ["%s_%s_%s" % (words[4], words[2], words[5]), "%s_%s" % (words[3], words[6])])


@stage("my0", "Expand missing variables")
def expand_missing(t):
    name, value = t.words(2)
    missing = t.ints(0, 10, 3)
    _expansion_stage(t, [(name, value)],
                     "${missing_var_%d}_suffix ${%s} ${missing_var_%d} $missing_var_%d" % (
                         missing[0], name, missing[1], missing[2]),
                     ["_suffix", value])
