#pragma once
#include "../sink.h"
#include "dsp/block.h"


namespace dsp::routing {
    template <class T>
    class Splitter : public Sink<T> {
        using base_type = Sink<T>;
    public:
        const char *origin = "splitter.no_origin";
        Splitter() {}

        Splitter(stream<T>* in) { base_type::init(in); }

        void bindStream(stream<T>* stream) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            
            // Check that the stream isn't already bound
            if (std::find(streams.begin(), streams.end(), stream) != streams.end()) {
                throw std::runtime_error("[Splitter] Tried to bind stream to that is already bound");
            }

            // Add to the list
            base_type::tempStop();
            base_type::registerOutput(stream);
            streams.push_back(stream);
            base_type::tempStart();
        }

        void unbindStream(stream<T>* stream) {
            assert(base_type::_block_init);
            std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
            
            // Check that the stream is bound
            auto sit = std::find(streams.begin(), streams.end(), stream);
            if (sit == streams.end()) {
                throw std::runtime_error("[Splitter] Tried to unbind stream to that isn't bound");
            }

            // Add to the list
            base_type::tempStop();
            streams.erase(sit);
            base_type::unregisterOutput(stream);
            base_type::tempStart();
        }

        std::function<void(T*, int)> hook;

        void setHook(const std::function<void(T*, int)> &_hook) {
            this->hook = _hook;
        }

        std::string getBlockName() override {
            const char* tidName = typeid(*this).name();
            return "Splitter:" +Sink<T>::simplifyTN(tidName);
        }

        long long workedCount = 0; // this is to enable cascade stop Reader.

        int run() override {
//            flog::info("Splitter {} reading from {}", origin, base_type::_in->origin);
            int count = base_type::_in->read();
//            flog::info("Splitter {} got from {}: {}", origin, base_type::_in->origin, count);
            if (count < 0) {
/*
                if (workedCount > 0) {              // don't during init and various reconnections
                    for (const auto& stream : streams) {
                        stream->stopReader();           // this is for clean shutdown. NOT TESTED WELL YET. This sucks on block enable/disable (eg nr enable/disable)
                    }
                }
*/
                return -1;
            }

            workedCount += count;

            for (const auto& stream : streams) {
                memcpy(stream->writeBuf, base_type::_in->readBuf, count * sizeof(T));
//                flog::info("Splitter {} flushing to {}", origin, stream->origin);
                if (!stream->swap(count)) {
//                    flog::info("Splitter {} flushing to {} - oops", origin, stream->origin);
                    base_type::_in->flush();
                    return -1;
                }
//                flog::info("Splitter {} flushed to {}.", origin, stream->origin);
            }

            if (hook) {
                hook(base_type::_in->readBuf, count);
            }

            base_type::_in->flush();

            return count;
        }

    protected:
        std::vector<stream<T>*> streams;

    };
}