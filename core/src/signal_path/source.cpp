#include <server.h>
#include <signal_path/source.h>
#include <utils/flog.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <http_debug_server.h>

namespace {
    std::string currentSourceType;
    SourceManager::SourceHandler* selectedHandlerRef = nullptr;

    void registerSourceConfigEndpoint(const std::string& sourceName, SourceManager::SourceHandler* handler) {
        httpdebug::procfs::registerEndpoint("/source/" + sourceName + "/config", [sourceName]() -> std::string {
                core::configManager.acquire();
                std::string json = core::configManager.conf[sourceName].dump();
                core::configManager.release();
                return json; }, [sourceName](const std::string& val) {
                try {
                    json j = json::parse(val);
                    core::configManager.acquire();
                    core::configManager.conf[sourceName].update(j);
                    core::configManager.release(true);
                } catch (...) {} }, httpdebug::procfs::Type::String);
    }
}

SourceManager::SourceManager() {
    nullSource.origin = "source.nullsource";

    httpdebug::procfs::registerEndpoint("/source/type", []() -> std::string { return currentSourceType; }, [](const std::string& val) {
            currentSourceType = val;
            httpdebug::requestSourceChange(val); }, httpdebug::procfs::Type::String);

    httpdebug::procfs::registerEndpoint("/source/type:options", []() -> std::string {
            auto names = sigpath::sourceManager.getSourceNames();
            std::string json = "[";
            for (size_t i = 0; i < names.size(); i++) {
                if (i > 0) json += ",";
                std::string escaped;
                for (char c : names[i]) {
                    if (c == '"') escaped += "\\\"";
                    else if (c == '\\') escaped += "\\\\";
                    else escaped += c;
                }
                json += "\"" + escaped + "\"";
            }
            json += "]";
            return json; }, nullptr, httpdebug::procfs::Type::String);
}

void SourceManager::registerSource(std::string name, SourceHandler* handler) {
    if (sources.find(name) != sources.end()) {
        flog::error("Tried to register new source with existing name: {0}", name);
        return;
    }
    sources[name] = handler;
    onSourceRegistered.emit(name);
}

void SourceManager::unregisterSource(std::string name) {
    if (sources.find(name) == sources.end()) {
        flog::error("Tried to unregister non existent source: {0}", name);
        return;
    }
    onSourceUnregister.emit(name);
    if (name == selectedName) {
        if (selectedHandler != NULL) {
            sources[selectedName]->deselectHandler(sources[selectedName]->ctx);
        }
        sigpath::iqFrontEnd.setInput(&nullSource);
        selectedHandler = NULL;
    }
    sources.erase(name);
    onSourceUnregistered.emit(name);
}

std::vector<std::string> SourceManager::getSourceNames() {
    std::vector<std::string> names;
    for (auto const& [name, src] : sources) { names.push_back(name); }
    return names;
}

void SourceManager::selectSource(std::string name) {
    if (sources.find(name) == sources.end()) {
        flog::error("Tried to select non existent source: {0}", name);
        return;
    }
    if (selectedHandler != NULL) {
        sources[selectedName]->deselectHandler(sources[selectedName]->ctx);
    }
    selectedHandler = sources[name];
    selectedHandler->selectHandler(selectedHandler->ctx);
    selectedName = name;
    currentSourceType = name;
    selectedHandlerRef = selectedHandler;
    if (core::args["server"].b()) {
        server::setInput(selectedHandler->stream);
    }
    else {
        sigpath::iqFrontEnd.setInput(selectedHandler->stream);
    }
    // Set server input here
    registerSourceConfigEndpoint(name, selectedHandler);
    onSourceSelected.emit(name);
}

void SourceManager::showSelectedMenu() {
    if (selectedHandler == NULL) {
        return;
    }
    selectedHandler->menuHandler(selectedHandler->ctx);
}

void SourceManager::start() {
    if (selectedHandler == NULL) {
        return;
    }
    selectedHandler->startHandler(selectedHandler->ctx);
}

void SourceManager::stop() {
    if (selectedHandler == NULL) {
        return;
    }
    selectedHandler->stopHandler(selectedHandler->ctx);
}

void SourceManager::tune(double freq) {
    if (selectedHandler == NULL) {
        return;
    }
    // TODO: No need to always retune the hardware in Panadapter mode
    selectedHandler->tuneHandler(abs(((tuneMode == TuningMode::NORMAL) ? freq : ifFreq) + tuneOffset), selectedHandler->ctx);
    onRetune.emit(freq);
    currentFreq = freq;
    onTuneChanged.emit(freq);
}

void SourceManager::setTuningOffset(double offset) {
    tuneOffset = offset;
    tune(currentFreq);
}

void SourceManager::setTuningMode(TuningMode mode) {
    tuneMode = mode;
    tune(currentFreq);
}

void SourceManager::setPanadapterIF(double freq) {
    ifFreq = freq;
    tune(currentFreq);
}