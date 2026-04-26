//  _  ___  _   _____ _     _
// / |/ _ \/ | |_   _| |__ (_)_ __   __ _ ___
// | | | | | |   | | | '_ \| | '_ \ / _` / __|
// | | |_| | |   | | | | | | | | | | (_| \__ \.
// |_|\___/|_|   |_| |_| |_|_|_| |_|\__, |___/
//                                  |___/
//
// Dawson CW Decoder - IQ DSP Engine Implementation

#include "cw_iq_dsp_engine.h"
#include <dsp/types.h>
#include <utils/usleep.h>
#include <utils/arrays.h>
#include <utils/spectrum_utils.h>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace dawson_cw {

// Compute optimal FFT size for target resolution (next power of 2)
uint16_t IQCWDSPEngine::compute_fft_size(float sample_rate, float target_resolution) {
    float bins_needed = sample_rate / target_resolution;
    uint16_t fft_size = 1;
    while (fft_size < bins_needed) {
        fft_size <<= 1;  // Next power of 2
    }
    // Clamp to reasonable limits
    if (fft_size < 512) fft_size = 512;
    if (fft_size > 16384) fft_size = 16384;
    return fft_size;
}

// Initialize FFT with correct size for current sample rate
void IQCWDSPEngine::init_fft() {
    // Compute optimal FFT size
    fft_size = compute_fft_size(config.sample_rate, TARGET_RESOLUTION_HZ);
    bin_width_hz = config.sample_rate / fft_size;
    
    printf("[CWDBG] FFT init: sample_rate=%.0f Hz, fft_size=%d, bin_width=%.1f Hz\n",
           config.sample_rate, fft_size, bin_width_hz);
    fflush(stdout);
    
    // Resize buffers
    fft_window.resize(fft_size);
    window_coeffs.resize(fft_size);
    magnitude_spectrum.resize(fft_size);
    noise_floor.resize(fft_size);
    smoothed_magnitude.resize(fft_size);
    gate_count.resize(fft_size);
    
    // Initialize arrays
    std::fill(magnitude_spectrum.begin(), magnitude_spectrum.end(), 0.0f);
    std::fill(noise_floor.begin(), noise_floor.end(), 0.0f);
    std::fill(smoothed_magnitude.begin(), smoothed_magnitude.end(), 0.0f);
    std::fill(gate_count.begin(), gate_count.end(), 0);
    
    // Calculate Hann window coefficients
    for (uint16_t i = 0; i < fft_size; i++) {
        window_coeffs[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (fft_size - 1)));
        fft_window[i] = window_coeffs[i];
    }
    
    // Allocate FFT plan
    fft_plan = dsp::arrays::allocateFFTWPlan(false, fft_size);
    fft_input = dsp::arrays::npzeros_c(fft_size);
}

IQCWDSPEngine::IQCWDSPEngine()
    : running(false),
      fft_size(1024),
      bin_width_hz(46.875f),
      next_channel_id(1),
      frame_count(0) {
    
    // Initialize base class - must call init() to set _block_init = true
    init(nullptr);
    printf("[CWDBG] Constructor: _block_init=%d\n", (int)_block_init);
    fflush(stdout);
    
    running = true;
}

IQCWDSPEngine::~IQCWDSPEngine() {
    running = false;
    clear_all_channels();
}

void IQCWDSPEngine::set_config(const IQConfig& cfg) {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    // Check if sample rate changed
    bool rate_changed = (config.sample_rate != cfg.sample_rate);
    
    config = cfg;
    
    // Reinitialize FFT if sample rate changed
    if (rate_changed) {
        init_fft();
    }
}

IQConfig IQCWDSPEngine::get_config() const {
    std::lock_guard<std::mutex> lock(channels_mutex);
    return config;
}

void IQCWDSPEngine::set_center_frequency(double freq_hz) {
    rf_center_frequency = freq_hz;
}

void IQCWDSPEngine::init(dsp::stream<dsp::complex_t>* in) {
    printf("[CWDBG] init() in=%p\n", (void*)in);
    fflush(stdout);
    base_type::init(in);
    
    // Initialize FFT on first init
    if (fft_plan == nullptr) {
        init_fft();
    }
    
    printf("[CWDBG] init() complete _in=%p\n", (void*)_in);
    fflush(stdout);
}

void IQCWDSPEngine::setInput(dsp::stream<dsp::complex_t>* in) {
    printf("[CWDBG] setInput() in=%p _block_init=%d\n", (void*)in, (int)_block_init);
    fflush(stdout);
    if (!_block_init) {
        init(in);
    } else {
        base_type::setInput(in);
    }
    printf("[CWDBG] setInput() complete _in=%p\n", (void*)_in);
    fflush(stdout);
}

void IQCWDSPEngine::set_enabled(bool enabled) {
    printf("[CWDBG] set_enabled(%d) called\n", enabled);
    _enabled = enabled;
}

void IQCWDSPEngine::doStart() {
    printf("[CWDBG] doStart() _in=%p running=%d\n", (void*)_in, (int)running);
    fflush(stdout);
    base_type::doStart();
    printf("[CWDBG] doStart() complete, worker should be running\n");
    fflush(stdout);
}

void IQCWDSPEngine::start() {
    printf("[CWDBG] start() called\n");
    fflush(stdout);
    base_type::start();
    printf("[CWDBG] start() complete\n");
    fflush(stdout);
}

void IQCWDSPEngine::doStop() {
    printf("[CWDBG] doStop() called\n");
    base_type::doStop();
}

int IQCWDSPEngine::run() {
    static int run_count = 0;
    static int debug_count = 0;
    run_count++;
    
    if (!_in) {
        if (debug_count++ < 10) {
            printf("[CWDBG] run#%d _in=NULL\n", run_count);
            fflush(stdout);
        }
        return -1;
    }
    
    int count = _in->read();
    if (count < 0) {
        if (debug_count++ < 10) {
            printf("[CWDBG] run#%d count=%d\n", run_count, count);
            fflush(stdout);
        }
        return -1;
    }
    
    // Debug first few successful reads
    if (debug_count++ < 20) {
        printf("[CWDBG] run#%d count=%d enabled=%d\n", run_count, count, (int)_enabled);
        fflush(stdout);
    }
    
    // Simple passthrough - copy input to output
    std::memcpy(out.writeBuf, _in->readBuf, count * sizeof(dsp::complex_t));
    _in->flush();
    if (!out.swap(count)) return -1;
    
    // Process samples for channel detection when enabled
    if (_enabled) {
        total_samples += count;
        
        // Feed samples into IQ processing pipeline for channel detection
        for (int i = 0; i < count; i++) {
            process_iq_sample(_in->readBuf[i]);
        }
        
        // Debug: show buffer size
        if (run_count % 100 == 0) {
            printf("[CWDBG] samples=%llu buffer=%zu\n", 
                   (unsigned long long)total_samples, iq_buffer.size());
            fflush(stdout);
        }
    }
    
    return count;
}

void IQCWDSPEngine::process_iq_sample(const dsp::complex_t& sample) {
    iq_buffer.push_back(sample);
    
    // Process when we have a full frame
    if (iq_buffer.size() >= fft_size) {
        process_frame();
        // Remove processed samples, keep overlap for continuity
        size_t overlap = fft_size / 4;  // 75% overlap for smooth detection
        iq_buffer.erase(iq_buffer.begin(), iq_buffer.begin() + (fft_size - overlap));
    }
}

void IQCWDSPEngine::apply_window() {
    // Window is now precomputed in init_fft()
    // This function is kept for compatibility but does nothing
}

void IQCWDSPEngine::perform_fft() {
    if (!fft_plan) return;
    
    // Apply window and copy to FFT input
    for (uint16_t i = 0; i < fft_size; i++) {
        (*fft_input)[i].re = iq_buffer[i].re * fft_window[i];
        (*fft_input)[i].im = iq_buffer[i].im * fft_window[i];
    }
    
    // Execute FFT
    dsp::arrays::npfftfft(fft_input, fft_plan);
    
    // Swap so 0 frequency is at center (negative freqs on left, positive on right)
    dsp::arrays::swapfft(fft_plan->getOutput());
    
    // Get output
    fft_output = fft_plan->getOutput();
}

void IQCWDSPEngine::process_frame() {
    frame_count++;
    
    // Ensure FFT is initialized
    if (!fft_plan) {
        init_fft();
    }
    if (!fft_plan) return;
    
    // Perform complex FFT
    perform_fft();
    if (!fft_output) return;
    
    // Calculate magnitude spectrum for all bins
    float max_mag = 0;
    uint16_t max_bin = 0;
    
    for (uint16_t i = 0; i < fft_size; i++) {
        float re = (*fft_output)[i].re;
        float im = (*fft_output)[i].im;
        float mag = std::sqrt(re * re + im * im);
        magnitude_spectrum[i] = mag;
        if (mag > max_mag) {
            max_mag = mag;
            max_bin = i;
        }
    }
    
    // Debug first few frames
    if (frame_count <= 5) {
        float freq = (max_bin < fft_size/2) ? max_bin * bin_width_hz : (max_bin - fft_size) * bin_width_hz;
        printf("[CWDBG] frame#%d max_mag=%.2f @ bin%d (%.0f Hz)\n", 
               frame_count, max_mag, max_bin, freq);
        fflush(stdout);
    }
    
    // Use reusable spectrum utilities for noise floor and peak detection
    // Convert magnitude spectrum to FloatArray
    auto mag_array = dsp::arrays::npzeros(fft_size);
    for (uint16_t i = 0; i < fft_size; i++) {
        (*mag_array)[i] = magnitude_spectrum[i];
    }
    
    // Estimate noise floor using variance-based method (from experimental_fft_compressor)
    auto noise_array = dsp::arrays::estimateNoiseFloor(mag_array, 16, 0.15f);
    
    // Copy noise floor back to array
    for (uint16_t i = 0; i < fft_size; i++) {
        noise_floor[i] = (*noise_array)[i];
    }
    
    // Detect peaks using reusable function
    // Calculate min spacing in bins: spacing_hz / bin_width
    int min_spacing_bins = static_cast<int>(config.min_channel_spacing / bin_width_hz);
    min_spacing_bins = std::max(1, min_spacing_bins);  // At least 1 bin
    
    auto detected_peaks = dsp::arrays::detectSpectrumPeaks(
        mag_array, noise_array, 
        config.threshold_mult, 
        config.min_snr_db,
        min_spacing_bins
    );
    
    // Convert to our peak format and filter by absolute CW band (14.000-14.070 MHz)
    std::vector<IQPeakInfo> peaks;
    for (const auto& peak : detected_peaks) {
        int bin = std::get<0>(peak);
        float mag = std::get<1>(peak);
        float snr = std::get<2>(peak);
        
        // Calculate relative frequency from FFT bin
        float freq_relative = (bin < fft_size / 2) ? bin * bin_width_hz : (bin - fft_size) * bin_width_hz;
        
        // Calculate absolute frequency
        float freq_absolute = rf_center_frequency + freq_relative;
        
        // Only include peaks within actual CW band (14.000-14.070 MHz)
        if (freq_absolute >= config.min_cw_abs_freq && freq_absolute <= config.max_cw_abs_freq) {
            IQPeakInfo p;
            p.bin_index = bin;
            p.frequency_hz = freq_relative;  // Store relative for internal use
            p.magnitude = mag;
            p.snr = snr;
            peaks.push_back(p);
        }
    }
    
    if (frame_count <= 5 && !peaks.empty()) {
        printf("[CWDBG] frame#%d peaks=%zu\n", frame_count, peaks.size());
        for (auto& p : peaks) {
            printf("[CWDBG]   peak: %.0f Hz mag=%.2f snr=%.1f\n", p.frequency_hz, p.magnitude, p.snr);
        }
        fflush(stdout);
    }
    
    // Manage channels based on detected peaks
    if (!peaks.empty()) {
        merge_and_manage_channels(peaks);
    }
    
    // Debug channel count periodically
    if (frame_count % 50 == 0) {
        std::lock_guard<std::mutex> lock(channels_mutex);
        printf("[CWDBG] frame=%d channels=%zu samples=%llu\n", 
               frame_count, channels.size(), (unsigned long long)total_samples);
        fflush(stdout);
    }
    
    // Process each active channel - extract keying signal from its FFT bin
    {
        std::lock_guard<std::mutex> lock(channels_mutex);
        for (auto& channel : channels) {
            if (channel->is_active) {
                float mag = magnitude_spectrum[channel->bin_index];
                process_channel(*channel, mag);
            }
        }
    }
    
    // Periodic timeout check (every ~1 second)
    if (frame_count % 43 == 0) {
        update_timeouts();
    }
}

// Legacy functions - now implemented via spectrum_utils.h
void IQCWDSPEngine::update_noise_floor() {
    // Stub - real implementation is in process_frame()
}

std::vector<IQPeakInfo> IQCWDSPEngine::detect_peaks() {
    // Stub - real implementation is in process_frame()
    return std::vector<IQPeakInfo>();
}

void IQCWDSPEngine::merge_and_manage_channels(const std::vector<IQPeakInfo>& peaks) {
    std::lock_guard<std::mutex> lock(channels_mutex);
    auto now = std::chrono::steady_clock::now();
    
    for (const auto& peak : peaks) {
        // Try to find nearby existing channel
        IQChannelState* existing = find_nearby_channel(peak.frequency_hz);
        
        if (existing) {
            // Update existing channel
            existing->center_freq_hz = peak.frequency_hz;
            existing->bin_index = peak.bin_index;
            existing->has_signal = true;
            existing->snr = peak.snr;
            existing->signal_level = peak.magnitude;
            existing->last_signal_time = now;
        } else if (static_cast<int>(channels.size()) < config.max_channels) {
            // Create new channel
            auto new_channel = std::make_unique<IQChannelState>(
                next_channel_id++,
                peak.frequency_hz,
                peak.bin_index
            );
            new_channel->snr = peak.snr;
            new_channel->signal_level = peak.magnitude;
            new_channel->has_signal = true;
            new_channel->last_signal_time = now;
            channels.push_back(std::move(new_channel));
        }
    }
}

IQChannelState* IQCWDSPEngine::find_nearby_channel(float freq_hz) {
    for (auto& channel : channels) {
        if (std::abs(channel->center_freq_hz - freq_hz) < config.min_channel_spacing / 2) {
            return channel.get();
        }
    }
    return nullptr;
}

void IQCWDSPEngine::process_channel(IQChannelState& channel, float magnitude) {
    // Update noise estimate for this channel
    float threshold = noise_floor[channel.bin_index] * config.threshold_mult;
    
    // Detect keying: is the magnitude above threshold?
    bool key_down = magnitude > threshold;
    
    // Update SNR
    if (key_down) {
        channel.snr = 20.0f * std::log10(magnitude / noise_floor[channel.bin_index]);
        channel.signal_level = 0.9f * channel.signal_level + 0.1f * magnitude;
    } else {
        channel.noise_level = 0.99f * channel.noise_level + 0.01f * magnitude;
    }
    
    // Build observations for HamFist decoder
    // Each FFT frame represents ~10.7ms at 768kHz/8192 samples
    float frame_duration_ms = 1000.0f * fft_size / config.sample_rate;
    
    channel.frame_count++;
    if (key_down != channel.current_key_state) {
        // State change - save observation
        s_observation obs;
        obs.mark = channel.current_key_state ? 1 : 0;  // 1 = key down, 0 = key up
        obs.duration = channel.current_duration_frames * frame_duration_ms;
        
        // Filter unreasonable durations (CW element timing)
        if (obs.duration >= 8.0f && obs.duration <= 500.0f) {
            channel.observations.push_back(obs);
        }
        
        channel.current_key_state = key_down;
        channel.current_duration_frames = 0;
        
        // Update WPM from decoder
        if (channel.decoder) {
            channel.wpm = channel.decoder->get_WPM();
        }
    }
    channel.current_duration_frames++;
    
    // Timeout check for observations (inter-character gap)
    if (channel.current_duration_frames >= TIMEOUT_FRAMES) {
        if (channel.snr > config.min_snr_db) {
            decode_channel_observations(channel);
        }
        channel.observations.clear();
        channel.current_duration_frames = 0;
        channel.decoder->reset();
        channel.has_signal = false;
    }
    
    // Decode when we have enough observations
    if (channel.observations.size() >= OBSERVATION_BUFFER_SIZE ||
        (channel.observations.size() >= OBSERVATION_BURST_SIZE && channel.decoder->has_good_estimates())) {
        if (channel.snr > config.min_snr_db) {
            decode_channel_observations(channel);
        }
        channel.observations.clear();
        channel.current_duration_frames = 0;
    }
}

void IQCWDSPEngine::decode_channel_observations(IQChannelState& channel) {
    if (!channel.decoder || channel.observations.empty()) return;
    
    // Copy observations to array for decoder
    std::vector<s_observation> obs_copy = channel.observations;
    
    // Decode using HamFist
    channel.decoder->decode(obs_copy.data(), static_cast<int>(obs_copy.size()));
    
    // Get decoded text
    std::string new_text = channel.decoder->get_text();
    if (!new_text.empty()) {
        std::lock_guard<std::mutex> lock(channel.text_mutex);
        
        // Append new text
        channel.decoded_text += new_text;
        
        // Keep only last N characters
        const size_t MAX_TEXT_LEN = 100;
        if (channel.decoded_text.length() > MAX_TEXT_LEN) {
            channel.decoded_text = channel.decoded_text.substr(channel.decoded_text.length() - MAX_TEXT_LEN);
        }
        
        channel.last_decode_time = std::chrono::steady_clock::now();
    }
}

void IQCWDSPEngine::update_timeouts() {
    std::lock_guard<std::mutex> lock(channels_mutex);
    auto now = std::chrono::steady_clock::now();
    
    for (auto& channel : channels) {
        // Signal loss check
        auto signal_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - channel->last_signal_time).count();
        if (signal_elapsed > config.signal_loss_seconds) {
            channel->has_signal = false;
        }
        
        // Decode timeout check - mark inactive if no recent decodes
        auto decode_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - channel->last_decode_time).count();
        if (decode_elapsed > config.timeout_seconds) {
            channel->is_active = false;
        }
    }
    
    // Remove inactive channels
    channels.erase(
        std::remove_if(channels.begin(), channels.end(),
            [](const std::unique_ptr<IQChannelState>& ch) { return !ch->is_active; }),
        channels.end()
    );
}

std::string IQCWDSPEngine::get_channel_text(uint32_t channel_id) {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    for (const auto& channel : channels) {
        if (channel->id == channel_id) {
            std::lock_guard<std::mutex> text_lock(channel->text_mutex);
            return channel->decoded_text;
        }
    }
    return "";
}

float IQCWDSPEngine::get_channel_snr(uint32_t channel_id) {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    for (const auto& channel : channels) {
        if (channel->id == channel_id) {
            return channel->snr;
        }
    }
    return 0.0f;
}

float IQCWDSPEngine::get_channel_wpm(uint32_t channel_id) {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    for (const auto& channel : channels) {
        if (channel->id == channel_id) {
            return channel->wpm;
        }
    }
    return 0.0f;
}

float IQCWDSPEngine::get_channel_frequency(uint32_t channel_id) {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    for (const auto& channel : channels) {
        if (channel->id == channel_id) {
            return rf_center_frequency + channel->center_freq_hz;
        }
    }
    return 0.0f;
}

bool IQCWDSPEngine::is_channel_active(uint32_t channel_id) {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    for (const auto& channel : channels) {
        if (channel->id == channel_id) {
            return channel->is_active;
        }
    }
    return false;
}

size_t IQCWDSPEngine::get_active_channel_count() const {
    std::lock_guard<std::mutex> lock(channels_mutex);
    return channels.size();
}

std::vector<float> IQCWDSPEngine::get_active_frequencies() const {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    std::vector<float> freqs;
    for (const auto& channel : channels) {
        if (channel->is_active) {
            freqs.push_back(channel->center_freq_hz);
        }
    }
    return freqs;
}

std::vector<float> IQCWDSPEngine::get_absolute_frequencies() const {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    std::vector<float> freqs;
    for (const auto& channel : channels) {
        if (channel->is_active) {
            freqs.push_back(rf_center_frequency + channel->center_freq_hz);
        }
    }
    return freqs;
}

void IQCWDSPEngine::get_spectrum_magnitude(float* out_buffer, uint16_t num_bins) const {
    if (!out_buffer || num_bins == 0 || magnitude_spectrum.empty()) return;
    
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    // Return downsampled magnitude spectrum
    uint16_t fft_bins = static_cast<uint16_t>(magnitude_spectrum.size());
    uint16_t step = fft_bins / num_bins;
    if (step < 1) step = 1;
    
    for (uint16_t i = 0; i < num_bins && i * step < fft_bins; i++) {
        // Convert to dB for display
        float mag = magnitude_spectrum[i * step];
        float db = 20.0f * std::log10(mag + 1e-10f);
        out_buffer[i] = db;
    }
}

void IQCWDSPEngine::reset() {
    std::lock_guard<std::mutex> lock(channels_mutex);
    
    iq_buffer.clear();
    frame_count = 0;
    
    for (auto& channel : channels) {
        channel->decoder->reset();
        channel->observations.clear();
        channel->current_key_state = false;
        channel->current_duration_frames = 0;
        channel->decoded_text.clear();
    }
}

void IQCWDSPEngine::clear_all_channels() {
    std::lock_guard<std::mutex> lock(channels_mutex);
    channels.clear();
    next_channel_id = 1;
}

} // namespace dawson_cw
