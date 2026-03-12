/**
 * @file scinterm_wcwidth.h
 * @brief Unicode character width utilities for Scinterm NotCurses
 */
#ifndef SCINTERM_WCWIDTH_H
#define SCINTERM_WCWIDTH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Returns the number of columns needed for a Unicode codepoint (0, 1, or 2). */
int scinterm_wcwidth(uint32_t ucs);

/** Returns the number of columns needed for a UTF-8 string. */
int scinterm_wcswidth_utf8(const char *str, int len);

#ifdef __cplusplus
}
#endif

#endif /* SCINTERM_WCWIDTH_H */
