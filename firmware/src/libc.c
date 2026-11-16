/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The few libc routines the compiler may call implicitly (struct copies,
 * zeroing). Freestanding: no other libc. */
/* Word fast paths (timing candidate 2026-10-27): identical results to the
 * original byte loops. memset aligns the head, stores whole words, then the
 * tail. memcpy uses words only when both pointers are 4-byte aligned and the
 * regions do not overlap; any overlap keeps the exact original forward byte
 * loop, so even out-of-contract overlapping callers see unchanged bytes. */
typedef unsigned int libc_word __attribute__((__may_alias__));
void *memset(void *d, int c, unsigned n)
{
    unsigned char *p = d;
    const unsigned char v = (unsigned char)c;
    while (n && ((unsigned long)p & 3u)) {
        *p++ = v;
        n--;
    }
    if (n >= 4u) {
        const unsigned int w = 0x01010101u * v;
        libc_word *q = (libc_word *)p;
        while (n >= 16u) {
            q[0] = w; q[1] = w; q[2] = w; q[3] = w;
            q += 4;
            n -= 16u;
        }
        while (n >= 4u) {
            *q++ = w;
            n -= 4u;
        }
        p = (unsigned char *)q;
    }
    while (n--)
        *p++ = v;
    return d;
}

void *memcpy(void *d, const void *s, unsigned n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    const unsigned long dp = (unsigned long)p, sp = (unsigned long)q;
    const int overlap = dp < sp + n && sp < dp + n;
    if (!overlap && ((dp | sp) & 3u) == 0u) {
        libc_word *pw = (libc_word *)p;
        const libc_word *qw = (const libc_word *)q;
        while (n >= 16u) {
            pw[0] = qw[0]; pw[1] = qw[1]; pw[2] = qw[2]; pw[3] = qw[3];
            pw += 4;
            qw += 4;
            n -= 16u;
        }
        while (n >= 4u) {
            *pw++ = *qw++;
            n -= 4u;
        }
        p = (unsigned char *)pw;
        q = (const unsigned char *)qw;
    }
    while (n--)
        *p++ = *q++;
    return d;
}

int memcmp(const void *a, const void *b, unsigned n)
{
    const unsigned char *p = a, *q = b;
    for (; n; n--, p++, q++)
        if (*p != *q)
            return *p - *q;
    return 0;
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static unsigned str_len(const char *s)
{
    unsigned n = 0;
    while (s[n])
        n++;
    return n;
}

static void str_cpy(char *d, const char *s, unsigned max)
{
    unsigned i = 0;
    while (s[i] && i + 1 < max) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}

/* decimal with an optional fixed point: fmt_fix(buf, -63, 1) -> "-6.3" */
static void fmt_fix(char *b, int32_t v, int decimals)
{
    char t[12];
    int n = 0, neg = v < 0, i;
    uint32_t u = (uint32_t)(neg ? -v : v);
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
        if (n == decimals)
            t[n++] = '.';
    } while (u || n <= decimals);
    if (neg)
        t[n++] = '-';
    for (i = 0; i < n; i++)
        b[i] = t[n - 1 - i];
    b[n] = 0;
}

static void fmt_int(char *b, int32_t v) { fmt_fix(b, v, 0); }

static uint32_t rng_state = 0x1234567u;
static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
