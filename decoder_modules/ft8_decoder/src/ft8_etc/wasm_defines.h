#pragma once

#ifdef __wasm__
#define WASM_EXPORT(name) __attribute__((visibility("default"))) __attribute__((export_name(name)))
#define WASM_IMPORT2(module, name) __attribute__((import_module(module))) __attribute__((import_name(name)))

#else
#define WASM_EXPORT(name)
#define WASM_IMPORT2(module, name)
#endif

#define WASM_IMPORT(name) WASM_IMPORT2("env", name)
