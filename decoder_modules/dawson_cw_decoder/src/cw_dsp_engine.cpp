//  _  ___  _   _____ _     _
// / |/ _ \/ | |_   _| |__ (_)_ __   __ _ ___
// | | | | | |   | | | '_ \| | '_ \ / _` / __|
// | | |_| | |   | | | | | | | | | | (_| \__ \.
// |_|\___/|_|   |_| |_| |_|_|_| |_|\__, |___/
//                                  |___/
//
// Dawson CW Decoder - DSP Engine Implementation

#include "cw_dsp_engine.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace dawson_cw {

CWDSPEngine::CWDSPEngine() 
    : running(false), 
      decimation_counter(0),
      next_channel_id(1),
      noise_initialized(false),
      frame_count(0) {
    
    // Initialize buffers
    std::memset(fft_real, 0, sizeof(fft_real));
    std::memset(fft_imag, 0, sizeof(fft_imag));
    std::memset(magnitude, 0, sizeof(magnitude));
    std::memset(noise_floor, 0, sizeof(noise_floor));
    std::memset(smoothed_magnitude, 0, sizeof(smoothed_magnitude));
    std::memset(gate_count, 0, sizeof(gate_count));
    
    running = true;
}

CWDSPEngine::~CWDSPEngine() {
    running = false;
    clear_all_channels();
}

void CWDSPEngine::set_config(const Config& cfg) {
    std::lock_guard<std::mutex> lock(channels_mutex);
    config = cfg;
}

Config CWDSPEngine::get_config() const {
    std::lock_guard<std::mutex> lock(channels_mutex);
    return config;
}

void CWDSPEngine::process_audio_sample(int16_t sample) {
    // Decimate from 15000 Hz to 7500 Hz (2:1)
    decimation_counter++;
    if (decimation_counter < 2) {
        return;  // Skip every other sample
    }
    decimation_counter = 0;
    
    // Add to buffer
    sample_buffer.push_back(sample);
    
    // Process when we have a full frame
    if (sample_buffer.size() >= FRAME_SIZE) {
        // Copy to FFT buffer
        for (size_t i = 0; i < FRAME_SIZE; i++) {
            fft_real[i] = static_cast<float>(sample_buffer[i]);
            fft_imag[i] = 0.0f;
        }
        
        // Remove processed samples
        sample_buffer.erase(sample_buffer.begin(), sample_buffer.begin() + FRAME_SIZE);
        
        // Process frame
        process_frame();
    }
}

void CWDSPEngine::process_audio_block(const int16_t* samples, size_t count) {
    for (size_t i = 0; i < count; i++) {
        process_audio_sample(samples[i]);
    }
}

void CWDSPEngine::process_frame() {
    frame_count++;
    
    // Apply window
    FFTProcessor::apply_window(fft_real, FRAME_SIZE);
    
    // Perform FFT
    fft_processor.process(fft_real, fft_imag, 6);  // 2^6 = 64
    
    // Calculate magnitude
    FFTProcessor::calculate_magnitude(fft_real, fft_imag, magnitude, FRAME_SIZE / 2);
    
    // Update noise floor
    update_noise_floor();
    
    // Detect peaks
    auto peaks = detect_peaks();
    
    // Manage channels
    if (!peaks.empty()) {
        merge_and_manage_channels(peaks);
    }
    
    // Process active channels
    {
        std::lock_guard<std::mutex> lock(channels_mutex);
        for (auto& channel : channels) {
            if (channel->is_active) {
                process_channel(*channel);
            }
        }
    }
    
    // Periodic timeout check (every ~1 second)
    if (frame_count % 120 == 0) {
        update_timeouts();
    }
}

void CWDSPEngine::update_noise_floor() {
    for (uint16_t i = 0; i < FRAME_SIZE / 2; i++) {
        if ((magnitude[i] < 2.0f * noise_floor[i]) || magnitude[i] < 5.0f) {
            noise_floor[i] = 0.99f * noise_floor[i] + 0.01f * magnitude[i];
            gate_count[i] = 0;
        } else if (gate_count[i] > 50) {
            noise_floor[i] = 0.9f * noise_floor[i] + 0.1f * magnitude[i];
        } else {
            gate_count[i]++;
        }
        noise_floor[i] = std::max(noise_floor[i], 1.0f);
    }
}

std::vector<PeakInfo> CWDSPEngine::detect_peaks() {
    std::vector<PeakInfo> peaks;
    const uint16_t num_bins = FRAME_SIZE / 2;
    
    // Find local maxima above threshold
    for (uint16_t i = 1; i < num_bins - 1; i++) {
        float threshold = noise_floor[i] * config.threshold_mult;
        
        if (magnitude[i] > magnitude[i-1] && 
            magnitude[i] > magnitude[i+1] &&
            magnitude[i] > threshold) {
            
            float snr = 20.0f * log10f(magnitude[i] / noise_floor[i]);
            if (snr < config.min_snr_db) continue;
            
            PeakInfo peak;
            peak.bin_index = i;
            peak.frequency_hz = i * BIN_WIDTH_HZ;
            peak.magnitude = magnitude[i];
            peak.snr = snr;
            peaks.push_back(peak);
        }
    }
    
    // Sort by magnitude (strongest first)
    std::sort(peaks.begin(), peaks.end(), 
              [](const PeakInfo& a, const PeakInfo& b) {
                  return a.magnitude > b.magnitude;
              });
    
    // Merge close peaks (within ~200Hz = ~1.7 bins)
    std::vector<PeakInfo> merged;
    for (const auto& peak : peaks) {
        bool too_close = false;
        for (const auto& existing : merged) {
            if (std::abs(peak.frequency_hz - existing.frequency_hz) < 200.0f) {
                too_close = true;
                break;
            }
        }
        if (!too_close && static_cast<int>(merged.size()) < config.max_channels) {
            merged.push_back(peak);
        }
    }
    
    return merged;
}

void CWDSPEngine::merge_and_manage_channels(const std::vector<PeakInfo>& peaks) {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    for (const auto& peak : peaks) {
        // Try to find nearby existing channel
        ChannelState* existing = find_nearby_channel(peak.frequency_hz);
        
        if (existing) {
            // Update existing channel
            existing->center_freq_hz = peak.frequency_hz;
            existing->start_bin = peak.bin_index >= 2 ? peak.bin_index - 2 : 0;
            existing->end_bin = std::min(
                static_cast<uint16_t>(peak.bin_index + CHANNEL_WIDTH_BINS - 2),
                static_cast<uint16_t>(FRAME_SIZE / 2 - 1)
            );
            existing->has_signal = true;
            existing->snr = peak.snr;
            existing->last_signal_time = std::chrono::steady_clock::now();
        } else if (static_cast<int>(channels.size()) < config.max_channels) {
            // Create new channel
            uint16_t start_bin = peak.bin_index >= 2 ? peak.bin_index - 2 : 0;
            uint16_t end_bin = std::min(
                static_cast<uint16_t>(peak.bin_index + CHANNEL_WIDTH_BINS - 2),
                static_cast<uint16_t>(FRAME_SIZE / 2 - 1)
            );
            
            auto new_channel = std::make_unique<ChannelState>(
                next_channel_id++,
                peak.frequency_hz,
                start_bin,
                end_bin
            );
            new_channel->snr = peak.snr;
            new_channel->has_signal = true;
            channels.push_back(std::move(new_channel));
        }
    }
}

ChannelState* CWDSPEngine::find_nearby_channel(float freq_hz) {
    for (auto& channel : channels) {
        if (std::abs(channel->center_freq_hz - freq_hz) < 50.0f) {
            return channel.get();
        }
    }
    return nullptr;
}

void CWDSPEngine::process_channel(ChannelState& channel) {
    // Calculate magnitude in channel bandwidth
    float max_mag = 0;
    float avg_mag = 0;
    uint16_t count = 0;
    
    for (uint16_t i = channel.start_bin; i <= channel.end_bin && i < FRAME_SIZE / 2; i++) {
        max_mag = std::max(max_mag, magnitude[i]);
        avg_mag += magnitude[i];
        count++;
    }
    avg_mag /= count;
    
    // Threshold detection
    float threshold = noise_floor[channel.start_bin] * config.threshold_mult;
    bool value = max_mag > threshold;
    
    // Update SNR
    if (value) {
        channel.snr = 20.0f * log10f(max_mag / noise_floor[channel.start_bin]);
    }
    
    // Build observations
    channel.frame_count++;
    if (value != channel.current_value) {
        // State change - save observation
        s_observation obs;
        obs.mark = channel.current_value;
        obs.duration = FRAME_MS * channel.current_duration_ms;
        
        // Filter unreasonable durations
        if (obs.duration >= 8.0f && obs.duration <= 1440.0f) {
            channel.observations.push_back(obs);
        }
        
        channel.current_value = value;
        channel.current_duration_ms = 0;
        
        // Update WPM from decoder
        if (channel.decoder) {
            channel.wpm = channel.decoder->get_WPM();
        }
    }
    channel.current_duration_ms++;
    
    // Timeout check for observations
    if (channel.current_duration_ms >= TIMEOUT_FRAMES) {
        if (channel.snr > config.min_snr_db) {
            decode_channel_observations(channel);
        }
        channel.observations.clear();
        channel.current_duration_ms = 0;
        channel.decoder->reset();
        channel.has_signal = false;
    }
    
    // Full buffer decode
    if (channel.observations.size() >= OBSERVATION_BUFFER_SIZE ||
        (channel.observations.size() >= OBSERVATION_BURST_SIZE && channel.decoder->has_good_estimates())) {
        if (channel.snr > config.min_snr_db) {
            decode_channel_observations(channel);
        }
        channel.observations.clear();
        channel.current_duration_ms = 0;
    }
}

void CWDSPEngine::decode_channel_observations(ChannelState& channel) {
    if (!channel.decoder || channel.observations.empty()) return;
    
    // Copy observations to array for decoder
    std::vector<s_observation> obs_copy = channel.observations;
    
    // Decode
    channel.decoder->decode(obs_copy.data(), static_cast<int>(obs_copy.size()));
    
    // Get decoded text
    std::string new_text = channel.decoder->get_text();
    if (!new_text.empty()) {
        std::lock_guard<std::mutex> lock(channel.text_mutex);
        
        // Append new text
        channel.decoded_text += new_text;
        
        // Keep only last 40 characters (plus some for scrolling)
        if (channel.decoded_text.length() > 60) {
            channel.decoded_text = channel.decoded_text.substr(channel.decoded_text.length() - 60);
        }
        
        channel.last_decode_time = std::chrono::steady_clock::now();
    }
}

void CWDSPEngine::update_timeouts() {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    auto now = std::chrono::steady_clock::now();
    
    for (auto& channel : channels) {
        // Signal loss check
        auto signal_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - channel->last_signal_time).count();
        if (signal_elapsed > config.signal_loss_seconds) {
            channel->has_signal = false;
        }
        
        // Decode timeout check
        auto decode_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - channel->last_decode_time).count();
        if (decode_elapsed > config.timeout_seconds) {
            channel->is_active = false;
        }
    }
    
    // Remove inactive channels
    channels.erase(
        std::remove_if(channels.begin(), channels.end(),
            [](const std::unique_ptr<ChannelState>& ch) { return !ch->is_active; }),
        channels.end()
    );
}

std::string CWDSPEngine::get_channel_text(uint32_t channel_id) {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    for (const auto& channel : channels) {
        if (channel->id == channel_id) {
            std::lock_guard<std::mutex> text_lock(channel->text_mutex);
            return channel->decoded_text;
        }
    }
    return "";
}

float CWDSPEngine::get_channel_snr(uint32_t channel_id) {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    for (const auto& channel : channels) {
        if (channel->id == channel_id) {
            return channel->snr;
        }
    }
    return 0.0f;
}

float CWDSPEngine::get_channel_wpm(uint32_t channel_id) {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    for (const auto& channel : channels) {
        if (channel->id == channel_id) {
            return channel->wpm;
        }
    }
    return 0.0f;
}

float CWDSPEngine::get_channel_frequency(uint32_t channel_id) {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    for (const auto& channel : channels) {
        if (channel->id == channel_id) {
            return channel->center_freq_hz;
        }
    }
    return 0.0f;
}

bool CWDSPEngine::is_channel_active(uint32_t channel_id) {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    for (const auto& channel : channels) {
        if (channel->id == channel_id) {
            return channel->is_active;
        }
    }
    return false;
}

std::vector<float> CWDSPEngine::get_active_frequencies() const {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    std::vector<float> freqs;
    for (const auto& channel : channels) {
        if (channel->is_active) {
            freqs.push_back(channel->center_freq_hz);
        }
    }
    return freqs;
}

size_t CWDSPEngine::get_active_channel_count() const {
    std::lock_guard<std::mutex> lock(channels_mutex);
    return channels.size();
}

void CWDSPEngine::reset() {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    sample_buffer.clear();
    frame_count = 0;
    
    for (auto& channel : channels) {
        channel->decoder->reset();
        channel->observations.clear();
        channel->current_value = false;
        channel->current_duration_ms = 0;
        channel->decoded_text.clear();
    }
}

void CWDSPEngine::clear_all_channels() {
    std::lock_guard<std::mutex> lock(channels_mutex);
    channels.clear();
    next_channel_id = 1;
}

} // namespace dawson_cw
