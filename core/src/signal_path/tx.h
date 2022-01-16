#pragma once
#include <string>
#include <vector>
#include <map>
#include <dsp/stream.h>
#include <dsp/types.h>
#include <utils/event.h>

class TransmitterManager {
public:
    TransmitterManager();

    struct TransmitterHandler {
        dsp::stream<dsp::complex_t>* stream;
        void (*menuHandler)(void* ctx);
        void (*selectHandler)(void* ctx);
        void (*deselectHandler)(void* ctx);
        void (*startHandler)(void* ctx);
        void (*stopHandler)(void* ctx);
        void (*tuneHandler)(double freq, void* ctx);
        void* ctx;
    };

    void registerTransmitter(std::string name, TransmitterHandler* handler);
    void unregisterTransmitter(std::string name);
    void selectTransmitter(std::string name);
    void showSelectedMenu();
    void start();
    void stop();
    void tune(double freq);
    void setTuningOffset(double offset);

    std::vector<std::string> getTransmitterNames();

    Event<std::string> onTransmitterRegistered;
    Event<std::string> onTransmitterUnregister;
    Event<std::string> onTransmitterUnregistered;

private:
    std::map<std::string, TransmitterHandler*> transmitters;
    std::string selectedName;
    TransmitterHandler* selectedHandler = NULL;
    double tuneOffset;
    double currentFreq;
    dsp::stream<dsp::complex_t> nullTransmitter;
};