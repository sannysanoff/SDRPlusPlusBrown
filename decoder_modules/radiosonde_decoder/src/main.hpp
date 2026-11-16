#pragma once

#include <mutex>
#include <tuple>
#include <dsp/block.h>
#include <dsp/demod/fm.h>
#include <dsp/multirate/rational_resampler.h>
#include <module.h>
#include <signal_path/signal_path.h>
#include "decode/decoder.hpp"
#include "gpx.hpp"
#include "ptu.hpp"

/* Display name, bandwidth, decoder */
typedef std::tuple<const char*, float, dsp::block*> sondespec_t;

class RadiosondeDecoderModule : public ModuleManager::Instance {
public:
    RadiosondeDecoderModule(std::string name);
    ~RadiosondeDecoderModule();

    void postInit() override;
    void enable() override;
    void disable() override;
    bool isEnabled() override;
    std::string handleDebugCommand(const std::string& cmd, const std::string& args) override;

private:
    std::string name;
    bool enabled = false;
    bool gpxOutput = false;
    bool ptuOutput = false;
    char gpxFilename[2048] = {};
    char ptuFilename[2048] = {};
    VFOManager::VFO* vfo = nullptr;
    dsp::demod::FM<float> fmDemod;
    dsp::multirate::RationalResampler<float> resampler;

    radiosonde::Decoder<RS41Decoder, rs41_decoder_init, rs41_decoder_deinit, rs41_decode> rs41decoder;
    radiosonde::Decoder<DFM09Decoder, dfm09_decoder_init, dfm09_decoder_deinit, dfm09_decode> dfm09decoder;
    radiosonde::Decoder<IMS100Decoder, ims100_decoder_init, ims100_decoder_deinit, ims100_decode> ims100decoder;
    radiosonde::Decoder<M10Decoder, m10_decoder_init, m10_decoder_deinit, m10_decode> m10decoder;
    radiosonde::Decoder<IMET4Decoder, imet4_decoder_init, imet4_decoder_deinit, imet4_decode> imet4decoder;
    radiosonde::Decoder<C50Decoder, c50_decoder_init, c50_decoder_deinit, c50_decode> c50decoder;
    radiosonde::Decoder<MRZN1Decoder, mrzn1_decoder_init, mrzn1_decoder_deinit, mrzn1_decode> mrzn1decoder;

    const sondespec_t supportedTypes[7] = {
        sondespec_t("RS41", 10000.0f, &rs41decoder),
        sondespec_t("DFM06/09", 15000.0f, &dfm09decoder),
        sondespec_t("iMS100/RS-11G", 20000.0f, &ims100decoder),
        sondespec_t("M10/M20", 50000.0f, &m10decoder),
        sondespec_t("iMet-4", 20000.0f, &imet4decoder),
        sondespec_t("SRS-C50", 20000.0f, &c50decoder),
        sondespec_t("MRZ-N1", 20000.0f, &mrzn1decoder),
    };
    int selectedType = 0;
    dsp::block* activeDecoder = nullptr;

    std::mutex dataMtx;
    SondeFullData lastData;
    uint64_t packetCount = 0;
    GPXWriter gpxWriter;
    PTUWriter ptuWriter;

    void resetActiveDecoder();
    static void menuHandler(void* ctx);
    static void sondeDataHandler(SondeFullData* data, void* ctx);
    static void onTypeSelected(void* ctx, int selection);
    static void onGPXOutputChanged(void* ctx);
    static void onPTUOutputChanged(void* ctx);
};
