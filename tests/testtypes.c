#define _FILE_OFFSET_BITS 64
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <time.h>      /* for time_t */
#include <sys/types.h> /* for off_t */

#define TYPE_SIGNED(t) (! ((t) 0 < (t) -1))
#define TYPE_MAX(t) \
  ((t) (! TYPE_SIGNED (t) \
        ? (t) -1 \
        : ~ (~ (t) 0 << (sizeof (t) * CHAR_BIT - 1))))

int main(void)
{
    uint32_t uint32=0xffffFFFF;
    uint64_t uint64=0xFFFFFFFFFFFFFFFFLL;
    off_t offset=TYPE_MAX(off_t); /* Depends on _FILE_OFFSET_BITS */
    size_t size=TYPE_MAX(size_t); /* Depends on int size */

    printf("native int bits%20u %16x\n", sizeof(int)*CHAR_BIT, UINT_MAX);
    printf("uint32_t max   %20"PRIu32" %16"PRIx32"\n"
           "uint64_t max   %20"PRIu64" %16"PRIx64"\n"
           "off_t max      %20jd %16jx\n" /* try PRIdMAX if %jd unsupported */
           "size_t max     %20zu %16zx\n",
           uint32, uint32,
           uint64, uint64,
           (intmax_t)offset, (intmax_t)offset,
           size, size);

    return 0;
}
