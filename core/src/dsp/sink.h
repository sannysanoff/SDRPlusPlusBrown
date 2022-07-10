#pragma once
<<<<<<< HEAD
#include <dsp/block.h>
#include <dsp/buffer.h>
#include <fstream>
#include <utils/wstr.h>
=======
#include "block.h"
>>>>>>> master

namespace dsp {
    template <class T>
    class Sink : public block {
    public:
        Sink() {}

        Sink(stream<T>* in) { init(in); }

        virtual ~Sink() {}

        virtual void init(stream<T>* in) {
            _in = in;
<<<<<<< HEAD
            file = std::ofstream(wstr::str2wstr(path), std::ios::binary);
            generic_block<FileSink<T>>::registerInput(_in);
            generic_block<FileSink<T>>::_block_init = true;
=======
            registerInput(_in);
            _block_init = true;
>>>>>>> master
        }

        virtual void setInput(stream<T>* in) {
            assert(_block_init);
            std::lock_guard<std::recursive_mutex> lck(ctrlMtx);
            tempStop();
            unregisterInput(_in);
            _in = in;
            registerInput(_in);
            tempStart();
        }

        virtual int run() = 0;

    protected:
        stream<T>* _in;
    };
}
