#ifndef CURL_DIAGNOSTICS_H
#define CURL_DIAGNOSTICS_H

#include <curl/curl.h>
#include "util/log.h"

static inline CURLcode TLEscopeCurlPerform(CURL *curl)
{
    char error[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);

    CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK)
    {
        const char *detail = error[0] ? error : curl_easy_strerror(result);
        LOG_ERROR("libcurl transport failure: %s (code %d)", detail, (int)result);
    }
    return result;
}

#define curl_easy_perform(handle) TLEscopeCurlPerform(handle)

#endif /* CURL_DIAGNOSTICS_H */
