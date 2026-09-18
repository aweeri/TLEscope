#ifndef CURL_DIAGNOSTICS_H
#define CURL_DIAGNOSTICS_H

#include <curl/curl.h>
#include "util/log.h"

#ifdef __cplusplus
inline long &TLEscopeCurlTimeoutSecondsStorage(void)
{
    static long timeout_seconds = 45L;
    return timeout_seconds;
}

inline void TLEscopeSetCurlTimeoutSeconds(long seconds)
{
    if (seconds < 15L) seconds = 15L;
    if (seconds > 300L) seconds = 300L;
    TLEscopeCurlTimeoutSecondsStorage() = seconds;
}

inline long TLEscopeGetCurlTimeoutSeconds(void)
{
    return TLEscopeCurlTimeoutSecondsStorage();
}
#endif

static inline CURLcode TLEscopeCurlPerform(CURL *curl)
{
    char error[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);

#ifdef __cplusplus
    const long timeout_seconds = TLEscopeGetCurlTimeoutSeconds();
#else
    const long timeout_seconds = 45L;
#endif
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);

    CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK)
    {
        const char *detail = error[0] ? error : curl_easy_strerror(result);
        double total_time = 0.0;
        double connect_time = 0.0;
        double tls_time = 0.0;
        long proxy_connect_code = 0;

        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_time);
        curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connect_time);
        curl_easy_getinfo(curl, CURLINFO_APPCONNECT_TIME, &tls_time);
        curl_easy_getinfo(curl, CURLINFO_HTTP_CONNECTCODE, &proxy_connect_code);

        LOG_ERROR(
            "libcurl transport failure: %s (code %d, total %.2fs, connect %.2fs, TLS %.2fs, proxy CONNECT %ld)",
            detail, (int)result, total_time, connect_time, tls_time, proxy_connect_code);
    }
    return result;
}

#define curl_easy_perform(handle) TLEscopeCurlPerform(handle)

#endif /* CURL_DIAGNOSTICS_H */
