#include <core.h>
#include <iostream>
#include <stdio.h>
#include <module.h>
#include <ctm.h>
#include <fftw3.h>
#include <utils/wav.h>
#include <utils/riff.h>
#include "dsp/types.h"
#include "dsp/multirate/rational_resampler.h"

#include "ft8_etc/mshv_support.h"
#include <wasmer.h>
#include "fftw_mshv_plug.h"
#include "fftw_mshv_plug_original.h"

namespace ft8 {
    // input stereo samples, nsamples (number of pairs of float)
    void decodeFT8(int threads, const char *mode, int sampleRate, dsp::stereo_t *samples, int nsamples,
                   std::function<void(int mode, QStringList result)> callback);
}

wasm_engine_t *wasmEngine;
wasm_store_t *wasmEngineStore;

struct WasmerFT8Decoder {
    static wasm_module_t *module;

    static void setupModule(const std::string &wasmPath) {
        if (!wasmEngine) {
            wasmEngine = wasm_engine_new();
            wasmEngineStore = wasm_store_new(wasmEngine);
        }
        FILE *file = fopen(wasmPath.c_str(), "rb");
        if (!file) {
            abort();
        }
        fseek(file, 0L, SEEK_END);
        size_t file_size = ftell(file);
        fseek(file, 0L, SEEK_SET);
        wasm_byte_vec_t binary;
        wasm_byte_vec_new_uninitialized(&binary, file_size);
        if (fread(binary.data, file_size, 1, file) != 1) {
            printf("> Error loading module!\n");
            return;
        }
        fclose(file);
        module = wasm_module_new(wasmEngineStore, &binary);
        if (!module) {
            printf("> Error compiling module!\n");
        }
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

        void put_string(size_t offset, const char *str) {
            if (offset + strlen(str) + 1 <= limit) {
                strcpy(mem + offset, str);
            }
        }

        char * dump(size_t offset) {
            static char buf[10000];
            buf[0] = 0;
            for(int q=0;q < 32; q++) {
                sprintf(buf + strlen(buf),"%02x ", mem[offset+q]);
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
    wasm_func_t *wasmMallocFunction;
    wasm_func_t *wasmFreeFunction;


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


    WasmerFT8Decoder() {
        std::vector<wasm_extern_t *> externs;
        std::unordered_map<std::string, wasm_func_t *> exported;

        wasm_importtype_vec_t imports;
        wasm_module_imports(module, &imports);

        wasm_store_t *store = wasm_store_new(wasmEngine);


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
            printf("import: %s: name = %s\n", ctyp, nm.c_str());
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
            if (nm ==  "decodeResultOutput") {
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
            if (nm ==  "fftplug_allocate_plan_r2c") {
                auto t1 = wasm_functype_new_1_1(wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, fftplug_allocate_plan_r2c_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if (nm == "fftplug_allocate_plan_c2r") {
                auto t1 = wasm_functype_new_1_1(wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, fftplug_allocate_plan_c2r_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if (nm == "fftplug_get_complex_input") {
                auto t1 = wasm_functype_new_1_1(wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, fftplug_get_complex_input_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if (nm ==  "fftplug_get_complex_output") {
                auto t1 = wasm_functype_new_1_1(wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, fftplug_get_complex_output_host, this, nullptr);
                externs.emplace_back(wasm_func_as_extern(fun));
                continue;
            }
            if ((nm == "fftplug_get_float_input")) {
                auto t1 = wasm_functype_new_1_1(wasm_valtype_new_i32(), wasm_valtype_new_i32());
                auto fun = wasm_func_new_with_env(store, t1, fftplug_get_float_input_host, this, nullptr);
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
                auto t1 = wasm_functype_new_1_0(wasm_valtype_new_i32());
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
            printf("export/extern: %s: name = %s\n", ctyp, nm.c_str());
            if ((nm == "memory")) {
                externMemory = wasm_extern_as_memory(iexterns.data[q]);
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
            if ((nm == "wasmMalloc")) {
                wasmMallocFunction = wasm_extern_as_func(iexterns.data[q]);
                continue;
            }
            if ((nm == "wasmFree")) {
                wasmFreeFunction = wasm_extern_as_func(iexterns.data[q]);
                continue;
            }
            printf("Unknown exported function, go away: %s\n", nm.c_str());
            return;
        }
    }

    static wasm_trap_t *decodeResultOutput_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto *thiz = (WasmerFT8Decoder *) env;
        auto offset = args->data[0].of.i32;
        printf(":: %s\n", (const char*)thiz->memory.mem+offset);
        return nullptr;
    }

    static wasm_trap_t *fftplug_allocate_plan_c2c_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto *thiz = (WasmerFT8Decoder *) env;
        auto nfft = args->data[0].of.i32;
        auto forward = args->data[1].of.i32;
        FFT_PLAN plan = Fftplug_allocate_plan_c2c(nativeStorage, nfft, forward, *thiz);
        results->data[0] = WASM_I32_VAL(plan.handle);
        return nullptr;
    }

    static wasm_trap_t *fftplug_allocate_plan_c2r_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto *thiz = (WasmerFT8Decoder *) env;
        auto nfft = args->data[0].of.i32;
        FFT_PLAN plan = Fftplug_allocate_plan_c2r(nativeStorage, nfft, *thiz);
        results->data[0] = WASM_I32_VAL(plan.handle);
        return nullptr;
    }

    static wasm_trap_t *fftplug_allocate_plan_r2c_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto *thiz = (WasmerFT8Decoder *) env;
        auto nfft = args->data[0].of.i32;
        FFT_PLAN plan = Fftplug_allocate_plan_r2c(nativeStorage, nfft, *thiz);
        results->data[0] = WASM_I32_VAL(plan.handle);
        return nullptr;
    }

    static wasm_trap_t *fftplug_get_complex_input_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto *thiz = (WasmerFT8Decoder *) env;
        FFT_PLAN plan = { args->data[0].of.i32 };
        auto *ptr = (byte_t *)Fftplug_get_complex_input(nativeStorage, plan);
        results->data[0] = WASM_I32_VAL((int)(ptr - thiz->memory.mem));
        return nullptr;
    }

    static wasm_trap_t *fftplug_get_complex_output_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto *thiz = (WasmerFT8Decoder *) env;
        FFT_PLAN plan = { args->data[0].of.i32 };
        auto *ptr = (byte_t *)Fftplug_get_complex_output(nativeStorage, plan);
        results->data[0] = WASM_I32_VAL((int)(ptr - thiz->memory.mem));
        return nullptr;
    }

    static wasm_trap_t *fftplug_get_float_output_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto *thiz = (WasmerFT8Decoder *) env;
        FFT_PLAN plan = { args->data[0].of.i32 };
        auto *ptr = (byte_t *)Fftplug_get_float_output(nativeStorage, plan);
        results->data[0] = WASM_I32_VAL((int)(ptr - thiz->memory.mem));
        return nullptr;
    }

    static wasm_trap_t *fftplug_get_float_input_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        auto *thiz = (WasmerFT8Decoder *) env;
        FFT_PLAN plan = { args->data[0].of.i32 };
        auto *ptr = (byte_t *)Fftplug_get_float_input(nativeStorage, plan);
        results->data[0] = WASM_I32_VAL((int)(ptr - thiz->memory.mem));
        return nullptr;
    }

    static wasm_trap_t *fftplug_free_plan_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        FFT_PLAN plan = { args->data[0].of.i32 };
        Fftplug_free_plan(nativeStorage, plan);
        return nullptr;
    }

    static wasm_trap_t *fftplug_execute_plan_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        FFT_PLAN plan = { args->data[0].of.i32 };
        Fftplug_execute_plan(nativeStorage, plan);
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
        printf("Callback!!\n");
        abort();
    }

    static wasm_trap_t *sched_yield_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results) {
        printf("Sched yield!!\n");
        abort();
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
        printf("unref abort!!\n");
        abort();
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
        for(int i=0; i<iovsLength; i++) {
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

    float *arrayAllocatorFloat(int count) {
        return (float*)callWasmMalloc(count * sizeof(float));
    }
    fftwf_complex *arrayAllocatorComplex(int count) {
        return (fftwf_complex*)callWasmMalloc(count * sizeof(fftwf_complex));
    }
    void deallocatorFloat(float *f) {
        return callWasmFree(f);
    }
    void deallocatorComplex(fftwf_complex *f) {
        return callWasmFree(f);
    }


    void callWasmFree(void *ptr) {
        auto bptr = (byte_t *)ptr;
        auto offset = bptr - memory.mem;
        wasm_val_t args_val[1] = {WASM_I32_VAL((int)offset)};
        wasm_val_t results_val[] = {};
        wasm_val_vec_t args = WASM_ARRAY_VEC(args_val);
        wasm_val_vec_t results = WASM_ARRAY_VEC(results_val);

        if (auto trap = wasm_func_call(wasmFreeFunction, &args, &results)) {
            print_trap(trap);
        }
    }

    void *callWasmMalloc(int size) {
        wasm_val_t args_val[1] = {WASM_I32_VAL(size)};
        wasm_val_t results_val[1] = {WASM_I32_VAL(0)};
        wasm_val_vec_t args = WASM_ARRAY_VEC(args_val);
        wasm_val_vec_t results = WASM_ARRAY_VEC(results_val);

        if (auto trap = wasm_func_call(wasmMallocFunction, &args, &results)) {
            print_trap(trap);
            return nullptr;
        }
        auto valu = results.data[0].of.i32;
        if (!valu) {
            abort();
        }
        return memory.mem + valu;
    }


    static errno_t clock_time_get(
        int32_t clock_id,
        uint64_t precision,
        int32_t destptr
    ) {
        using namespace std::chrono;

        if (destptr == 0) {
            return EINVAL;
        }

        /*

        switch (clock_id) {
            case 1: // __WASI_CLOCKID_MONOTONIC:
            {
                auto now = steady_clock::now().time_since_epoch();
                *time = duration_cast<nanoseconds>(now).count();
                break;
            }
            case 0: // __WASI_CLOCKID_REALTIME:
            {
                auto now = system_clock::now().time_since_epoch();
                *time = duration_cast<nanoseconds>(now).count();
                break;
            }
            default:
                return EINVAL;
        }

        */

        return 0;
    }

    void decodeFT8(int threads, const char *mode, int sampleRate, dsp::stereo_t *samples, long long nsamples,
                   const std::function<void(const char *)> &dest) { {
            wasm_val_t args_val[0] = {};
            wasm_val_t results_val[1] = {WASM_I32_VAL(0)};
            wasm_val_vec_t args = WASM_ARRAY_VEC(args_val);
            wasm_val_vec_t results = WASM_ARRAY_VEC(results_val);

            if (auto trap = wasm_func_call(getFT8InputBufferSizeFunction, &args, &results)) {
                print_trap(trap);
                return;
            }

            auto maxBufferSize = results.data[0].of.i32;

            if (auto trap = wasm_func_call(getFT8InputBufferFunction, &args, &results)) {
                print_trap(trap);
                return;
            }

            auto inputBufferOffset = results.data[0].of.i32;


            printf("OK here: %d  %d\n", inputBufferOffset, maxBufferSize);

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
    uint8_t *buf = (uint8_t *) malloc(size);
    if (!buf) {
        fprintf(stderr, "ERROR Cannot alloc %lld\n", size);
        exit(1);
    }
    (void) fread((void *) buf, size, 1, f);
    fclose(f);
    riff::ChunkHeader *riffHeader = (riff::ChunkHeader *) (buf);
    riff::ChunkHeader *fmtHeader = (riff::ChunkHeader *) (buf + 12);
    wav::FormatHeader *hdr = (wav::FormatHeader *) (buf + 12 + 8); // skip RIFF + WAV
    riff::ChunkHeader *dta = (riff::ChunkHeader *) (buf + 12 + 8 + sizeof(wav::FormatHeader));
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
    int nSamples = ((char *) (buf + size) - (char *) data) / 2 / (hdr->bitDepth / 8);
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
            auto ctm = currentTimeMillis();
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

            if (false) {
                ft8::decodeFT8(threads, mode, sampleRate, stereoData, nSamples, [](int mode, QStringList result) {
                });
            } else {
                WasmerFT8Decoder::setupModule(
                    "/Users/san/Fun/SDRPlusPlus/decoder_modules/ft8_decoder/wasm/sdrpp_ft8_mshv");
                WasmerFT8Decoder wd;
                wd.decodeFT8(threads, mode, sampleRate, stereoData, nSamples, [](const char *line) {
                    fprintf(stdout, "%s", line);
                    fflush(stdout);
                });
                std::cout << "Time taken: " << currentTimeMillis() - ctm << " ms" << std::endl;
                std::cout << "DECODE_EOF" << std::endl;
                std::cout << "DECODE_EOF" << std::endl;
                fflush(stdout);
            }
        }
    } catch (std::runtime_error &e) {
        fprintf(stderr, "ERROR %s \n", e.what());
    }
}


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
