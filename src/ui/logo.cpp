#include "ui/logo.h"
#include "ui/logo_raster.h"

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
