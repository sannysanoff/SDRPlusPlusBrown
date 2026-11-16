#pragma once

#include <cstdio>
#include "decode/common.hpp"

/**
 * Wrapper around a CSV file, containing PTU data as well as time and location data
 */
class PTUWriter {
public:
    PTUWriter() = default;
    ~PTUWriter() { deinit(); }

    bool init(const char* fname);
    void deinit();

    /**
     * Log a new point to file.
     *
     * @param data data to log
     */
    void addPoint(const SondeFullData* data);

private:
    FILE* m_fd = nullptr;
};
