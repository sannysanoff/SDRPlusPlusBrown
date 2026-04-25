//  _  ___  _   _____ _     _
// / |/ _ \/ | |_   _| |__ (_)_ __   __ _ ___
// | | | | | |   | | | '_ \| | '_ \ / _` / __|
// | | |_| | |   | | | | | | | | | | (_| \__ \.
// |_|\___/|_|   |_| |_| |_|_|_| |_|\__, |___/
//                                  |___/
//
// Dawson CW Decoder - DSP Engine
// Multi-channel CW decoder with dynamic channel management

#ifndef __CW_DSP_ENGINE_H__
#define __CW_DSP_ENGINE_H__

#include "cw_decoder_core.h"
#include "fft_processor.h"
#include <cstdint>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include <atomic>

namespace dawson_cw {

// Peak detection result
struct PeakInfo {
    uint16_t bin_index;
    float frequency_hz;
    float magnitude;
    float snr;
};

// Active channel state
struct ChannelState {
    uint32_t id;
    float center_freq_hz;
    uint16_t start_bin;
    uint16_t end_bin;
    
    // Decoder
    std::unique_ptr<c_cw_decoder> decoder;
    std::vector<s_observation> observations;
    
    // Timing
    bool current_value;
    float current_duration_ms;
    uint32_t frame_count;
    
    // Output
    std::string decoded_text;
    std::mutex text_mutex;
    float snr;
    float wpm;
    std::chrono::steady_clock::time_point last_decode_time;
    std::chrono::steady_clock::time_point last_signal_time;
    bool has_signal;
    bool is_active;
    
    // Display scroll
    float scroll_offset;
    bool scrolling;
    
    ChannelState(uint32_t channel_id, float freq, uint16_t start, uint16_t end)
        : id(channel_id), center_freq_hz(freq), start_bin(start), end_bin(end),
          decoder(std::make_unique<c_cw_decoder>(channel_id)),
          current_value(false), current_duration_ms(0), frame_count(0),
          snr(0), wpm(0), has_signal(false), is_active(true),
          scroll_offset(0), scrolling(false) {
        last_decode_time = std::chrono::steady_clock::now();
        last_signal_time = std::chrono::steady_clock::now();
    }
};

// Configuration
struct Config {
    int max_channels = 20;
    float threshold_mult = 9.0f;
    float min_snr_db = 12.0f;
    int min_wpm = 10;
    int max_wpm = 40;
    int timeout_seconds = 30;
    int signal_loss_seconds = 5;
    bool show_partial = true;
    float sample_rate = 7500.0f;  // Effective sample rate after downsampling
};

// Main DSP engine class
class CWDSPEngine {
public:
    CWDSPEngine();
    ~CWDSPEngine();
    
    // Configuration
    void set_config(const Config& config);
    Config get_config() const;
    
    // Audio input (mono, 16-bit signed)
    void process_audio_sample(int16_t sample);
    void process_audio_block(const int16_t* samples, size_t count);
    
    // Channel management
    size_t get_active_channel_count() const;
    const std::vector<std::unique_ptr<ChannelState>>& get_channels() const { return channels; }
    
    // Get decoded text (thread-safe)
    std::string get_channel_text(uint32_t channel_id);
    float get_channel_snr(uint32_t channel_id);
    float get_channel_wpm(uint32_t channel_id);
    float get_channel_frequency(uint32_t channel_id);
    bool is_channel_active(uint32_t channel_id);
    
    // Peak frequency for display
    std::vector<float> get_active_frequencies() const;
    
    // Reset
    void reset();
    void clear_all_channels();
    
    // Timeout check - call periodically
    void update_timeouts();
    
private:
    // FFT processing
    void process_frame();
    void update_noise_floor();
    std::vector<PeakInfo> detect_peaks();
    void merge_and_manage_channels(const std::vector<PeakInfo>& peaks);
    
    // Channel processing
    void process_channel(ChannelState& channel);
    void decode_channel_observations(ChannelState& channel);
    
    // Channel management
    ChannelState* find_or_create_channel(float freq_hz, uint16_t start_bin, uint16_t end_bin);
    ChannelState* find_nearby_channel(float freq_hz);
    void release_inactive_channels();
    
    // HamFist-compatible constants
    static constexpr uint16_t FRAME_SIZE = 64;
    static constexpr float SAMPLE_FREQUENCY = 15000.0f;  // Input rate
    static constexpr float EFFECTIVE_SAMPLE_RATE = 7500.0f;  // After decimation
    static constexpr float FRAME_MS = 1000.0f * FRAME_SIZE / EFFECTIVE_SAMPLE_RATE;  // ~8.5ms
    static constexpr uint16_t OBSERVATION_BUFFER_SIZE = 50;
    static constexpr uint16_t OBSERVATION_BURST_SIZE = 10;
    static constexpr uint16_t CHANNEL_WIDTH_BINS = 5;  // ~585Hz per channel
    static constexpr float BIN_WIDTH_HZ = EFFECTIVE_SAMPLE_RATE / FRAME_SIZE;  // ~117Hz
    static constexpr uint16_t TIMEOUT_FRAMES = 500;  // ~4.25 seconds
    
    // State
    Config config;
    std::atomic<bool> running;
    
    // Sample buffering for decimation
    std::vector<int16_t> sample_buffer;
    uint32_t decimation_counter;
    
    // FFT buffers
    float fft_real[FRAME_SIZE];
    float fft_imag[FRAME_SIZE];
    float magnitude[FRAME_SIZE / 2];
    float noise_floor[FRAME_SIZE / 2];
    float smoothed_magnitude[FRAME_SIZE / 2];
    uint32_t gate_count[FRAME_SIZE / 2];
    
    // FFT processor
    FFTProcessor fft_processor;
    
    // Channels
    std::vector<std::unique_ptr<ChannelState>> channels;
    mutable std::mutex channels_mutex;
    uint32_t next_channel_id;
    
    // Noise tracking
    bool noise_initialized;
    
    // Statistics
    uint32_t frame_count;
};

} // namespace dawson_cw

#endif
