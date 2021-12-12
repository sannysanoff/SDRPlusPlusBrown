#pragma once

#include <spdlog/spdlog.h>
#include <string>

inline bool hl2_error(std::string str) {
    spdlog::error("{0}", str.c_str());
    return false;
}