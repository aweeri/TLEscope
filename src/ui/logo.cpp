#include "ui/logo.h"
#include "ui/logo_raster.h"

/*
 * The logo geometry and rasterizer live in ui/logo_raster.h, which has no
 * raylib dependency so the build-time Windows icon generator can reuse the
 * exact same pixels. This file only bridges that code to raylib types.
 */

Image LogoGenerateImage(int size)
{
    if (size < 1)
        size = 1;

    Image image = GenImageColor(size, size, BLANK);
    if (image.data == nullptr)
        return image;

    logo_raster::Render(size, static_cast<unsigned char *>(image.data));
    return image;
}

Texture2D LogoLoadTexture(int size)
{
    Image image = LogoGenerateImage(size);
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    return texture;
}
