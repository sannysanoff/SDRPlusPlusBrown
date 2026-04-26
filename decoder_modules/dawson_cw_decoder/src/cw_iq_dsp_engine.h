//  _  ___  _   _____ _     _
// / |/ _ \/ | |_   _| |__ (_)_ __   __ _ ___
// | | | | | |   | | | '_ \| | '_ \ / _` / __|
// | | |_| | |   | | | | | | | | | | (_| \__ \.
// |_|\___/|_|   |_| |_| |_|_|_| |_|\__, |___/
//                                  |___/
//
// Dawson CW Decoder - IQ DSP Engine
// Direct IQ processing with FFT-based channel detection

#ifndef __CW_IQ_DSP_ENGINE_H__
#define __CW_IQ_DSP_ENGINE_H__

#include "cw_decoder_core.h"
#include <dsp/types.h>
#include <dsp/stream.h>
#include <dsp/processor.h>
#include <utils/arrays.h>
#include <cstdint>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include <atomic>

namespace dawson_cw {

// Target frequency resolution for CW detection (Hz)
// CW contest stations can be ~50-100 Hz apart
static constexpr float TARGET_RESOLUTION_HZ = 150.0f;

// Peak detection result
struct IQPeakInfo {
    uint16_t bin_index;
    float frequency_hz;  // Relative to center (can be negative)
    float magnitude;
    float snr;
};

// Active channel state for IQ processing
struct IQChannelState {
    uint32_t id;
    float center_freq_hz;  // Relative frequency from center
    uint16_t bin_index;
    
    // Decoder - one per channel
    std::unique_ptr<c_cw_decoder> decoder;
    std::vector<s_observation> observations;
    
    // Keying detection state
    bool current_key_state;  // true = key down (tone present)
    float current_duration_frames;
    uint32_t frame_count;
    
    // Output
    std::string decoded_text;
    std::mutex text_mutex;
    float snr;
    float wpm;
    
    // Timing
    std::chrono::steady_clock::time_point last_signal_time;
    std::chrono::steady_clock::time_point last_decode_time;
    bool has_signal;
    bool is_active;
    
    // Magnitude tracking for SNR
    float noise_level;
    float signal_level;
    
    IQChannelState(uint32_t channel_id, float freq, uint16_t bin)
        : id(channel_id), center_freq_hz(freq), bin_index(bin),
          decoder(std::make_unique<c_cw_decoder>(channel_id)),
          current_key_state(false), current_duration_frames(0), frame_count(0),
          snr(0), wpm(0), noise_level(1.0f), signal_level(0),
          has_signal(false), is_active(true) {
        last_decode_time = std::chrono::steady_clock::now();
        last_signal_time = std::chrono::steady_clock::now();
    }
};

// Configuration for IQ mode
struct IQConfig {
    int max_channels = 100;
    float threshold_mult = 3.0f;      // Multiplier above noise floor
    float min_snr_db = 6.0f;          // Minimum SNR in dB
    int min_wpm = 10;
    int max_wpm = 40;
    int timeout_seconds = 30;
    int signal_loss_seconds = 5;
    bool show_partial = true;
    float sample_rate = 48000.0f;     // Input sample rate
    
    // CW-specific: frequency ranges for detection
    // Full bandwidth is used for FFT, but only peaks within absolute
    // CW band (14.000-14.070 MHz) are kept
    float min_cw_freq = -400000.0f;     // Full bandwidth lower bound
    float max_cw_freq = 400000.0f;      // Full bandwidth upper bound
    
    // CW band limits - set dynamically from main.cpp based on current frequency
    // These define the absolute frequency range for peak filtering
    float min_cw_abs_freq = 3000000.0f;   // Default: 3 MHz (lowest ham band)
    float max_cw_abs_freq = 30000000.0f;  // Default: 30 MHz (highest ham band)
    
    // Channel spacing: minimum separation between CW stations (Hz)
    // CW contest stations can be ~50-100Hz apart
    float min_channel_spacing = 150.0f;  // Hz
};

// IQ DSP Engine - processes wideband IQ directly as a preprocessor
class IQCWDSPEngine : public dsp::Processor<dsp::complex_t, dsp::complex_t> {
    using base_type = dsp::Processor<dsp::complex_t, dsp::complex_t>;
    
public:
    IQCWDSPEngine();
    ~IQCWDSPEngine();
    
    // dsp::Processor interface
    void init(dsp::stream<dsp::complex_t>* in) override;
    void setInput(dsp::stream<dsp::complex_t>* in) override;
    int run() override;
    void start() override;
    void doStart() override;
    void doStop() override;
    
    // Enable/disable processing (bypass mode when disabled)
    void set_enabled(bool enabled);
    bool is_enabled() const { return _enabled; }
    
    // Configuration
    void set_config(const IQConfig& config);
    IQConfig get_config() const;
    
    // Set actual RF center frequency for absolute frequency reporting
    void set_center_frequency(double freq_hz);
    
    // Set CW band limits for peak filtering
    void set_cw_band(float min_abs_hz, float max_abs_hz);
    
    // Channel management
    size_t get_active_channel_count() const;
    const std::vector<std::unique_ptr<IQChannelState>>& get_channels() const { return channels; }
    
    // Sample counting for verification
    uint64_t get_total_samples_processed() const { return total_samples; }
    
    // Get decoded text (thread-safe)
    std::string get_channel_text(uint32_t channel_id);
    float get_channel_snr(uint32_t channel_id);
    float get_channel_wpm(uint32_t channel_id);
    float get_channel_frequency(uint32_t channel_id);  // Absolute frequency
    bool is_channel_active(uint32_t channel_id);
    
    // Peak frequencies for display
    std::vector<float> get_active_frequencies() const;  // Relative to center
    std::vector<float> get_absolute_frequencies() const;
    
    // Get spectrum data for waterfall overlay
    void get_spectrum_magnitude(float* out_buffer, uint16_t num_bins) const;
    
    // Reset
    void reset();
    void clear_all_channels();
    
    // Timeout check - call periodically
    void update_timeouts();
    
private:
    // Compute optimal FFT size for target resolution
    uint16_t compute_fft_size(float sample_rate, float target_resolution);
    
    // Initialize/resize FFT for current sample rate
    void init_fft();
    
    // FFT processing
    void process_frame();
    void update_noise_floor();
    std::vector<IQPeakInfo> detect_peaks();
    void merge_and_manage_channels(const std::vector<IQPeakInfo>& peaks);
    
    // Channel processing - key detection from FFT magnitude
    void process_channel(IQChannelState& channel, float magnitude);
    void decode_channel_observations(IQChannelState& channel);
    
    // Process individual IQ sample (called from run())
    void process_iq_sample(const dsp::complex_t& sample);
    
    // Channel management
    IQChannelState* find_nearby_channel(float freq_hz);
    
    // Apply window to complex buffer
    void apply_window();
    
    // Complex FFT
    void perform_fft();
    
    // Timing constants
    static constexpr uint16_t OBSERVATION_BUFFER_SIZE = 50;
    static constexpr uint16_t OBSERVATION_BURST_SIZE = 10;
    static constexpr uint16_t TIMEOUT_FRAMES = 500;  // At ~23ms/frame @ 48kHz
    
    // State
    IQConfig config;
    std::atomic<bool> running;
    double rf_center_frequency = 0.0;  // For absolute freq calculation
    
    // FFT configuration (dynamic based on sample rate)
    uint16_t fft_size = 1024;  // Will be computed based on sample rate
    float bin_width_hz = 46.875f;  // Will be computed: sample_rate / fft_size
    
    // Sample buffering
    std::vector<dsp::complex_t> iq_buffer;
    
    // FFT using SDR++ arrays (dynamically sized)
    dsp::arrays::Arg<dsp::arrays::FFTPlan> fft_plan;
    dsp::arrays::ComplexArray fft_input;
    dsp::arrays::ComplexArray fft_output;
    std::vector<float> fft_window;  // Dynamic size
    std::vector<float> window_coeffs;  // Precomputed window coefficients
    
    // Spectrum analysis (dynamically sized)
    std::vector<float> magnitude_spectrum;
    std::vector<float> noise_floor;
    std::vector<float> smoothed_magnitude;
    std::vector<uint32_t> gate_count;
    
    // Channels
    std::vector<std::unique_ptr<IQChannelState>> channels;
    mutable std::mutex channels_mutex;
    uint32_t next_channel_id;
    
    // Frame counting for timeouts
    uint32_t frame_count;
    
    // Sample counting (for testing/verification)
    std::atomic<uint64_t> total_samples{0};
    
    // Enable flag
    std::atomic<bool> _enabled{false};
};

} // namespace dawson_cw

#endif
