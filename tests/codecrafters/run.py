"""Runs the reconstructed CodeCrafters stage tests against a shell.

    python3 -I run.py <shell> [--stage SLUG ...] [--seed N] [--list]

Every stage starts the shell on a pseudo terminal in a fresh temporary directory, so stages do not
affect each other and can run in parallel. The values the stages make up (words, numbers) come
from a seeded generator: a failure prints the seed, and `--seed` repeats the run.

Exit status: 0 all passed, 1 a stage failed, 77 a stage could not run here (no C compiler).
"""

import argparse
import os
import random
import sys
import time
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import stages_basic  # noqa: E402,F401  (registers the stages)
import stages_completion  # noqa: E402,F401
import stages_more  # noqa: E402,F401
import tools  # noqa: E402
from harness import Stage, StageFailure  # noqa: E402
from registry import STAGES  # noqa: E402


def run_stage(shell, slug, seed):
    name, function = STAGES[slug]
    stage = Stage(shell, seed)
    started = time.time()
    try:
        function(stage)
        return "ok", "", time.time() - started
    except StageFailure as failure:
        return "FAIL", str(failure), time.time() - started
    except tools.ToolBuildError as error:
        return "SKIP", str(error), time.time() - started
    except Exception:  # A bug in the stage or the harness: show it, do not hide it as a shell failure
        return "ERROR", traceback.format_exc(), time.time() - started
    finally:
        stage.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("shell", nargs="?", help="the shell executable to test")
    parser.add_argument("--stage", action="append", help="a stage slug (several allowed), default all")
    parser.add_argument("--seed", type=int, help="seed for the made up words and numbers")
    parser.add_argument("--list", action="store_true", help="print the stage slugs and names and exit")
    arguments = parser.parse_args()

    if arguments.list:
        for slug, (name, _) in STAGES.items():
            print("%s\t%s" % (slug, name))
        return 0
    if not arguments.shell:
        parser.error("the shell executable is required")

    slugs = arguments.stage or list(STAGES)
    unknown = [slug for slug in slugs if slug not in STAGES]
    if unknown:
        parser.error("unknown stage(s): %s" % ", ".join(unknown))

    worst = 0
    for slug in slugs:
        seed = arguments.seed if arguments.seed is not None else random.randrange(1 << 30)
        status, detail, seconds = run_stage(arguments.shell, slug, seed)
        print("%-5s %s  %-45s %5.1fs  (seed %d)" % (status, slug, STAGES[slug][0], seconds, seed))
        if detail:
            print("\n".join("      " + text for text in detail.rstrip().split("\n")))
        if status in ("FAIL", "ERROR"):
            worst = 1
        elif status == "SKIP" and worst == 0:
            worst = 77
    return worst


if __name__ == "__main__":
    sys.exit(main())
