// pl_mpeg's implementation, compiled once (src/movie uses its demuxer and MPEG-1 video decoder).
// The header uses size_t and FILE without including their headers.
#include <stddef.h>
#include <stdio.h>
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
