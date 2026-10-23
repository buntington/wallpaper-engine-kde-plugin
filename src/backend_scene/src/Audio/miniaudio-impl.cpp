#include "miniaudio-config.h"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio/miniaudio.h>
/* stb_vorbis implementation must come after the implementation of miniaudio. */
#undef STB_VORBIS_HEADER_ONLY
#include <miniaudio/extras/stb_vorbis.c>
