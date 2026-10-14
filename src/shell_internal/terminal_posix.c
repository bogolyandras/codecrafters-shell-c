#ifndef _WIN32

#include "shell_internal/terminal.h"

#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

static struct termios saved_termios;
static int raw_active = 0;
static int signal_handlers_installed = 0;

/**
 * Fatal signals do not run atexit handlers or cleanup attributes, so put the terminal back
 * ourselves. tcsetattr, signal and raise are async-signal-safe.
 */
static void restore_on_fatal_signal(int signo) {
    if (raw_active) {
        tcsetattr(STDIN_FILENO, TCSADRAIN, &saved_termios);
    }

    signal(signo, SIG_DFL);
    raise(signo);
}

static void install_signal_handlers(void) {
    if (signal_handlers_installed) {
        return;
    }

    struct sigaction action;
    action.sa_handler = restore_on_fatal_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGHUP, &action, NULL);
    signal_handlers_installed = 1;
}

int terminal_is_interactive(void) {
    return isatty(STDIN_FILENO);
}

int terminal_enter_raw(void) {
    if (raw_active) {
        return 0;
    }

    if (tcgetattr(STDIN_FILENO, &saved_termios) != 0) {
        return -1;
    }

    struct termios raw = saved_termios;
    // Keep OPOST so "\n" still returns the cursor to the first column
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSADRAIN, &raw) != 0) {
        return -1;
    }

    raw_active = 1;
    install_signal_handlers();
    return 0;
}

void terminal_restore(void) {
    if (!raw_active) {
        return;
    }

    tcsetattr(STDIN_FILENO, TCSADRAIN, &saved_termios);
    raw_active = 0;
}

int terminal_read_char(PChar *out) {
    while (1) {
        const ssize_t result = read(STDIN_FILENO, out, 1);
        if (result >= 0) {
            return (int)result;
        }
        if (errno != EINTR) {
            return -1;
        }
    }
}

void terminal_write(const PChar *text, size_t length) {
    while (length > 0) {
        const ssize_t written = write(STDOUT_FILENO, text, length);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }

        text += written;
        length -= (size_t)written;
    }
}

#endif //_WIN32
