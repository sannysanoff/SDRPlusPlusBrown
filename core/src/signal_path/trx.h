#pragma once
#include <map>
#include <string>
#include <dsp/stream.h>
#include <dsp/types.h>
#include "../dsp/routing/splitter.h"
#include "../dsp/audio/volume.h"
#include "../dsp/sink/null_sink.h"
#include <mutex>
#include <utils/event.h>
#include <vector>


struct Transmitter {


    // for stream transmission
//    virtual int getInputStreamFramerate() = 0;      // 48000 hz for hermes lite 2 input stream
    virtual void setTransmitStatus(bool status) = 0;
    virtual void setTransmitStream(dsp::stream<dsp::complex_t> *stream) = 0;
    virtual void setTransmitSoftwareGain(unsigned char gain) = 0;   // 0..255
    virtual void setTransmitHardwareGain(unsigned char gain) = 0;   // 0..255
    virtual unsigned char getTransmitHardwareGain() = 0;
    virtual void setTransmitFrequency(int freq) = 0;
//    virtual int getTransmittedBufferLatency() = 0;
//    virtual void setTransmittedBufferLatency(int latency) = 0;
//    virtual int getTransmittedPttDelay() = 0;
//    virtual void setTransmittedPttDelay(int delay) = 0;

    // for tone transmission
//    virtual void startGenerateTone(int frequency) = 0;
//    virtual void stopGenerateTone() = 0;
//    virtual void setToneGain() = 0;

    virtual void setPAEnabled(bool enabled) = 0;

    virtual int getTXStatus() = 0;      // tone or stream

    virtual float getTransmitPower() = 0;
    virtual float getReflectedPower() = 0;
    virtual float getTransmitSWR() = 0;
    virtual int getNormalZone() = 0;   // e.g. 5w
    virtual int getRedZone() = 0;  // e.g. 10w, always greater than normal zone
    virtual std::string &getTransmitterName() = 0;

    virtual ~Transmitter() {}

};