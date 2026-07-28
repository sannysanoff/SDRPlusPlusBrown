#pragma once
#include <vector>
#include <string>
#include <stdint.h>
#include <mutex>
#include "sdrpp_export.h"

#define FORMAT_BUF_SIZE 16
#define ESCAPE_CHAR     '\\'

namespace flog {
    enum Type {
        TYPE_DEBUG,
        TYPE_INFO,
        TYPE_WARNING,
        TYPE_ERROR,
        _TYPE_COUNT
    };

    struct LogRec {
        int64_t ts;
        Type typ;
        std::string message;
    };

    extern std::mutex outMtx;
    extern std::vector<LogRec> logRecords;

    void setMemoryLogEnabled(bool enabled);
    bool isMemoryLogEnabled();


    // IO functions
    void __log__(Type type, const char* fmt, const std::vector<std::string>& args);

    // Conversion functions
    std::string __toString__(bool value);
    std::string __toString__(char value);
    std::string __toString__(int8_t value);
    std::string __toString__(int16_t value);
    std::string __toString__(int32_t value);
    std::string __toString__(int64_t value);
    std::string __toString__(uint8_t value);
    std::string __toString__(uint16_t value);
    std::string __toString__(uint32_t value);
    std::string __toString__(uint64_t value);
    std::string __toString__(float value);
    std::string __toString__(double value);
    std::string __toString__(const char* value);
    std::string __toString__(const void* value);
    std::string __toString__(void* value);
#ifdef __ANDROID__
    std::string __toString__(long long);
#endif

    template <class T>
    std::string __toString__(const T& value) {
        return (std::string)value;
    }

    // Utility to generate a list from arguments
    inline void __genArgList__(std::vector<std::string>& args) {}
    template <typename First, typename... Others>
    inline void __genArgList__(std::vector<std::string>& args, First first, Others... others) {
        // Add argument
        args.push_back(__toString__(first));

        // Recursive call that will be unrolled since the function is inline
        __genArgList__(args, others...);
    }

    // Formatting function
    std::string formatString(const char* fmt, const std::vector<std::string>& args);
    
    template <typename... Args>
    inline std::string format(const char* fmt, Args... args) {
        std::vector<std::string> _args;
        _args.reserve(sizeof...(args));
        __genArgList__(_args, args...);
        return formatString(fmt, _args);
    }

    // Logging functions
    template <typename... Args>
    void log(Type type, const char* fmt, Args... args) {
        std::string formatted = format(fmt, args...);
        __log__(type, formatted.c_str(), {});
    }

    template <typename... Args>
    inline void debug(const char* fmt, Args... args) {
        log(TYPE_DEBUG, fmt, args...);
    }

    template <typename... Args>
    inline void info(const char* fmt, Args... args) {
        log(TYPE_INFO, fmt, args...);
    }

    template <typename... Args>
    inline void warn(const char* fmt, Args... args) {
        log(TYPE_WARNING, fmt, args...);
    }

    template <typename... Args>
    inline void error(const char* fmt, Args... args) {
        log(TYPE_ERROR, fmt, args...);
    }
}
