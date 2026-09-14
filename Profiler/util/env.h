#pragma once

#include <cstdlib>
#include <optional>
#include <string>

namespace profiler::utils
{

inline std::optional<std::string> get_env(const char* name)
{
    const char* v = std::getenv(name);
    if (v == nullptr)
        return std::nullopt;
    return std::string(v);
}

inline bool check_env(const char* name)
{
    const char* v = std::getenv(name);
    return v != nullptr && v[0] != '\0' && v[0] != '0';
}

}  // namespace profiler::utils
