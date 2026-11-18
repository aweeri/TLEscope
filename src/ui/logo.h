#ifndef UI_LOGO_H
#define UI_LOGO_H

#include <raylib.h>

/* Rasterize the logo at `size` x `size` pixels (RGBA, anti-aliased). */
Image LogoGenerateImage(int size);

/* Generate the logo and upload it as a GPU texture. */
Texture2D LogoLoadTexture(int size);

#endif
