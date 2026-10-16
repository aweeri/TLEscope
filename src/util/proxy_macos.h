#pragma once

#include <string>
#include <vector>

bool ResolveMacSystemProxies(const char *target_url,
                             std::vector<std::string> *proxy_urls,
                             std::string *error);
