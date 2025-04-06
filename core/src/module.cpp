#include <module.h>
#include <filesystem>
#include <utils/wstr.h>
#include <utils/flog.h>

ModuleManager::Module_t ModuleManager::loadModule(std::string path) {
    Module_t mod;

#ifdef BUILD_TESTS
    // Check if we're using a whitelist and if this module is in it
    if (useWhitelist) {
        // Extract the filename from the path
        std::string filename;
        size_t lastSlash = path.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            filename = path.substr(lastSlash + 1);
        } else {
            filename = path;
        }

        // Check if this plugin is in the whitelist
        bool allowed = false;
        for (const auto& plugin : pluginWhitelist) {
            // Check for exact match or match with extension
            if (filename == plugin ||
                filename == plugin + SDRPP_MOD_EXTENTSION) {
                allowed = true;
                break;
            }
        }

        if (!allowed) {
            flog::info("Skipping module {0} (not in whitelist)", path);
            mod.handle = NULL;
            return mod;
        }
    }
#endif

    // On android, the path has to be relative, don't make it absolute
#ifndef __ANDROID__
    if (!std::filesystem::exists(path)) {
        flog::error("{0} does not exist", path);
        mod.handle = NULL;
        return mod;
    }
    if (!std::filesystem::is_regular_file(path)) {
        flog::error("{0} isn't a loadable module", path);
        mod.handle = NULL;
        return mod;
    }
#endif
#ifdef _WIN32
    auto wide = wstr::str2wstr(path);
    wchar_t wideBuf[1024];
    GetShortPathNameW(wide.c_str(), wideBuf, sizeof(wideBuf)/sizeof(TCHAR));
    mod.handle = LoadLibraryExW(wideBuf, NULL, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);


    if (mod.handle == NULL) {
        auto err = GetLastError();
        LPWSTR errorMessageBuffer = NULL;
        size_t size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&errorMessageBuffer, 0, NULL);
        auto narrow = wstr::wstr2str(errorMessageBuffer);

        flog::error("Couldn't LoadLibraryExW {0}. Error: {1} - {2}", path.c_str(), (int64_t)err, narrow.c_str());
        LocalFree(errorMessageBuffer);
        mod.handle = NULL;
        return mod;
    }
    mod.info = (ModuleInfo_t*)GetProcAddress(mod.handle, "_INFO_");
    mod.init = (void (*)())GetProcAddress(mod.handle, "_INIT_");
    mod.createInstance = (Instance * (*)(std::string)) GetProcAddress(mod.handle, "_CREATE_INSTANCE_");
    mod.deleteInstance = (void (*)(Instance*))GetProcAddress(mod.handle, "_DELETE_INSTANCE_");
    mod.end = (void (*)())GetProcAddress(mod.handle, "_END_");
#else
    try {
        mod.handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    } catch(std::exception& e) {
        flog::error("Couldn't load {}: {}", path, e.what());
        mod.handle = NULL;
    } catch(...) {
        flog::error("Couldn't load {0}.", path);
        mod.handle = NULL;
    }
    if (mod.handle == NULL) {
        char *err = dlerror();
        flog::error("Couldn't load {0}: {1}.", path, err);
        mod.handle = NULL;
        return mod;
    }
    mod.info = (ModuleInfo_t*)dlsym(mod.handle, "_INFO_");
    mod.init = (void (*)())dlsym(mod.handle, "_INIT_");
    mod.createInstance = (Instance * (*)(std::string)) dlsym(mod.handle, "_CREATE_INSTANCE_");
    mod.deleteInstance = (void (*)(Instance*))dlsym(mod.handle, "_DELETE_INSTANCE_");
    mod.end = (void (*)())dlsym(mod.handle, "_END_");
#endif
    if (mod.info == NULL) {
        flog::error("{0} is missing _INFO_ symbol", path);
        mod.handle = NULL;
        return mod;
    }
    if (mod.init == NULL) {
        flog::error("{0} is missing _INIT_ symbol", path);
        mod.handle = NULL;
        return mod;
    }
    if (mod.createInstance == NULL) {
        flog::error("{0} is missing _CREATE_INSTANCE_ symbol", path);
        mod.handle = NULL;
        return mod;
    }
    if (mod.deleteInstance == NULL) {
        flog::error("{0} is missing _DELETE_INSTANCE_ symbol", path);
        mod.handle = NULL;
        return mod;
    }
    if (mod.end == NULL) {
        flog::error("{0} is missing _END_ symbol", path);
        mod.handle = NULL;
        return mod;
    }
    if (modules.find(mod.info->name) != modules.end()) {
        flog::error("{0} has the same name as an already loaded module", path);
        mod.handle = NULL;
        return mod;
    }
    for (auto const& [name, _mod] : modules) {
        if (mod.handle == _mod.handle) {
            return _mod;
        }
    }
    try {
        mod.init();
        modules[mod.info->name] = mod;
        flog::info(" ..... ok {}", path);
        return mod;
    } catch(std::exception& e) {
        flog::error("Failed to initialize module {0}: {}", path, e.what());
        mod.handle = NULL;
        return mod;
    }
}

int ModuleManager::createInstance(std::string name, std::string module) {
    if (modules.find(module) == modules.end()) {
        flog::error("Module '{0}' doesn't exist", module);
        return -1;
    }
    if (instances.find(name) != instances.end()) {
        flog::error("A module instance with the name '{0}' already exists", name);
        return -1;
    }
    int maxCount = modules[module].info->maxInstances;
    if (countModuleInstances(module) >= maxCount && maxCount > 0) {
        flog::error("Maximum number of instances reached for '{0}'", module);
        return -1;
    }
    Instance_t inst;
    inst.module = modules[module];
    inst.instance = inst.module.createInstance(name);
    instances[name] = inst;
    onInstanceCreated.emit(name);
    return 0;
}

int ModuleManager::deleteInstance(std::string name) {
    if (instances.find(name) == instances.end()) {
        flog::error("Tried to remove non-existent instance '{0}'", name);
        return -1;
    }
    onInstanceDelete.emit(name);
    Instance_t inst = instances[name];
    inst.module.deleteInstance(inst.instance);
    instances.erase(name);
    onInstanceDeleted.emit(name);
    return 0;
}

int ModuleManager::deleteInstance(ModuleManager::Instance* instance) {
    flog::error("Delete instance not implemented");
    return -1;
}

int ModuleManager::enableInstance(std::string name) {
    if (instances.find(name) == instances.end()) {
        flog::error("Cannot enable '{0}', instance doesn't exist", name);
        return -1;
    }
    instances[name].instance->enable();
    return 0;
}

int ModuleManager::disableInstance(std::string name) {
    if (instances.find(name) == instances.end()) {
        flog::error("Cannot disable '{0}', instance doesn't exist", name);
        return -1;
    }
    instances[name].instance->disable();
    return 0;
}

bool ModuleManager::instanceEnabled(std::string name) {
    if (instances.find(name) == instances.end()) {
        flog::error("Cannot check if '{0}' is enabled, instance doesn't exist", name);
        return false;
    }
    return instances[name].instance->isEnabled();
}

void ModuleManager::postInit(std::string name) {
    if (instances.find(name) == instances.end()) {
        flog::error("Cannot post-init '{0}', instance doesn't exist", name);
        return;
    }
    instances[name].instance->postInit();
}

std::string ModuleManager::getInstanceModuleName(std::string name) {
    if (instances.find(name) == instances.end()) {
        flog::error("Cannot get module name of'{0}', instance doesn't exist", name);
        return "";
    }
    return std::string(instances[name].module.info->name);
}

int ModuleManager::countModuleInstances(std::string module) {
    if (modules.find(module) == modules.end()) {
        flog::error("Cannot count instances of '{0}', Module doesn't exist", module);
        return -1;
    }
    ModuleManager::Module_t mod = modules[module];
    int count = 0;
    for (auto const& [name, instance] : instances) {
        if (instance.module == mod) { count++; }
    }
    return count;
}

void ModuleManager::doPostInitAll() {
    for (auto& [name, inst] : instances) {
        flog::info("Running post-init for {0}", name);
        inst.instance->postInit();
    }
}
