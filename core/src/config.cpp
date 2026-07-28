#include <config.h>
#include <utils/flog.h>
#include <fstream>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <vector>
#include "utils/wstr.h"
#include "utils/usleep.h"
#include "core.h"

struct ConfigManager::SaveJob {
    std::mutex mtx;
    ConfigManager* manager = nullptr;
    bool queued = false;
    std::chrono::steady_clock::time_point due;
};

class ConfigSaveWorker {
public:
    ConfigSaveWorker() : workerThread(&ConfigSaveWorker::worker, this) {
    }

    void enqueue(const std::shared_ptr<ConfigManager::SaveJob>& job) {
        std::lock_guard<std::mutex> lock(mtx);
        std::lock_guard<std::mutex> jobLock(job->mtx);
        if (!job->manager) { return; }

        job->due = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        if (!job->queued) {
            job->queued = true;
            jobs.push_back(job);
        }
        wakeup.notify_one();
    }

    void cancel(const std::shared_ptr<ConfigManager::SaveJob>& job) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = std::remove(jobs.begin(), jobs.end(), job);
            jobs.erase(it, jobs.end());
        }
        {
            std::lock_guard<std::mutex> jobLock(job->mtx);
            job->queued = false;
            job->manager = nullptr;
        }
        wakeup.notify_one();
    }

private:
    void worker() {
        SetThreadName("config-save");
        std::unique_lock<std::mutex> lock(mtx);
        while (true) {
            if (jobs.empty()) {
                wakeup.wait(lock);
                continue;
            }

            auto next = std::min_element(jobs.begin(), jobs.end(), [](const auto& a, const auto& b) {
                return a->due < b->due;
            });
            if ((*next)->due > std::chrono::steady_clock::now()) {
                wakeup.wait_until(lock, (*next)->due);
                continue;
            }

            auto job = *next;
            jobs.erase(next);
            lock.unlock();
            bool retry = false;
            {
                std::lock_guard<std::mutex> jobLock(job->mtx);
                job->queued = false;
                if (job->manager && job->manager->mtx.try_lock()) {
                    if (job->manager->autoSaveEnabled && job->manager->changed) {
                        job->manager->changed = false;
                        job->manager->save(false);
                    }
                    job->manager->mtx.unlock();
                }
                else if (job->manager) {
                    retry = true;
                }
            }
            if (retry) {
                enqueue(job);
            }
            lock.lock();
        }
    }

    std::mutex mtx;
    std::condition_variable wakeup;
    std::vector<std::shared_ptr<ConfigManager::SaveJob>> jobs;
    std::thread workerThread;
};

static ConfigSaveWorker& configSaveWorker() {
    // The core library outlives all modules. Keep the worker alive until process
    // exit so ConfigManager destructors can safely unregister their jobs.
    static ConfigSaveWorker* worker = new ConfigSaveWorker();
    return *worker;
}

ConfigManager::ConfigManager() : saveJob(std::make_shared<SaveJob>()) {
    saveJob->manager = this;
}

ConfigManager::~ConfigManager() {
    disableAutoSave();
}

void ConfigManager::setPath(std::string file) {
    path = std::filesystem::absolute(file).string();
}

void ConfigManager::load(json def, bool lock) {
    if (lock) { mtx.lock(); }
    if (path == "") {
        flog::error("Config manager tried to load file with no path specified");
        return;
    }
    if (!std::filesystem::exists(path)) {
        flog::warn("Config file '{0}' does not exist, creating it", path);
        conf = def;
        save(false);
    }
    if (!std::filesystem::is_regular_file(path)) {
        flog::error("Config file '{0}' isn't a file", path);
        return;
    }

    auto filesize = std::filesystem::file_size(path);
    try {
        std::ifstream file(wstr::str2wstr(path));
        file >> conf;
        file.close();
    }
    catch (const std::exception e) {
        flog::error("Config file '{}' with size={} is corrupted ({}), resetting it", path, (int64_t)filesize, e.what());
        usleep(3000000);
        conf = def;
        save(false);
    }
    if (lock) { mtx.unlock(); }
}

void ConfigManager::save(bool lock) {
    if (lock) { mtx.lock(); }
    auto justpath = wstr::str2wstr(path);
    auto newpath = wstr::str2wstr(path + ".new");
    std::ofstream file(newpath);
    file << conf.dump(4);
    file.close();
    std::filesystem::rename(newpath, justpath);
    if (lock) { mtx.unlock(); }
}

void ConfigManager::enableAutoSave() {
    bool expected = false;
    if (!autoSaveEnabled.compare_exchange_strong(expected, true)) { return; }
    {
        std::lock_guard<std::mutex> lock(saveJob->mtx);
        saveJob->manager = this;
    }
    configSaveWorker().enqueue(saveJob);
}

void ConfigManager::disableAutoSave() {
    if (!autoSaveEnabled.exchange(false)) { return; }
    configSaveWorker().cancel(saveJob);
}

void ConfigManager::acquire() {
    mtx.lock();
}

void ConfigManager::release(bool modified) {
    bool queueSave = false;
    if (modified) {
        changed = true;
        queueSave = autoSaveEnabled;
    }
    mtx.unlock();
    if (queueSave) {
        configSaveWorker().enqueue(saveJob);
    }
}
