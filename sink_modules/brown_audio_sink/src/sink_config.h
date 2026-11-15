#pragma once
#include <config.h>

// Always release the lock, including when JSON access or allocation throws.
class SinkConfigLock {
public:
    explicit SinkConfigLock(ConfigManager& manager) : manager(manager) { manager.acquire(); }
    ~SinkConfigLock() { manager.release(modified); }
    SinkConfigLock(const SinkConfigLock&) = delete;
    SinkConfigLock& operator=(const SinkConfigLock&) = delete;
    bool modified = false;
private:
    ConfigManager& manager;
};

inline json& sinkSettings(json& root, const std::string& name) {
    if (!root.is_object()) { root = json::object(); }
    auto& settings = root[name];
    if (!settings.is_object()) { settings = json::object(); }
    if (!settings["device"].is_string()) { settings["device"] = ""; }
    if (!settings["devices"].is_object()) { settings["devices"] = json::object(); }
    return settings;
}

inline unsigned int sinkSampleRate(const json& value, unsigned int fallback) {
    if (!value.is_number_integer()) { return fallback; }
    // Bound before conversion, so negative/oversized JSON integers cannot wrap.
    if (value < 8000 || value > 768000) { return fallback; }
    return value.get<unsigned int>();
}
