#ifndef UI_LOGO_RASTER_H
#define UI_LOGO_RASTER_H

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <vector>

/*
 * Framework-free TLEscope logo rasterizer.
*/

namespace logo_raster {

constexpr float kViewBox = 325.0f;
constexpr int   kSuperSample = 4;
constexpr int   kCurveSteps = 12;

struct V2 {
    float x, y;
};

/* centre layer: gradient fill */
inline const char *kCenterPath =
    "m194.88 47.62c12.44-12.58 23.97-23.71 25.62-24.74 1.65-1.03 3.9-1.89 5-1.9 1.1-0.02 3.8 0.69 6 1.58 "
    "2.88 1.16 12 9.6 32.69 30.27 20.92 20.91 29.23 29.89 30.72 33.17 1.13 2.47 2.05 5.63 2.05 7 0 1.37-0.89 "
    "4.41-1.98 6.75-1.3 2.78-9.48 11.82-23.73 26.18-17.15 17.29-22.81 22.4-26.75 24.12-3.65 1.59-6.08 2.02-9 "
    "1.58q-4-0.59-9-4.78c-2.75-2.3-9.39-8.5-24.51-23.35l-19.84 20 12.46 13c6.85 7.15 13.11 14.46 13.92 16.25 "
    "0.81 1.79 1.47 4.82 1.47 6.75 0 1.93-0.68 5.07-1.5 7-0.82 1.93-6.34 8.25-12.25 14.06-5.91 5.81-12.55 "
    "11.43-14.75 12.49-2.2 1.06-5.46 1.93-7.25 1.94-1.79 0-4.82-0.67-6.75-1.49-1.93-0.82-9.15-7.01-28.6-26l-20.4 "
    "20.36 12.41 12.82c6.82 7.05 13.23 14.17 14.25 15.82 1.01 1.65 1.83 4.46 1.84 6.25 0 1.79-0.9 5.05-2 "
    "7.25-1.12 2.23-12.08 14.27-24.83 27.25-16.58 16.89-23.96 23.73-27 25q-4.17 1.75-7.92 1.15c-3.3-0.53-7.38-4.24-33.86-30.75-19.09-19.11-30.9-31.71-32.25-34.4-1.18-2.34-2.14-5.26-2.14-6.5 "
    "0.01-1.24 0.94-3.82 2.09-5.75 1.15-1.93 12.39-13.74 25-26.25 13.45-13.35 24.36-23.37 26.41-24.25 1.93-0.82 "
    "4.96-1.48 6.75-1.45 1.79 0.02 4.48 0.58 5.99 1.25 1.51 0.66 8.44 6.6 15.4 13.2 6.96 6.6 12.92 12.12 "
    "13.26 12.26 0.33 0.14 5.04-4.14 20.31-19.26l-11.78-12.5c-6.48-6.87-12.6-13.85-13.6-15.5-1-1.65-1.82-4.91-1.83-7.25 "
    "0-2.34 0.66-5.71 1.46-7.5 0.81-1.79 6.88-8.76 13.5-15.5 10.07-10.24 12.65-12.35 15.79-12.87 2.06-0.35 "
    "5.55-0.32 7.75 0.06 2.2 0.37 5.57 1.87 7.5 3.33 1.93 1.46 8.34 7.28 25 23.23l19.98-19.5-12.67-13c-6.97-7.15-13.26-14.46-13.99-16.25-0.72-1.79-1.32-4.94-1.32-7 0.01-2.06 0.97-5.33 2.14-7.25 1.17-1.92 12.3-13.79 24.74-26.38zm31.97-12.62c-1.37 0-5.53 3.34-11.49 9.25l-9.33 9.25c57.52 58.59 59.6 60.49 61.47 59.2 1.1-0.75 4.32-3.8 7.16-6.78 2.84-2.98 6.55-7.11 8.25-9.17 "
    "2.49-3.03 2.9-4.14 2.09-5.75-0.55-1.1-10.79-11.97-22.75-24.15-11.96-12.18-24.34-24.33-27.5-27-3.16-2.67-6.72-4.85-7.9-4.85zm-26.99 26.99c-1.28-0.68-3.65 1.13-10.85 8.32-8.54 8.51-9.17 9.41-8.54 12.19 "
    "0.49 2.15 8.2 10.53 27.36 29.71 23.06 23.1 27.08 26.72 29.67 26.72 2.44-0.01 4.72-1.72 12.28-9.22l9.28-9.21c-44.61-44.66-58.3-58.02-59.2-58.51zm-59.87 59.01c-0.55 0-6.17 5.01-12.49 11.13-6.54 6.34-11.49 11.94-11.49 13-0.01 1.1 9.8 11.84 23.74 26.01 13.06 13.28 25.89 25.88 28.5 28 2.61 2.13 5.43 3.86 6.25 3.86 0.82 0 3.86-2.11 6.75-4.69 2.89-2.58 8.06-7.76 11.5-11.5 5.3-5.77 6.1-7.11 5.25-8.81-0.55-1.1-9.66-10.84-20.25-21.64-10.59-10.8-23.19-23.17-28-27.5-4.81-4.32-9.2-7.86-9.76-7.86zm-63.47 64c-0.82 0-5.88 4.25-11.25 9.45l-9.77 9.44 60.47 60.61c8.72-8.52 13-13.14 15.14-15.75 "
    "2.14-2.61 3.87-5.54 3.85-6.5-0.03-0.96-9.82-11.59-21.75-23.62-11.94-12.03-24.75-24.52-28.46-27.75-3.71-3.24-7.42-5.88-8.23-5.88zm-27.58 26c-0.31 0-5.01 4.44-10.45 9.86-5.43 5.42-9.79 10.37-9.68 11 0.1 0.63 6.38 7.48 13.94 15.22 7.56 7.75 19.15 19.43 25.75 25.96 6.6 6.53 13.58 13.02 15.5 14.43 1.93 1.41 4.06 2.56 4.75 2.55 0.69-0.01 5.64-4.29 11-9.51l9.75-9.49c-46.5-46.52-60.25-60.02-60.56-60.02zm206.25 7.29c0.61-3.42 1.3-6.68 1.51-7.25 0.22-0.57 2.74-1.05 10.8-1.06l-0.81 6.76c-0.44 3.72-1.94 10.14-3.34 14.26-1.39 4.13-4.03 9.75-5.86 12.5-1.83 2.75-6.18 7.74-9.66 11.08-3.95 3.8-9.15 7.47-13.83 9.78-4.13 2.03-9.97 4.35-13 5.15-3.03 0.81-7.64 1.47-10.25 1.48-4.62 0.01-4.76-0.08-5.12-3.24-0.21-1.79-0.03-4.15 0.38-5.25 0.57-1.53 2.37-2.3 7.62-3.29 3.78-0.71 9.57-2.54 12.87-4.06 3.66-1.69 8.86-5.47 13.34-9.71 5.4-5.1 8.26-8.8 10.79-13.94 1.89-3.85 3.94-9.8 4.56-13.21zm36.34 12.71c0.91-3.57 1.9-9.76 2.2-13.75l0.54-7.25 10.23 0.01-0.32 9.24c-0.18 5.09-0.99 11.95-1.81 15.25-0.81 3.3-2.84 9.38-4.51 13.5-1.67 4.13-4.75 10.43-6.86 14-2.11 3.57-6.89 9.88-10.62 14-3.73 4.13-9.28 9.64-12.33 12.26-3.05 2.62-10.28 7.2-16.05 10.18-5.78 2.97-13.88 6.31-18 7.42-4.13 1.11-11.78 2.52-17 3.14-5.22 0.62-9.86 0.76-10.3 0.31-0.43-0.45-1.05-2.83-1.92-9.81l8.36-1.34c4.6-0.74 13.08-2.77 18.86-4.51 6.15-1.86 13.4-4.91 17.5-7.39 3.85-2.33 10.15-6.97 14-10.32 3.85-3.34 9.41-9.31 12.37-13.26 2.95-3.95 7.31-11.23 9.69-16.18 2.37-4.95 5.06-11.93 5.97-15.5z";

/* edge accents: solid white */
inline const char *kEdgePath =
    "m216.02 3.2c-2.71 1.39-12.72 10.41-25.3 22.8l-20.81 20.5 0.09 10.5c36.81-35.81 48.51-46.61 49.75-47.11 "
    "1.24-0.49 4.27-0.89 6.75-0.89 2.47 0 5.74 0.49 7.25 1.08 1.51 0.6 7.37 5.66 13.02 11.25l10.27 10.17-0.04-12.5c-12.01-10.85-17.07-14.77-19-15.71-1.94-0.95-6.61-1.86-10.5-2.05-5.78-0.29-7.78 0.06-11.48 1.96zm-95.52 287.51c-13.81 13.57-23.49 22.3-26 23.44-2.2 0.99-5.13 1.81-6.5 1.82-1.38 0.01-4.19-0.43-6.25-0.98-2.67-0.7-6.78-3.88-14.25-10.99l-10.5-10v11c12.44 11.24 17.73 15.4 19.77 16.5 2.71 1.45 5.64 2 10.73 2 5.76 0 7.8-0.47 11.5-2.64 2.47-1.45 13.84-11.68 25.25-22.75l20.75-20.11c0-7.46-0.43-8.99-1.25-8.96-0.69 0.03-11.15 9.78-23.25 21.67z";

/*
 * Minimal SVG path flattener. Supports the commands used by the logo
 * (M, L, H, V, C, Q, S, T, Z in both absolute and relative form).
 */
inline void FlattenPath(const char *d, std::vector<std::vector<V2>> &out)
{
    const char *p = d;
    V2 cur{0.0f, 0.0f}, start{0.0f, 0.0f}, lastCtrl{0.0f, 0.0f};
    char cmd = 0;
    std::vector<V2> poly;

    auto isSep = [](char c) {
        return c == ' ' || c == ',' || c == '\t' || c == '\n' || c == '\r';
    };
    auto skip = [&]() {
        while (*p && isSep(*p))
            ++p;
    };
    auto number = [&](float &v) -> bool {
        skip();
        char *end = nullptr;
        v = std::strtof(p, &end);
        if (end == p)
            return false;
        p = end;
        return true;
    };
    auto flush = [&]() {
        if (poly.size() > 1)
            out.push_back(poly);
        poly.clear();
    };

    while (true) {
        skip();
        if (!*p)
            break;
        if (std::isalpha(static_cast<unsigned char>(*p)))
            cmd = *p++;

        const bool rel = (cmd >= 'a' && cmd <= 'z');
        const char c = rel ? static_cast<char>(cmd - 'a' + 'A') : cmd;
        const V2 base = rel ? cur : V2{0.0f, 0.0f};
        auto point = [&](float x, float y) -> V2 { return V2{base.x + x, base.y + y}; };

        switch (c) {
        case 'M': {
            float x, y;
            if (!number(x) || !number(y))
                return;
            flush();
            cur = point(x, y);
            start = cur;
            poly.push_back(cur);
            /* implicit lineto for any following coordinate pairs */
            cmd = rel ? 'l' : 'L';
            break;
        }
        case 'L': {
            float x, y;
            if (!number(x) || !number(y))
                return;
            cur = point(x, y);
            poly.push_back(cur);
            break;
        }
        case 'H': {
            float x;
            if (!number(x))
                return;
            cur = V2{base.x + x, cur.y};
            poly.push_back(cur);
            break;
        }
        case 'V': {
            float y;
            if (!number(y))
                return;
            cur = V2{cur.x, base.y + y};
            poly.push_back(cur);
            break;
        }
        case 'C': {
            float x1, y1, x2, y2, x, y;
            if (!number(x1) || !number(y1) || !number(x2) || !number(y2) || !number(x) || !number(y))
                return;
            const V2 p0 = cur, p1 = point(x1, y1), p2 = point(x2, y2), p3 = point(x, y);
            for (int i = 1; i <= kCurveSteps; ++i) {
                const float t = static_cast<float>(i) / kCurveSteps, mt = 1.0f - t;
                poly.push_back(V2{mt * mt * mt * p0.x + 3 * mt * mt * t * p1.x + 3 * mt * t * t * p2.x + t * t * t * p3.x,
                                  mt * mt * mt * p0.y + 3 * mt * mt * t * p1.y + 3 * mt * t * t * p2.y + t * t * t * p3.y});
            }
            lastCtrl = p2;
            cur = p3;
            break;
        }
        case 'Q': {
            float x1, y1, x, y;
            if (!number(x1) || !number(y1) || !number(x) || !number(y))
                return;
            const V2 p0 = cur, p1 = point(x1, y1), p2 = point(x, y);
            for (int i = 1; i <= kCurveSteps; ++i) {
                const float t = static_cast<float>(i) / kCurveSteps, mt = 1.0f - t;
                poly.push_back(V2{mt * mt * p0.x + 2 * mt * t * p1.x + t * t * p2.x,
                                  mt * mt * p0.y + 2 * mt * t * p1.y + t * t * p2.y});
            }
            lastCtrl = p1;
            cur = p2;
            break;
        }
        case 'S': {
            float x2, y2, x, y;
            if (!number(x2) || !number(y2) || !number(x) || !number(y))
                return;
            const V2 p0 = cur, p1{2 * p0.x - lastCtrl.x, 2 * p0.y - lastCtrl.y};
            const V2 p2 = point(x2, y2), p3 = point(x, y);
            for (int i = 1; i <= kCurveSteps; ++i) {
                const float t = static_cast<float>(i) / kCurveSteps, mt = 1.0f - t;
                poly.push_back(V2{mt * mt * mt * p0.x + 3 * mt * mt * t * p1.x + 3 * mt * t * t * p2.x + t * t * t * p3.x,
                                  mt * mt * mt * p0.y + 3 * mt * mt * t * p1.y + 3 * mt * t * t * p2.y + t * t * t * p3.y});
            }
            lastCtrl = p2;
            cur = p3;
            break;
        }
        case 'T': {
            float x, y;
            if (!number(x) || !number(y))
                return;
            const V2 p0 = cur, p1{2 * p0.x - lastCtrl.x, 2 * p0.y - lastCtrl.y};
            const V2 p2 = point(x, y);
            for (int i = 1; i <= kCurveSteps; ++i) {
                const float t = static_cast<float>(i) / kCurveSteps, mt = 1.0f - t;
                poly.push_back(V2{mt * mt * p0.x + 2 * mt * t * p1.x + t * t * p2.x,
                                  mt * mt * p0.y + 2 * mt * t * p1.y + t * t * p2.y});
            }
            lastCtrl = p1;
            cur = p2;
            break;
        }
        case 'Z':
            flush();
            cur = start;
            break;
        default:
            return;
        }
    }
    flush();
}

inline void ScanFill(const std::vector<std::vector<V2>> &polys, int width, int height,
                     unsigned char *pixels, bool gradient)
{
    std::vector<float> crossings;
    for (int y = 0; y < height; ++y) {
        const float sy = (static_cast<float>(y) + 0.5f) / height * kViewBox;
        crossings.clear();
        for (const auto &poly : polys) {
            const size_t n = poly.size();
            for (size_t i = 0; i < n; ++i) {
                const V2 a = poly[i], b = poly[(i + 1) % n];
                if ((a.y <= sy && b.y > sy) || (b.y <= sy && a.y > sy)) {
                    const float t = (sy - a.y) / (b.y - a.y);
                    crossings.push_back(a.x + t * (b.x - a.x));
                }
            }
        }
        std::sort(crossings.begin(), crossings.end());
        for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
            int x0 = static_cast<int>(std::ceil(crossings[i] / kViewBox * width - 0.5f));
            int x1 = static_cast<int>(std::floor(crossings[i + 1] / kViewBox * width - 0.5f));
            if (x0 < 0)
                x0 = 0;
            if (x1 >= width)
                x1 = width - 1;
            for (int x = x0; x <= x1; ++x) {
                unsigned char *q = pixels + 4 * (y * width + x);
                if (gradient) {
                    /* left-to-right gradient across the 325-unit viewBox */
                    const float f = (static_cast<float>(x) + 0.5f) / width;
                    auto mix = [f](unsigned char a, unsigned char b) {
                        return static_cast<unsigned char>(a + (b - a) * f + 0.5f);
                    };
                    q[0] = mix(0xc0, 0xa2); /* #c046ff -> #a2e0ff */
                    q[1] = mix(0x46, 0xe0);
                    q[2] = mix(0xff, 0xff);
                } else {
                    q[0] = q[1] = q[2] = 255;
                }
                q[3] = 255;
            }
        }
    }
}

/*
 * Rasterize the logo into RGBA. Pixels outside the mark are fully transparent.
 */
inline void Render(int size, unsigned char *rgba)
{
    if (size < 1 || rgba == nullptr)
        return;

    const int ss = kSuperSample;
    const int sw = size * ss, sh = size * ss;

    std::vector<std::vector<V2>> edges, center;
    FlattenPath(kEdgePath, edges);
    FlattenPath(kCenterPath, center);

    /* supersampled scratch buffer, edges first then the gradient centre on top */
    std::vector<unsigned char> layer(static_cast<size_t>(sw) * sh * 4, 0);
    ScanFill(edges, sw, sh, layer.data(), false);
    ScanFill(center, sw, sh, layer.data(), true);

    /* box-filter down to the requested size (alpha-weighted for clean edges) */
    const int samples = ss * ss;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            unsigned int r = 0, g = 0, b = 0, a = 0;
            for (int by = 0; by < ss; ++by) {
                const unsigned char *row = layer.data() + static_cast<size_t>((y * ss + by) * sw + x * ss) * 4;
                for (int bx = 0; bx < ss; ++bx) {
                    const unsigned char *q = row + bx * 4;
                    const unsigned int alpha = q[3];
                    r += q[0] * alpha;
                    g += q[1] * alpha;
                    b += q[2] * alpha;
                    a += alpha;
                }
            }
            unsigned char *out = rgba + (y * size + x) * 4;
            out[0] = out[1] = out[2] = 0;
            if (a > 0) {
                out[0] = static_cast<unsigned char>(r / a);
                out[1] = static_cast<unsigned char>(g / a);
                out[2] = static_cast<unsigned char>(b / a);
            }
            out[3] = static_cast<unsigned char>(a / samples);
        }
    }
}

} // namespace logo_raster

#endif
