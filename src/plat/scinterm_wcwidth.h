/**
 * @file scinterm_wcwidth.h
 * @brief Unicode character width utilities for Scinterm NotCurses
 * 
 * Features LRU caching for high-performance width lookups during rendering.
 * Cache hit rate is typically >99% for natural language text.
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

/** Clear the glyph width cache (call if terminal encoding changes). */
void scinterm_wcwidth_cache_clear(void);

/** Get cache statistics for debugging. */
void scinterm_wcwidth_cache_stats(int *hits, int *total);

#ifdef __cplusplus
}
#endif

#endif /* SCINTERM_WCWIDTH_H */
