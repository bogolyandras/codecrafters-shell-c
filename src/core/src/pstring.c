#include "core/pstring.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

#include "core/platform.h"

static int write_failed[2];

PChar *p_strdup(const PChar *text) {
    const size_t length = p_strlen(text);

    PChar *copy = malloc((length + 1) * sizeof(PChar));
    if (copy == NULL) {
        return NULL;
    }

    p_memcpy(copy, text, length + 1);
    return copy;
}

static int write_text(OutStream stream, const PChar *text, size_t length) {
    if (platform_write(stream, text, length) != 0) {
        write_failed[stream] = 1;
        return -1;
    }
    return 0;
}

/** Formats into a buffer (the stack for short text) and writes it with one call */
static int print_formatted(OutStream stream, const PChar *format, va_list args) {
    PChar stack_buffer[512];
    size_t capacity = sizeof stack_buffer / sizeof stack_buffer[0];
    PChar *buffer = stack_buffer;
    int result = -1;

    while (1) {
        va_list copy;
        va_copy(copy, args);
        const int length = p_vsnprintf(buffer, capacity, format, copy);
        va_end(copy);

        if (length >= 0 && (size_t)length < capacity) {
            result = write_text(stream, buffer, (size_t)length) == 0 ? length : -1;
            break;
        }

        // POSIX returns the length that was needed, Windows a negative value: double until it fits
        size_t larger_capacity = length >= 0 ? (size_t)length + 1 : capacity * 2;
        if (larger_capacity > ((size_t)1 << 24)) {
            errno = EOVERFLOW;
            break;
        }
        if (buffer != stack_buffer) {
            free(buffer);
        }
        buffer = malloc(larger_capacity * sizeof(PChar));
        if (buffer == NULL) {
            return -1;
        }
        capacity = larger_capacity;
    }

    if (buffer != stack_buffer) {
        free(buffer);
    }
    return result;
}

int p_printf(const PChar *format, ...) {
    va_list args;
    va_start(args, format);
    const int result = print_formatted(OUT_STDOUT, format, args);
    va_end(args);
    return result;
}

int p_eprintf(const PChar *format, ...) {
    va_list args;
    va_start(args, format);
    const int result = print_formatted(OUT_STDERR, format, args);
    va_end(args);
    return result;
}

int p_write(OutStream stream, const PChar *text) {
    return write_text(stream, text, p_strlen(text));
}

int p_take_write_error(OutStream stream) {
    const int failed = write_failed[stream];
    write_failed[stream] = 0;
    return failed;
}
