#include <stdint.h>

#define max(a, b) ((a) >= (b) ? (a) : (b))
typedef char * * tc_comments_t;

/* Define the format specifier to use for uint64_t values. */
#ifndef PRIu64 /* Maybe <inttypes.h> already defined this. */
#if defined(_MSC_VER) || defined(__MINGW32__) /* Older versions of msvcrt.dll don't recognize %llu. */
#define PRIu64 "I64u"
#elif ULONG_MAX==0xffffffffffffffff
#define PRIu64 "lu"
#else
#define PRIu64 "llu"
#endif
#endif /* PRIu64 */

#define SOX_LIB_ERROR 399
#define SILENCE_DURATION "0.1"
#define SILENCE_THRESHOLD ".3%"

void show_stats(sox_format_t * in);
