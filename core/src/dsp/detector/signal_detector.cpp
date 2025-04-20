#include "signal_detector.h"
#include "../window/blackman.h"
#include <utils/flog.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace dsp::detector {



    // Helper function for logging with timestamp
    static std::string getTimestampStr() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        
        // Add milliseconds
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        ss << "." << std::setfill('0') << std::setw(3) << ms.count();
        
        return ss.str();
    }

    // Log line with timestamp
    static void logLine(const std::string& message) {
        std::string timestamp = getTimestampStr();
        flog::info("[{}] {}", timestamp, message);
    }

    // Normalize magnitudes by subtracting a moving average
    static std::vector<float> normalizeMagnitudes(const ArrayView<float>& magnitudes) {
        const int WINDOW = 300;
        const int n = magnitudes.size();
        std::vector<float> normalized(n);
        
        // Compute moving average for each point
        for (int i = 0; i < n; i++) {
            // Define window boundaries, ensuring they stay within array bounds
            int start_idx = std::max(0, i - WINDOW/2);
            int end_idx = std::min(n-1, i + WINDOW/2);
            
            // Compute the mean of the window
            float window_sum = 0.0f;
            for (int j = start_idx; j <= end_idx; j++) {
                window_sum += magnitudes[j];
            }
            float window_mean = window_sum / (end_idx - start_idx + 1);
            
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
        return arr[k - 1];
    }

    // Find median of a vector - int version (using ArrayView)
    static int median(const ArrayView<int>& data) {
        if (data.size() == 0) {
            return 0;
        }
    
        // Need to copy for nth_element
        std::vector<int> temp(data.begin(), data.end());
        return median_destructive(temp);
    }

    // Find median of a vector - destructively modifies the input vector
    template <typename T>
    static T median_destructive(std::vector<T>& data) {
        if (data.size() == 0) {
            return T();
        }
    
        size_t n = data.size();
    
        if (n % 2 == 0) {
            // Even size: median is average of two middle elements
            std::nth_element(data.begin(), data.begin() + n/2, data.end());
            T val1 = data[n/2];
            std::nth_element(data.begin(), data.begin() + (n/2 - 1), data.end());
            T val2 = data[n/2 - 1];
            return (val1 + val2) / 2;
        } else {
            // Odd size: median is the middle element
            std::nth_element(data.begin(), data.begin() + n/2, data.end());
            return data[n/2];
        }
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
        for (int d = min_interval; d <= max_interval; d++) {
            intervals.push_back(d);
        }
        
        int num_intervals = intervals.size();
        
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
        std::vector<std::vector<float>>& smoothed_responses = raw_responses;
        
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
            
            dominant_intervals[i] = intervals[max_k];
            confidence_scores[i] = max_val;
        }
        
        return std::make_pair(dominant_intervals, confidence_scores);
    }

    // Get line candidates from a frequency slice
    static std::shared_ptr<std::vector<float>> getLineCandidates(const ArrayView<float>& first_slice_db) {
        // Initialize return value with -20 for all elements
        auto retval = std::make_shared<std::vector<float>>(first_slice_db.size(), -20.0f);
        
        // Normalize the magnitudes
        auto normalized_slice = normalizeMagnitudes(first_slice_db);
        
        // Find dominant harmonic intervals
        auto result = findDominantHarmonicIntervals(normalized_slice, 250, 8, 35);
        std::vector<int>& freq = result.first;
        std::vector<float>& score = result.second;
        
        // Scan through the data in sections
        for (size_t p = 0; p < freq.size() - 100; p += 50) {
            size_t vl1 = p;
            size_t vl2 = p + 100;
            
            // Extract view of data for this section using ArrayView
            ArrayView<float> subdata(&normalized_slice[vl1], vl2 - vl1);
            
            // Extract view of frequency for this section
            std::vector<int> freqSection(freq.begin() + vl1, freq.begin() + vl2);
            
            // Find dominant frequency (median) directly using int ArrayView
            ArrayView<int> freqSectionView(freqSection.data(), freqSection.size());
            int domfreq_int = median(freqSectionView);

            // Initialize offset score
            std::vector<float> offset_score(domfreq_int, 0.0f);
            
            // Accumulate signals at each phase offset
            for (size_t x = 0; x < subdata.size(); x++) {
                int idx = 1 + (x - 1 + offset_score.size()) % offset_score.size(); // ensure positive modulo
                offset_score[idx] += subdata[x];
            }
            
            // Find phase with maximum score
            auto max_it = std::max_element(offset_score.begin(), offset_score.end());
            int phase = std::distance(offset_score.begin(), max_it);

            // Collect in-phase samples
            std::vector<float> inphase;
            std::vector<float> inphaseix;
            
            for (int ix = phase; ix < subdata.size(); ix += domfreq_int) {
                if (ix <= subdata.size()) {
                    inphase.push_back(subdata[ix]); // -1 for 0-based indexing
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
                        int ix = std::round(inphaseix[x]);
                        if (ix >= 0 && ix < static_cast<int>(first_slice_db.size())) {
                            (*retval)[ix] = -normalized_slice[ix];
                        }
                    }
                }
            }
        }
        
        return retval;
    }


    SignalDetector::SignalDetector() {
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
                    inVec[j].re = buffer[j].re * fftWindowBuf[j];
                    inVec[j].im = buffer[j].im * fftWindowBuf[j];
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
                    (*mag)[j] = 20.0f * log10f((*mag)[j] + 1e-10f);
                }

                addSingleFFTRow(*mag);
            }
            // Pass through the original data unchanged
            base_type::out.writeBuf[i] = base_type::_in->readBuf[i];
        }

        base_type::_in->flush();
        if (!base_type::out.swap(count)) { return -1; }
        return count;
    }

    void SignalDetector::aggregateAndDetect() {

    }

    void SignalDetector::addSingleFFTRow(const ArrayView<float> &rowView) {
        auto candidates = getLineCandidates(rowView);
        suppressedCarrierCandidates.push_back(candidates);
        while (suppressedCarrierCandidates.size() > N_FFT_ROWS) {
            suppressedCarrierCandidates.erase(suppressedCarrierCandidates.begin());
        }
        if (suppressedCarrierCandidates.size() > MIN_DETECT_FFT_ROWS) {
            aggregateAndDetect();
        }
    }
}
