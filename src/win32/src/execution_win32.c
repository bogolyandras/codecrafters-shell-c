#include "core/execution_handler.h"

#include <errno.h>
#include <stdlib.h>
#include <windows.h>

#include "core/platform.h"
#include "platform_win32.h"
#include "core/raii.h"
#include "handle_raii.h"
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

/** The standard handles of this process at this moment: where the shell's own output goes */
static HANDLE standard_input(void) {
    return GetStdHandle(STD_INPUT_HANDLE);
}

static HANDLE standard_output(void) {
    return GetStdHandle(STD_OUTPUT_HANDLE);
}

static HANDLE standard_error(void) {
    return GetStdHandle(STD_ERROR_HANDLE);
}

/** Opens the null device. Returns INVALID_HANDLE_VALUE with errno set on failure */
static HANDLE open_null_device(DWORD access) {
    HANDLE handle = CreateFileW(L"NUL", access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        platform_set_errno_from_last_error();
    }
    return handle;
}

/**
 * Starts the program with the given handles as its standard input, output and error. Only these
 * handles are inherited (a handle list), so a pipe end the shell holds for another stage never
 * leaks into this program and keeps a pipe open. Returns the process handle, or NULL with errno set.
 */
static HANDLE start_process(const PChar *executable, PChar *const *args, HANDLE input, HANDLE output, HANDLE error) {
    AUTO_FREE PChar *command_line = build_command_line(executable, args);
    if (command_line == NULL) {
        return NULL;
    }

    const HANDLE standard[3] = {input, output, error};
    HANDLE inherited[3];
    DWORD inherited_count = 0;
    for (int i = 0; i < 3; i++) {
        if (!handle_is_usable(standard[i])) {
            continue;
        }

        // A handle list may name a handle once, and the handle must be inheritable to be in it
        int listed = 0;
        for (DWORD j = 0; j < inherited_count; j++) {
            listed |= inherited[j] == standard[i];
        }
        if (!listed) {
            SetHandleInformation(standard[i], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
            inherited[inherited_count++] = standard[i];
        }
    }

    STARTUPINFOEXW startup;
    ZeroMemory(&startup, sizeof startup);
    startup.StartupInfo.cb = sizeof startup;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = handle_is_usable(input) ? input : NULL;
    startup.StartupInfo.hStdOutput = handle_is_usable(output) ? output : NULL;
    startup.StartupInfo.hStdError = handle_is_usable(error) ? error : NULL;

    AUTO_FREE char *attribute_storage = NULL;
    DWORD creation_flags = 0;
    if (inherited_count > 0) {
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(NULL, 1, 0, &size); // fails on purpose, and tells the size
        attribute_storage = malloc(size);
        if (attribute_storage == NULL) {
            return NULL;
        }
        LPPROC_THREAD_ATTRIBUTE_LIST list = (LPPROC_THREAD_ATTRIBUTE_LIST)attribute_storage;
        if (!InitializeProcThreadAttributeList(list, 1, 0, &size)) {
            platform_set_errno_from_last_error();
            return NULL;
        }
        if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                       inherited_count * sizeof(HANDLE), NULL, NULL)) {
            platform_set_errno_from_last_error();
            DeleteProcThreadAttributeList(list);
            return NULL;
        }
        startup.lpAttributeList = list;
        creation_flags = EXTENDED_STARTUPINFO_PRESENT;
    }

    // The application name is left out so that batch files are run through the command interpreter
    PROCESS_INFORMATION process;
    ZeroMemory(&process, sizeof process);
    const BOOL created = CreateProcessW(NULL, command_line, NULL, NULL, inherited_count > 0, creation_flags, NULL,
                                        NULL, &startup.StartupInfo, &process);
    if (!created) {
        platform_set_errno_from_last_error();
    }
    if (startup.lpAttributeList != NULL) {
        DeleteProcThreadAttributeList(startup.lpAttributeList);
    }
    if (!created) {
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
    // A redirection changed the standard handles of this process, so pass those on explicitly
    HANDLE process = start_process(executable_full_path, args, standard_input(), standard_output(),
                                   standard_error());
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

    AUTO_CLOSE_HANDLE HANDLE null_input = open_null_device(GENERIC_READ);
    if (null_input == INVALID_HANDLE_VALUE) {
        const int saved_errno = errno;
        free(process);
        errno = saved_errno;
        return NULL;
    }

    HANDLE handle = start_process(executable_full_path, args, null_input, standard_output(), standard_error());
    if (handle == NULL) {
        const int saved_errno = errno;
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

    HANDLE read_end = INVALID_HANDLE_VALUE;
    HANDLE write_end = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES attributes = {sizeof attributes, NULL, FALSE};
    if (!CreatePipe(&read_end, &write_end, &attributes, 0)) {
        platform_set_errno_from_last_error();
        return -1;
    }
    AUTO_CLOSE_HANDLE HANDLE read_handle = read_end;
    AUTO_CLOSE_HANDLE HANDLE write_handle = write_end;

    AUTO_CLOSE_HANDLE HANDLE null_input = open_null_device(GENERIC_READ);
    if (null_input == INVALID_HANDLE_VALUE) {
        return -1;
    }
    AUTO_CLOSE_HANDLE HANDLE null_error = open_null_device(GENERIC_WRITE);

    for (const EnvVar *variable = environment; variable != NULL && variable->name != NULL; variable++) {
        SetEnvironmentVariableW(variable->name, variable->value);
    }
    HANDLE process = start_process(executable_full_path, args, null_input, write_handle,
                                   null_error != INVALID_HANDLE_VALUE ? null_error : standard_error());
    const int start_errno = errno;
    for (const EnvVar *variable = environment; variable != NULL && variable->name != NULL; variable++) {
        SetEnvironmentVariableW(variable->name, NULL);
    }

    // The shell's copy of the write end must go, or the read below never sees the end of the output
    CloseHandle(TAKE_HANDLE(write_handle));

    if (process == NULL) {
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
    CloseHandle(TAKE_HANDLE(read_handle));

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

/** Pipe ends of the link after a stage. INVALID_HANDLE_VALUE once closed or given away */
typedef struct {
    HANDLE read_end;
    HANDLE write_end;
} Link;

static void close_handle_if_open(HANDLE *handle) {
    if (handle_is_usable(*handle)) {
        CloseHandle(*handle);
    }
    *handle = INVALID_HANDLE_VALUE;
}

static void close_links(Link *links, size_t count) {
    for (size_t i = 0; i < count; i++) {
        close_handle_if_open(&links[i].read_end);
        close_handle_if_open(&links[i].write_end);
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
        links[i].read_end = INVALID_HANDLE_VALUE;
        links[i].write_end = INVALID_HANDLE_VALUE;
    }

    int setup_errno = 0;

    // Not inheritable: each program is given exactly the ends it needs, through a handle list
    for (size_t i = 0; i + 1 < count; i++) {
        SECURITY_ATTRIBUTES attributes = {sizeof attributes, NULL, FALSE};
        if (!CreatePipe(&links[i].read_end, &links[i].write_end, &attributes, 65536)) {
            platform_set_errno_from_last_error();
            setup_errno = errno;
            links[i].read_end = INVALID_HANDLE_VALUE;
            links[i].write_end = INVALID_HANDLE_VALUE;
            break;
        }
    }

    // Programs first, so that they are already reading when a builtin writes
    for (size_t i = 0; i < count && setup_errno == 0; i++) {
        if (stages[i].path == NULL) {
            continue;
        }

        AUTO_CLOSE_HANDLE HANDLE file_out = INVALID_HANDLE_VALUE;
        AUTO_CLOSE_HANDLE HANDLE file_err = INVALID_HANDLE_VALUE;
        HANDLE input = i > 0 ? links[i - 1].read_end : standard_input();
        HANDLE output = i + 1 < count ? links[i].write_end : standard_output();
        HANDLE error = standard_error();

        const Redirections *redirections = stages[i].redirections;
        if (redirections->stdout_target.path != NULL) {
            file_out = redirect_open(&redirections->stdout_target);
            if (file_out == INVALID_HANDLE_VALUE) {
                p_perror(redirections->stdout_target.path);
                setup_errno = errno;
                break;
            }
            output = file_out;
        }
        if (redirections->stderr_target.path != NULL) {
            file_err = redirect_open(&redirections->stderr_target);
            if (file_err == INVALID_HANDLE_VALUE) {
                p_perror(redirections->stderr_target.path);
                setup_errno = errno;
                break;
            }
            error = file_err;
        }

        processes[i] = start_process(stages[i].path, stages[i].argv, input, output, error);
        if (processes[i] == NULL) {
            setup_errno = errno;
            break;
        }

        // The program has its own copies: the shell must not keep the ends, or nobody sees the end
        if (i > 0) {
            close_handle_if_open(&links[i - 1].read_end);
        }
        if (i + 1 < count) {
            close_handle_if_open(&links[i].write_end);
        }
    }

    // A builtin does not read, so whatever feeds it is cut off now and its writer sees the end
    for (size_t i = 1; i < count; i++) {
        if (stages[i].path == NULL) {
            close_handle_if_open(&links[i - 1].read_end);
        }
    }

    for (size_t i = 0; i < count && setup_errno == 0; i++) {
        if (stages[i].path != NULL) {
            continue;
        }

        // The builtin runs in this process with its standard handles pointed at the pipe and the
        // files. The cleanups put them back, and closing the pipe end is part of that: it has to
        // happen before the programs are waited for, or their reader never sees the end
        CLEANUP(restore_stream) SavedStream saved_stdout = SAVED_STREAM_INIT(REDIRECT_STDOUT);
        CLEANUP(restore_stream) SavedStream saved_stderr = SAVED_STREAM_INIT(REDIRECT_STDERR);

        if (i + 1 < count) {
            if (redirect_to_handle(links[i].write_end, REDIRECT_STDOUT, &saved_stdout) != 0) {
                setup_errno = errno;
                break;
            }
            links[i].write_end = INVALID_HANDLE_VALUE; // the standard handle owns the only copy now
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
