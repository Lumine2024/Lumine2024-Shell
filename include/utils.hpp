#pragma once

#include <cwctype>
#include <string>

namespace lumine_sh {

inline std::wstring widen_ascii(const std::string &value) {
    return std::wstring(value.begin(), value.end());
}

inline std::string narrow_ascii(const std::wstring &value) {
    return std::string(value.begin(), value.end());
}

inline std::wstring trim(const std::wstring &value) {
    size_t left = 0;
    while(left < value.size() && iswspace(value[left])) {
        ++left;
    }
    size_t right = value.size();
    while(right > left && iswspace(value[right - 1])) {
        --right;
    }
    return value.substr(left, right - left);
}

inline bool starts_with(const std::wstring &value, const std::wstring &prefix) {
    return value.rfind(prefix, 0) == 0;
}

inline bool ends_with(const std::wstring &value, const std::wstring &suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}
