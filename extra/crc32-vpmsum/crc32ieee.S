#ifdef __powerpc__

#define CONSTANTS .crc32_constants
#define SHORT_CONSTANTS .crc32_short_constants
#define BARRETT_CONSTANTS .crc32_barrett_constants

#include "crc32ieee_constants.h"

#define __F __crc32ieee_vpmsum

#include "crc32.iS"

#endif

