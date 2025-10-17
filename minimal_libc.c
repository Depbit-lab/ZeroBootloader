#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) {
        p++;
    }
    return (size_t)(p - s);
}

int strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb) {
            return ca - cb;
        }
        if (ca == '\0') {
            return 0;
        }
    }
    return 0;
}

static int is_delim(char c, const char *delim)
{
    while (*delim) {
        if (c == *delim) {
            return 1;
        }
        delim++;
    }
    return 0;
}

char *strtok(char *str, const char *delim)
{
    static char *save;
    if (str) {
        save = str;
    } else if (!save) {
        return NULL;
    }

    /* Skip leading delimiters */
    while (*save && is_delim(*save, delim)) {
        save++;
    }
    if (*save == '\0') {
        save = NULL;
        return NULL;
    }

    char *token_start = save;
    while (*save && !is_delim(*save, delim)) {
        save++;
    }
    if (*save) {
        *save++ = '\0';
    } else {
        save = NULL;
    }
    return token_start;
}

unsigned long strtoul(const char *nptr, char **endptr, int base)
{
    const char *p = nptr;
    unsigned long result = 0;
    int negative = 0;

    /* Skip leading whitespace */
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '\f' || *p == '\v') {
        p++;
    }

    if (*p == '+') {
        p++;
    } else if (*p == '-') {
        negative = 1;
        p++;
    }

    if (base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            base = 16;
            p += 2;
        } else if (p[0] == '0') {
            base = 8;
            p += 1;
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
        }
    }

    while (*p) {
        int digit;
        if (*p >= '0' && *p <= '9') {
            digit = *p - '0';
        } else if (*p >= 'a' && *p <= 'f') {
            digit = *p - 'a' + 10;
        } else if (*p >= 'A' && *p <= 'F') {
            digit = *p - 'A' + 10;
        } else {
            break;
        }
        if (digit >= base) {
            break;
        }
        result = result * (unsigned long)base + (unsigned long)digit;
        p++;
    }

    if (endptr) {
        *endptr = (char *)p;
    }

    if (negative) {
        return (unsigned long)(-(long)result);
    }
    return result;
}

void *memset(void *dest, int value, size_t count)
{
    unsigned char *d = (unsigned char *)dest;
    for (size_t i = 0; i < count; i++) {
        d[i] = (unsigned char)value;
    }
    return dest;
}

static int append_char(char **buf, size_t *remaining, char c)
{
    if (*remaining > 1) {
        **buf = c;
        (*buf)++;
        (*remaining)--;
        return 0;
    } else if (*remaining == 1) {
        **buf = '\0';
        *remaining = 0;
        return -1;
    }
    return -1;
}

static int append_string(char **buf, size_t *remaining, const char *s)
{
    while (*s) {
        if (append_char(buf, remaining, *s++) != 0) {
            return -1;
        }
    }
    return 0;
}

static int append_unsigned(char **buf, size_t *remaining, unsigned int value)
{
    char tmp[10];
    int idx = 0;
    if (value == 0) {
        tmp[idx++] = '0';
    } else {
        while (value > 0 && idx < (int)sizeof(tmp)) {
            tmp[idx++] = (char)('0' + (value % 10));
            value /= 10;
        }
    }
    for (int i = idx - 1; i >= 0; i--) {
        if (append_char(buf, remaining, tmp[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int append_signed(char **buf, size_t *remaining, int value)
{
    if (value < 0) {
        if (append_char(buf, remaining, '-') != 0) {
            return -1;
        }
        return append_unsigned(buf, remaining, (unsigned int)(-value));
    }
    return append_unsigned(buf, remaining, (unsigned int)value);
}

static int mini_vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
    char *buf = str;
    size_t remaining = size;

    while (*format) {
        if (*format != '%') {
            if (append_char(&buf, &remaining, *format++) != 0) {
                break;
            }
            continue;
        }
        format++;
        if (*format == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) {
                s = "(null)";
            }
            if (append_string(&buf, &remaining, s) != 0) {
                break;
            }
        } else if (*format == 'd') {
            int v = va_arg(ap, int);
            if (append_signed(&buf, &remaining, v) != 0) {
                break;
            }
        } else if (*format == 'u') {
            unsigned int v = va_arg(ap, unsigned int);
            if (append_unsigned(&buf, &remaining, v) != 0) {
                break;
            }
        } else if (*format == 'c') {
            int v = va_arg(ap, int);
            if (append_char(&buf, &remaining, (char)v) != 0) {
                break;
            }
        } else if (*format == '%') {
            if (append_char(&buf, &remaining, '%') != 0) {
                break;
            }
        } else {
            /* Unsupported specifier; output literally */
            if (append_char(&buf, &remaining, '%') != 0) {
                break;
            }
            if (append_char(&buf, &remaining, *format) != 0) {
                break;
            }
        }
        format++;
    }

    if (size > 0) {
        if (remaining > 0) {
            *buf = '\0';
        } else {
            str[size - 1] = '\0';
        }
    }

    return (int)(buf - str);
}

int snprintf(char *str, size_t size, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int written = mini_vsnprintf(str, size, format, ap);
    va_end(ap);
    return written;
}

