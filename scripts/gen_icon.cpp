/*
 * gen_icon.cpp - build-time application icon generator.
 *
 * Rasterizes the hardcoded TLEscope logo (src/ui/logo_raster.h, the same code
 * the application renders with) into one of two container formats:
 *
 *   .ico  - multi-size Windows icon: 16, 32, 48, 64, 128 and 256 px as
 *           uncompressed 32-bpp BMP entries.
 *   .icns - macOS icon: PNG representations covering 16..1024 px
 *           (icp4/icp5/icp6, ic07-ic10, plus the @2x slots ic11-ic14).
 *
 * The PNG writer and its deflate compressor are self-contained: this tool is
 * built with the bare host compiler (no zlib, no image library) on every
 * platform the project builds or cross-compiles from.
 *
 * Usage: gen_icon [output.ico|output.icns]   (default: build/logo.ico)
 *        the format is picked from the file extension
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "ui/logo_raster.h"

namespace {

/* Sizes embedded in the .ico. 256 is encoded as 0 in the directory entry. */
constexpr int kIcoSizes[] = {16, 32, 48, 64, 128, 256};
constexpr int kIcoSizeCount = static_cast<int>(sizeof(kIcoSizes) / sizeof(kIcoSizes[0]));

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

void PutU32BE(std::vector<uint8_t> &b, uint32_t v)
{
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    b.push_back(static_cast<uint8_t>(v & 0xff));
}

/* ---------------------------------------------------------------- PNG ---- */

uint32_t Crc32(const uint8_t *data, size_t len)
{
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? 0xedb88320u ^ (c >> 1) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < len; ++i)
        crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

uint32_t Adler32(const uint8_t *data, size_t len)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

uint32_t ReverseBits(uint32_t code, int len)
{
    uint32_t out = 0;
    for (int i = 0; i < len; ++i) {
        out = (out << 1) | (code & 1u);
        code >>= 1;
    }
    return out;
}

/* DEFLATE fixed-Huffman literal/length codes (RFC 1951 section 3.2.6). */
void LiteralCode(int sym, uint32_t &code, int &len)
{
    if (sym < 144) {
        code = 0x30u + static_cast<uint32_t>(sym);
        len = 8;
    } else if (sym < 256) {
        code = 0x190u + static_cast<uint32_t>(sym - 144);
        len = 9;
    } else if (sym < 280) {
        code = static_cast<uint32_t>(sym - 256);
        len = 7;
    } else {
        code = 0xC0u + static_cast<uint32_t>(sym - 280);
        len = 8;
    }
    code = ReverseBits(code, len);
}

const uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10,  11,  13,  15,  17,  19,  23, 27,
                               31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                               2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1,    2,    3,    4,    5,     7,     9,     13,    17,    25,
                                33,   49,   65,   97,   129,   193,   257,   385,   513,   769,
                                1025, 1537, 2049, 3073, 4097,  6145,  8193,  12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2,  2,  3,  3,  4,  4,  5,  5,  6,
                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

struct BitWriter {
    std::vector<uint8_t> &out;
    uint32_t bits = 0;
    int count = 0;

    explicit BitWriter(std::vector<uint8_t> &o) : out(o) {}

    /* `code` must already be bit-reversed for Huffman codes */
    void Put(uint32_t code, int len)
    {
        bits |= (code & ((1u << len) - 1u)) << count;
        count += len;
        while (count >= 8) {
            out.push_back(static_cast<uint8_t>(bits & 0xffu));
            bits >>= 8;
            count -= 8;
        }
    }

    void Flush()
    {
        if (count > 0) {
            out.push_back(static_cast<uint8_t>(bits & 0xffu));
            bits = 0;
            count = 0;
        }
    }
};

/*
 * zlib stream (RFC 1950) wrapping a single fixed-Huffman DEFLATE block with a
 * greedy LZ77 match finder and a 32 KiB window.
 */
std::vector<uint8_t> ZlibFixed(const std::vector<uint8_t> &src)
{
    constexpr int kWindow = 32768;
    constexpr int kMinMatch = 3;
    constexpr int kMaxMatch = 258;
    constexpr int kMaxChain = 48;
    constexpr int kHashSize = 1 << 15;

    std::vector<uint8_t> out;
    out.reserve(src.size() / 2 + 64);
    out.push_back(0x78); /* CMF: deflate, 32 KiB window */
    out.push_back(0x01); /* FLG: no preset dictionary, fastest */

    {
        BitWriter bw(out);
        bw.Put(1, 1); /* BFINAL */
        bw.Put(1, 2); /* BTYPE = 01, fixed Huffman */

        const size_t n = src.size();
        std::vector<int32_t> head(static_cast<size_t>(kHashSize), -1);
        std::vector<int32_t> prev(n > 0 ? n : 1, -1);

        auto hash3 = [&](size_t p) -> uint32_t {
            const uint32_t h = (static_cast<uint32_t>(src[p]) << 10) ^
                               (static_cast<uint32_t>(src[p + 1]) << 5) ^
                               static_cast<uint32_t>(src[p + 2]);
            return h & (kHashSize - 1);
        };
        auto insert = [&](size_t p) {
            if (p + kMinMatch > n)
                return;
            const uint32_t h = hash3(p);
            prev[p] = head[h];
            head[h] = static_cast<int32_t>(p);
        };

        size_t pos = 0;
        while (pos < n) {
            int bestLen = 0;
            int bestDist = 0;

            if (pos + kMinMatch <= n) {
                const uint32_t h = hash3(pos);
                int32_t cand = head[h];
                int chain = kMaxChain;
                const int maxLen = static_cast<int>(std::min<size_t>(kMaxMatch, n - pos));
                while (cand >= 0 && static_cast<size_t>(pos) - static_cast<size_t>(cand) <=
                                        static_cast<size_t>(kWindow) &&
                       chain-- > 0) {
                    const size_t c = static_cast<size_t>(cand);
                    int len = 0;
                    while (len < maxLen && src[c + len] == src[pos + len])
                        ++len;
                    if (len > bestLen) {
                        bestLen = len;
                        bestDist = static_cast<int>(pos - c);
                        if (len >= maxLen)
                            break;
                    }
                    cand = prev[c];
                }
            }

            if (bestLen >= kMinMatch) {
                int li = 0;
                while (li < 28 && bestLen >= kLenBase[li + 1])
                    ++li;
                uint32_t code;
                int len;
                LiteralCode(257 + li, code, len);
                bw.Put(code, len);
                if (kLenExtra[li] > 0)
                    bw.Put(static_cast<uint32_t>(bestLen - kLenBase[li]), kLenExtra[li]);

                int di = 0;
                while (di < 29 && bestDist >= kDistBase[di + 1])
                    ++di;
                /* distance symbols are 5-bit codes, sent MSB first */
                bw.Put(ReverseBits(static_cast<uint32_t>(di), 5), 5);
                if (kDistExtra[di] > 0)
                    bw.Put(static_cast<uint32_t>(bestDist - kDistBase[di]), kDistExtra[di]);

                for (int k = 0; k < bestLen; ++k)
                    insert(pos + static_cast<size_t>(k));
                pos += static_cast<size_t>(bestLen);
            } else {
                uint32_t code;
                int len;
                LiteralCode(src[pos], code, len);
                bw.Put(code, len);
                insert(pos);
                ++pos;
            }
        }

        uint32_t code;
        int len;
        LiteralCode(256, code, len); /* end of block */
        bw.Put(code, len);
        bw.Flush();
    }

    PutU32BE(out, Adler32(src.data(), src.size()));
    return out;
}

/* One filter byte per row, then Sub-filtered (left-neighbour delta) RGBA. */
std::vector<uint8_t> PngScanlines(const unsigned char *rgba, int size)
{
    const size_t rowBytes = static_cast<size_t>(size) * 4u;
    std::vector<uint8_t> raw;
    raw.reserve((rowBytes + 1u) * static_cast<size_t>(size));
    for (int y = 0; y < size; ++y) {
        const unsigned char *row = rgba + static_cast<size_t>(y) * rowBytes;
        raw.push_back(1); /* filter type 1 = Sub */
        for (size_t x = 0; x < rowBytes; ++x) {
            const int left = (x >= 4u) ? row[x - 4u] : 0;
            raw.push_back(static_cast<uint8_t>(row[x] - left));
        }
    }
    return raw;
}

std::vector<uint8_t> EncodePng(const unsigned char *rgba, int size)
{
    std::vector<uint8_t> png;
    const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    png.insert(png.end(), signature, signature + 8);

    auto chunk = [&png](const char *type, const std::vector<uint8_t> &data) {
        PutU32BE(png, static_cast<uint32_t>(data.size()));
        const size_t crcStart = png.size();
        png.insert(png.end(), type, type + 4);
        png.insert(png.end(), data.begin(), data.end());
        PutU32BE(png, Crc32(png.data() + crcStart, png.size() - crcStart));
    };

    std::vector<uint8_t> ihdr;
    PutU32BE(ihdr, static_cast<uint32_t>(size));
    PutU32BE(ihdr, static_cast<uint32_t>(size));
    ihdr.push_back(8); /* bit depth */
    ihdr.push_back(6); /* colour type: truecolour with alpha */
    ihdr.push_back(0); /* compression: deflate */
    ihdr.push_back(0); /* filter method */
    ihdr.push_back(0); /* interlace: none */
    chunk("IHDR", ihdr);
    chunk("IDAT", ZlibFixed(PngScanlines(rgba, size)));
    chunk("IEND", {});
    return png;
}

std::vector<uint8_t> RenderRgba(int size)
{
    std::vector<uint8_t> rgba(static_cast<size_t>(size) * static_cast<size_t>(size) * 4u);
    logo_raster::Render(size, rgba.data());
    return rgba;
}

/* ----------------------------------------------------------------- ICO ---- */

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

std::vector<uint8_t> EncodeIco()
{
    std::vector<std::vector<uint8_t>> images;
    images.reserve(kIcoSizeCount);
    for (int i = 0; i < kIcoSizeCount; ++i) {
        const int s = kIcoSizes[i];
        const std::vector<uint8_t> rgba = RenderRgba(s);
        images.push_back(EncodeDib(rgba.data(), s));
    }

    /* header: reserved=0, type=1 (icon), count */
    std::vector<uint8_t> file;
    PutU16(file, 0);
    PutU16(file, 1);
    PutU16(file, static_cast<uint16_t>(kIcoSizeCount));

    /* directory entries; image data starts after the directory */
    uint32_t offset = 6u + 16u * static_cast<uint32_t>(kIcoSizeCount);
    for (int i = 0; i < kIcoSizeCount; ++i) {
        const int s = kIcoSizes[i];
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
    return file;
}

/* ---------------------------------------------------------------- ICNS ---- */

struct IcnsEntry {
    const char *type;
    int pixels;
};

/*
 * macOS 10.7+ icon representations. icp4/icp5/icp6 and ic07-ic10 are the 1x
 * ladder; ic11-ic14 are their @2x counterparts (ic11 = 16@2x ... ic14 =
 * 256@2x). Every slot carries a PNG so the file stays small.
 */
const IcnsEntry kIcnsEntries[] = {
    {"icp4", 16},   {"icp5", 32},  {"icp6", 64},   {"ic07", 128},
    {"ic08", 256},  {"ic09", 512}, {"ic10", 1024}, {"ic11", 32},
    {"ic12", 64},   {"ic13", 256}, {"ic14", 512},
};
constexpr int kIcnsCount = static_cast<int>(sizeof(kIcnsEntries) / sizeof(kIcnsEntries[0]));

std::vector<uint8_t> EncodeIcns()
{
    /* rasterize + PNG-encode each distinct pixel size once, reuse for @2x */
    std::vector<int> sizes;
    std::vector<std::vector<uint8_t>> pngs;
    auto pngFor = [&](int px) -> const std::vector<uint8_t> & {
        for (size_t i = 0; i < sizes.size(); ++i)
            if (sizes[i] == px)
                return pngs[i];
        const std::vector<uint8_t> rgba = RenderRgba(px);
        sizes.push_back(px);
        pngs.push_back(EncodePng(rgba.data(), px));
        return pngs.back();
    };

    std::vector<uint8_t> elements;
    for (int i = 0; i < kIcnsCount; ++i) {
        const std::vector<uint8_t> &png = pngFor(kIcnsEntries[i].pixels);
        elements.insert(elements.end(), kIcnsEntries[i].type, kIcnsEntries[i].type + 4);
        PutU32BE(elements, static_cast<uint32_t>(png.size() + 8u));
        elements.insert(elements.end(), png.begin(), png.end());
    }

    std::vector<uint8_t> file;
    file.insert(file.end(), {'i', 'c', 'n', 's'});
    PutU32BE(file, static_cast<uint32_t>(elements.size() + 8u));
    file.insert(file.end(), elements.begin(), elements.end());
    return file;
}

/* ----------------------------------------------------------------- IO ----- */

bool WriteFile(const char *path, const std::vector<uint8_t> &data)
{
    FILE *fp = std::fopen(path, "wb");
    if (!fp) {
        std::fprintf(stderr, "gen_icon: cannot open '%s' for writing\n", path);
        return false;
    }
    const size_t written = std::fwrite(data.data(), 1, data.size(), fp);
    std::fclose(fp);
    if (written != data.size()) {
        std::fprintf(stderr, "gen_icon: short write to '%s'\n", path);
        return false;
    }
    return true;
}

bool EndsWith(const char *s, const char *suffix)
{
    const size_t n = std::strlen(s), m = std::strlen(suffix);
    return n >= m && std::strcmp(s + n - m, suffix) == 0;
}

} // namespace

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "build/logo.ico";

    if (EndsWith(path, ".icns")) {
        const std::vector<uint8_t> file = EncodeIcns();
        if (!WriteFile(path, file))
            return 1;
        std::printf("gen_icon: wrote %s (%u bytes, %d representations)\n", path,
                    static_cast<unsigned>(file.size()), kIcnsCount);
        return 0;
    }

    if (EndsWith(path, ".ico")) {
        const std::vector<uint8_t> file = EncodeIco();
        if (!WriteFile(path, file))
            return 1;
        std::printf("gen_icon: wrote %s (%u bytes, %d sizes)\n", path,
                    static_cast<unsigned>(file.size()), kIcoSizeCount);
        return 0;
    }

    std::fprintf(stderr, "gen_icon: unknown output format for '%s' (expected .ico or .icns)\n", path);
    return 1;
}
