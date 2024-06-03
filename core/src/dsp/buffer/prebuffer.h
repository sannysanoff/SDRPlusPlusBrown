#pragma once

#include "../types.h"
#include "../processor.h"
#include "../utils/usleep.h"
#include "../ctm.h"


namespace dsp::buffer {

    template<typename T>
    class Prebuffer : public Processor<T, T> {
        using base_type = Processor<T, T>;
    public:
        Prebuffer() {}

        Prebuffer(stream<T> *in) { base_type::init(in); }

        int prebufferMsec = 0;
        std::mutex bufferLock;
        std::vector<T> buffer;
        bool bufferReached = false;
        bool logging = false;
        void setPrebufferMsec(int msec) {
            this->prebufferMsec = msec;
        }

        int sampleRate = 48000;
        void setSampleRate(int sampleRate) {
            this->sampleRate = sampleRate;
            clear();
        }

        bool volatile running = false;

        int getNeededBufferSize() const {
            return prebufferMsec * sampleRate / 1000;
        }

        virtual void doStart() {
            base_type ::doStart();
            running = true;
            std::thread([=]() {
                if (logging) {
                    flog::info("Prebuffer: begin: running={}, &running={}", running, (void *) &running);
                }
                const int ITER_STEP_MILLIS = 15;
                long long lastSend;
                while (running) {
                    // this is long loop wating until preload goal is reached
                    // preload goal may be set again if underflow during normal op.
                    while(running && !bufferReached) {
                        bufferLock.lock();
                        auto bs = buffer.size();
                        bufferLock.unlock();
                        if (bs > 0) {
                            bufferReached = bs >= getNeededBufferSize();
                            if (bufferReached) {
                                lastSend = currentTimeMillis() - ITER_STEP_MILLIS;
                                if (logging) {
                                    flog::info("prebuffer: first block, bs={} > {} reached=true", (int) bs,
                                               (int) getNeededBufferSize());
                                }
                                break;
                            }
                        }
                        usleep(1000 * ITER_STEP_MILLIS);
                    }
                    if (!running) {
                        break;
                    }
                    // buffer has been reached. Check if enough data.
                    auto ctm = currentTimeMillis();
                    long long samplesNeeded = (ctm - lastSend) * sampleRate / 1000;
                    lastSend = ctm;
                    if (running) {
                        bufferLock.lock();
                        auto bs = buffer.size();
                        bufferLock.unlock();
                        if (bs >= samplesNeeded) {
                            // fall through
                        } else {
                            // not enough data in buffer to send.
                            if (bufferReached) {
                                bufferReached = false; // want to fill again.
                                if (logging) {
                                    flog::info("prebuffer: second block, bs={} reached=false, sleep+continue", (int) bs);
                                }
                            }
                            usleep(1000 * ITER_STEP_MILLIS);
                            continue; // sleep again.
                        }
                    } else {
                        break;
                    }

                    if (samplesNeeded == 0) {
                        usleep(1000 * ITER_STEP_MILLIS);
                        if (logging) {
                            flog::info("prebuffer: samples needed = 0, continue");
                        }
                        continue; // sleep again.
                    }

                    if (!running) {
                        break;
                    }
                    bufferLock.lock();
                    auto bs = buffer.size();
                    if (bs < samplesNeeded) {
                        if (logging) {
                            flog::info("prebuffer WTF: bs={} < samplesNeeded={}, running={}", (int) bs, (int) samplesNeeded, (int)running);
                        }
                        bufferLock.unlock();
                        usleep(1000); // just in case
                        continue; // sleep again.
                    }
                    std::copy(buffer.begin(), buffer.begin() + samplesNeeded, base_type::out.writeBuf);
                    buffer.erase(buffer.begin(), buffer.begin() + samplesNeeded);
                    if (getNeededBufferSize() > 0) {
                        auto maxAllow = 1.5 * getNeededBufferSize();
                        if (buffer.size() > maxAllow) {
                            // drop over 100%
                            if (logging) {
                                flog::info("prebuffer: dropping excess: {} -> {}", (int)buffer.size(), (int)maxAllow);
                            }
                            auto drop = buffer.size() - maxAllow;
                            buffer.erase(buffer.begin(), buffer.begin() + drop);
                        }
                    }
                    bufferLock.unlock();
                    if (logging) {
                        flog::info("sending from prebuffer...{}, remains {}", (int) samplesNeeded, (int) buffer.size());
                    }
                    base_type::out.swap(samplesNeeded);
                    if (logging) {
                        flog::info("sent from prebuffer...");
                    }
                }
                if (logging) {
                    flog::info("Prebuffer: finished, running={}, &running={}", running, (void *) &running);
                }
                return ;
            }).detach();
        }

        virtual void doStop() {
            base_type::doStop();
            running = false;
        }

        void clear() {
            bufferLock.lock();
            buffer.clear();
            bufferReached = false;
            bufferLock.unlock();
        }

        long long lastReceived = 0;

        int getTimeDelayInMillis() { // buffer head time delay from last arrived packet.
            if (sampleRate == 0) {
                return 0;
            }
            return buffer.size() * 1000 / sampleRate;
        }

        int getPercentFull() {
            bufferLock.lock();
            if (getNeededBufferSize() <= 0) {
                bufferLock.unlock();
                return 100;
            }
            int percent = 100 * buffer.size() / getNeededBufferSize();
            bufferLock.unlock();
            return percent;
        }

        int run() {
            int count = base_type::_in->read();
            if (count < 0) { return -1; }
            lastReceived = currentTimeMillis();
            bufferLock.lock();
            buffer.insert(buffer.end(), base_type::_in->readBuf, base_type::_in->readBuf + count);
            bufferLock.unlock();
            base_type::_in->flush();
            return 0;
        }

    };

}