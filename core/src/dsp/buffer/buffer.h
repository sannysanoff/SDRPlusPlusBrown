#pragma once

#ifndef __EMSCRIPTEN__
#include <volk/volk.h>
#endif

#include <module.h>

namespace dsp::buffer {

    SDRPP_EXPORT void _register_buffer_dbg(void **buffer, const char *info);
    SDRPP_EXPORT void _unregister_buffer_dbg(void *buffer);
    SDRPP_EXPORT void _trace_buffer_alloc(void *buffer);

    void runVerifier();

    template<class T>
    void register_buffer_dbg(T *&buffer, const char *info) {
        _register_buffer_dbg((void**)&buffer, info);
    }

    template<class T>
    inline T* alloc(int count) {
#ifndef __EMSCRIPTEN__
        auto rv = (T*)volk_malloc(count * sizeof(T), volk_get_alignment());
#else
        return (T*)malloc(count * sizeof(T));
#endif
        //_trace_buffer_alloc(rv);
        return rv;
    }

    template<class T>
    inline void clear(T* buffer, int count, int offset = 0) {
        memset(&buffer[offset], 0, count * sizeof(T));
    }

    inline void free(void* buffer) {
        //_unregister_buffer_dbg(buffer);
#ifndef __EMSCRIPTEN__
        volk_free(buffer);
#else
        return ::free(buffer);
#endif
    }


}