#ifdef _WIN32

#include "shell_internal/terminal.h"

#include <errno.h>
#include <stdlib.h>
#include <windows.h>

#include "platform.h"

static DWORD saved_input_mode;
static int raw_active = 0;

/** Units of a key that was translated into an escape sequence, waiting to be returned */
static PChar pending_units[4];
static size_t pending_count = 0;
static size_t pending_index = 0;

int terminal_is_interactive(void) {
    DWORD mode;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0;
}

int terminal_enter_raw(void) {
    if (raw_active) {
        return 0;
    }

    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);

    if (!GetConsoleMode(input, &saved_input_mode)) {
        platform_set_errno_from_last_error();
        return -1;
    }

    // No line buffering, no echo, and Ctrl-C arrives as the character 0x03. Keys are read as
    // console input events (see terminal_read_char), which does not depend on virtual terminal
    // input being available.
    const DWORD raw_mode = saved_input_mode & ~(DWORD)(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    if (!SetConsoleMode(input, raw_mode)) {
        platform_set_errno_from_last_error();
        return -1;
    }

    raw_active = 1;
    return 0;
}

void terminal_restore(void) {
    if (!raw_active) {
        return;
    }

    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), saved_input_mode);
    pending_count = 0;
    pending_index = 0;
    raw_active = 0;
}

int terminal_read_char(PChar *out) {
    if (pending_index < pending_count) {
        *out = pending_units[pending_index++];
        return 1;
    }

    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);

    while (1) {
        INPUT_RECORD record;
        DWORD records_read = 0;
        if (!ReadConsoleInputW(input, &record, 1, &records_read) || records_read == 0) {
            platform_set_errno_from_last_error();
            return -1;
        }

        if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
            continue; // Mouse, resize, focus and key releases
        }

        const KEY_EVENT_RECORD *key = &record.Event.KeyEvent;
        if (key->uChar.UnicodeChar != 0) {
            *out = key->uChar.UnicodeChar;
            return 1;
        }

        // Keys without a character: the arrows that the line editor uses are given to it as the
        // escape sequences a POSIX terminal sends, so that it needs only one way to read them
        if (key->wVirtualKeyCode == VK_UP || key->wVirtualKeyCode == VK_DOWN) {
            pending_units[0] = L'[';
            pending_units[1] = key->wVirtualKeyCode == VK_UP ? L'A' : L'B';
            pending_count = 2;
            pending_index = 0;
            *out = 0x1b;
            return 1;
        }
    }
}

void terminal_write(const PChar *text, size_t length) {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;

    if (GetConsoleMode(output, &mode)) {
        while (length > 0) {
            DWORD written = 0;
            if (!WriteConsoleW(output, text, (DWORD)length, &written, NULL) || written == 0) {
                return;
            }
            text += written;
            length -= written;
        }
        return;
    }

    // Standard output is a file or a pipe: write UTF-8, like the rest of the shell does
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, (int)length, NULL, 0, NULL, NULL);
    if (bytes <= 0) {
        return;
    }

    char *utf8 = malloc((size_t)bytes);
    if (utf8 == NULL) {
        return;
    }
    WideCharToMultiByte(CP_UTF8, 0, text, (int)length, utf8, bytes, NULL, NULL);

    DWORD written = 0;
    WriteFile(output, utf8, (DWORD)bytes, &written, NULL);
    free(utf8);
}

#endif //_WIN32
