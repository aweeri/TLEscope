/*
 * gen_icon.cpp - build-time Windows application icon generator.
 *
 * Rasterizes the hardcoded TLEscope logo (src/ui/logo_raster.h, the same code
 * the application uses) and writes a multi-size .ico containing uncompressed
 * 32-bpp BMP entries for 16, 32, 48, 64, 128 and 256 px.
 * 
 * Usage: gen_icon [output.ico]   (default: build/logo.ico)
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "ui/logo_raster.h"

namespace {

/* Sizes embedded in the icon. 256 is encoded as 0 in the directory entry. */
constexpr int kSizes[] = {16, 32, 48, 64, 128, 256};
constexpr int kSizeCount = static_cast<int>(sizeof(kSizes) / sizeof(kSizes[0]));

void PutU16(std::vector<uint8_t> &b, uint16_t v)
{
    b.push_back(static_cast<uint8_t>(v & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
}

void PutU32(std::vector<uint8_t> &b, uint32_t v)
{
    b.push_back(static_cast<uint8_t>(v & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

std::vector<uint8_t> EncodeDib(const unsigned char *rgba, int size)
{
    std::vector<uint8_t> out;

    const uint32_t xorStride = static_cast<uint32_t>(size) * 4u;       /* 32bpp: always 4-byte aligned */
    const uint32_t andStride = (static_cast<uint32_t>(size) + 31u) / 32u * 4u;
    const uint32_t xorBytes = xorStride * static_cast<uint32_t>(size);
    const uint32_t andBytes = andStride * static_cast<uint32_t>(size);

    PutU32(out, 40);                                   /* biSize          */
    PutU32(out, static_cast<uint32_t>(size));          /* biWidth         */
    PutU32(out, static_cast<uint32_t>(size) * 2u);     /* biHeight        */
    PutU16(out, 1);                                    /* biPlanes        */
    PutU16(out, 32);                                   /* biBitCount      */
    PutU32(out, 0);                                    /* biCompression   */
    PutU32(out, xorBytes + andBytes);                  /* biSizeImage     */
    PutU32(out, 0);                                    /* biXPelsPerMeter */
    PutU32(out, 0);                                    /* biYPelsPerMeter */
    PutU32(out, 0);                                    /* biClrUsed       */
    PutU32(out, 0);                                    /* biClrImportant  */

    /* XOR bitmap: bottom-up rows, BGRA */
    for (int y = size - 1; y >= 0; --y) {
        const unsigned char *row = rgba + static_cast<size_t>(y) * size * 4;
        for (int x = 0; x < size; ++x) {
            const unsigned char *p = row + x * 4;
            out.push_back(p[2]); /* B */
            out.push_back(p[1]); /* G */
            out.push_back(p[0]); /* R */
            out.push_back(p[3]); /* A */
        }
    }

    /* AND mask: all zero, alpha in the XOR bitmap drives transparency */
    out.insert(out.end(), andBytes, 0);
    return out;
}

} // namespace

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "build/logo.ico";

    /* rasterize each size once */
    std::vector<std::vector<uint8_t>> images;
    images.reserve(kSizeCount);
    for (int i = 0; i < kSizeCount; ++i) {
        const int s = kSizes[i];
        std::vector<uint8_t> rgba(static_cast<size_t>(s) * s * 4);
        logo_raster::Render(s, rgba.data());
        images.push_back(EncodeDib(rgba.data(), s));
    }

    /* header: reserved=0, type=1 (icon), count */
    std::vector<uint8_t> file;
    PutU16(file, 0);
    PutU16(file, 1);
    PutU16(file, static_cast<uint16_t>(kSizeCount));

    /* directory entries; image data starts after the directory */
    uint32_t offset = 6u + 16u * static_cast<uint32_t>(kSizeCount);
    for (int i = 0; i < kSizeCount; ++i) {
        const int s = kSizes[i];
        const uint32_t bytes = static_cast<uint32_t>(images[i].size());
        file.push_back(static_cast<uint8_t>(s >= 256 ? 0 : s)); /* width  (0 = 256) */
        file.push_back(static_cast<uint8_t>(s >= 256 ? 0 : s)); /* height (0 = 256) */
        file.push_back(0);                                      /* color count      */
        file.push_back(0);                                      /* reserved         */
        PutU16(file, 1);                                        /* planes           */
        PutU16(file, 32);                                       /* bit count        */
        PutU32(file, bytes);                                    /* bytes in resource*/
        PutU32(file, offset);                                   /* offset           */
        offset += bytes;
    }

    /* image payloads */
    for (const auto &img : images)
        file.insert(file.end(), img.begin(), img.end());

    FILE *fp = std::fopen(path, "wb");
    if (!fp) {
        std::fprintf(stderr, "gen_icon: cannot open '%s' for writing\n", path);
        return 1;
    }
    const size_t written = std::fwrite(file.data(), 1, file.size(), fp);
    std::fclose(fp);
    if (written != file.size()) {
        std::fprintf(stderr, "gen_icon: short write to '%s'\n", path);
        return 1;
    }

    std::printf("gen_icon: wrote %s (%u bytes, %d sizes)\n", path,
                static_cast<unsigned>(file.size()), kSizeCount);
    return 0;
}
