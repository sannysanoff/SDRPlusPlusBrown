#include "flog.h"
#include <mutex>
#include <chrono>
#include <string.h>
#include <inttypes.h>

#ifdef _WIN32
#include <Windows.h>
#endif

#ifdef __ANDROID__
#include <android/log.h>
#ifndef FLOG_ANDROID_TAG
#define FLOG_ANDROID_TAG    "flog"
#endif
#endif


namespace flog {

    std::mutex outMtx;
    std::vector<LogRec> logRecords;
    int performAdhocFileLogging = -1;
    static const char *adhocLogFileName = "/tmp/sdrpp.adhoc.log";

    const char* TYPE_STR[_TYPE_COUNT] = {
        "DEBUG",
        "INFO",
        "WARN",
        "ERROR"
    };


#ifdef _WIN32
#define COLOR_WHITE (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
    const WORD TYPE_COLORS[_TYPE_COUNT] = {
        FOREGROUND_GREEN | FOREGROUND_BLUE,
                FOREGROUND_GREEN | FOREGROUND_INTENSITY,                  // Bright green
                FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY, // Bright yellow
                FOREGROUND_RED | FOREGROUND_INTENSITY                     // Bright red
        };
#else
#define COLOR_WHITE "\x1B[0m"
    const char* TYPE_COLORS[_TYPE_COUNT] = {
        "\x1B[36m",
        "\x1B[32m",
        "\x1B[33m",
        "\x1B[31m",
    };
#endif

#ifdef __ANDROID__
    const android_LogPriority TYPE_PRIORITIES[_TYPE_COUNT] = {
        ANDROID_LOG_DEBUG,
        ANDROID_LOG_INFO,
        ANDROID_LOG_WARN,
        ANDROID_LOG_ERROR
    };
#endif

    std::string formatString(const char* fmt, const std::vector<std::string>& args) {
        // Reserve a buffer for the final output
        int argCount = args.size();
        int fmtLen = strlen(fmt) + 1;
        int totSize = fmtLen;
        for (const auto& a : args) { totSize += a.size(); }
        std::string out;
        out.reserve(totSize);
        
        // Parse format string
        bool escaped = false;
        int formatCounter = 0;
        bool inFormat = false;
        int formatLen = 0;
        char formatBuf[FORMAT_BUF_SIZE+1];
        for (int i = 0; i < fmtLen; i++) {
            // Get char
            const char c = fmt[i];

            // If this character is escaped, don't try to parse it
            if (escaped) {
                escaped = false;
                out += c;
                continue;
            }

            // State machine
            if (!inFormat && c != '{') {
                // Write to formatted output if not escape character
                if (c == ESCAPE_CHAR) {
                    escaped = true;
                }
                else {
                    out += c;
                }
            }
            else if (!inFormat) {
                // Start format mode
                inFormat = true;
            }
            else if (c == '}') {
                // Stop format mode
                inFormat = false;

                // Insert string value or error
                if (!formatLen) {
                    // Use format counter as ID if available or print wrong format string
                    if (formatCounter < argCount) {
                        out += args[formatCounter++];
                    }
                    else {
                        out += "{}";
                    }
                }
                else {
                    // Parse number
                    formatBuf[formatLen] = 0;
                    formatCounter = std::atoi(formatBuf);

                    // Use ID if available or print wrong format string
                    if (formatCounter < argCount) {
                        out += args[formatCounter];
                    }
                    else {
                        out += '{';
                        out += formatBuf;
                        out += '}';
                    }

                    // Increment format counter
                    formatCounter++;
                }

                // Reset format counter
                formatLen = 0;
            }
            else {
                // Add to format buffer 
                if (formatLen < FORMAT_BUF_SIZE) { formatBuf[formatLen++] = c; }
            }
        }
        return out;
    }

    void __log__(Type type, const char* fmt, const std::vector<std::string>& args) {
        std::string out = formatString(fmt, args);
        
        // Get output stream depending on type
        FILE* outStream = (type == TYPE_ERROR) ? stderr : stdout;

        // Get time
        auto now = std::chrono::system_clock::now();
        auto nowt = std::chrono::system_clock::to_time_t(now);
        long long msec = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();

        // Write to output
        {
            std::lock_guard<std::mutex> lck(outMtx);
            auto nowc = std::localtime(&nowt); // This is now threadsafe
#if defined(_WIN32)
            // Get output handle and return if invalid
            int wOutStream = (type == TYPE_ERROR) ? STD_ERROR_HANDLE  : STD_OUTPUT_HANDLE;
            HANDLE conHndl = GetStdHandle(wOutStream);
            if (!conHndl || conHndl == INVALID_HANDLE_VALUE) { return; }

            static CONSOLE_SCREEN_BUFFER_INFO console_info { 0 };
            static WORD bg = 0;
            if (console_info.wAttributes == 0) {
                GetConsoleScreenBufferInfo (conHndl, &console_info);
                bg = (console_info.wAttributes & ~7);
            }


            // Print beginning of log line
            SetConsoleTextAttribute(conHndl, bg | COLOR_WHITE);
            fprintf(outStream, "[%02d/%02d/%02d %02d:%02d:%02d.%03d] [", nowc->tm_mday, nowc->tm_mon + 1, nowc->tm_year + 1900, nowc->tm_hour, nowc->tm_min, nowc->tm_sec, 0);

            // Switch color to the log color, print log type and 
            SetConsoleTextAttribute(conHndl, bg | TYPE_COLORS[type]);
            fputs(TYPE_STR[type], outStream);
            

            // Switch back to default color and print rest of log string
            SetConsoleTextAttribute(conHndl, bg | COLOR_WHITE);
            fprintf(outStream, "] %s\n", out.c_str());
#elif defined(__ANDROID__)
            // Print format string
            __android_log_print(ANDROID_LOG_WARN, FLOG_ANDROID_TAG, "%s\n", out.c_str());
//            __android_log_print(TYPE_PRIORITIES[type], FLOG_ANDROID_TAG, COLOR_WHITE "[%02d/%02d/%02d %02d:%02d:%02d.%03d] [%s%s" COLOR_WHITE "] %s\n",
//                    nowc->tm_mday, nowc->tm_mon + 1, nowc->tm_year + 1900, nowc->tm_hour, nowc->tm_min, nowc->tm_sec, 0, TYPE_COLORS[type], TYPE_STR[type], out.c_str());
#else
            // Print format string
            fprintf(outStream, COLOR_WHITE "[%02d/%02d/%02d %02d:%02d:%02d.%03d] [%s%s" COLOR_WHITE "] %s\n",
                    nowc->tm_mday, nowc->tm_mon + 1, nowc->tm_year + 1900, nowc->tm_hour, nowc->tm_min, nowc->tm_sec, (int)(msec % 1000), TYPE_COLORS[type], TYPE_STR[type], out.c_str());
#endif

            if (performAdhocFileLogging == -1) {
                FILE *f = fopen(adhocLogFileName,"rt");
                if (f) {
                    fclose(f);
                    performAdhocFileLogging = 1;
                }
                else {
                    performAdhocFileLogging = 0;
                }
            }
            if (performAdhocFileLogging == 1) {
                FILE *f = fopen(adhocLogFileName,"at");
                fprintf(f, "[%02d/%02d/%02d %02d:%02d:%02d.%03d] [%s" "] %s\n",
                    nowc->tm_mday, nowc->tm_mon + 1, nowc->tm_year + 1900, nowc->tm_hour, nowc->tm_min, nowc->tm_sec, (int)(msec % 1000), TYPE_STR[type], out.c_str());
                fclose(f);
            }
            logRecords.emplace_back(LogRec{msec, type, out});
        }
    }

    std::string __toString__(bool value) {
        return value ? "true" : "false";
    }

    std::string __toString__(char value) {
        return std::string("")+value;
    }

    std::string __toString__(int8_t value) {
        char buf[8];
        snprintf(buf, sizeof buf, "%" PRId8, value);
        return buf;
    }

    std::string __toString__(int16_t value) {
        char buf[16];
        snprintf(buf, sizeof buf, "%" PRId16, value);
        return buf;
    }

    std::string __toString__(int32_t value) {
        char buf[32];
        snprintf(buf, sizeof buf, "%" PRId32, value);
        return buf;
    }

    std::string __toString__(int64_t value) {
        char buf[64];
        snprintf(buf, sizeof buf, "%" PRId64, value);
        return buf;
    }

    std::string __toString__(uint8_t value) {
        char buf[8];
        snprintf(buf, sizeof buf, "%" PRIu8, value);
        return buf;
    }

    std::string __toString__(uint16_t value) {
        char buf[16];
        snprintf(buf, sizeof buf, "%" PRIu16, value);
        return buf;
    }

    std::string __toString__(uint32_t value) {
        char buf[32];
        snprintf(buf, sizeof buf, "%" PRIu32, value);
        return buf;
    }

    std::string __toString__(uint64_t value) {
        char buf[64];
        snprintf(buf, sizeof buf, "%" PRIu64, value);
        return buf;
    }

    std::string __toString__(float value) {
        char buf[256];
        snprintf(buf, sizeof buf, "%f", value);
        return buf;
    }

    std::string __toString__(double value) {
        char buf[256];
        snprintf(buf, sizeof buf, "%lf", value);
        return buf;
    }

    std::string __toString__(const char* value) {
        return value;
    }

    std::string __toString__(const void* value) {
        char buf[32];
        snprintf(buf, sizeof buf, "0x%p", value);
        return buf;
    }
    std::string __toString__(void* value) {
        char buf[32];
        snprintf(buf, sizeof buf, "0x%p", value);
        return buf;
    }
}
