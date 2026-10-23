#pragma once
// miniaudio settings shared by every file that uses it; the implementation
// is compiled once, in miniaudio-impl.cpp.
#define MA_NO_WASAPI
#define MA_NO_DSOUND
#define MA_NO_WINMM
#define MA_NO_COREAUDIO
#define MA_NO_ENCODING
#define STB_VORBIS_HEADER_ONLY
#include <miniaudio/extras/stb_vorbis.c> /* Enables Vorbis decoding. */
#include <miniaudio/miniaudio.h>
