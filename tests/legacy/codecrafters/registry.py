"""The list of stages. A stage is a function that takes a harness.Stage and drives the shell."""

STAGES = {}  # slug -> (name, function), in the order of the course


def stage(slug, name):
    def register(function):
        if slug in STAGES:
            raise ValueError("duplicate stage %s" % slug)
        STAGES[slug] = (name, function)
        return function

    return register
