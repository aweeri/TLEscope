#ifndef UI_LOGO_H
#define UI_LOGO_H

#include <raylib.h>

/*
 * Built-in TLEscope logo.
 *
 * The mark is defined purely in code (see logo.cpp). It is rasterized on demand, so it stays crisp at any
 * size and needs no external image file at runtime or in the packages.
 */

/* Rasterize the logo at `size` x `size` pixels (RGBA, anti-aliased). */
Image LogoGenerateImage(int size);

/* Generate the logo and upload it as a GPU texture. */
Texture2D LogoLoadTexture(int size);

#endif
