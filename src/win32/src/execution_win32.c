#include "core/execution_handler.h"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <windows.h>

#include "core/platform.h"
#include "platform_win32.h"
#include "core/raii.h"
#include "fd_raii.h"
#include "redirection_win32.h"

/**
 * Appends one argument to `out`, quoted so that CommandLineToArgvW and the C runtime read it back
 * unchanged, and returns the number of units. With `out` NULL it only measures.
 */
static size_t quote_argument(const PChar *argument, PChar *out) {
    size_t length = 0;

#define EMIT(c) do { if (out != NULL) { out[length] = (c); } length++; } while (0)

    const int needs_quotes = argument[0] == L'\0' || wcspbrk(argument, L" \t\n\v\"") != NULL;
    if (!needs_quotes) {
        for (const PChar *p = argument; *p != L'\0'; p++) {
            EMIT(*p);
        }
        return length;
    }

    EMIT(L'"');
    for (const PChar *p = argument;; p++) {
        size_t backslashes = 0;
        while (*p == L'\\') {
            backslashes++;
            p++;
        }

        if (*p == L'\0') {
            // Backslashes before the closing quote must not escape it
            for (size_t i = 0; i < backslashes * 2; i++) {
                EMIT(L'\\');
            }
            break;
        }

        if (*p == L'"') {
            for (size_t i = 0; i < backslashes * 2 + 1; i++) {
                EMIT(L'\\');
            }
        } else {
            for (size_t i = 0; i < backslashes; i++) {
                EMIT(L'\\');
            }
        }
        EMIT(*p);
    }
    EMIT(L'"');

#undef EMIT

    return length;
}

/** Builds the command line: the quoted executable path, then the quoted arguments after argv[0] */
static PChar *build_command_line(const PChar *executable, PChar *const *args) {
    size_t total = quote_argument(executable, NULL);
    for (size_t i = 1; args[i] != NULL; i++) {
        total += 1 + quote_argument(args[i], NULL);
    }

    PChar *line = malloc((total + 1) * sizeof(PChar));
    if (line == NULL) {
        return NULL;
    }

    size_t length = quote_argument(executable, line);
    for (size_t i = 1; args[i] != NULL; i++) {
        line[length++] = L' ';
        length += quote_argument(args[i], line + length);
    }
    line[length] = L'\0';
    return line;
}

/** The OS handle behind a C runtime descriptor, made inheritable, or NULL if there is none */
static HANDLE inheritable_handle(int fd) {
    const intptr_t value = _get_osfhandle(fd);
    if (value == -1 || value == -2) {
        return NULL;
    }

    HANDLE handle = (HANDLE)value;
    SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    return handle;
}

/**
 * Starts the program with the given C runtime descriptors as its standard input, output and
 * error. Returns the process handle, or NULL with errno set.
 */
static HANDLE start_process(const PChar *executable, PChar *const *args, int input_fd, int output_fd, int error_fd) {
    AUTO_FREE PChar *command_line = build_command_line(executable, args);
    if (command_line == NULL) {
        return NULL;
    }

    STARTUPINFOW startup;
    ZeroMemory(&startup, sizeof startup);
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = inheritable_handle(input_fd);
    startup.hStdOutput = inheritable_handle(output_fd);
    startup.hStdError = inheritable_handle(error_fd);

    // The application name is left out so that batch files are run through the command interpreter
    PROCESS_INFORMATION process;
    ZeroMemory(&process, sizeof process);
    if (!CreateProcessW(NULL, command_line, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process)) {
        platform_set_errno_from_last_error();
        return NULL;
    }

    CloseHandle(process.hThread);
    return process.hProcess;
}

/** Waits for the process, closes its handle, and returns the exit status or -1 with errno set */
static int wait_for(HANDLE process) {
    DWORD exit_code = 0;
    const DWORD wait_result = WaitForSingleObject(process, INFINITE);
    const BOOL got_exit_code = wait_result == WAIT_OBJECT_0 && GetExitCodeProcess(process, &exit_code);
    CloseHandle(process);

    if (!got_exit_code) {
        errno = ECHILD;
        return -1;
    }

    return exit_code > 255 ? 255 : (int)exit_code;
}

int do_execution(const PChar* executable_full_path, PChar *const* args) {
    // Redirections are applied to the C runtime descriptors, so pass those on explicitly
    HANDLE process = start_process(executable_full_path, args, 0, 1, 2);
    if (process == NULL) {
        return -1;
    }

    return wait_for(process);
}

struct BackgroundProcess {
    HANDLE handle; /** NULL once the process was seen to end and the handle is closed */
    unsigned long id; /** Kept, because the handle is closed when the process ends */
};

BackgroundProcess *start_background(const PChar *executable_full_path, PChar *const *args) {
    BackgroundProcess *process = malloc(sizeof *process);
    if (process == NULL) {
        return NULL;
    }

    const int null_input = _wopen(L"NUL", _O_RDONLY);
    if (null_input < 0) {
        const int saved_errno = errno;
        free(process);
        errno = saved_errno;
        return NULL;
    }

    HANDLE handle = start_process(executable_full_path, args, null_input, 1, 2);
    const int saved_errno = errno;
    close(null_input);
    if (handle == NULL) {
        free(process);
        errno = saved_errno;
        return NULL;
    }

    process->handle = handle;
    process->id = (unsigned long)GetProcessId(handle);
    return process;
}

unsigned long background_process_id(const BackgroundProcess *process) {
    return process->id;
}

void background_process_free(BackgroundProcess *process) {
    if (process == NULL) {
        return;
    }
    if (process->handle != NULL) {
        CloseHandle(process->handle);
    }
    free(process);
}

int poll_background(BackgroundProcess *process, int *status) {
    HANDLE handle = process->handle;
    const DWORD wait_result = WaitForSingleObject(handle, 0);
    if (wait_result == WAIT_TIMEOUT) {
        return 1;
    }

    DWORD exit_code = 0;
    const BOOL got_exit_code = wait_result == WAIT_OBJECT_0 && GetExitCodeProcess(handle, &exit_code);
    CloseHandle(handle);
    process->handle = NULL;

    if (!got_exit_code) {
        errno = ECHILD;
        return -1;
    }

    *status = exit_code > 255 ? 255 : (int)exit_code;
    return 0;
}

int run_capture(const PChar *executable_full_path, PChar *const *args, const EnvVar *environment,
                PChar **output) {
    *output = NULL;

    HANDLE read_handle = NULL;
    HANDLE write_handle = NULL;
    SECURITY_ATTRIBUTES attributes = {sizeof attributes, NULL, TRUE};
    if (!CreatePipe(&read_handle, &write_handle, &attributes, 0)) {
        platform_set_errno_from_last_error();
        return -1;
    }
    SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0);

    // The write end becomes a C runtime descriptor: closing that also closes the handle
    const int write_fd = _open_osfhandle((intptr_t)write_handle, _O_WRONLY);
    if (write_fd < 0) {
        CloseHandle(read_handle);
        CloseHandle(write_handle);
        return -1;
    }

    const int null_input = _wopen(L"NUL", _O_RDONLY);
    if (null_input < 0) {
        const int saved_errno = errno;
        close(write_fd);
        CloseHandle(read_handle);
        errno = saved_errno;
        return -1;
    }

    // The child inherits the environment of the shell: the extra variables are set around the start
    for (const EnvVar *variable = environment; variable != NULL && variable->name != NULL; variable++) {
        SetEnvironmentVariableW(variable->name, variable->value);
    }
    // Messages of the child would land in the middle of the line being edited
    const int null_error = _wopen(L"NUL", _O_WRONLY);
    HANDLE process = start_process(executable_full_path, args, null_input, write_fd,
                                   null_error >= 0 ? null_error : 2);
    const int start_errno = errno;
    for (const EnvVar *variable = environment; variable != NULL && variable->name != NULL; variable++) {
        SetEnvironmentVariableW(variable->name, NULL);
    }

    close(write_fd);
    close(null_input);
    if (null_error >= 0) {
        close(null_error);
    }

    if (process == NULL) {
        CloseHandle(read_handle);
        errno = start_errno;
        return -1;
    }

    const size_t limit = (size_t)1024 * 1024;
    AUTO_FREE char *buffer = malloc(limit + 1);
    size_t size = 0;
    if (buffer != NULL) {
        while (size < limit) {
            DWORD count = 0;
            if (!ReadFile(read_handle, buffer + size, (DWORD)(limit - size), &count, NULL) || count == 0) {
                break;
            }
            size += count;
        }
    }
    CloseHandle(read_handle);

    if (size >= limit) {
        TerminateProcess(process, 1);
    }
    const int status = wait_for(process);

    if (buffer == NULL) {
        errno = ENOMEM;
        return -1;
    }
    buffer[size] = '\0';

    *output = p_from_utf8(buffer);
    return *output != NULL ? status : -1;
}

/** Pipe ends of the link after a stage, as C runtime descriptors. -1 once closed */
typedef struct {
    int read_end;
    int write_end;
} Link;

static void close_fd_if_open(int *fd) {
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static void close_links(Link *links, size_t count) {
    for (size_t i = 0; i < count; i++) {
        close_fd_if_open(&links[i].read_end);
        close_fd_if_open(&links[i].write_end);
    }
}

int run_pipeline(const PipelineStage *stages, size_t count, BuiltinRunner run_builtin, void *context) {
    AUTO_FREE Link *links = malloc(count * sizeof(Link)); // links[i] joins stage i to stage i + 1
    AUTO_FREE HANDLE *processes = calloc(count, sizeof(HANDLE));
    AUTO_FREE int *builtin_status = calloc(count, sizeof(int));
    if (links == NULL || processes == NULL || builtin_status == NULL) {
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        links[i].read_end = -1;
        links[i].write_end = -1;
    }

    int setup_errno = 0;

    // Not inherited by default: each program is given exactly the ends it needs
    for (size_t i = 0; i + 1 < count; i++) {
        int ends[2];
        if (_pipe(ends, 65536, _O_BINARY | _O_NOINHERIT) != 0) {
            setup_errno = errno;
            break;
        }
        links[i].read_end = ends[0];
        links[i].write_end = ends[1];
    }

    // Programs first, so that they are already reading when a builtin writes
    for (size_t i = 0; i < count && setup_errno == 0; i++) {
        if (stages[i].path == NULL) {
            continue;
        }

        AUTO_CLOSE int file_out = -1;
        AUTO_CLOSE int file_err = -1;
        int input_fd = i > 0 ? links[i - 1].read_end : 0;
        int output_fd = i + 1 < count ? links[i].write_end : 1;
        int error_fd = 2;

        const Redirections *redirections = stages[i].redirections;
        if (redirections->stdout_target.path != NULL) {
            file_out = redirect_open(&redirections->stdout_target);
            if (file_out < 0) {
                p_perror(redirections->stdout_target.path);
                setup_errno = errno;
                break;
            }
            output_fd = file_out;
        }
        if (redirections->stderr_target.path != NULL) {
            file_err = redirect_open(&redirections->stderr_target);
            if (file_err < 0) {
                p_perror(redirections->stderr_target.path);
                setup_errno = errno;
                break;
            }
            error_fd = file_err;
        }

        processes[i] = start_process(stages[i].path, stages[i].argv, input_fd, output_fd, error_fd);
        if (processes[i] == NULL) {
            setup_errno = errno;
            break;
        }

        // The program has its own copies: the shell must not keep the ends, or nobody sees the end
        if (i > 0) {
            close_fd_if_open(&links[i - 1].read_end);
        }
        if (i + 1 < count) {
            close_fd_if_open(&links[i].write_end);
        }
    }

    // A builtin does not read, so whatever feeds it is cut off now and its writer sees the end
    for (size_t i = 1; i < count; i++) {
        if (stages[i].path == NULL) {
            close_fd_if_open(&links[i - 1].read_end);
        }
    }

    for (size_t i = 0; i < count && setup_errno == 0; i++) {
        if (stages[i].path != NULL) {
            continue;
        }

        // The builtin runs in this process with its descriptors pointed at the pipe and the files
        CLEANUP(restore_stream) SavedStream saved_stdout = SAVED_STREAM_INIT(REDIRECT_STDOUT);
        CLEANUP(restore_stream) SavedStream saved_stderr = SAVED_STREAM_INIT(REDIRECT_STDERR);

        if (i + 1 < count) {
            if (redirect_to_fd(links[i].write_end, REDIRECT_STDOUT, &saved_stdout) != 0) {
                setup_errno = errno;
                break;
            }
            close_fd_if_open(&links[i].write_end); // fd 1 holds the only copy now
        }

        const Redirections *redirections = stages[i].redirections;
        SavedStream saved_file_out = SAVED_STREAM_INIT(REDIRECT_STDOUT);
        SavedStream saved_file_err = SAVED_STREAM_INIT(REDIRECT_STDERR);
        if (redirections->stdout_target.path != NULL &&
            redirect_stream(&redirections->stdout_target, REDIRECT_STDOUT, &saved_file_out) != 0) {
            p_perror(redirections->stdout_target.path);
            builtin_status[i] = 1;
            restore_stream(&saved_file_out);
            continue;
        }
        if (redirections->stderr_target.path != NULL &&
            redirect_stream(&redirections->stderr_target, REDIRECT_STDERR, &saved_file_err) != 0) {
            p_perror(redirections->stderr_target.path);
            builtin_status[i] = 1;
            restore_stream(&saved_file_out);
            continue;
        }

        builtin_status[i] = run_builtin(stages[i].argv, 1, context);

        restore_stream(&saved_file_err);
        restore_stream(&saved_file_out);
    }

    close_links(links, count);

    int status = 0;
    for (size_t i = 0; i < count; i++) {
        if (stages[i].path == NULL) {
            status = builtin_status[i];
        } else if (processes[i] != NULL) {
            status = wait_for(processes[i]);
        }
    }

    if (setup_errno != 0) {
        errno = setup_errno;
        return -1;
    }

    return status;
}

