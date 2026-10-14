#include "pstring.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

#include "common.h"

PChar *p_strdup(const PChar *text) {
    const size_t length = p_strlen(text);

    PChar *copy = malloc((length + 1) * sizeof(PChar));
    if (copy == NULL) {
        return NULL;
    }

    p_memcpy(copy, text, length + 1);
    return copy;
}

#ifndef PSTRING_IS_WIDE

const PChar *p_strerror(int error_number) {
    return strerror(error_number);
}

void p_perror(const PChar *text) {
    fprintf(stderr, "%s: %s" NEWLINE, text, strerror(errno));
}

PChar *p_from_utf8(const char *utf8) {
    return p_strdup(utf8);
}

char *p_to_utf8(const PChar *text) {
    return p_strdup(text);
}

#else

#define REPLACEMENT_CHARACTER 0xFFFD

/** Decodes one UTF-8 sequence, returns the code point and advances `*cursor` */
static unsigned long decode_utf8(const unsigned char **cursor) {
    const unsigned char *p = *cursor;
    const unsigned char lead = *p++;

    size_t continuation_count;
    unsigned long code_point;
    unsigned long minimum;

    if (lead < 0x80) {
        *cursor = p;
        return lead;
    } else if ((lead & 0xE0) == 0xC0) {
        continuation_count = 1;
        code_point = lead & 0x1F;
        minimum = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        continuation_count = 2;
        code_point = lead & 0x0F;
        minimum = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        continuation_count = 3;
        code_point = lead & 0x07;
        minimum = 0x10000;
    } else {
        *cursor = p;
        return REPLACEMENT_CHARACTER;
    }

    for (size_t i = 0; i < continuation_count; i++) {
        if ((*p & 0xC0) != 0x80) {
            *cursor = p; // Truncated sequence
            return REPLACEMENT_CHARACTER;
        }
        code_point = (code_point << 6) | (*p++ & 0x3F);
    }

    *cursor = p;
    if (code_point < minimum || code_point > 0x10FFFF || (code_point >= 0xD800 && code_point <= 0xDFFF)) {
        return REPLACEMENT_CHARACTER;
    }
    return code_point;
}

/** Stores a code point as one or two wchar_t units, returns how many */
static size_t store_wide(unsigned long code_point, wchar_t *out) {
    if (sizeof(wchar_t) > 2 || code_point < 0x10000) {
        if (out != NULL) {
            out[0] = (wchar_t)code_point;
        }
        return 1;
    }

    code_point -= 0x10000;
    if (out != NULL) {
        out[0] = (wchar_t)(0xD800 + (code_point >> 10));
        out[1] = (wchar_t)(0xDC00 + (code_point & 0x3FF));
    }
    return 2;
}

PChar *p_from_utf8(const char *utf8) {
    size_t units = 0;
    for (const unsigned char *p = (const unsigned char *)utf8; *p != '\0';) {
        units += store_wide(decode_utf8(&p), NULL);
    }

    PChar *result = malloc((units + 1) * sizeof(PChar));
    if (result == NULL) {
        return NULL;
    }

    size_t out = 0;
    for (const unsigned char *p = (const unsigned char *)utf8; *p != '\0';) {
        out += store_wide(decode_utf8(&p), result + out);
    }
    result[out] = L'\0';
    return result;
}

char *p_to_utf8(const PChar *text) {
    // Every unit becomes at most three bytes (a surrogate pair becomes four for two units)
    char *result = malloc(p_strlen(text) * 3 + 1);
    if (result == NULL) {
        return NULL;
    }

    size_t out = 0;
    for (const PChar *p = text; *p != L'\0'; p++) {
        unsigned long code_point = (unsigned long)*p;

        if (sizeof(wchar_t) == 2 && code_point >= 0xD800 && code_point <= 0xDBFF && p[1] != L'\0' &&
            (unsigned long)p[1] >= 0xDC00 && (unsigned long)p[1] <= 0xDFFF) {
            code_point = 0x10000 + ((code_point - 0xD800) << 10) + ((unsigned long)p[1] - 0xDC00);
            p++;
        } else if ((code_point >= 0xD800 && code_point <= 0xDFFF) || code_point > 0x10FFFF) {
            code_point = REPLACEMENT_CHARACTER; // A lone surrogate or an invalid value
        }

        if (code_point < 0x80) {
            result[out++] = (char)code_point;
        } else if (code_point < 0x800) {
            result[out++] = (char)(0xC0 | (code_point >> 6));
            result[out++] = (char)(0x80 | (code_point & 0x3F));
        } else if (code_point < 0x10000) {
            result[out++] = (char)(0xE0 | (code_point >> 12));
            result[out++] = (char)(0x80 | ((code_point >> 6) & 0x3F));
            result[out++] = (char)(0x80 | (code_point & 0x3F));
        } else {
            result[out++] = (char)(0xF0 | (code_point >> 18));
            result[out++] = (char)(0x80 | ((code_point >> 12) & 0x3F));
            result[out++] = (char)(0x80 | ((code_point >> 6) & 0x3F));
            result[out++] = (char)(0x80 | (code_point & 0x3F));
        }
    }
    result[out] = '\0';
    return result;
}

const PChar *p_strerror(int error_number) {
    static PChar message[256];

    PChar *converted = p_from_utf8(strerror(error_number));
    if (converted == NULL) {
        message[0] = L'\0';
        return message;
    }

    wcsncpy(message, converted, sizeof message / sizeof message[0] - 1);
    message[sizeof message / sizeof message[0] - 1] = L'\0';
    free(converted);
    return message;
}

void p_perror(const PChar *text) {
    p_fprintf(stderr, L"%ls: %ls" NEWLINE, text, p_strerror(errno));
}

/** Writes wide text, straight to the console when the stream is one, as UTF-8 otherwise */
static void write_wide(FILE *stream, const wchar_t *text, size_t length) {
    if (length == 0) {
        return;
    }

    const intptr_t handle = _get_osfhandle(_fileno(stream));
    DWORD mode;
    if (handle != -1 && handle != -2 && GetConsoleMode((HANDLE)handle, &mode)) {
        size_t done = 0;
        while (done < length) {
            DWORD written = 0;
            if (!WriteConsoleW((HANDLE)handle, text + done, (DWORD)(length - done), &written, NULL) ||
                written == 0) {
                return;
            }
            done += written;
        }
        return;
    }

    char *utf8 = p_to_utf8(text);
    if (utf8 == NULL) {
        return;
    }
    fwrite(utf8, 1, strlen(utf8), stream);
    free(utf8);
}

static int write_formatted(FILE *stream, const wchar_t *format, va_list args) {
    size_t capacity = 512;

    while (1) {
        wchar_t *buffer = malloc(capacity * sizeof(wchar_t));
        if (buffer == NULL) {
            return -1;
        }

        va_list copy;
        va_copy(copy, args);
        const int length = vswprintf(buffer, capacity, format, copy);
        va_end(copy);

        if (length >= 0) {
            write_wide(stream, buffer, (size_t)length);
            free(buffer);
            return length;
        }

        free(buffer);
        if (capacity >= ((size_t)1 << 24)) {
            return -1; // Not a matter of the buffer size
        }
        capacity *= 2;
    }
}

int p_printf(const PChar *format, ...) {
    va_list args;
    va_start(args, format);
    const int result = write_formatted(stdout, format, args);
    va_end(args);
    return result;
}

int p_fprintf(FILE *stream, const PChar *format, ...) {
    va_list args;
    va_start(args, format);
    const int result = write_formatted(stream, format, args);
    va_end(args);
    return result;
}

int p_fputs(const PChar *text, FILE *stream) {
    write_wide(stream, text, p_strlen(text));
    return ferror(stream) ? -1 : 0;
}

PChar *p_fgets(PChar *buffer, int size, FILE *stream) {
    if (size <= 0) {
        return NULL;
    }

    // Every UTF-8 sequence is at least as many bytes as units, so size - 1 bytes fit in the buffer
    char *bytes = malloc((size_t)size);
    if (bytes == NULL) {
        return NULL;
    }
    if (fgets(bytes, size, stream) == NULL) {
        free(bytes);
        return NULL;
    }

    PChar *wide = p_from_utf8(bytes);
    free(bytes);
    if (wide == NULL) {
        return NULL;
    }

    p_memcpy(buffer, wide, p_strlen(wide) + 1);
    free(wide);
    return buffer;
}

PInt p_getchar(void) {
    const int lead = getchar();
    if (lead == EOF) {
        return WEOF;
    }
    if (lead < 0x80) {
        return (PInt)lead;
    }

    const size_t continuation_count = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
    char bytes[5] = {(char)lead, 0, 0, 0, 0};
    size_t length = 1;
    for (size_t i = 0; i < continuation_count; i++) {
        const int next = getchar();
        if (next == EOF) {
            break;
        }
        if ((next & 0xC0) != 0x80) {
            ungetc(next, stdin);
            break;
        }
        bytes[length++] = (char)next;
    }

    PChar *wide = p_from_utf8(bytes);
    const PInt result = wide != NULL && wide[0] != L'\0' ? (PInt)wide[0] : (PInt)REPLACEMENT_CHARACTER;
    free(wide);
    return result;
}

#endif
