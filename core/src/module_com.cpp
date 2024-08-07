#include <module_com.h>
#include <utils/flog.h>

bool ModuleComManager::registerInterface(std::string moduleName, std::string name, void (*handler)(int code, void* in, void* out, void* ctx), void* ctx) {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    if (interfaces.find(name) != interfaces.end()) {
        flog::error("Tried creating module interface with an existing name: {0}", name);
        return false;
    }
    ModuleComInterface iface;
    iface.moduleName = moduleName;
    iface.handler = handler;
    iface.ctx = ctx;
    interfaces[name] = iface;
    return true;
}

bool ModuleComManager::unregisterInterface(std::string name) {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    if (interfaces.find(name) == interfaces.end()) {
        flog::error("Tried to erase module interface with unknown name: {0}", name);
        return false;
    }
    interfaces.erase(name);
    return true;
}

bool ModuleComManager::interfaceExists(std::string name) {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    if (interfaces.find(name) == interfaces.end()) { return false; }
    return true;
}

std::string ModuleComManager::getModuleName(std::string name) {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    if (interfaces.find(name) == interfaces.end()) {
        flog::error("Tried to call unknown module interface: {0}", name);
        return "";
    }
    return interfaces[name].moduleName;
}

bool ModuleComManager::callInterface(std::string name, int code, void* in, void* out) {
    std::lock_guard<std::recursive_mutex> lck(mtx);
    if (interfaces.find(name) == interfaces.end()) {
        flog::error("Tried to call unknown module interface: {0}", name);
        return false;
    }
    ModuleComInterface iface = interfaces[name];
    iface.handler(code, in, out, iface.ctx);
    return true;
}

std::vector<std::string> ModuleComManager::findInterfaces(std::string moduleName) {
    std::lock_guard lck(mtx);
    std::vector<std::string> result;
    for(auto [f, s] : interfaces) {
        if (s.moduleName == moduleName) {
            result.push_back(f);
        }
    }
    return result;
}
