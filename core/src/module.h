#pragma once
#include <string>
#include <map>
#include <json.hpp>
#include <utils/event.h>
#include "sdrpp_export.h"


#ifdef _WIN32
#include <Windows.h>
#define MOD_EXPORT           extern "C" __declspec(dllexport)
#define SDRPP_MOD_EXTENTSION ".dll"
#else
#include <dlfcn.h>
#define MOD_EXPORT extern "C"
#ifdef __APPLE__
#define SDRPP_MOD_EXTENTSION ".dylib"
#else
#define SDRPP_MOD_EXTENTSION ".so"
#endif
#endif

class ModuleManager {
public:
    struct ModuleInfo_t {
        const char* name;
        const char* description;
        const char* author;
        const int versionMajor;
        const int versionMinor;
        const int versionBuild;
        const int maxInstances;
    };

    class Instance {
    public:
        virtual ~Instance() {}
        virtual void postInit() = 0;
        virtual void enable() = 0;
        virtual void disable() = 0;
        virtual bool isEnabled() = 0;
        virtual void *getInterface(const char *name) {
            return nullptr;
        }
    };

    struct Module_t {
#ifdef _WIN32
        HMODULE handle;
#else
        void* handle;
#endif
        ModuleManager::ModuleInfo_t* info;
        void (*init)();
        ModuleManager::Instance* (*createInstance)(std::string name);
        void (*deleteInstance)(ModuleManager::Instance* instance);
        void (*end)();

        friend bool operator==(const Module_t& a, const Module_t& b) {
            if (a.handle != b.handle) { return false; }
            if (a.info != b.info) { return false; }
            if (a.init != b.init) { return false; }
            if (a.createInstance != b.createInstance) { return false; }
            if (a.deleteInstance != b.deleteInstance) { return false; }
            if (a.end != b.end) { return false; }
            return true;
        }
    };

    struct Instance_t {
        ModuleManager::Module_t module;
        ModuleManager::Instance* instance;
    };

    ModuleManager::Module_t loadModule(std::string path);

    int createInstance(std::string name, std::string module);
    int deleteInstance(std::string name);
    int deleteInstance(ModuleManager::Instance* instance);

    int enableInstance(std::string name);
    int disableInstance(std::string name);
    bool instanceEnabled(std::string name);
    void postInit(std::string name);
    std::string getInstanceModuleName(std::string name);

    int countModuleInstances(std::string module);

    template <typename T>
    std::vector<T*> getAllInterfaces(const std::string &interfaceName) {
        std::vector<T *> retval;
        for(auto x: instances) {
            if (x.second.instance == nullptr) {
                continue;
            }
            auto rv = x.second.instance->getInterface(interfaceName.c_str());
            if (rv != nullptr) {
                retval.emplace_back((T*)rv);
            }
        }
        return retval;
    }

    void *getInterface(const std::string &name, const std::string &interfaceName) {
        if (name != "") {       //
            auto it = instances.find(name);
            if (it == instances.end()) {
                return nullptr;
            }
            if (it->second.instance == nullptr) {
                return nullptr;
            }
            return it->second.instance->getInterface(interfaceName.c_str());
        }
        // no name given -> first one
        for(auto x: instances) {
            if (x.second.instance == nullptr) {
                continue;
            }
            auto rv = x.second.instance->getInterface(interfaceName.c_str());
            if (rv != nullptr) {
                return rv;
            }
        }
        return nullptr;
    }

    void doPostInitAll();

    Event<std::string> onInstanceCreated;
    Event<std::string> onInstanceDelete;
    Event<std::string> onInstanceDeleted;

    std::map<std::string, ModuleManager::Module_t> modules;
    std::map<std::string, ModuleManager::Instance_t> instances;

#ifdef BUILD_TESTS
    // Plugin whitelist for test mode
    std::vector<std::string> pluginWhitelist;
    bool useWhitelist = false;
#endif

};

#define SDRPP_MOD_INFO MOD_EXPORT const ModuleManager::ModuleInfo_t _INFO_

