#include <cassert>
#include <iostream>
#include "../../../sink_modules/brown_audio_sink/src/sink_config.h"

// Same locking primitive as ConfigManager, without the unrelated disk worker.
ConfigManager::ConfigManager() {}
ConfigManager::~ConfigManager() {}
void ConfigManager::acquire() { mtx.lock(); }
void ConfigManager::release(bool) { mtx.unlock(); }

int main() {
    ConfigManager manager;
    for (json broken : {json(nullptr), json(false), json(1), json("bad"), json::array()}) {
        manager.conf = broken;
        {
            SinkConfigLock lock(manager);
            auto& settings = sinkSettings(manager.conf, "Radio");
            assert(settings["device"] == "");
            assert(settings["devices"].is_object());
            settings["device"] = broken;
        }
        try {
            SinkConfigLock lock(manager);
            manager.conf["Radio"]["device"].get<std::string>();
        } catch (const json::exception&) {}
        // Would deadlock if the exception left the nonrecursive mutex locked.
        SinkConfigLock lock(manager);
        assert(sinkSettings(manager.conf, "Radio")["device"].is_string());
    }
    for (json value : {json(nullptr), json("48000"), json(-1), json(0), json(48000.5), json(UINT64_MAX)}) {
        assert(sinkSampleRate(value, 44100) == 44100);
    }
    assert(sinkSampleRate(json(48000), 44100) == 48000);
    std::cout << "Malformed config recovery and exception-safe unlocking passed\n";
}
