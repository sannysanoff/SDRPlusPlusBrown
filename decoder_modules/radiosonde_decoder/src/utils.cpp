#include "utils.hpp"
#include <core.h>
#include <cstdlib>

std::string getTempFile(const std::string& file) {
#ifdef _WIN32
    const char* env = getenv("TEMP");
    if (!env) { env = getenv("TMP"); }
    if (!env) { env = "."; }
    std::string dir(env);
    if (!dir.empty() && dir.back() != '\\' && dir.back() != '/') {
        dir += '\\';
    }
    return dir + file;
#elif defined(__ANDROID__)
    const char* env = getenv("TMPDIR");
    if (env && env[0]) {
        std::string dir(env);
        if (dir.back() != '/') { dir += '/'; }
        return dir + file;
    }
    std::string root = core::getRoot();
    if (!root.empty() && root.back() != '/') { root += '/'; }
    return root + file;
#else
    const char* env = getenv("TMPDIR");
    if (!env) { env = getenv("TMP"); }
    if (!env) { env = "/tmp"; }
    std::string dir(env);
    if (!dir.empty() && dir.back() != '/') {
        dir += '/';
    }
    return dir + file;
#endif
}
