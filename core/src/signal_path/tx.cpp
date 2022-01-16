#include <signal_path/source.h>
#include <spdlog/spdlog.h>
#include <signal_path/signal_path.h>

TransmitterManager::TransmitterManager() {
}

void TransmitterManager::registerTransmitter(std::string name, TransmitterHandler* handler) {
    if (transmitters.find(name) != transmitters.end()) {
        spdlog::error("Tried to register new source with existing name: {0}", name);
        return;
    }
    transmitters[name] = handler;
    onTransmitterRegistered.emit(name);
}

void TransmitterManager::unregisterTransmitter(std::string name) {
    if (transmitters.find(name) == transmitters.end()) {
        spdlog::error("Tried to unregister non existent source: {0}", name);
        return;
    }
    onTransmitterUnregister.emit(name);
    if (name == selectedName) {
        if (selectedHandler != NULL) {
            transmitters[selectedName]->deselectHandler(transmitters[selectedName]->ctx);
        }
//        sigpath::signalPath.setInput(&nullTransmitter);
        selectedHandler = NULL;
    }
    transmitters.erase(name);
    onTransmitterUnregistered.emit(name);
}

std::vector<std::string> TransmitterManager::getTransmitterNames() {
    std::vector<std::string> names;
    for (auto const& [name, src] : transmitters) { names.push_back(name); }
    return names;
}

void TransmitterManager::selectTransmitter(std::string name) {
    if (transmitters.find(name) == transmitters.end()) {
        spdlog::error("Tried to select non existent source: {0}", name);
        return;
    }
    if (selectedHandler != NULL) {
        transmitters[selectedName]->deselectHandler(transmitters[selectedName]->ctx);
    }
    selectedHandler = transmitters[name];
    selectedHandler->selectHandler(selectedHandler->ctx);
    selectedName = name;
//    sigpath::transmitSignalPath.set(selectedHandler->stream);
}

void TransmitterManager::showSelectedMenu() {
    if (selectedHandler == NULL) {
        return;
    }
    selectedHandler->menuHandler(selectedHandler->ctx);
}

void TransmitterManager::start() {
    if (selectedHandler == NULL) {
        return;
    }
    selectedHandler->startHandler(selectedHandler->ctx);
}

void TransmitterManager::stop() {
    if (selectedHandler == NULL) {
        return;
    }
    selectedHandler->stopHandler(selectedHandler->ctx);
}

void TransmitterManager::tune(double freq) {
    if (selectedHandler == NULL) {
        return;
    }
    selectedHandler->tuneHandler(freq + tuneOffset, selectedHandler->ctx);
    currentFreq = freq;
}

void TransmitterManager::setTuningOffset(double offset) {
    tuneOffset = offset;
    tune(currentFreq);
}