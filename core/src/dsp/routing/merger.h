#pragma once
#include "../sink.h"
#include "core.h"
#include <atomic>
#include <utils/flog.h>
#include <ctm.h>


/**
 * this one copies data from any input to output, using the secondary input as higher priority stream.
 * if any secondary streams have no data, default input is used.
 */

namespace dsp::routing {
    template <class T>
    class Merger : public Sink<T> {
        using base_type = Sink<T>;
        dsp::stream<T> out;
    public:
        Merger() {
            this->registerOutput(&out);
            out.origin = "merger.out";
            running = true;
        }

        virtual ~Merger() {
            this->stop();
        }

        dsp::stream<T>* getOutput() {
            return &out;
        }

        // add new input stream with priority
        void bindStream(int priority, stream<T>* stream) {
            if (!base_type::_block_init) {
                base_type::init(nullptr);
            }
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);

            for(std::shared_ptr<SecondaryStream<T>> &s : secondaryStreams) {
                if (s->astream == stream) {
                    throw std::runtime_error("[Splitter] Tried to bind stream to that is already bound");
                }
            }
            // Add to the list
            auto newStream = std::make_shared<SecondaryStream<T>>();
            newStream->priority = priority;
            newStream->astream = stream;

            auto ns = secondaryStreams.size();
            secondaryStreams.push_back(newStream);
//            this->registerInput(stream);

            std::thread x([ns, newStream, this]() {
                SetThreadName("merger.rd");
                while(true) {
                    int rd = newStream->astream->read();
                    if (rd < 0) {
                        break;
                    }
                    newStream->dataLock.lock();
                    auto dest = newStream->receivedData.size();
                    newStream->receivedData.resize(newStream->receivedData.size() + rd);
                    std::copy(newStream->astream->readBuf, newStream->astream->readBuf + rd, newStream->receivedData.begin() + dest);
                    newStream->astream->flush();
//                    flog::info("merger: {}: got something {} from stream {}, now size {}", (int64_t)currentTimeMillis(), rd, newStream->astream->origin, newStream->receivedData.size());
                    newStream->dataLock.unlock();
                    dataReadyMutex.lock();
                    dataReady.notify_one();
                    dataReadyMutex.unlock();
                }
            });
            x.detach();
//            this->registerInput(stream);
        }

        void doStop() override {
            for(auto s : secondaryStreams) {
                s->astream->stopWriter();
                s->astream->stopReader();
            }
            running = false;
            proceedWithoutWait = true;
            {
                std::unique_lock<std::mutex> lk(dataReadyMutex);
                dataReady.notify_all();
            }
            base_type::doStop();
            for(auto s : secondaryStreams) {
                s->astream->clearWriteStop();
            }
        }

        void unbindStream(stream<T>* stream) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);

            int ix = 0;
            for(auto s : secondaryStreams) {
                if (s->astream == stream) {
                    s->astream->stopReader();
                    secondaryStreams.erase(secondaryStreams.begin() + ix);
                    return;
                }
                ix++;
            }
            this->unregisterInput(stream);
        }

        bool proceedWithoutWait = false;

        int SWITCH_DELAY = 100; // 100 msec
        int lastSeenBestPriority = 0;
        long long lastSeenBestTime = 0;
        bool running;

        int run() override {
            SetThreadName("merger.run");
            if (!running) {
                return -1;
            }
            if (!proceedWithoutWait) {
//                flog::info("Merger waits..");
                std::unique_lock<std::mutex> lk(dataReadyMutex);
                dataReady.wait(lk);
                if (!running) {
                    flog::info("Merger: terminating");
                    return -1;
                }
            } else {
//                flog::info("Merger does not wait.");
            }
//            flog::info("Merger endwait");
            static int _cnt = 0;
            _cnt++;
            std::lock_guard<std::mutex> lck(streamMtx);
            std::shared_ptr<SecondaryStream<T>> bestStream;
            // here we select (maybe) new stream. If SWITCH_DELAY has not passed, using old stream.
            auto ctm = currentTimeMillis();
            for(auto &ss : secondaryStreams) {
                std::lock_guard g(ss->dataLock);
                if (ctm - lastSeenBestTime < SWITCH_DELAY) {
                    if (ss->priority <= lastSeenBestPriority) {
                        bestStream = ss;
                    }
                } else {
                    if (!ss->receivedData.empty()) {
                        if (!bestStream || ss->priority < bestStream->priority) {
                            bestStream = ss;
                        }
                    }
                }
            }
            if (bestStream == nullptr || bestStream->receivedData.empty()) {
                proceedWithoutWait = false;
                return 1;
            }
            lastSeenBestPriority = bestStream->priority;
            lastSeenBestTime = ctm;
            int streamIndex = 0;
            for(auto &ss : secondaryStreams) {
                std::lock_guard g(ss->dataLock);
                if (bestStream && ss == bestStream) {
                    int count = ss->receivedData.size();
                    if (count > 1024) {
                        count = 1024;
                    }
                    memcpy(out.writeBuf, ss->receivedData.data(), count * sizeof(T));
//                    flog::info("merger: swapping {} to {}...", count, out.origin);
                    if (!out.swap(count)) {
                        flog::info("merger: swapping oops: -1");
                        return -1;
                    }
//                    flog::info("merger: swapped {}", count);
                    ss->receivedData.erase(ss->receivedData.begin(), ss->receivedData.begin() + count);
                    proceedWithoutWait = ss->receivedData.size() > 0;
//                    flog::info("merger: {}: wrote {} from stream {}, now size remains {}", (int64_t)currentTimeMillis(), count, streamIndex, ss->receivedData.size());
                } else {
                    ss->receivedData.clear();
                }
                streamIndex++;
            }
            return 1;
        }


        template<class TT>
        struct SecondaryStream {
            SecondaryStream() : receivedData(), dataLock() {
                priority = 0;
                astream = nullptr;
            }

            stream<T>* astream;
            int priority;
            std::vector<TT> receivedData;
            std::mutex dataLock;
        };

        std::vector<std::shared_ptr<SecondaryStream<T>>> secondaryStreams;

        std::condition_variable dataReady;
        std::mutex dataReadyMutex;
        std::mutex streamMtx;


    };
}