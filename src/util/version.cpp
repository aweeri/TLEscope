#include "version.h"

#include <cctype>
#include <cstring>

namespace {

/* Parse the leading "MAJOR.MINOR.PATCH" from a "vX.Y.Z..." string.
 * Returns true and fills out[3] on success. Stops at the first non-numeric
 * character (e.g. '-' for a describe suffix). */
bool parse_tag(const char *s, int out[3])
{
    if (!s) return false;
    if (*s == 'v' || *s == 'V') ++s;

    int part = 0;
    int value = 0;
    bool any = false;
    for (const char *p = s; *p; ++p)
    {
        if (*p >= '0' && *p <= '9')
        {
            value = value * 10 + (*p - '0');
            any = true;
        }
        else if (*p == '.')
        {
            if (!any) return false;
            out[part++] = value;
            value = 0;
            any = false;
            if (part == 3) return true; /* stop after the third component */
        }
        else
        {
            break; /* '-' or anything else ends the tag */
        }
    }
    if (!any) return false;
    if (part == 2)
    {
        out[part] = value;
        return true;
    }
    return false;
}

} // namespace

int tlescope_version_compare(const char *a, const char *b)
{
    int av[3], bv[3];
    if (parse_tag(a, av) && parse_tag(b, bv))
    {
        for (int i = 0; i < 3; ++i)
        {
            if (av[i] < bv[i]) return -1;
            if (av[i] > bv[i]) return 1;
        }
        return 0;
    }
    return std::strcmp(a ? a : "", b ? b : "");
}