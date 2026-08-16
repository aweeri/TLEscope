/* compatibility stubs for C23 libc symbols referenced by prebuilt raylib.
   glibc < 2.38 does not provide __isoc23_* variants.
*/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>

extern "C" {

int __isoc23_sscanf(const char *s, const char *format, ...)
{
    int ret;
    va_list ap;
    va_start(ap, format);
    ret = vsscanf(s, format, ap);
    va_end(ap);
    return ret;
}

long int __isoc23_strtol(const char *nptr, char **endptr, int base)
{
    return (long int)strtoimax(nptr, endptr, base);
}

unsigned long int __isoc23_strtoul(const char *nptr, char **endptr, int base)
{
    return (unsigned long int)strtoumax(nptr, endptr, base);
}

long long int __isoc23_strtoll(const char *nptr, char **endptr, int base)
{
    return (long long int)strtoimax(nptr, endptr, base);
}

} // extern "C"