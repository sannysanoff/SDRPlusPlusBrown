#pragma once
#include <json.hpp>
#include <thread>
#include <string>
#include <mutex>
#include <memory>
#include <atomic>

using nlohmann::json;

class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();
    void setPath(std::string file);
    void load(json def, bool lock = true);
    void save(bool lock = true);
    void enableAutoSave();
    void disableAutoSave();
    void acquire();
    void release(bool modified = false);

    json conf;

private:
    struct SaveJob;

    std::string path = "";
    bool changed = false;
    std::atomic<bool> autoSaveEnabled = false;
    std::shared_ptr<SaveJob> saveJob;
    std::mutex mtx;

    friend class ConfigSaveWorker;
};
