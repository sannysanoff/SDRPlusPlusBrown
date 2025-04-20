#include "signal_detector.h"
#include "../window/blackman.h"
#include <utils/flog.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <ctime>
#include <chrono>
#include <ctm.h>
#include <iomanip>
#include <sstream>

namespace dsp::detector {

    // Normalize magnitudes by subtracting a moving average
    static std::vector<float> normalizeMagnitudes(const ArrayView<float>& magnitudes) {
        const int WINDOW = 300;
        const int n = magnitudes.size();
        std::vector<float> normalized(n, 0.0f);
        
        if (n == 0) {
            return normalized;
        }
        
        // Compute moving average for each point
        for (int i = 0; i < n; i++) {
            // Define window boundaries, ensuring they stay within array bounds
            int start_idx = std::max(0, i - WINDOW/2);
            int end_idx = std::min(n-1, i + WINDOW/2);
            
            if (start_idx > end_idx) {
                continue;  // Skip if window is invalid
            }
                
            // Compute the mean of the window
            float window_sum = 0.0f;
            int count = 0;
            for (int j = start_idx; j <= end_idx; j++) {
                window_sum += magnitudes[j];
                count++;
            }
            
            float window_mean = (count > 0) ? window_sum / count : 0.0f;
                
            // Subtract the local mean from the current value
            normalized[i] = magnitudes[i] - window_mean;
        }
        
        return normalized;
    }

    // Helper for percentile calculation
    static float percentileFast(std::vector<float> arr, float p) {
        size_t n = arr.size();
        size_t k = std::ceil(n * p);
        std::nth_element(arr.begin(), arr.begin() + k - 1, arr.end());
        return arr.at(k - 1);
    }


    // Find median of a vector - destructively modifies the input vector
    template <typename T>
    static T median_destructive(std::vector<T>& data) {
        if (data.size() == 0) {
            return T();
        }
    
        size_t n = data.size();
    
        std::nth_element(data.begin(), data.begin() + n/2, data.end());
        return data.at(n/2);
    }

    // Find the dominant harmonic intervals in a spectrum
    static std::pair<std::vector<int>, std::vector<float>> findDominantHarmonicIntervals(
        const ArrayView<float>& spectrum,
        int epsilon,
        int min_interval,
        int max_interval
    ) {
        int N = spectrum.size();
        if (N == 0) {
            return std::make_pair(std::vector<int>(), std::vector<float>());
        }

        // Validate input parameters
        if (min_interval <= 0 || max_interval < min_interval || max_interval >= N) {
            flog::error("Invalid interval range: min={}, max={}, N={}", min_interval, max_interval, N);
            return std::make_pair(std::vector<int>(), std::vector<float>());
        }

        if (epsilon <= 0) {
            flog::error("Epsilon must be positive: epsilon={}", epsilon);
            return std::make_pair(std::vector<int>(), std::vector<float>());
        }

        // Create range of intervals
        std::vector<int> intervals;
        intervals.reserve(max_interval - min_interval + 1);
        for (int d = min_interval; d <= max_interval; d++) {
            intervals.push_back(d);
        }

        int num_intervals = intervals.size();
        if (num_intervals == 0) {
            return std::make_pair(std::vector<int>(), std::vector<float>());
        }

        // Raw responses matrix: intervals x spectrum
        std::vector<std::vector<float>> raw_responses(num_intervals, std::vector<float>(N, 0.0f));

        // 1. Compute lag products
        for (int k = 0; k < num_intervals; k++) {
            int d = intervals[k];
            int valid_len = N - d;

            if (valid_len > 0) {
                for (int i = 0; i < valid_len; i++) {
                    raw_responses[k][i] = std::max(0.0f, spectrum[i]) * std::max(0.0f, spectrum[i + d]);
                }
            }
        }

        // 2. Use raw responses directly for further processing without smoothing
        const std::vector<std::vector<float>>& smoothed_responses = raw_responses;

        // 3. Find dominant interval and confidence for each position
        std::vector<int> dominant_intervals(N, 0);
        std::vector<float> confidence_scores(N, 0.0f);

        for (int i = 0; i < N; i++) {
            float max_val = -std::numeric_limits<float>::infinity();
            int max_k = 0;

            for (int k = 0; k < num_intervals; k++) {
                if (smoothed_responses[k][i] > max_val) {
                    max_val = smoothed_responses[k][i];
                    max_k = k;
                }
            }

            if (max_k < static_cast<int>(intervals.size())) {
                dominant_intervals[i] = intervals[max_k];
                confidence_scores[i] = max_val;
            }
        }

        return std::make_pair(dominant_intervals, confidence_scores);
    }

    // Get line candidates from a frequency slice
    static std::shared_ptr<std::vector<float>> getLineCandidates(const ArrayView<float>& first_slice_db) {
        // Guard against empty input
        if (first_slice_db.size() == 0) {
            return std::make_shared<std::vector<float>>();
        }
        
        // Initialize return value with -20 for all elements
        auto retval = std::make_shared<std::vector<float>>(first_slice_db.size(), -20.0f);
        
        // Normalize the magnitudes
        std::vector<float> normalized_slice = normalizeMagnitudes(first_slice_db);

        // Find dominant harmonic intervals
        auto result = findDominantHarmonicIntervals(
            ArrayView<float>(normalized_slice), 250, 8, 35);

        const std::vector<int>& freq = result.first;
        const std::vector<float>& score = result.second;

        // Early return if frequencies couldn't be determined
        if (freq.empty() || freq.size() < 100) {
            flog::info("Not enough frequency data for line candidate detection");
            return retval;
        }

        // Scan through the data in sections
        for (size_t p = 0; p + 100 <= freq.size(); p += 50) {
            size_t vl1 = p;
            size_t vl2 = p + 100;

            if (vl2 > normalized_slice.size()) {
                vl2 = normalized_slice.size();
            }

            if (vl2 <= vl1) {
                continue;
            }

            // Extract view of data for this section
            ArrayView<float> subdata(&normalized_slice[vl1], vl2 - vl1);

            // Extract frequency for this section
            std::vector<int> freqSection(freq.begin() + vl1, freq.begin() + vl2);

            if (freqSection.empty()) {
                continue;
            }

            int domfreq_int = median_destructive(freqSection);

            // Guard against invalid dominant frequency
            if (domfreq_int <= 0) {
                continue;
            }

            // Initialize offset score
            std::vector<float> offset_score(domfreq_int, 0.0f);

            // Accumulate signals at each phase offset
            for (size_t x = 0; x < subdata.size(); x++) {
                // Ensure modulo calculation is correct and in bounds
                int offset = static_cast<int>((x + offset_score.size() - 1) % offset_score.size());
                if (offset >= 0 && offset < static_cast<int>(offset_score.size())) {
                    offset_score[offset] += subdata[x];
                }
            }

            // Find phase with maximum score
            auto max_it = std::max_element(offset_score.begin(), offset_score.end());
            if (max_it == offset_score.end()) {
                continue;
            }

            int phase = std::distance(offset_score.begin(), max_it);

            // Collect in-phase samples
            std::vector<float> inphase;
            std::vector<size_t> inphaseix;

            for (int ix = phase; ix < static_cast<int>(subdata.size()); ix += domfreq_int) {
                if (ix >= 0 && ix < static_cast<int>(subdata.size())) {
                    inphase.push_back(subdata[ix]);
                    inphaseix.push_back(vl1 + ix);
                }
            }

            // Find maximum in-phase value
            if (!inphase.empty()) {
                auto max_inphase_it = std::max_element(inphase.begin(), inphase.end());
                int maxi = std::distance(inphase.begin(), max_inphase_it);

                // Mark peaks before maximum
                for (int x = maxi - 4; x < maxi; x++) {
                    if (x >= 0 && x < static_cast<int>(inphase.size())) {
                        size_t ix = inphaseix[x];
                        if (ix < normalized_slice.size() && ix < retval->size()) {
                            (*retval)[ix] = -normalized_slice[ix];
                        }
                    }
                }
            }
        }

        return retval;
    }


    SignalDetector::SignalDetector() {
        flog::info("Signal detector.");
    }

    SignalDetector::~SignalDetector() {
        if (!base_type::_block_init) { return; }
        base_type::stop();

        if (fftWindowBuf) {
            delete[] fftWindowBuf;
            fftWindowBuf = nullptr;
        }

        fftInArray.reset();
        fftPlan.reset();
    }

    void SignalDetector::init(stream<complex_t> *in) {
        base_type::init(in);
    }

    void SignalDetector::setSampleRate(double sampleRate) {
        if (this->sampleRate == sampleRate) {
            return;
        }

        this->sampleRate = sampleRate;
        updateFFTSize();
        clear();
    }

    void SignalDetector::setCenterFrequency(double centerFrequency) {
        if (this->centerFrequency == centerFrequency) {
            return;
        }

        this->centerFrequency = centerFrequency;
        flog::info("Signal detector center frequency set to {0} Hz", centerFrequency);

        // Reset buffer position to start fresh with new frequency
        clear();
    }

    void SignalDetector::clear() {
        bufferPos = 0;
        framesSinceLastDetect = 0;
    }

    void SignalDetector::updateFFTSize() {
        if (sampleRate <= 0) {
            return;
        }

        // Calculate FFT size as samplerate/10
        int newFFTSize = sampleRate * TIME_SLICE;

        fftSize = newFFTSize;
        flog::info("Signal detector FFT size set to {0}", fftSize);

        // Reset buffer
        buffer.resize(fftSize);
        bufferPos = 0;

        // Clean up old window buffer
        if (fftWindowBuf) {
            delete[] fftWindowBuf;
            fftWindowBuf = nullptr;
        }

        // Allocate window buffer
        fftWindowBuf = new float[fftSize];

        // Allocate new input array
        fftInArray = std::make_shared<std::vector<complex_t> >(fftSize);

        // Create FFT plan (forward transform)
        fftPlan = dsp::arrays::allocateFFTWPlan(false, fftSize);

        // Generate window function
        generateWindow();
    }

    void SignalDetector::generateWindow() {
        if (!fftWindowBuf || fftSize <= 0) {
            return;
        }

        // Generate Blackman window
        for (int i = 0; i < fftSize; i++) {
            fftWindowBuf[i] = window::blackman(i, fftSize);
        }
    }

    int SignalDetector::run() {
        int count = base_type::_in->read();
        if (count < 0) {
            return -1;
        }

        // If FFT is not initialized, just pass through the data
        if (fftSize <= 0 || !fftPlan) {
            memcpy(base_type::out.writeBuf, base_type::_in->readBuf, count * sizeof(complex_t));
            base_type::_in->flush();
            if (!base_type::out.swap(count)) { return -1; }
            return count;
        }

        // Process samples and perform FFT when buffer is full
        for (int i = 0; i < count; i++) {
            // Store sample in buffer
            if (bufferPos < fftSize) {
                buffer[bufferPos++] = base_type::_in->readBuf[i];
            }

            // When buffer is full, perform FFT
            if (bufferPos >= fftSize) {
                // Apply window function
                auto &inVec = *fftInArray;
                for (int j = 0; j < fftSize; j++) {
                    inVec.at(j).re = buffer.at(j).re * fftWindowBuf[j];
                    inVec.at(j).im = buffer.at(j).im * fftWindowBuf[j];
                }

                // Execute FFT
                dsp::arrays::npfftfft(fftInArray, fftPlan);
                
                // Swap FFT output so that 0 frequency is at the center
                dsp::arrays::swapfft(fftPlan->getOutput());

                // Compute magnitude spectrum
                auto mag = dsp::arrays::npabsolute(fftPlan->getOutput());

                // Convert magnitude to logarithmic scale (dB)
                for (int j = 0; j < fftSize; j++) {
                    // Add small value (1e-10) to avoid log of zero, multiply by 20 to convert to dB
                    mag->at(j) = 20.0f * log10f(mag->at(j) + 1e-10f);
                }

                addSingleFFTRow(*mag);
                bufferPos = 0;
            }
            // Pass through the original data unchanged
            base_type::out.writeBuf[i] = base_type::_in->readBuf[i];
        }

        base_type::_in->flush();
        if (!base_type::out.swap(count)) { return -1; }
        return count;
    }

    void SignalDetector::aggregateAndDetect() {
        if (suppressedCarrierCandidates.empty()) {
            return;
        }

        // Get dimensions
        size_t freq_bins = suppressedCarrierCandidates[0]->size();
        size_t time_bins = suppressedCarrierCandidates.size();

        if (freq_bins == 0 || time_bins == 0) {
            return;
        }

        // Create a 2D array for chosen candidates (in linearized form)
        std::vector<float> chosen_candidates(freq_bins * time_bins, 0.0f);

        std::vector<float> fscores(1 + 2 * FREQ_WINDOW_SIZE, 0.0f);
        // Process frequency windows for candidate selection
        for (size_t i = 0; i < time_bins; i++) {
            for (size_t f = 0; f < freq_bins; f += FREQ_WINDOW_SIZE) {
                // Define window boundaries
                size_t fmin = (f > FREQ_WINDOW_SIZE) ? f - FREQ_WINDOW_SIZE : 0;
                size_t fmax = std::min(freq_bins, f + FREQ_WINDOW_SIZE);
                size_t tmin = (i > 10) ? i - 10 : 0;
                size_t tmax = std::min(time_bins, i + 10);

                std::fill(fscores.begin(), fscores.end(), 0.0f);

                // Count positive values in each frequency bin across nearby time bins
                for (size_t fcheck = 0; fcheck < fscores.size(); fcheck++) {
                    size_t freq_idx = fmin + fcheck;
                    for (size_t tcheck = tmin; tcheck < tmax; tcheck++) {
                        const auto& candidates = suppressedCarrierCandidates[tcheck];
                        if (candidates && freq_idx < candidates->size() && candidates->at(freq_idx) > 0) {
                            fscores[fcheck] += 1.0f;
                        }
                    }
                }

                // Find the frequency bin with maximum score
                auto max_it = std::max_element(fscores.begin(), fscores.end());
                if (max_it != fscores.end()) {
                    size_t bestf = std::distance(fscores.begin(), max_it);
                    chosen_candidates[i * freq_bins + f] = fscores[bestf];
                }
            }
        }

        // Compute mean across time dimension (vertical mean of all candidates)
        std::vector<float> sigs(freq_bins, 0.0f);
        for (size_t f = 0; f < freq_bins; f++) {
            float sum = 0.0f;
            int count = 0;
            for (size_t t = 0; t < time_bins; t++) {
                const auto& candidates = suppressedCarrierCandidates[t];
                if (candidates && f < candidates->size()) {
                    sum += candidates->at(f);
                    count++;
                }
            }
            sigs[f] = (count > 0) ? sum / count : 0.0f;
        }

        // Apply simple moving average with centered kernel of size 3
        sigs_smoothed.resize(sigs.size());
        for (size_t i = 0; i < sigs.size(); i++) {
            if (i == 0) {
                // Edge case: first element
                sigs_smoothed[i] = (sigs[i] + sigs[i+1]) / 2.0f;
            }
            else if (i == sigs.size() - 1) {
                // Edge case: last element
                sigs_smoothed[i] = (sigs[i-1] + sigs[i]) / 2.0f;
            }
            else {
                // Standard case: centered kernel
                sigs_smoothed[i] = (sigs[i-1] + sigs[i] + sigs[i+1]) / 3.0f;
            }
        }


    }

    void SignalDetector::addSingleFFTRow(const ArrayView<float> &rowView) {
        auto candidates = getLineCandidates(rowView);

        if (candidates && candidates->size() == rowView.size()) {
            suppressedCarrierCandidates.push_back(candidates);

            // Keep a fixed history length
            while (suppressedCarrierCandidates.size() > N_FFT_ROWS) {
                suppressedCarrierCandidates.erase(suppressedCarrierCandidates.begin());
            }

            // Increment counter for frames since last detection
            framesSinceLastDetect++;

            // Only detect when we have enough data AND detection interval has passed
            if (suppressedCarrierCandidates.size() > MIN_DETECT_FFT_ROWS && 
                framesSinceLastDetect >= DETECT_INTERVAL_FRAMES) {
                long long l1 = currentTimeMillis();
                aggregateAndDetect();
                long long l2 = currentTimeMillis();
                flog::info("aggregateAndDetect, took {}", (int64_t)(l2-l1));
                framesSinceLastDetect = 0; // Reset counter after detection
            }
        }
    }
}
