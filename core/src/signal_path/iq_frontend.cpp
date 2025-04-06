#include "iq_frontend.h"
#include "../dsp/window/blackman.h"
#include "../dsp/window/nuttall.h"
#include <utils/flog.h>
#include <gui/gui.h>
#include <core.h>
#include <ctm.h>
#include "signal_path/signal_path.h"

IQFrontEnd::~IQFrontEnd() {
    if (!_init) { return; }
    stop();
    dsp::buffer::free(fftWindowBuf);
//    fftwf_destroy_plan(fftwPlanImplFFTW);
//    fftwf_free(fftInBuf);
//    fftwf_free(fftOutBuf);
}

void IQFrontEnd::init(dsp::stream<dsp::complex_t>* in, double sampleRate, bool buffering, int decimRatio, bool dcBlocking, int fftSize, double fftRate, FFTWindow fftWindow, float* (*acquireFFTBuffer)(void* ctx), void (*releaseFFTBuffer)(void* ctx), void* fftCtx) {
    _sampleRate = sampleRate;
    _decimRatio = decimRatio;
    _fftSize = fftSize;
    _fftRate = fftRate;
    _fftWindow = fftWindow;
    _acquireFFTBuffer = acquireFFTBuffer;
    _releaseFFTBuffer = releaseFFTBuffer;
    _fftCtx = fftCtx;

    effectiveSr = _sampleRate / _decimRatio;

    inBuf.init(in);
    inBuf.bypass = !buffering;

    decim.init(NULL, _decimRatio);
    dcBlock.init(NULL, genDCBlockRate(effectiveSr));
    conjugate.init(NULL);
    detectorPreprocessor.init(NULL);
    detectorPreprocessor.setSampleRate(effectiveSr);

    preproc.init(&inBuf.out);
    preproc.addBlock(&decim, _decimRatio > 1);
    preproc.addBlock(&dcBlock, dcBlocking);
    preproc.addBlock(&conjugate, false); // TODO: Replace by parameter
    preproc.addBlock(&detectorPreprocessor, false);

    split.init(preproc.out);

    // TODO: Do something to avoid basically repeating this code twice
    int skip;
    genReshapeParams(effectiveSr, _fftSize, _fftRate, skip, _nzFFTSize);
    reshape.init(&fftIn, fftSize, skip);
    fftSink.init(&reshape.out, handler, this);
    fftIn.origin = "iq_frontend.fft_in";

    fftWindowBuf = dsp::buffer::alloc<float>(_nzFFTSize);
    switch(_fftWindow) {
        case FFTWindow::RECTANGULAR:
            for (int i = 0; i < _nzFFTSize; i++) { fftWindowBuf[i] = 1.0; }
            break;
        case FFTWindow::BLACKMAN:
            for (int i = 0; i < _nzFFTSize; i++) { fftWindowBuf[i] = dsp::window::blackman(i, _nzFFTSize); }
            break;
        case FFTWindow::NUTTALL:
            for (int i = 0; i < _nzFFTSize; i++) { fftWindowBuf[i] = dsp::window::nuttall(i, _nzFFTSize); }
            break;
    }

//    fftInBuf = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));
//    fftOutBuf = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));
//    fftwPlanImplFFTW = fftwf_plan_dft_1d(_fftSize, fftInBuf, fftOutBuf, FFTW_FORWARD, FFTW_ESTIMATE);
    //    fft_in = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fftSize);
    //    fft_out = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fftSize);
    //    fftwPlanImplFFTW = fftwf_plan_dft_1d(fftSize, fft_in, fft_out, FFTW_FORWARD, FFTW_ESTIMATE);
    fftPlan = dsp::arrays::allocateFFTWPlan(false, _fftSize);

    // Clear the rest of the FFT input buffer
    dsp::buffer::clear(fftPlan->getInput()->data(), _fftSize - _nzFFTSize, _nzFFTSize);

    split.bindStream(&fftIn);
    split.origin = "iqfrontent.split";

    _init = true;
}

void IQFrontEnd::setInput(dsp::stream<dsp::complex_t>* in) {
    this->_currentStreamTime = 0;
    inBuf.setInput(in);
}

void IQFrontEnd::setSampleRate(double sampleRate) {
    // Temp stop the necessary blocks
    dcBlock.tempStop();
    for (auto& [name, vfo] : vfos) {
        vfo->tempStop();
    }

    // Update the samplerate
    _sampleRate = sampleRate;
    effectiveSr = _sampleRate / _decimRatio;
    onEffectiveSampleRateChange.emit(effectiveSr);
    dcBlock.setRate(genDCBlockRate(effectiveSr));
    detectorPreprocessor.setSampleRate(effectiveSr);
    for (auto& [name, vfo] : vfos) {
        vfo->setInSamplerate(effectiveSr);
    }



    // Reconfigure the FFT
    updateFFTPath();

    // Restart blocks
    dcBlock.tempStart();
    for (auto& [name, vfo] : vfos) {
        vfo->tempStart();
    }

}

void IQFrontEnd::setBuffering(bool enabled) {
    inBuf.bypass = !enabled;
}

void IQFrontEnd::setDecimation(int ratio) {
    // Temp stop the decimator
    decim.tempStop();

    // Update the decimation ratio
    _decimRatio = ratio;
    if (_decimRatio > 1) { decim.setRatio(_decimRatio); }
    setSampleRate(_sampleRate);

    // Restart the decimator if it was running
    decim.tempStart();

    // Enable or disable in the chain
    preproc.setBlockEnabled(&decim, _decimRatio > 1, [=](dsp::stream<dsp::complex_t>* out){ split.setInput(out); });

    // Update the DSP sample rate (TODO: Find a way to get rid of this)
    core::setInputSampleRate(_sampleRate);
}

void IQFrontEnd::setDCBlocking(bool enabled) {
    preproc.setBlockEnabled(&dcBlock, enabled, [=](dsp::stream<dsp::complex_t>* out){ split.setInput(out); });
}

void IQFrontEnd::setInvertIQ(bool enabled) {
    preproc.setBlockEnabled(&conjugate, enabled, [=](dsp::stream<dsp::complex_t>* out){ split.setInput(out); });
}

void IQFrontEnd::bindIQStream(dsp::stream<dsp::complex_t>* stream) {
    split.bindStream(stream);
}

void IQFrontEnd::unbindIQStream(dsp::stream<dsp::complex_t>* stream) {
    split.unbindStream(stream);
}

dsp::channel::RxVFO* IQFrontEnd::addVFO(std::string name, double sampleRate, double bandwidth, double offset) {
    // Make sure no other VFO with that name already exists
    if (vfos.find(name) != vfos.end()) {
        flog::error("[IQFrontEnd] Tried to add VFO with existing name.");
        return NULL;
    }

    // Create VFO and its input stream
    dsp::stream<dsp::complex_t>* vfoIn = new dsp::stream<dsp::complex_t>;
    dsp::channel::RxVFO* vfo = new dsp::channel::RxVFO(vfoIn, effectiveSr, sampleRate, bandwidth, offset);

    // Register them
    vfoStreams[name] = vfoIn;
    vfos[name] = vfo;
    bindIQStream(vfoIn);

    // Start VFO
    vfo->start();

    return vfo;
}

void IQFrontEnd::removeVFO(std::string name) {
    // Make sure that a VFO with that name exists
    if (vfos.find(name) == vfos.end()) {
        flog::error("[IQFrontEnd] Tried to remove a VFO that doesn't exist.");
        return;
    }

    // Remove the VFO and stream from registry
    dsp::stream<dsp::complex_t>* vfoIn = vfoStreams[name];
    dsp::channel::RxVFO* vfo = vfos[name];

    // Stop the VFO
    vfo->stop();

    unbindIQStream(vfoIn);
    vfoStreams.erase(name);
    vfos.erase(name);

    // Delete the VFO and its input stream
    delete vfo;
    delete vfoIn;
}

void IQFrontEnd::setFFTSize(int size) {
    _fftSize = size;
    updateFFTPath(true);
}

void IQFrontEnd::setFFTRate(double rate) {
    _fftRate = rate;
    updateFFTPath();
}

void IQFrontEnd::setFFTWindow(FFTWindow fftWindow) {
    _fftWindow = fftWindow;
    updateFFTPath();
}

void IQFrontEnd::flushInputBuffer() {
    inBuf.flush();
}

void IQFrontEnd::start() {
    // Start input buffer
    inBuf.start();

    // Start pre-proc chain (automatically start all bound blocks)
    preproc.start();

    // Start IQ splitter
    split.start();

    // Start all VFOs
    for (auto& [name, vfo] : vfos) {
        vfo->start();
    }

    // Start FFT chain
    reshape.start();
    fftSink.start();
}

void IQFrontEnd::stop() {
    // Stop input buffer
    inBuf.stop();

    // Stop pre-proc chain (automatically start all bound blocks)
    preproc.stop();

    // Stop IQ splitter
    split.stop();

    // Stop all VFOs
    for (auto& [name, vfo] : vfos) {
        vfo->stop();
    }

    // Stop FFT chain
    reshape.stop();
    fftSink.stop();
}

double IQFrontEnd::getEffectiveSamplerate() {
    return effectiveSr;
}

void IQFrontEnd::handler(dsp::complex_t* data, int count, void* ctx) {
    IQFrontEnd* _this = (IQFrontEnd*)ctx;

    // Apply window
    volk_32fc_32f_multiply_32fc((lv_32fc_t*)_this->fftPlan->getInput()->data(), (lv_32fc_t*)data, _this->fftWindowBuf, _this->_nzFFTSize);

    // Execute FFT
    dsp::arrays::npfftfft(_this->fftPlan->getInput(), _this->fftPlan);
//    fftwf_execute(_this->fftwPlanImplFFTW);

    // Aquire buffer
    float* fftBuf = _this->_acquireFFTBuffer(_this->_fftCtx);

    // Convert the complex output of the FFT to dB amplitude
    if (fftBuf) {
        volk_32fc_s32f_power_spectrum_32f(fftBuf, (lv_32fc_t*)_this->fftPlan->getOutput()->data(), _this->_fftSize, _this->_fftSize);
    }

    // Release buffer
    _this->_releaseFFTBuffer(_this->_fftCtx);
}

void IQFrontEnd::updateFFTPath(bool updateWaterfall) {
    // Temp stop branch
    reshape.tempStop();
    fftSink.tempStop();

    // Update reshaper settings
    int skip;
    genReshapeParams(effectiveSr, _fftSize, _fftRate, skip, _nzFFTSize);
    reshape.setKeep(_nzFFTSize);
    reshape.setSkip(skip);

    // Update window
    dsp::buffer::free(fftWindowBuf);
    fftWindowBuf = dsp::buffer::alloc<float>(_nzFFTSize);
    if (_fftWindow == FFTWindow::RECTANGULAR) {
        for (int i = 0; i < _nzFFTSize; i++) { fftWindowBuf[i] = 1.0f * ((i % 2) ? -1.0f : 1.0f); }
    }
    else if (_fftWindow == FFTWindow::BLACKMAN) {
        for (int i = 0; i < _nzFFTSize; i++) { fftWindowBuf[i] = dsp::window::blackman(i, _nzFFTSize) * ((i % 2) ? -1.0f : 1.0f); }
    }
    else if (_fftWindow == FFTWindow::NUTTALL) {
        for (int i = 0; i < _nzFFTSize; i++) { fftWindowBuf[i] = dsp::window::nuttall(i, _nzFFTSize) * ((i % 2) ? -1.0f : 1.0f); }
    }

    // Update FFT plan
    fftPlan = dsp::arrays::allocateFFTWPlan(false, _fftSize);

    // Clear the rest of the FFT input buffer
    dsp::buffer::clear(fftPlan->getInput()->data(), _fftSize - _nzFFTSize, _nzFFTSize);

    // Update waterfall (TODO: This is annoying, it makes this module non testable and will constantly clear the waterfall for any reason)
    if (updateWaterfall) { gui::waterfall.setRawFFTSize(_fftSize); }

    // Restart branch
    reshape.tempStart();
    fftSink.tempStart();
}

void IQFrontEnd::addPreprocessor(dsp::Processor<dsp::complex_t, dsp::complex_t>* processor, bool enabled) {
    preproc.addBlock(processor, enabled);
    split.setInput(preproc.out);
}

void IQFrontEnd::removePreprocessor(dsp::Processor<dsp::complex_t, dsp::complex_t>* processor) {
    preproc.removeBlock(processor, [=](dsp::stream<dsp::complex_t>* out) { split.setInput(out); });
}

void IQFrontEnd::togglePreprocessor(dsp::Processor<dsp::complex_t, dsp::complex_t>* processor, bool enabled) {
    if (enabled) {
        preproc.enableBlock(processor, [=](dsp::stream<dsp::complex_t>* out) { split.setInput(out); });
    } else {
        preproc.disableBlock(processor, [=](dsp::stream<dsp::complex_t>* out) { split.setInput(out); });
    }
}

long long IQFrontEnd::getCurrentStreamTime() {
    auto ctm =  _currentStreamTime.load();
    if (ctm == 0) {
        ctm = currentTimeMillis();
    }
    return ctm + secondsAdjustment * 1000L;
}
