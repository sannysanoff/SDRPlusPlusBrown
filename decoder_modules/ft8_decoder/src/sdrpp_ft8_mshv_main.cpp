#include <core.h>
#include <iostream>
#include <stdio.h>
#include <chrono>
#include <module.h>
#include <ctm.h>
#include <fftw3.h>
#include <utils/wav.h>
#include <utils/riff.h>
#include "dsp/types.h"
#include "dsp/multirate/rational_resampler.h"

#define DO_WASMER

#include "ft8_etc/mshv_support.h"
#ifdef DO_WASMER
#include <wasmer.h>
#include <wasm.h>
#endif
#include <sys/fcntl.h>

#include "fftw_mshv_plug.h"
#include "fftw_mshv_plug_original.h"
#include <wasmedge/wasmedge.h>


bool isFileNewer(const std::string &file1, const std::string &file2) {
    std::filesystem::path path1(file1);
    std::filesystem::path path2(file2);

    if (!std::filesystem::exists(path1) || !std::filesystem::exists(path2)) {
        // One or both files don't exist
        return false;
    }

    auto time1 = std::filesystem::last_write_time(path1);
    auto time2 = std::filesystem::last_write_time(path2);

    return time1 > time2;
}

namespace ft8 {
    // input stereo samples, nsamples (number of pairs of float)
    void decodeFT8(int threads, const char *mode, int sampleRate, dsp::stereo_t *samples, int nsamples,
                   std::function<void(int mode, QStringList result)> callback);
}

struct WasmedgeFT8Decoder {
    static WasmEdge_ModuleInstanceContext *module;

    static WasmEdge_VMContext *VMCxt;
    static WasmEdge_ASTModuleContext *astModuleContext;


    struct WASMMemory {
        WasmEdge_MemoryInstanceContext *memctx;


        WASMMemory() {
        }

        WASMMemory(WasmEdge_MemoryInstanceContext *memctx) : memctx(memctx) {
        }

        uint8_t *ptr(int offset, int length) {
            auto wasm_edge_memory_instance_get_pointer = WasmEdge_MemoryInstanceGetPointer(memctx, offset, length);
            return wasm_edge_memory_instance_get_pointer;

        }

        void write_i32(size_t offset, int32_t value) {
            unsigned char mem[4];
            mem[0] = (uint8_t) (value & 0xFF);
            mem[1] = (uint8_t) ((value >> 8) & 0xFF);
            mem[2] = (uint8_t) ((value >> 16) & 0xFF);
            mem[3] = (uint8_t) ((value >> 24) & 0xFF);
            auto res = WasmEdge_MemoryInstanceSetData(memctx, mem, offset, 4);
            if (!WasmEdge_ResultOK(res)) {
                abort();
            }
        }

        void write_i64(size_t offset, int64_t value) {
            unsigned char mem[8];
            mem[0] = (uint8_t) (value & 0xFF);
            mem[+1] = (uint8_t) ((value >> 8) & 0xFF);
            mem[+2] = (uint8_t) ((value >> 16) & 0xFF);
            mem[+3] = (uint8_t) ((value >> 24) & 0xFF);
            mem[+4] = (uint8_t) ((value >> 32) & 0xFF);
            mem[+5] = (uint8_t) ((value >> 40) & 0xFF);
            mem[+6] = (uint8_t) ((value >> 48) & 0xFF);
            mem[+7] = (uint8_t) ((value >> 56) & 0xFF);
            auto res = WasmEdge_MemoryInstanceSetData(memctx, mem, offset, 8);
            if (!WasmEdge_ResultOK(res)) {
                abort();
            }
        }


        void put(size_t offset, const char *str, int len) {
            auto res = WasmEdge_MemoryInstanceSetData(memctx, (uint8_t*)str, offset, len);
            if (!WasmEdge_ResultOK(res)) {
                abort();
            }
        }

        void put_string(size_t offset, const char *str) {
            auto res = WasmEdge_MemoryInstanceSetData(memctx, (uint8_t*)str, offset, strlen(str)+1);
            if (!WasmEdge_ResultOK(res)) {
                abort();
            }
        }

        char *dump(size_t offset) {
            static char buf[10000];
            buf[0] = 0;
            for (int q = 0; q < 32; q++) {
                snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "%02x ", read_i8(offset + q));
            }
            return buf;
        }

        int32_t read_i32(size_t offset) {
            unsigned char mem[4];
            WasmEdge_MemoryInstanceGetData(memctx, mem, offset, 4);

            int32_t value = (int32_t) ((uint32_t) mem[0] |
                                       (((uint32_t) mem[ + 1]) << 8) |
                                       (((uint32_t) mem[ + 2]) << 16) |
                                       (((uint32_t) mem[ + 3]) << 24));
            return value;
        }

        int16_t read_i16(size_t offset) {
            unsigned char mem[2];
            WasmEdge_MemoryInstanceGetData(memctx, mem, offset, 4);
            int16_t value = (int16_t) ((uint16_t) mem[0] |
                                       (((uint16_t) mem[ + 1]) << 8));
            return value;
        }

        int8_t read_i8(size_t offset) {
            int8_t mem[1];
            WasmEdge_MemoryInstanceGetData(memctx, (uint8_t*)mem, offset, 4);
            return mem[0];
        }
    };

    WASMMemory memory;

    static bool setupModule(const std::string &wasmPath) {
        /* Create the VM context. */

        WasmEdge_Result result;
        auto aotPath = wasmPath + ".aot";

        bool interpreter = false;
#ifdef __ANDROID__
        interpreter = true;
#endif

        if (!interpreter) {
            if (isFileNewer(aotPath, wasmPath)) {
                // don't
            } else {
                WasmEdge_CompilerContext *CompilerCxt = WasmEdge_CompilerCreate(NULL);
                result = WasmEdge_CompilerCompile(CompilerCxt, wasmPath.c_str(), aotPath.c_str());
                if (!WasmEdge_ResultOK(result)) {
                    printf("Error loading WASM module: %s\n", WasmEdge_ResultGetMessage(result));
                    return false;
                }
            }
        }


        // Load and validate WASM module
        auto cfgContext = WasmEdge_ConfigureCreate();
        auto loaderContext = WasmEdge_LoaderCreate(cfgContext);


        if (!interpreter) {
            result = WasmEdge_LoaderParseFromFile(loaderContext, &astModuleContext, aotPath.c_str());
        } else {
            result = WasmEdge_LoaderParseFromFile(loaderContext, &astModuleContext, wasmPath.c_str());
        }
        if (!WasmEdge_ResultOK(result)) {
            printf("Error loading WASM module: %s\n", WasmEdge_ResultGetMessage(result));
            return false;
        }

        auto validator = WasmEdge_ValidatorCreate(cfgContext);
        result = WasmEdge_ValidatorValidate(validator, astModuleContext);
        if (!WasmEdge_ResultOK(result)) {
            printf("Error loading WASM module: %s\n", WasmEdge_ResultGetMessage(result));
            return false;
        }

        return true;
    }


    static WasmEdge_Result fftplug_allocate_plan_c2c_host(
        void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt, const WasmEdge_Value *In, WasmEdge_Value *Out) {
        int nfft = WasmEdge_ValueGetI32(In[0]);
        int forward = WasmEdge_ValueGetI32(In[1]);
        auto *thiz = (WasmedgeFT8Decoder *) env;
        FFT_PLAN plan = Fftplug_allocate_plan_c2c(*nativeStorage, nfft, forward, *thiz);
        Out[0] = WasmEdge_ValueGenI32(plan.handle);
        return WasmEdge_Result_Success;
    }

    static WasmEdge_Result decodeResultOutput_host(void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt,
                                                   const WasmEdge_Value *In, WasmEdge_Value *Out) {
        auto *thiz = (WasmedgeFT8Decoder *) env;
        auto offset = WasmEdge_ValueGetI32(In[0]);
        printf(":: %s\n", (const char *) thiz->memory.ptr(offset, 2000));
        return WasmEdge_Result_Success;
    }

    // static WasmEdge_Result fftplug_allocate_plan_c2r_host(
    //     void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt, const WasmEdge_Value *In, WasmEdge_Value *Out) {
    //     int nfft = WasmEdge_ValueGetI32(In[0]);
    //     auto *thiz = (WasmedgeFT8Decoder *) env;
    //     FFT_PLAN plan = Fftplug_allocate_plan_c2r(*nativeStorage, nfft, *thiz);
    //     Out[0] = WasmEdge_ValueGenI32(plan.handle);
    //     return WasmEdge_Result_Success;
    // }
    //
    static WasmEdge_Result fftplug_allocate_plan_r2c_host(
        void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt, const WasmEdge_Value *In, WasmEdge_Value *Out) {
        int nfft = WasmEdge_ValueGetI32(In[0]);
        auto *thiz = (WasmedgeFT8Decoder *) env;
        FFT_PLAN plan = Fftplug_allocate_plan_r2c(*nativeStorage, nfft, *thiz);
        Out[0] = WasmEdge_ValueGenI32(plan.handle);
        return WasmEdge_Result_Success;
    }


    static WasmEdge_Result fftplug_free_plan_host(void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt,
                                                  const WasmEdge_Value *In, WasmEdge_Value *Out) {
        FFT_PLAN plan = {WasmEdge_ValueGetI32(In[0])};
        Fftplug_free_plan(*nativeStorage, plan);
        return WasmEdge_Result_Success;
    }

    static WasmEdge_Result fftplug_execute_plan_host(void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt,
                                                     const WasmEdge_Value *In, WasmEdge_Value *Out) {
        FFT_PLAN plan = {WasmEdge_ValueGetI32(In[0])};
        auto *thiz = (WasmedgeFT8Decoder *) env;
        auto sourcePtr = WasmEdge_ValueGetI32(In[1]);
        auto destPtr = WasmEdge_ValueGetI32(In[3]);
        auto sourcePtrSize = WasmEdge_ValueGetI32(In[2]);
        auto destPtrSize = WasmEdge_ValueGetI32(In[4]);
        Fftplug_execute_plan(*nativeStorage, plan, thiz->memory.ptr(sourcePtr, 8), sourcePtrSize, thiz->memory.ptr(destPtr, 8), destPtrSize);
        return WasmEdge_Result_Success;
    }

    static WasmEdge_Result args_get_host(void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt,
                                         const WasmEdge_Value *In, WasmEdge_Value *Out) {
        auto *thiz = (WasmedgeFT8Decoder *) env;
        auto argvPtr = WasmEdge_ValueGetI32(In[0]);
        auto argvBufPtr = WasmEdge_ValueGetI32(In[1]);
        printf("args_get_host!! ptr=%x bufptr=%x\n", argvPtr, argvBufPtr);
        thiz->memory.put_string(argvBufPtr, "self"); // end of string
        thiz->memory.write_i32(argvPtr, argvBufPtr); // immediate zero (end array)
        thiz->memory.write_i32(argvPtr + 4, 0); // immediate zero (end array)
        Out[0] = WasmEdge_ValueGenI32(0);
        return WasmEdge_Result_Success;
    }

    static WasmEdge_Result args_sizes_get_host(void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt,
                                               const WasmEdge_Value *In, WasmEdge_Value *Out) {
        printf("args_sizes_get_host!!\n");
        auto *thiz = (WasmedgeFT8Decoder *) env;
        auto nargsPtr = WasmEdge_ValueGetI32(In[0]);
        auto sizePtr = WasmEdge_ValueGetI32(In[1]);
        thiz->memory.write_i32(nargsPtr, 1);
        thiz->memory.write_i32(sizePtr, 100);
        Out[0] = WasmEdge_ValueGenI32(0);
        return WasmEdge_Result_Success;
    }

    static WasmEdge_Result environ_get_host(void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt,
                                            const WasmEdge_Value *In, WasmEdge_Value *Out) {
        // This function is typically used to access environment variables.
        // Given the original just prints "Callback!!" and calls abort(), it seems like a placeholder.
        // An actual implementation would involve accessing the environment variables and writing them to WASM memory.
        printf("environ_get!!\n");
        abort();
        return WasmEdge_Result_Success; // To keep the compiler happy, should not reach here.
    }

    static WasmEdge_Result environ_sizes_get_host(void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt,
                                                  const WasmEdge_Value *In, WasmEdge_Value *Out) {
        printf("environ_sizes_get!!\n");
        abort();
        return WasmEdge_Result_Success; // To keep the compiler happy, should not reach here.
    }

    static WasmEdge_Result clock_time_get_host(void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt,
                                               const WasmEdge_Value *In, WasmEdge_Value *Out) {
        auto *thiz = (WasmedgeFT8Decoder *) env;
        auto clockId = WasmEdge_ValueGetI32(In[0]);
        long long rv;

        switch (clockId) {
            case 1: // __WASI_CLOCKID_MONOTONIC:
            {
                auto now = std::chrono::steady_clock::now().time_since_epoch();
                rv = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
                break;
            }
            case 0: // __WASI_CLOCKID_REALTIME:
            {
                auto now = std::chrono::system_clock::now().time_since_epoch();
                rv = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
                break;
            }
            default:
                Out[0] = WasmEdge_ValueGenI32(EINVAL);
                return WasmEdge_Result_Success;
        }

        // Assuming the destination pointer is passed as the second parameter
        auto destPtr = WasmEdge_ValueGetI32(In[1]);
        thiz->memory.write_i64(destPtr, rv);
        Out[0] = WasmEdge_ValueGenI32(0);
        return WasmEdge_Result_Success;
    }

    static WasmEdge_Result sched_yield_host(void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt,
                                            const WasmEdge_Value *In, WasmEdge_Value *Out) {
        std::this_thread::yield();
        Out[0] = WasmEdge_ValueGenI32(0);
        return WasmEdge_Result_Success;
    }

    static WasmEdge_Result thread_spawn_host(void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt,
                                             const WasmEdge_Value *In, WasmEdge_Value *Out) {
        // Spawning a new thread and calling a specific function.
        // Note: This example assumes you have a way to retrieve and call a WASM function pointer.
        // You might need to adapt it to your specific environment and threading model.
        printf("Thread spawn feature is not directly supported without adapting to the specific threading model.\n");
        abort();
        return WasmEdge_Result_Success; // To keep the compiler happy, should not reach here.
    }

    static WasmEdge_Result fd_close_host(void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt,
                                         const WasmEdge_Value *In, WasmEdge_Value *Out) {
        printf("fd_close!!\n");
        abort();
        return WasmEdge_Result_Success; // To keep the compiler happy, should not reach here.
    }

    static WasmEdge_Result fd_fdstat_get_host(void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt,
                                              const WasmEdge_Value *In, WasmEdge_Value *Out) {
        auto fd = WasmEdge_ValueGetI32(In[0]);
        auto bufPtr = WasmEdge_ValueGetI32(In[1]); // Assuming a similar setup for accessing memory.

        // This is a simplified version. Actual implementation should fetch fd status and write to WASM memory.
        printf("fd_fdstat_get is a placeholder and needs proper implementation.\n");
        abort();
        return WasmEdge_Result_Success; // To keep the compiler happy, should not reach here.
    }

    static WasmEdge_Result fd_seek_host(void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt,
                                        const WasmEdge_Value *In, WasmEdge_Value *Out) {
        printf("fd_seek!!\n");
        abort();
        return WasmEdge_Result_Success; // To keep the compiler happy, should not reach here.
    }

    static WasmEdge_Result poll_oneoff_host(void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt,
                                            const WasmEdge_Value *In, WasmEdge_Value *Out) {
        auto *thiz = (WasmedgeFT8Decoder *) env;
        auto neventsPtr = WasmEdge_ValueGetI32(In[3]);
        thiz->memory.write_i32(neventsPtr, 0);
        Out[0] = WasmEdge_ValueGenI32(0);
        return WasmEdge_Result_Success; // To keep the compiler happy, should not reach here.
    }

    static WasmEdge_Result proc_exit_host(void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt,
                                          const WasmEdge_Value *In, WasmEdge_Value *Out) {
        printf("proc_exit!!\n");
        // Properly handle process exit based on your application's needs.
        // This might involve cleaning up resources, logging, etc.
        abort();
        return WasmEdge_Result_Success; // To keep the compiler happy, should not reach here.
    }


    static WasmEdge_Result fd_write_host(void *Data, const WasmEdge_CallingFrameContext *Frame,
                                         const WasmEdge_Value *In, WasmEdge_Value *Out) {
        auto *thiz = reinterpret_cast<WasmedgeFT8Decoder *>(Data); // Adjust this according to your actual environment
        auto fd = WasmEdge_ValueGetI32(In[0]);
        auto iovsPointer = WasmEdge_ValueGetI32(In[1]);
        auto iovsLength = WasmEdge_ValueGetI32(In[2]);
        auto nWrittenPtr = WasmEdge_ValueGetI32(In[3]);

        if (fd > 2) {
            Out[0] = WasmEdge_ValueGenI32(EINVAL);
            return WasmEdge_Result_Success;
        }

        // Assuming thiz->memory is a valid WasmEdge_MemoryInstanceContext*
        WasmEdge_MemoryInstanceContext *memCxt = nullptr; // thiz->memory;
        int cnt = 0;
        for (int i = 0; i < iovsLength; ++i) {
            int32_t ptrBuf, lenBuf;
            WasmEdge_MemoryInstanceGetData(memCxt, reinterpret_cast<uint8_t *>(&ptrBuf), iovsPointer + i * 8,
                                           sizeof(int32_t));
            WasmEdge_MemoryInstanceGetData(memCxt, reinterpret_cast<uint8_t *>(&lenBuf), iovsPointer + i * 8 + 4,
                                           sizeof(int32_t));
            if (lenBuf == 0) {
                continue;
            }
            std::vector<uint8_t> writeBuf(lenBuf);
            WasmEdge_MemoryInstanceGetData(memCxt, writeBuf.data(), ptrBuf, lenBuf);
            int wrote = write(fd, writeBuf.data(), lenBuf);
            if (wrote <= 0) {
                Out[0] = WasmEdge_ValueGenI32(errno);
                WasmEdge_MemoryInstanceSetData(memCxt, reinterpret_cast<uint8_t *>(&cnt), nWrittenPtr, sizeof(cnt));
                return WasmEdge_Result_Success;
            }
            cnt += wrote;
            if (wrote != lenBuf) {
                break;
            }
        }
        Out[0] = WasmEdge_ValueGenI32(0);
        WasmEdge_MemoryInstanceSetData(memCxt, reinterpret_cast<uint8_t *>(&cnt), nWrittenPtr, sizeof(cnt));
        return WasmEdge_Result_Success;
    }

    static WasmEdge_Result fd_read_host(void *env, const WasmEdge_CallingFrameContext *CallingFrameCxt,
                                        const WasmEdge_Value *In, WasmEdge_Value *Out) {
        printf("fd_read!!\n");
        abort();
        return WasmEdge_Result_Success; // To keep the compiler happy, should not reach here.
    }

    // float *arrayAllocatorFloat(int count) {
    //     return (float *) callWasmMalloc(count * sizeof(float));
    // }
    //
    // fftwf_complex *arrayAllocatorComplex(int count) {
    //     return (fftwf_complex *) callWasmMalloc(count * sizeof(fftwf_complex));
    // }
    //
    // void deallocatorFloat(float *f) {
    //     return callWasmFree(f);
    // }
    //
    // void deallocatorComplex(fftwf_complex *f) {
    //     return callWasmFree(f);
    // }

    /*
    void callWasmFree(void *ptr) {
        WasmEdge_Value args[1] = {WasmEdge_ValueGenI32(((int8_t *)ptr) - (int8_t *)memory.ptr(0, 8))};
        WasmEdge_Value results[0];

        auto res = WasmEdge_ExecutorInvoke(executor, wasmFreeFunction, args, 1, results, 0);
        if (WasmEdge_ResultOK(res)) {
            return;
        } else {
            abort();
        }
    }

    void *callWasmMalloc(int size) {
        WasmEdge_Value args[1] = {WasmEdge_ValueGenI32(size)};
        WasmEdge_Value results[1];

        auto res = WasmEdge_ExecutorInvoke(executor, wasmMallocFunction, args, 1, results, 1);
        if (WasmEdge_ResultOK(res)) {
            int32_t offset = WasmEdge_ValueGetI32(results[0]);
            return memory.ptr(offset, 8);
        } else {
            return nullptr;
        }
    }

    */
    void registerWASIFunctions(WasmEdge_ModuleInstanceContext *envModuleInstance) {
        ::WasmEdge_ValType ParamList[4];
        ::WasmEdge_ValType ReturnList[1];
        WasmEdge_FunctionTypeContext *HostFType;
        WasmEdge_FunctionInstanceContext *HostFunc;
        WasmEdge_String HostFuncName;

        // args_sizes_get
        ParamList[0] = WasmEdge_ValTypeGenI32();
        ParamList[1] = WasmEdge_ValTypeGenI32();
        ReturnList[0] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 2, ReturnList, 1);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, args_sizes_get_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("args_sizes_get");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);

        // environ_get
        ParamList[0] = WasmEdge_ValTypeGenI32();
        ParamList[1] = WasmEdge_ValTypeGenI32();
        ReturnList[0] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 2, ReturnList, 1);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, environ_get_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("environ_get");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);

        // environ_sizes_get
        ParamList[0] = WasmEdge_ValTypeGenI32();
        ParamList[1] = WasmEdge_ValTypeGenI32();
        ReturnList[0] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 2, ReturnList, 1);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, environ_sizes_get_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("environ_sizes_get");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);

        // clock_time_get
        ParamList[0] = WasmEdge_ValTypeGenI32();
        ParamList[1] = WasmEdge_ValTypeGenI64();
        ParamList[2] = WasmEdge_ValTypeGenI32();
        ReturnList[0] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 3, ReturnList, 1);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, clock_time_get_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("clock_time_get");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);

        // fd_close
        ParamList[0] = WasmEdge_ValTypeGenI32();
        ReturnList[0] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 1, ReturnList, 1);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, fd_close_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("fd_close");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);

        // fd_fdstat_get
        ParamList[0] = WasmEdge_ValTypeGenI32();
        ParamList[1] = WasmEdge_ValTypeGenI32();
        ReturnList[0] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 2, ReturnList, 1);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, fd_fdstat_get_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("fd_fdstat_get");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);

        // fd_read
        ParamList[0] = WasmEdge_ValTypeGenI32();
        ParamList[1] = WasmEdge_ValTypeGenI32();
        ParamList[2] = WasmEdge_ValTypeGenI32();
        ParamList[3] = WasmEdge_ValTypeGenI32();
        ReturnList[0] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 4, ReturnList, 1);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, fd_read_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("fd_read");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);


        // fd_write
        ParamList[0] = WasmEdge_ValTypeGenI32();
        ParamList[1] = WasmEdge_ValTypeGenI32();
        ParamList[2] = WasmEdge_ValTypeGenI32();
        ParamList[3] = WasmEdge_ValTypeGenI32();
        ReturnList[0] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 4, ReturnList, 1);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, fd_write_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("fd_write");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);

        // fd_seek
        ParamList[0] = WasmEdge_ValTypeGenI32();
        ParamList[1] = WasmEdge_ValTypeGenI64();
        ParamList[2] = WasmEdge_ValTypeGenI32();
        ParamList[3] = WasmEdge_ValTypeGenI32();
        ReturnList[0] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 4, ReturnList, 1);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, fd_seek_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("fd_seek");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);

        // poll_oneoff
        ParamList[0] = WasmEdge_ValTypeGenI32();
        ParamList[1] = WasmEdge_ValTypeGenI32();
        ParamList[2] = WasmEdge_ValTypeGenI32();
        ParamList[3] = WasmEdge_ValTypeGenI32();
        ReturnList[0] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 4, ReturnList, 1);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, poll_oneoff_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("poll_oneoff");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);

        // proc_exit
        ParamList[0] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 1, ReturnList, 0);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, proc_exit_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("proc_exit");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);

        // sched_yield
        HostFType = WasmEdge_FunctionTypeCreate(NULL, 0, ReturnList, 1);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, sched_yield_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("sched_yield");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);

        // thread_spawn
        ParamList[0] = WasmEdge_ValTypeGenI32();
        ReturnList[0] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 1, ReturnList, 1);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, thread_spawn_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("thread-spawn");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);
    }

    void registerEnvFunctions(WasmEdge_ModuleInstanceContext *envModuleInstance) {
        ::WasmEdge_ValType ParamList[5];
        ::WasmEdge_ValType ReturnList[1];
        WasmEdge_FunctionTypeContext *HostFType;
        WasmEdge_FunctionInstanceContext *HostFunc;
        WasmEdge_String HostFuncName;

        // fftplug_allocate_plan_c2c
        ParamList[0] = WasmEdge_ValTypeGenI32();
        ParamList[1] = WasmEdge_ValTypeGenI32();
        ReturnList[0] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 2, ReturnList, 1);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, fftplug_allocate_plan_c2c_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("fftplug_allocate_plan_c2c");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);


        // decodeResultOutput
        ParamList[0] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 1, ReturnList, 0);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, decodeResultOutput_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("decodeResultOutput");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);


        // decodeResultOutput
        ParamList[0] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 1, ReturnList, 0);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, decodeResultOutput_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("decodeResultOutput");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);

        // // fftplug_allocate_plan_c2r
        // ParamList[0] = WasmEdge_ValTypeGenI32();
        // ReturnList[0] = WasmEdge_ValTypeGenI32();
        // HostFType = WasmEdge_FunctionTypeCreate(ParamList, 1, ReturnList, 1);
        // HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, fftplug_allocate_plan_c2r_host, this, 0);
        // WasmEdge_FunctionTypeDelete(HostFType);
        // HostFuncName = WasmEdge_StringCreateByCString("fftplug_allocate_plan_c2r");
        // WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        // WasmEdge_StringDelete(HostFuncName);

        // fftplug_allocate_plan_r2c
        ParamList[0] = WasmEdge_ValTypeGenI32();
        ReturnList[0] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 1, ReturnList, 1);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, fftplug_allocate_plan_r2c_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("fftplug_allocate_plan_r2c");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);

        // fftplug_free_plan
        ParamList[0] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 1, ReturnList, 0);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, fftplug_free_plan_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("fftplug_free_plan");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);

        // fftplug_execute_plan
        ParamList[0] = WasmEdge_ValTypeGenI32();
        ParamList[1] = WasmEdge_ValTypeGenI32();
        ParamList[2] = WasmEdge_ValTypeGenI32();
        ParamList[3] = WasmEdge_ValTypeGenI32();
        ParamList[4] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 5, ReturnList, 0);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, fftplug_execute_plan_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("fftplug_execute_plan");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);

        // args_get
        ParamList[0] = WasmEdge_ValTypeGenI32();
        ParamList[1] = WasmEdge_ValTypeGenI32();
        ReturnList[0] = WasmEdge_ValTypeGenI32();
        HostFType = WasmEdge_FunctionTypeCreate(ParamList, 2, ReturnList, 1);
        HostFunc = WasmEdge_FunctionInstanceCreate(HostFType, args_get_host, this, 0);
        WasmEdge_FunctionTypeDelete(HostFType);
        HostFuncName = WasmEdge_StringCreateByCString("args_get");
        WasmEdge_ModuleInstanceAddFunction(envModuleInstance, HostFuncName, HostFunc);
        WasmEdge_StringDelete(HostFuncName);
    }

    WasmEdge_FunctionInstanceContext *decodeFT8MainFunction;
    WasmEdge_FunctionInstanceContext *decodeFT4MainFunction;
    WasmEdge_FunctionInstanceContext *getFT8InputBufferSizeFunction;
    WasmEdge_FunctionInstanceContext *getFT8InputBufferFunction;
    // WasmEdge_FunctionInstanceContext *wasmMallocFunction;
    WasmEdge_FunctionInstanceContext *testPrintFunction;
    // WasmEdge_FunctionInstanceContext *wasmFreeFunction;
    WasmEdge_FunctionInstanceContext *wasmInitializeFunction;
    WasmEdge_ModuleInstanceContext *executableModule;
    WasmEdge_ExecutorContext *executor;


    void wasmDecodeFT8(int threads, const char *mode, int sampleRate, dsp::stereo_t *samples, long long nsamples,
                       const std::function<void(const char *)> &dest) {
#define CHECK_RESULT(result) \
        if (!WasmEdge_ResultOK(result)) { \
        printf("Error loading WASM module: %s\n", WasmEdge_ResultGetMessage(result)); \
        return; \
        }


        WasmEdge_Result res;
        WasmEdge_String wasiModuleName = WasmEdge_StringCreateByCString("wasi_snapshot_preview1");
        WasmEdge_String envModuleName = WasmEdge_StringCreateByCString("env");
        // WasmEdge_String mainModuleName = WasmEdge_StringCreateByCString("main");
        auto storeContext = WasmEdge_StoreCreate();
        WasmEdge_ModuleInstanceContext *envModuleInstance = WasmEdge_ModuleInstanceCreate(envModuleName);
        WasmEdge_ModuleInstanceContext *wasiModuleInstance = WasmEdge_ModuleInstanceCreate(wasiModuleName);
        registerWASIFunctions(wasiModuleInstance);
        registerEnvFunctions(envModuleInstance);
        WasmEdge_StringDelete(envModuleName);
        WasmEdge_StringDelete(wasiModuleName);

        executor = WasmEdge_ExecutorCreate(nullptr, nullptr);
        res = WasmEdge_ExecutorRegisterImport(executor, storeContext, wasiModuleInstance);
        CHECK_RESULT(res);
        res = WasmEdge_ExecutorRegisterImport(executor, storeContext, envModuleInstance);
        CHECK_RESULT(res);


        res = WasmEdge_ExecutorInstantiate(executor, &executableModule, storeContext, astModuleContext);
        CHECK_RESULT(res);


        uint32_t FuncNum = WasmEdge_ModuleInstanceListFunctionLength(executableModule);
        WasmEdge_String *FuncNames = (WasmEdge_String *) malloc(FuncNum * sizeof(WasmEdge_String));
        WasmEdge_ModuleInstanceListFunction(executableModule, FuncNames, FuncNum);

        auto stringEquals = [](const WasmEdge_String &s, const char *test) {
            if (s.Length == strlen(test)) {
                return !memcmp(s.Buf, test, s.Length);
            }
            return false;
        };

        // Iterate over the exported functions
        for (uint32_t i = 0; i < FuncNum; i++) {
            // Get the exported function name
            // Get the exported function instance
            WasmEdge_FunctionInstanceContext *function = WasmEdge_ModuleInstanceFindFunction(
                executableModule, FuncNames[i]);
            if (function) {
                // if (stringEquals(FuncNames[i], "wasmMalloc")) {
                //     wasmMallocFunction = function;
                //     continue;
                // }
                if (stringEquals(FuncNames[i], "testPrint")) {
                    testPrintFunction = function;
                    continue;
                }
                // if (stringEquals(FuncNames[i], "wasmFree")) {
                //     wasmFreeFunction = function;
                //     continue;
                // }
                if (stringEquals(FuncNames[i], "_initialize")) {
                    wasmInitializeFunction = function;
                    continue;
                }
                if (stringEquals(FuncNames[i], "decodeFT8MainAt12000")) {
                    decodeFT8MainFunction = function;
                    continue;
                }
                if (stringEquals(FuncNames[i], "decodeFT4MainAt12000")) {
                    decodeFT4MainFunction = function;
                    continue;
                }
                if (stringEquals(FuncNames[i], "getFT8InputBufferSize")) {
                    getFT8InputBufferSizeFunction = function;
                    continue;
                }
                if (stringEquals(FuncNames[i], "getFT8InputBuffer")) {
                    getFT8InputBufferFunction = function;
                    continue;
                }
                printf("Unknown export: %s\n", FuncNames[i].Buf);
            }
        }

        // Clean up the array of function names
        free(FuncNames);


        /* The parameters and returns arrays. */
        WasmEdge_Value args[0] = {};
        WasmEdge_Value results[1];

        res = WasmEdge_ExecutorInvoke(executor, wasmInitializeFunction, args, 0, results, 1);
        CHECK_RESULT(res);

        res = WasmEdge_ExecutorInvoke(executor, getFT8InputBufferFunction, args, 0, results, 1);
        CHECK_RESULT(res);

        auto inputBufferOffset = WasmEdge_ValueGetI32(results[0]);

        WasmEdge_String memoryStr = WasmEdge_StringCreateByCString("memory");
        auto memoryInstanceContext = WasmEdge_ModuleInstanceFindMemory(executableModule, memoryStr);
        WasmEdge_StringDelete(memoryStr);
        if (!memoryInstanceContext) {
            return;
        }

        memory = WASMMemory(memoryInstanceContext);

        memory.put(inputBufferOffset, (const char*)samples, nsamples * sizeof(dsp::stereo_t));

        res = WasmEdge_ExecutorInvoke(executor, testPrintFunction, args, 0, results, 0);
        CHECK_RESULT(res);

        {
            WasmEdge_Value args[1] = {WasmEdge_ValueGenI32(nsamples), };
            WasmEdge_Value results[1] = {WasmEdge_ValueGenI32(0)};

            res = WasmEdge_ExecutorInvoke(executor, decodeFT8MainFunction, args, 1, results, 1);
            CHECK_RESULT(res);
        }


        printf("Ok\n");
    }
};

WasmEdge_ModuleInstanceContext *WasmedgeFT8Decoder::module;
WasmEdge_VMContext *WasmedgeFT8Decoder::VMCxt;
WasmEdge_ASTModuleContext *WasmedgeFT8Decoder::astModuleContext;

#ifdef DO_WASMER

wasm_engine_t *wasmEngine;
wasm_store_t *wasmEngineStore;

struct WasmerFT8Decoder {
    static wasm_module_t *module;

    static bool setupModule(const std::string &wasmPath) {
        if (!wasmEngine) {
            wasm_config_t *config = wasm_config_new();
#ifdef __ANDROID__
            char *triple = "aarch64-unknown-linux";
#endif
#ifdef __arm64__
            const char *triple = "aarch64-unknown-linux";
#endif
            auto tripleOverride = getenv("WASMER_TRIPLE");
            if (tripleOverride) {
                triple = tripleOverride;
            }
            wasm_name_t tripleName;
            printf("name-from-string: %s ...\n", triple);
            wasm_name_new_from_string(&tripleName, triple);
            printf("triple new...\n");
            auto tripleObj = wasmer_triple_new(&tripleName);
            printf("target new... triple=%p\n", tripleObj);
            auto cpuf = wasmer_cpu_features_new();
            wasmer_target_t  *target = wasmer_target_new(tripleObj, cpuf);
            printf("set target target...\n");
            wasm_config_set_target(config, target);

            printf("new engine...\n");
            wasmEngine = wasm_engine_new_with_config(config);
            printf("new store...\n");
            wasmEngineStore = wasm_store_new(wasmEngine);
        }
        FILE *file = fopen(wasmPath.c_str(), "rb");
        if (!file) {
            printf("> Cannot open wasm!\n");
            return false;
        }
        fseek(file, 0L, SEEK_END);
        size_t file_size = ftell(file);
        fseek(file, 0L, SEEK_SET);
        wasm_byte_vec_t binary;
        wasm_byte_vec_new_uninitialized(&binary, file_size);
        if (fread(binary.data, file_size, 1, file) != 1) {
            printf("> Error loading module!\n");
            return false;
        }
        fclose(file);
        module = wasm_module_new(wasmEngineStore, &binary);
        if (!module) {
            printf("> Error compiling module!\n");
            return false;
        }
        return true;
    }

    static inline wasm_functype_t *wasm_functype_new_4_1(
        wasm_valtype_t *p1, wasm_valtype_t *p2, wasm_valtype_t *p3, wasm_valtype_t *p4,
        wasm_valtype_t *r
    ) {
        wasm_valtype_t *ps[4] = {p1, p2, p3, p4};
        wasm_valtype_t *rs[1] = {r};
        wasm_valtype_vec_t params, results;
        wasm_valtype_vec_new(&params, 4, ps);
        wasm_valtype_vec_new(&results, 1, rs);
        return wasm_functype_new(&params, &results);
    }


    struct WASMMemory {
        wasm_memory_t *memory = nullptr;
        byte_t *mem = nullptr;
        long long limit = 0;

        WASMMemory() {
        }

        WASMMemory(wasm_memory_t *memory) : memory(memory) {
            mem = wasm_memory_data(memory);
            limit = wasm_memory_data_size(memory);
        }

        void write_i32(size_t offset, int32_t value) {
            if (offset + sizeof(int32_t) <= limit) {
                mem[offset] = (uint8_t) (value & 0xFF);
                mem[offset + 1] = (uint8_t) ((value >> 8) & 0xFF);
                mem[offset + 2] = (uint8_t) ((value >> 16) & 0xFF);
                mem[offset + 3] = (uint8_t) ((value >> 24) & 0xFF);
            }
        }

        void write_i64(size_t offset, int64_t value) {
            if (offset + sizeof(int64_t) <= limit) {
                mem[offset] = (uint8_t) (value & 0xFF);
                mem[offset + 1] = (uint8_t) ((value >> 8) & 0xFF);
                mem[offset + 2] = (uint8_t) ((value >> 16) & 0xFF);
                mem[offset + 3] = (uint8_t) ((value >> 24) & 0xFF);
                mem[offset + 4] = (uint8_t) ((value >> 32) & 0xFF);
                mem[offset + 5] = (uint8_t) ((value >> 40) & 0xFF);
                mem[offset + 6] = (uint8_t) ((value >> 48) & 0xFF);
                mem[offset + 7] = (uint8_t) ((value >> 56) & 0xFF);
            }
        }


        void put_string(size_t offset, const char *str) {
            if (offset + strlen(str) + 1 <= limit) {
                strcpy(mem + offset, str);
            }
        }

        char *dump(size_t offset) {
            static char buf[10000];
            buf[0] = 0;
            for (int q = 0; q < 32; q++) {
                snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "%02x ", mem[offset + q]);
            }
            return buf;
        }

        int32_t read_i32(size_t offset) {
            if (offset + sizeof(int32_t) <= limit) {
                int32_t value = (int32_t) ((uint32_t) mem[offset] |
                                           (((uint32_t) mem[offset + 1]) << 8) |
                                           (((uint32_t) mem[offset + 2]) << 16) |
                                           (((uint32_t) mem[offset + 3]) << 24));
                return value;
            }
            return 0;
        }
    };

    WASMMemory memory;

    wasm_memory_t *externMemory;
    wasm_func_t *startFunction;
    wasm_func_t *checkStaticInitFunction;
    wasm_func_t *decodeFT8MainFunction;
    wasm_func_t *decodeFT4MainFunction;
    wasm_func_t *getFT8InputBufferSizeFunction;
    wasm_func_t *getFT8InputBufferFunction;
    wasm_func_t *wasiThreadStartFunction;
    // wasm_func_t *wasmMallocFunction;
    // wasm_func_t *wasmFreeFunction;
    wasm_func_t *wasmInitializeFunction;
    wasm_func_t *testPrintFunction;

    wasm_store_t *store;


    void print_trap(wasm_trap_t *trap) {
        if (trap == NULL) {
            printf("No trap occurred.\n");
            return;
        }

        // Get the trap message
        wasm_message_t message;
        wasm_trap_message(trap, &message);

        // Print the trap message
        printf("Trap message: %.*s\n", (int) message.size, message.data);

        // Get the trap origin
        wasm_frame_t *frame = wasm_trap_origin(trap);
        if (frame != NULL) {
            // Get the function name from the frame
            auto fi = wasm_frame_func_index(frame);
            auto fo = wasm_frame_func_offset(frame);
            auto moff = wasm_frame_module_offset(frame);
            printf("Trap origin: func %d(%d) in (%d)\n", (int) fi, (int) fo, (int) moff);
            /*
            wasm_name_t function_name;
            wasm_func_name(wasm_frame_func(frame), &function_name);

            // Print the function name
            printf("Trap origin: %.*s\n", (int)function_name.size, function_name.data);

            // Clean up the function name
            wasm_name_delete(&function_name);

            // Get the module name from the frame
            wasm_name_t module_name;
            wasm_frame_module_name(frame, &module_name);

            // Print the module name
            printf("Module name: %.*s\n", (int)module_name.size, module_name.data);

            // Clean up the module name
            wasm_name_delete(&module_name);

            // Clean up the frame
            wasm_frame_delete(frame);
            */
        } else {
            printf("Trap origin not available.\n");
        }

        // Clean up the trap message
        wasm_byte_vec_delete(&message);
    }


    ~WasmerFT8Decoder() {
        wasm_memory_delete(memory.memory);
        wasm_store_delete(store);
    }

    static wasm_functype_t* wasm_functype_new_5_0(
       wasm_valtype_t* p1,  wasm_valtype_t* p2,  wasm_valtype_t* p3, wasm_valtype_t* p4,  wasm_valtype_t* p5
    ) {
        wasm_valtype_t* ps[5] = {p1, p2, p3, p4, p5};
        wasm_valtype_vec_t params, results;
        wasm_valtype_vec_new(&params, 5, ps);
        wasm_valtype_vec_new_empty(&results);
        return wasm_functype_new(&params, &results);
    }



    WasmerFT8Decoder() {
        std::vector<wasm_extern_t *> externs;
        std::unordered_map<std::string, wasm_func_t *> exported;

        wasm_importtype_vec_t imports;
        wasm_module_imports(module, &imports);

        store = wasm_store_new(wasmEngine);


        for (int q = 0; q < imports.size; q++) {
            const wasm_externtype_t *t0 = wasm_importtype_type(imports.data[q]);
            auto typ = wasm_externtype_kind(t0);
            const char *ctyp = "?";
            switch (typ) {
                case WASM_EXTERN_FUNC: ctyp = "func";
                    break;
                case WASM_EXTERN_TABLE: ctyp = "table";
                    break;
                case WASM_EXTERN_GLOBAL: ctyp = "global";
                    break;
                case WASM_EXTERN_MEMORY: ctyp = "memory";
                    break;
            }
            auto name = wasm_importtype_name(imports.data[q]);
            std::string nm(name->data, name->size);
            // printf("import: %s: name = %s\n", ctyp, nm.c_str());
            const char *cname = name->data;
            if (nm == "memory") {
                auto mt0 = wasm_externtype_as_memorytype((wasm_externtype_t *) t0);
                wasm_memory_t *memory0 = wasm_memory_new(store, mt0);
                printf("allocated memory size: %zu\n", wasm_memory_data_size(memory0));
                memory = WASMMemory(memory0);
                memory.dump(0);
                auto memory_import = wasm_memory_as_extern(memory0);
                externs.emplace_back(memory_import);
                continue;
                wasm_memorytype_delete(mt0);
            }
            if (nm == "decodeResultOutput") {
                auto t1 = wasm_functype_new_1_0(wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, decodeResultOutput_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if (nm == "fftplug_allocate_plan_c2c") {
                auto t1 = wasm_functype_new_2_1(wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, fftplug_allocate_plan_c2c_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if (nm == "fftplug_allocate_plan_r2c") {
                auto t1 = wasm_functype_new_1_1(wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, fftplug_allocate_plan_r2c_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if ((nm == "fftplug_free_plan")) {
                auto t1 = wasm_functype_new_1_0(wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, fftplug_free_plan_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if ((nm == "fftplug_execute_plan")) {
                auto t1 = wasm_functype_new_5_0(wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, fftplug_execute_plan_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if ((nm == "proc_exit")) {
                auto t1 = wasm_functype_new_1_0(wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, proc_exit_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if ((nm == "sched_yield")) {
                auto t1 = wasm_functype_new_0_1(wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, sched_yield_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if ((nm == "thread-spawn")) {
                auto t1 = wasm_functype_new_1_1(wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, thread_spawn_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if ((nm == "args_get")) {
                auto t1 = wasm_functype_new_2_1(wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, args_get_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if ((nm == "args_sizes_get")) {
                auto t1 = wasm_functype_new_2_1(wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, args_sizes_get_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if ((nm == "environ_get")) {
                auto t1 = wasm_functype_new_2_1(wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, environ_get_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if ((nm == "environ_sizes_get")) {
                auto t1 = wasm_functype_new_2_1(wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, environ_sizes_get_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if ((nm == "clock_time_get")) {
                auto t1 = wasm_functype_new_3_1(wasm_valtype_new_i32(), wasm_valtype_new_i64(), wasm_valtype_new_i32(),
                                                wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, clock_time_get_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if ((nm == "fd_close")) {
                auto t1 = wasm_functype_new_1_1(wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, fd_close_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if ((nm == "fd_fdstat_get")) {
                auto t1 = wasm_functype_new_2_1(wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, fd_fdstat_get_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if ((nm == "fd_read")) {
                auto t1 = wasm_functype_new_4_1(wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32(),
                                                wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, fd_read_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if ((nm == "fd_write")) {
                auto t1 = wasm_functype_new_4_1(wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32(),
                                                wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, fd_write_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if ((nm == "fd_seek")) {
                auto t1 = wasm_functype_new_4_1(wasm_valtype_new_i32(), wasm_valtype_new_i64(), wasm_valtype_new_i32(),
                                                wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, fd_seek_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if ((nm == "poll_oneoff")) {
                auto t1 = wasm_functype_new_4_1(wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32(),
                                                wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, poll_oneoff_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            break; // will warn first missing function properly
        }

        wasm_extern_vec_t ev;
        auto wasm_extern = externs.data();
        wasm_extern_vec_new(&ev, externs.size(), wasm_extern);
        wasm_instance_t *instance = wasm_instance_new(store, module, &ev, NULL);


        if (!instance) {
            std::string err(' ', wasmer_last_error_length() + 1);
            wasmer_last_error_message(err.data(), wasmer_last_error_length());
            printf("> Error instantiating module:\n%s\n", err.data());
            return;
        }

        wasm_extern_vec_t iexterns;
        wasm_instance_exports(instance, &iexterns);

        wasm_exporttype_vec_t exports;
        wasm_module_exports(module, &exports);

        if (iexterns.size != exports.size) {
            printf("mismatch iexterns.size != exports.size\n");
            return;
        }

        for (int q = 0; q < exports.size; q++) {
            auto t0 = wasm_exporttype_type(exports.data[q]);
            auto typ = wasm_externtype_kind(t0);
            const char *ctyp = "?";
            switch (typ) {
                case WASM_EXTERN_FUNC: ctyp = "func";
                    break;
                case WASM_EXTERN_TABLE: ctyp = "table";
                    break;
                case WASM_EXTERN_GLOBAL: ctyp = "global";
                    break;
                case WASM_EXTERN_MEMORY: ctyp = "memory";
                    break;
            }
            auto name = wasm_exporttype_name(exports.data[q]);
            std::string nm(name->data, name->size);
            // printf("export/extern: %s: name = %s\n", ctyp, nm.c_str());
            if ((nm == "memory")) {
                externMemory = wasm_extern_as_memory(iexterns.data[q]);
                if (memory.mem == nullptr) {
                    memory = WASMMemory(externMemory);
                }
                continue;
            }
            if ((nm == "_start")) {
                startFunction = wasm_extern_as_func(iexterns.data[q]);
                continue;
            }
            if ((nm == "decodeFT8MainAt12000")) {
                decodeFT8MainFunction = wasm_extern_as_func(iexterns.data[q]);
                continue;
            }
            if ((nm == "decodeFT4MainAt12000")) {
                decodeFT4MainFunction = wasm_extern_as_func(iexterns.data[q]);
                continue;
            }
            if ((nm == "getFT8InputBufferSize")) {
                getFT8InputBufferSizeFunction = wasm_extern_as_func(iexterns.data[q]);
                continue;
            }
            if ((nm == "getFT8InputBuffer")) {
                getFT8InputBufferFunction = wasm_extern_as_func(iexterns.data[q]);
                continue;
            }
            if ((nm == "wasi_thread_start")) {
                wasiThreadStartFunction = wasm_extern_as_func(iexterns.data[q]);
                continue;
            }
            if ((nm == "checkStaticInit")) {
                checkStaticInitFunction = wasm_extern_as_func(iexterns.data[q]);
                continue;
            }
            // if ((nm == "wasmMalloc")) {
            //     wasmMallocFunction = wasm_extern_as_func(iexterns.data[q]);
            //     continue;
            // }
            // if ((nm == "wasmFree")) {
            //     wasmFreeFunction = wasm_extern_as_func(iexterns.data[q]);
            //     continue;
            // }
            if ((nm == "_initialize")) {
                wasmInitializeFunction = wasm_extern_as_func(iexterns.data[q]);
                continue;
            }
            if ((nm == "testPrint")) {
                testPrintFunction = wasm_extern_as_func(iexterns.data[q]);
                continue;
            }
            printf("Unknown exported function, go away: %s\n", nm.c_str());
            return;
        }
    }

    static wasm_trap_t *decodeResultOutput_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto *thiz = (WasmerFT8Decoder *) env;
        auto offset = args->data[0].of.i32;
        printf(":: %s\n", (const char *) thiz->memory.mem + offset);
        return nullptr;
    }

    static wasm_trap_t *fftplug_allocate_plan_c2c_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto *thiz = (WasmerFT8Decoder *) env;
        auto nfft = args->data[0].of.i32;
        auto forward = args->data[1].of.i32;
        FFT_PLAN plan = Fftplug_allocate_plan_c2c(*nativeStorage, nfft, forward, *thiz);
        results->data[0] = WASM_I32_VAL(plan.handle);
        return nullptr;
    }

    static wasm_trap_t *fftplug_allocate_plan_r2c_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto *thiz = (WasmerFT8Decoder *) env;
        auto nfft = args->data[0].of.i32;
        FFT_PLAN plan = Fftplug_allocate_plan_r2c(*nativeStorage, nfft, *thiz);
        results->data[0] = WASM_I32_VAL(plan.handle);
        return nullptr;
    }

    static wasm_trap_t *fftplug_free_plan_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        FFT_PLAN plan = {args->data[0].of.i32};
        Fftplug_free_plan(*nativeStorage, plan);
        return nullptr;
    }

    static wasm_trap_t *fftplug_execute_plan_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        FFT_PLAN plan = {args->data[0].of.i32};
        auto srcPtr = args->data[1].of.i32;
        auto srcSize = args->data[2].of.i32;
        auto destPtr = args->data[3].of.i32;
        auto destSize = args->data[4].of.i32;
        auto *thiz = (WasmerFT8Decoder *) env;
        Fftplug_execute_plan(*nativeStorage, plan, thiz->memory.mem+srcPtr, srcSize, thiz->memory.mem+destPtr, destSize);
        return nullptr;
    }

    static wasm_trap_t *args_get_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto *thiz = (WasmerFT8Decoder *) env;
        auto argvPtr = args->data[0].of.i32;
        auto argvBufPtr = args->data[1].of.i32;
        printf("args_get_host!! ptr=%x bufptr=%x\n", argvPtr, argvBufPtr);
        thiz->memory.put_string(argvBufPtr, "self"); // end of string
        thiz->memory.write_i32(argvPtr, argvBufPtr); // immediate zero (end array)
        thiz->memory.write_i32(argvPtr + 4, 0); // immediate zero (end array)
        results->data[0] = WASM_I32_VAL(0);
        return nullptr;
    }


    static wasm_trap_t *args_sizes_get_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        printf("args_sizes_get_host!!\n");
        auto *thiz = (WasmerFT8Decoder *) env;
        auto nargsPtr = args->data[0].of.i32;
        auto sizePtr = args->data[1].of.i32;
        thiz->memory.write_i32(nargsPtr, 1);
        thiz->memory.write_i32(sizePtr, 100);
        results->data[0] = WASM_I32_VAL(0);
        return nullptr;
    }

    static wasm_trap_t *environ_get_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto *thiz = (WasmerFT8Decoder *) env;
        printf("Callback!!\n");
        abort();
    }

    static wasm_trap_t *environ_sizes_get_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto *thiz = (WasmerFT8Decoder *) env;
        printf("Callback!!\n");
        abort();
    }

    static wasm_trap_t *clock_time_get_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto *thiz = (WasmerFT8Decoder *) env;
        auto clockId = args->data[0].of.i32;
        auto prec = args->data[0].of.i64;
        auto destPtr = args->data[0].of.i32;
        long long rv;


        switch (clockId) {
            case 1: // __WASI_CLOCKID_MONOTONIC:
            {
                auto now = std::chrono::steady_clock::now().time_since_epoch();
                rv = (long long)duration_cast<std::chrono::nanoseconds>(now).count();
                break;
            }
            case 0: // __WASI_CLOCKID_REALTIME:
            {
                auto now = std::chrono::system_clock::now().time_since_epoch();
                rv = (long long)duration_cast<std::chrono::nanoseconds>(now).count();
                break;
            }
            default: {
                results->data[0] = WASM_I32_VAL(EINVAL);
                return nullptr;
            }
        }
        results->data[0] = WASM_I32_VAL(0);
        thiz->memory.write_i64(destPtr, rv);
        return nullptr;
    }

    static wasm_trap_t *sched_yield_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        results->data[0] = WASM_I32_VAL(0);
        return nullptr;
    }

    static wasm_trap_t *fd_read_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        printf("Sched yield!!\n");
        abort();
    }

    static wasm_trap_t *thread_spawn_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto *thiz = (WasmerFT8Decoder *) env;
        auto arg0 = args->data[0];
        std::thread x([=]() {
            auto id = std::this_thread::get_id();
            wasm_val_t args_val[1] = {arg0};
            wasm_val_t results_val[0] = {};
            wasm_val_vec_t args = WASM_ARRAY_VEC(args_val);
            wasm_val_vec_t results = WASM_ARRAY_VEC(results_val);
            if (auto trap = wasm_func_call(thiz->wasiThreadStartFunction, &args, &results)) {
                thiz->print_trap(trap);
            }
        });
        x.detach();
        return nullptr;
    }

    static wasm_trap_t *fd_close_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        printf("unref abort!!\n");
        abort();
    }


    static wasm_trap_t *fd_fdstat_get_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto fd = args->data[0].of.i32;
        auto bufPtr = args->data[1].of.i32; // 24 bytes
        auto *thiz = (WasmerFT8Decoder *) env;

        if (int FdFlags = ::fcntl(fd, F_GETFL); FdFlags < 0) {
            results->data[0] = WASM_I32_VAL(errno);
            return nullptr;
        } else {
            memset(thiz->memory.mem + bufPtr, 0, 24);
        }
        results->data[0] = WASM_I32_VAL(0);
        return nullptr;
    }

    static wasm_trap_t *fd_write_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto fd = args->data[0].of.i32;
        auto iovsPointer = args->data[1].of.i32;
        auto iovsLength = args->data[2].of.i32;
        auto nWrittenPtr = args->data[3].of.i32;
        if (fd > 2) {
            results->data[0] = WASM_I32_VAL(EINVAL);
            return nullptr;
        }
        auto *thiz = (WasmerFT8Decoder *) env;
        /*
        *  iovec: const void* buf;  size_t buf_len;
         */
        int cnt = 0;
        for (int i = 0; i < iovsLength; i++) {
            int ptrBuf = thiz->memory.read_i32(iovsPointer + i * 8);
            int lenBuf = thiz->memory.read_i32(iovsPointer + i * 8 + 4);
            if (lenBuf == 0) {
                continue;;
            }
            char *writeBuf = thiz->memory.mem + ptrBuf;
            int wrote = write(fd, writeBuf, lenBuf);
            if (wrote <= 0) {
                results->data[0] = WASM_I32_VAL(errno);
                thiz->memory.write_i32(nWrittenPtr, cnt);
                return nullptr;
            }
            cnt += wrote;
            if (wrote != lenBuf) {
                results->data[0] = WASM_I32_VAL(0);
                thiz->memory.write_i32(nWrittenPtr, cnt);
                return nullptr;
            }
        }
        results->data[0] = WASM_I32_VAL(0);
        thiz->memory.write_i32(nWrittenPtr, cnt);
        return nullptr;
    }

    static wasm_trap_t *fd_seek_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        printf("unref abort!!\n");
        abort();
    }

    static wasm_trap_t *poll_oneoff_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto neventsPtr = args->data[3].of.i32;
        auto *thiz = (WasmerFT8Decoder *) env;
        thiz->memory.mem[neventsPtr] = 0;
        results->data[0] = WASM_I32_VAL(0);
        return nullptr;
    }

    static wasm_trap_t *proc_exit_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        printf("proc_exit!!\n");
        return nullptr;
    }

    // float *arrayAllocatorFloat(int count) {
    //     return (float *) callWasmMalloc(count * sizeof(float));
    // }
    //
    // fftwf_complex *arrayAllocatorComplex(int count) {
    //     return (fftwf_complex *) callWasmMalloc(count * sizeof(fftwf_complex));
    // }

    // void deallocatorFloat(float *f) {
    //     return callWasmFree(f);
    // }
    //
    // void deallocatorComplex(fftwf_complex *f) {
    //     return callWasmFree(f);
    // }
    //

    // void callWasmFree(void *ptr) {
    //     auto bptr = (byte_t *) ptr;
    //     auto offset = bptr - memory.mem;
    //     wasm_val_t args_val[1] = {WASM_I32_VAL((int)offset)};
    //     wasm_val_t results_val[] = {};
    //     wasm_val_vec_t args = WASM_ARRAY_VEC(args_val);
    //     wasm_val_vec_t results = WASM_ARRAY_VEC(results_val);
    //
    //     if (auto trap = wasm_func_call(wasmFreeFunction, &args, &results)) {
    //         print_trap(trap);
    //     }
    // }
    //
    // void *callWasmMalloc(int size) {
    //     wasm_val_t args_val[1] = {WASM_I32_VAL(size)};
    //     wasm_val_t results_val[1] = {WASM_I32_VAL(0)};
    //     wasm_val_vec_t args = WASM_ARRAY_VEC(args_val);
    //     wasm_val_vec_t results = WASM_ARRAY_VEC(results_val);
    //
    //     if (auto trap = wasm_func_call(wasmMallocFunction, &args, &results)) {
    //         print_trap(trap);
    //         return nullptr;
    //     }
    //     auto valu = results.data[0].of.i32;
    //     if (!valu) {
    //         abort();
    //     }
    //     return memory.mem + valu;
    // }

    void wasmDecodeFT8(int threads, const char *mode, int sampleRate, dsp::stereo_t *samples, long long nsamples,
                       const std::function<void(const char *)> &dest) { {
            wasm_val_t args_val[0] = {};
            wasm_val_t results_val[1] = {WASM_I32_VAL(0)};
            wasm_val_vec_t args = WASM_ARRAY_VEC(args_val);
            wasm_val_vec_t results = WASM_ARRAY_VEC(results_val);

            if (auto trap = wasm_func_call(wasmInitializeFunction, &args, &results)) {
                print_trap(trap);
                return;
            }

            if (auto trap = wasm_func_call(getFT8InputBufferSizeFunction, &args, &results)) {
                print_trap(trap);
                return;
            }

            if (auto trap = wasm_func_call(getFT8InputBufferFunction, &args, &results)) {
                print_trap(trap);
                return;
            }

            auto inputBufferOffset = results.data[0].of.i32;


            // printf("OK here: %d  %d\n", inputBufferOffset, maxBufferSize);

            memcpy(memory.mem + inputBufferOffset, samples, nsamples * sizeof(dsp::stereo_t));
        } {
            wasm_val_t args_val[1] = {WASM_I32_VAL((int32_t)nsamples)};
            wasm_val_t results_val[1] = {WASM_I32_VAL((int32_t)nsamples)};
            wasm_val_vec_t args = WASM_ARRAY_VEC(args_val);
            wasm_val_vec_t results = WASM_ARRAY_VEC(results_val);

            if (auto trap = wasm_func_call(decodeFT8MainFunction, &args, &results)) {
                print_trap(trap);
                return;
            }
        }

        printf("OK here too: \n");
    }
};

wasm_module_t *WasmerFT8Decoder::module;

#endif



void doDecode(const char *mode, const char *path, int threads,
              std::function<void(int mode, std::vector<std::string> result)> callback) {
    mshv_init();
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ERROR Cannot open file %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(size, 0);
    (void) fread((void *) buf.data(), size, 1, f);
    fclose(f);
    riff::ChunkHeader *riffHeader = (riff::ChunkHeader *) (buf.data());
    riff::ChunkHeader *fmtHeader = (riff::ChunkHeader *) (buf.data() + 12);
    wav::FormatHeader *hdr = (wav::FormatHeader *) (buf.data() + 12 + 8); // skip RIFF + WAV
    riff::ChunkHeader *dta = (riff::ChunkHeader *) (buf.data() + 12 + 8 + sizeof(wav::FormatHeader));
    auto *data = (float *) ((uint8_t *) dta + sizeof(riff::ChunkHeader));
    printf("Channels: %d\n", hdr->channelCount);
    printf("SampleRate: %d\n", hdr->sampleRate);
    printf("BytesPerSample: %d\n", hdr->bytesPerSample);
    printf("BitDepth: %d\n", hdr->bitDepth);
    printf("Codec: %d\n", hdr->codec);
    fflush(stdout);
    bool handled = hdr->codec == 3 && hdr->bitDepth == 32 && hdr->channelCount == 2;
    handled |= hdr->codec == 1 && hdr->bitDepth == 16 && hdr->channelCount == 2;
    if (!handled) {
        fprintf(stderr, "ERROR Want Codec/BitDepth/channels: 3/32/2 or 1/16/2\n");
        return;
    }
    int nSamples = ((char *) (buf.data() + size) - (char *) data) / 2 / (hdr->bitDepth / 8);
    printf("NSamples: %d\n", nSamples);

    std::vector<dsp::stereo_t> converted;
    switch (hdr->codec) {
        case 1: {
            // short samples
            auto ptr = (short *) dta;
            converted.resize(nSamples);
            float maxx = 0.0f;
            for (int q = 0; q < nSamples; q++) {
                converted[q].l = ptr[2 * q] / 32767.0;
                converted[q].r = ptr[2 * q + 1] / 32767.0;
                maxx = std::max<float>(maxx, converted[q].r);
                maxx = std::max<float>(maxx, converted[q].l);
            }
            data = (float *) converted.data();
            printf("d0: %f   %f   maxx: %f\n", data[100], data[101], maxx);
            break;
        }
        case 3: // ieee float
        {
            data = (float *) dta;
            break;
        }
        default:
            printf("Unhandfled wav format/codec\n");
            exit(0);
    }

    auto floatData = data;

    fflush(stdout);
    fflush(stderr);
    try {
        for (int q = 0; q < 1; q++) {
            //            spdlog::info("=================================");

            auto sampleRate = hdr->sampleRate;
            std::vector<dsp::stereo_t> resampledV;

            dsp::stereo_t *stereoData;

            if (sampleRate != 12000) {
                long long int outSize = 3 * (nSamples * 12000) / sampleRate;
                resampledV.resize(outSize);
                dsp::multirate::RationalResampler<dsp::stereo_t> res;
                res.init(nullptr, sampleRate, 12000);
                nSamples = res.process(nSamples, converted.data(), resampledV.data());
                stereoData = resampledV.data();
                printf("Resampled, samples size=2 * %zu\n", resampledV.size() / 2);
            } else {
                stereoData = (dsp::stereo_t *) floatData;
            }

            sampleRate = 12000;

            if (true) {
                for (auto t = 0; t<12; t++) {
                    std::thread x([=]() {
                        for(int q=0; q<1000; q++) {
                            usleep((rand() % 5) * 1000000);
                            auto ctm = currentTimeMillis();
                            ft8::decodeFT8(threads, mode, sampleRate, stereoData, nSamples, [](int mode, QStringList result) {
                            });
                            std::cout << "["<<t<<"]Time taken: " << currentTimeMillis() - ctm << " ms" << std::endl;
                        }

                    });
                    x.detach();
                }
                sleep(100000);
            } else {
                std::string wasmPath = "/Users/san/Fun/SDRPlusPlus/decoder_modules/ft8_decoder/wasm/sdrpp_ft8_mshv";
                if (auto override = getenv("FT8WASM")) {
                    wasmPath = override;
                }
                if (WasmerFT8Decoder::setupModule(wasmPath)) {
                    for(int q=0; q<5; q++) {
                        planAllocTime = 0;
                        planExecTime = 0;
                        auto ctm = currentTimeMillis();
                        WasmerFT8Decoder wd;
                        wd.wasmDecodeFT8(threads, mode, sampleRate, stereoData, nSamples, [](const char *line) {
                            fprintf(stdout, "%s", line);
                            fflush(stdout);
                        });
                        std::cout << "DECODE_EOF" << std::endl;
                        std::cout << "Time taken: " << currentTimeMillis() - ctm << " ms" << std::endl;
                        std::cout << "PlanAllocTime: " << planAllocTime << " ms, Plan ExecTime:" << planExecTime << " ms" << std::endl;
                    }
                }
            }
        }
    } catch (std::runtime_error &e) {
        fprintf(stderr, "ERROR %s \n", e.what());
    }
}


#ifdef __ANDROID__
#ifdef DO_WASMER


extern "C" {
    void *__errno_location() {
        return &errno;
    }
    char * __xpg_strerror_r(int errnum, char * buf, size_t buflen) {
        snprintf(buf, buflen, "system__error %d", errnum);
    }

}
#endif
#endif



static void help(const char *cmd) {
    fprintf(stderr, "usage: %s --decode <path> [--mode <mode>]\n", cmd);
    exit(1);
}

#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    auto args = wstr::wstr2str(std::wstring(pCmdLine));
//    args = "--decode C:\\Temp\\sdrpp_ft8_mshv.wav.132 --mode ft8";
    std::vector<std::string> argsV;
    splitStringV(args, " ", argsV);
    std::vector<const char*> argv;
    argv.emplace_back("");
    for (auto& q : argsV) {
        argv.emplace_back(q.c_str());
    }
    auto argc = argv.size();
#else
int main(int argc, char *argv[]) {
#endif
    std::string decodeFile;
    std::string mode = "ft8";
    int threads = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--decode")) {
            i++;
            if (i < argc) {
                decodeFile = argv[i];
            }
        }
        if (!strcmp(argv[i], "--mode")) {
            i++;
            if (i < argc) {
                mode = argv[i];
            }
        }
        if (!strcmp(argv[i], "--threads")) {
            i++;
            if (i < argc) {
                threads = atoi(argv[i]);
                if (threads < 1 || threads > 8) {
                    threads = 1;
                }
            }
        }
    }
    if (false) {
        mode = "ft4";
        decodeFile = "C:\\Temp\\sdrpp_ft8_mshv.wav.236";
    }

    if (decodeFile == "") {
        fprintf(stderr, "ERROR: wav file for decode is not specified\n");
        help(argv[0]);
    }
    if (mode == "ft8" || mode == "ft4") {
        fprintf(stdout, "Using mode: %s\n", mode.c_str());
        fprintf(stdout, "Using file: %s\n", decodeFile.c_str());
        doDecode(mode.c_str(), decodeFile.c_str(), threads, [](int mode, std::vector<std::string> result) {
        });
        exit(0);
    } else {
        fprintf(stderr, "ERROR: invalid mode is specified. Valid modes: ft8, ft4\n");
        exit(1);
    }
}
