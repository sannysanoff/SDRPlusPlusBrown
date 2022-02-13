#pragma once

#if _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN 1
#endif
#include <Windows.h>
#endif

namespace wstr
{

#if _WIN32
    inline std::wstring str2wstr(std::string const &str)
    {
        int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
        std::wstring ret(len, '\0');
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), (LPWSTR)ret.data(), (int)ret.size());
        return ret;
    }

    inline std::string wstr2str(std::wstring const &str)
    {
        int len = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0, nullptr, nullptr);
        std::string ret(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.size(), (LPSTR)ret.data(), (int)ret.size(), nullptr, nullptr);
        return ret;
    }
#else
static inline std::string str2wstr(std::string const &str) {
        return str;
    }
    static inline std::string wstr2str(std::string const &str) {
        return str;
    }

#endif

}