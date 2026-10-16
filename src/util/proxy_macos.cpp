#include "proxy_macos.h"

#include <CFNetwork/CFNetwork.h>
#include <CoreFoundation/CoreFoundation.h>

#include <cstring>
#include <string>
#include <vector>

namespace
{

std::string ToString(CFStringRef value, const char *fallback = "")
{
    if (!value)
        return fallback;

    CFIndex size =
        CFStringGetMaximumSizeForEncoding(CFStringGetLength(value), kCFStringEncodingUTF8) + 1;
    std::vector<char> buffer((size_t)size);
    if (!CFStringGetCString(value, buffer.data(), size, kCFStringEncodingUTF8))
        return fallback;

    return buffer.data();
}

std::string ErrorString(CFErrorRef error)
{
    CFStringRef description = CFErrorCopyDescription(error);
    std::string result = ToString(description, "unknown CFNetwork error");
    if (description)
        CFRelease(description);
    return result;
}

struct PacResult
{
    CFArrayRef proxies = nullptr;
    CFErrorRef error = nullptr;
    bool done = false;
};

void PacCallback(void *client, CFArrayRef proxies, CFErrorRef error)
{
    PacResult *result = static_cast<PacResult *>(client);
    if (proxies)
        result->proxies = (CFArrayRef)CFRetain(proxies);
    if (error)
        result->error = (CFErrorRef)CFRetain(error);
    result->done = true;
}

CFArrayRef ExecutePacURL(CFURLRef pac_url, CFURLRef target_url, std::string *error)
{
    PacResult result;
    CFStreamClientContext context = {0, &result, nullptr, nullptr, nullptr};
    CFRunLoopSourceRef source =
        CFNetworkExecuteProxyAutoConfigurationURL(pac_url, target_url, PacCallback, &context);
    if (!source)
    {
        *error = "could not start PAC evaluation";
        return nullptr;
    }

    CFRunLoopRef run_loop = CFRunLoopGetCurrent();
    CFRunLoopAddSource(run_loop, source, kCFRunLoopDefaultMode);

    // PAC evaluation is asynchronous and runs on the fetch worker thread.
    for (int i = 0; i < 100 && !result.done; ++i)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);

    CFRunLoopRemoveSource(run_loop, source, kCFRunLoopDefaultMode);
    if (!result.done)
        CFRunLoopSourceInvalidate(source);
    CFRelease(source);

    if (result.done && !result.error)
        return result.proxies;

    *error = result.done ? ErrorString(result.error) : "PAC evaluation timed out";
    if (result.error)
        CFRelease(result.error);
    if (result.proxies)
        CFRelease(result.proxies);
    return nullptr;
}

bool AppendProxies(CFArrayRef proxies,
                   CFURLRef target_url,
                   std::vector<std::string> *proxy_urls,
                   std::string *error)
{
    const CFIndex count = proxies ? CFArrayGetCount(proxies) : 0;
    for (CFIndex i = 0; i < count; ++i)
    {
        CFDictionaryRef proxy = (CFDictionaryRef)CFArrayGetValueAtIndex(proxies, i);
        if (!proxy || CFGetTypeID(proxy) != CFDictionaryGetTypeID())
            continue;

        CFStringRef type = (CFStringRef)CFDictionaryGetValue(proxy, kCFProxyTypeKey);
        if (!type)
            continue;

        if (CFEqual(type, kCFProxyTypeNone))
        {
            proxy_urls->push_back("");
            continue;
        }

        if (CFEqual(type, kCFProxyTypeAutoConfigurationURL))
        {
            CFURLRef pac_url =
                (CFURLRef)CFDictionaryGetValue(proxy, kCFProxyAutoConfigurationURLKey);
            if (!pac_url)
            {
                *error = "PAC configuration has no URL";
                return false;
            }

            CFArrayRef resolved = ExecutePacURL(pac_url, target_url, error);
            if (!resolved)
                return false;
            const bool ok = AppendProxies(resolved, target_url, proxy_urls, error);
            CFRelease(resolved);
            if (!ok)
                return false;
            continue;
        }

        const bool socks = CFEqual(type, kCFProxyTypeSOCKS);
        if (!socks && !CFEqual(type, kCFProxyTypeHTTP) && !CFEqual(type, kCFProxyTypeHTTPS))
            continue;

        CFStringRef host = (CFStringRef)CFDictionaryGetValue(proxy, kCFProxyHostNameKey);
        CFNumberRef port_value = (CFNumberRef)CFDictionaryGetValue(proxy, kCFProxyPortNumberKey);
        std::string host_string = ToString(host);
        int port = 0;
        if (host_string.empty() || !port_value ||
            !CFNumberGetValue(port_value, kCFNumberIntType, &port) ||
            port <= 0 || port > 65535)
        {
            continue;
        }

        if (host_string.find(':') != std::string::npos && host_string.front() != '[')
            host_string = "[" + host_string + "]";

        proxy_urls->push_back((socks ? "socks5h://" : "http://") +
                              host_string + ":" + std::to_string(port));
    }

    return true;
}

} // namespace

bool ResolveMacSystemProxies(const char *target_url,
                             std::vector<std::string> *proxy_urls,
                             std::string *error)
{
    if (!target_url || !*target_url || !proxy_urls || !error)
        return false;

    proxy_urls->clear();
    error->clear();

    CFURLRef url =
        CFURLCreateWithBytes(kCFAllocatorDefault,
                             (const UInt8 *)target_url,
                             (CFIndex)std::strlen(target_url),
                             kCFStringEncodingUTF8,
                             nullptr);
    if (!url)
    {
        *error = "invalid target URL";
        return false;
    }

    CFDictionaryRef settings = CFNetworkCopySystemProxySettings();
    if (!settings)
    {
        CFRelease(url);
        proxy_urls->push_back("");
        return true;
    }

    CFArrayRef proxies = CFNetworkCopyProxiesForURL(url, settings);
    CFRelease(settings);
    const bool ok = proxies && AppendProxies(proxies, url, proxy_urls, error);
    if (proxies)
        CFRelease(proxies);
    else
        *error = "could not read system proxy settings";
    CFRelease(url);

    if (ok && proxy_urls->empty())
        proxy_urls->push_back("");
    return ok;
}
