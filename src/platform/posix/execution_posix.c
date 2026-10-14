#include "execution_handler.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "raii.h"

#define CAPTURE_LIMIT ((size_t)1024 * 1024)
#define CAPTURE_IDLE_TIMEOUT_MS 5000

/** Waits for the child and returns its exit status, or -1 with errno set */
static int wait_for(pid_t pid) {
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    errno = EINVAL;
    return -1;
}

int do_execution(const PChar* executable_full_path, PChar *const* args) {

    //Fork initializes a child process which
    //continues execution from here
    pid_t pid = fork();
    if (pid < 0) {
        return -1; // errno set by fork
    }

    //The process splits into two
    if (pid == 0) {
        // Child process enters here
        execv(executable_full_path, args);

        //only reached if execv fails. The parent can only see the status, so the reason is reported here
        p_perror(executable_full_path);
        _exit(127);
    }

    // Parent process enters here
    return wait_for(pid);
}

int start_background(const PChar *executable_full_path, PChar *const *args, BackgroundProcess *process) {
    const pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        setpgid(0, 0); // Not in the foreground group: the terminal's Ctrl-C is not for it

        const int null_input = open("/dev/null", O_RDONLY);
        if (null_input >= 0 && null_input != STDIN_FILENO) {
            dup2(null_input, STDIN_FILENO);
            close(null_input);
        }

        execv(executable_full_path, args);
        p_perror(executable_full_path);
        _exit(127);
    }

    process->id = (unsigned long)pid;
    process->handle = NULL;
    return 0;
}

int poll_background(BackgroundProcess *process, int *status) {
    int wait_status;
    pid_t result;
    while ((result = waitpid((pid_t)process->id, &wait_status, WNOHANG)) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }

    if (result == 0) {
        return 1;
    }

    *status = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : 128 + WTERMSIG(wait_status);
    return 0;
}

int run_capture(const PChar *executable_full_path, PChar *const *args, const EnvVar *environment,
                PChar **output) {
    *output = NULL;

    int link[2];
    if (pipe(link) != 0) {
        return -1;
    }
    fcntl(link[0], F_SETFD, FD_CLOEXEC);
    fcntl(link[1], F_SETFD, FD_CLOEXEC);

    AUTO_CLOSE int read_end = link[0];
    const int write_end = link[1];

    const pid_t pid = fork();
    if (pid < 0) {
        const int saved_errno = errno;
        close(write_end);
        errno = saved_errno;
        return -1;
    }

    if (pid == 0) {
        dup2(write_end, STDOUT_FILENO); // The copy has no close-on-exec flag

        const int null_input = open("/dev/null", O_RDONLY);
        if (null_input >= 0 && null_input != STDIN_FILENO) {
            dup2(null_input, STDIN_FILENO);
            close(null_input);
        }

        // Messages of the child would land in the middle of the line being edited
        const int null_error = open("/dev/null", O_WRONLY);
        if (null_error >= 0 && null_error != STDERR_FILENO) {
            dup2(null_error, STDERR_FILENO);
            close(null_error);
        }

        for (const EnvVar *variable = environment; variable != NULL && variable->name != NULL; variable++) {
            setenv(variable->name, variable->value, 1);
        }

        execv(executable_full_path, args);
        _exit(127);
    }

    close(write_end);

    AUTO_FREE char *buffer = NULL;
    size_t capacity = 0;
    size_t size = 0;
    int kill_child = 0;

    while (1) {
        struct pollfd poll_fd = {read_end, POLLIN, 0};
        const int ready = poll(&poll_fd, 1, CAPTURE_IDLE_TIMEOUT_MS);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0) {
            kill_child = 1; // Failed, or alive and silent for too long: a stuck completer must not hang the editor
            break;
        }

        if (size >= CAPTURE_LIMIT) {
            kill_child = 1;
            break;
        }
        if (size + 4096 + 1 > capacity) {
            const size_t new_capacity = capacity == 0 ? 8192 : capacity * 2;
            char *grown = realloc(buffer, new_capacity);
            if (grown == NULL) {
                kill_child = 1;
                break;
            }
            buffer = grown;
            capacity = new_capacity;
        }

        const ssize_t count = read(read_end, buffer + size, capacity - 1 - size);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break; // End of output (or an unreadable pipe)
        }
        size += (size_t)count;
    }

    if (kill_child) {
        kill(pid, SIGKILL);
    }
    close(read_end);
    read_end = -1;

    const int status = wait_for(pid);

    if (buffer == NULL) {
        buffer = calloc(1, 1);
        if (buffer == NULL) {
            return -1;
        }
    }
    buffer[size] = '\0';

    *output = p_from_utf8(buffer);
    return *output != NULL ? status : -1;
}

/** The child half of a pipeline stage. Never returns */
static void run_stage_in_child(const PipelineStage *stage, int input_fd, int output_fd,
                               BuiltinRunner run_builtin, void *context) {
    if (input_fd >= 0) {
        dup2(input_fd, STDIN_FILENO);
        close(input_fd);
    }
    if (output_fd >= 0) {
        dup2(output_fd, STDOUT_FILENO);
        close(output_fd);
    }

    // Files win over the pipes, like in other shells. The saved descriptors are not needed here
    SavedFd saved_stdout = {STDOUT_FILENO, -1};
    SavedFd saved_stderr = {STDERR_FILENO, -1};
    if (stage->redirections->stdout_target.path != NULL &&
        redirect_fd(&stage->redirections->stdout_target, STDOUT_FILENO, &saved_stdout) != 0) {
        p_perror(stage->redirections->stdout_target.path);
        _exit(1);
    }
    if (stage->redirections->stderr_target.path != NULL &&
        redirect_fd(&stage->redirections->stderr_target, STDERR_FILENO, &saved_stderr) != 0) {
        p_perror(stage->redirections->stderr_target.path);
        _exit(1);
    }

    if (stage->path == NULL) {
        _exit(run_builtin(stage->argv, 1, context));
    }

    execv(stage->path, stage->argv);
    p_perror(stage->path);
    _exit(127);
}

int run_pipeline(const PipelineStage *stages, size_t count, BuiltinRunner run_builtin, void *context) {
    AUTO_FREE pid_t *pids = calloc(count, sizeof(pid_t));
    if (pids == NULL) {
        return -1;
    }

    AUTO_CLOSE int previous_read = -1; // The read end feeding the stage being started
    size_t started = 0;
    int setup_errno = 0;

    for (size_t i = 0; i < count; i++) {
        int link[2] = {-1, -1};
        if (i + 1 < count && pipe(link) != 0) {
            setup_errno = errno;
            break;
        }

        const pid_t pid = fork();
        if (pid < 0) {
            setup_errno = errno;
            close(link[0]);
            close(link[1]);
            break;
        }

        if (pid == 0) {
            // Only this stage's own ends are kept
            if (link[0] >= 0) {
                close(link[0]);
            }
            run_stage_in_child(&stages[i], previous_read, link[1], run_builtin, context);
        }

        pids[started++] = pid;

        // The child has its copies: the shell must not hold the write end, or nobody sees the end
        if (previous_read >= 0) {
            close(previous_read);
        }
        if (link[1] >= 0) {
            close(link[1]);
        }
        previous_read = link[0];
    }

    // The read end of a link that was not consumed, after a failure
    if (previous_read >= 0) {
        close(previous_read);
        previous_read = -1;
    }

    int status = 0;
    for (size_t i = 0; i < started; i++) {
        status = wait_for(pids[i]);
    }

    if (setup_errno != 0) {
        errno = setup_errno;
        return -1;
    }

    return status;
}

