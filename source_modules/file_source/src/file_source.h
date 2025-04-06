
#pragma once

#include <string>

struct FileSourceInterface {
    virtual void openPath(const std::string &path) = 0;
};