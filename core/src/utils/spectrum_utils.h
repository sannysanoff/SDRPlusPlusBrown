#pragma once

#include "utils/arrays.h"
#include <vector>
#include <algorithm>
#include <cmath>

namespace dsp::arrays {

    /**
     * Estimate noise floor from spectrum using variance-based method
     * Similar to experimental_fft_compressor.h but simplified for CW detection
     * 
     * @param magnitudes FloatArray of spectrum magnitudes
     * @param noiseNPoints Window size for moving variance calculation (default 16)
     * @param percentile Percentile to use for noise threshold (0.0-1.0, default 0.15 = 15th percentile)
     * @return FloatArray with per-bin noise floor estimates
     */
    inline FloatArray estimateNoiseFloor(FloatArray magnitudes, int noiseNPoints = 16, float percentile = 0.15f) {
        int fftSize = magnitudes->size();
        if (fftSize == 0) return npzeros(1);
        
        // Calculate moving variance
        auto mvar = movingVariance(magnitudes, noiseNPoints);
        
        // Use percentile of variance as threshold
        // Signals have high variance (keying), noise has low variance
        auto sortedVar = clone(mvar);
        std::sort(sortedVar->begin(), sortedVar->end());
        int idx = static_cast<int>(percentile * sortedVar->size());
        float varianceThreshold = sortedVar->at(std::max(0, std::min(idx, (int)sortedVar->size() - 1)));
        
        // Calculate centered moving average as base noise estimate
        auto noiseFloor = centeredSma(magnitudes, noiseNPoints * 4);  // Wider window for noise
        
        // Mask out signal bins (where variance is high) by interpolating from neighbors
        std::vector<bool> isSignal(fftSize, false);
        for (int i = 0; i < fftSize; i++) {
            if (mvar->at(i) > varianceThreshold * 2.0f) {
                isSignal[i] = true;
                noiseFloor->at(i) = 0;  // Mark as signal
            }
        }
        
        // Linearly interpolate holes left by signals
        dsp::math::linearInterpolateHoles(noiseFloor->data(), fftSize);
        
        // Final smoothing
        noiseFloor = centeredSma(noiseFloor, noiseNPoints * 2);
        
        // Ensure minimum noise floor
        for (int i = 0; i < fftSize; i++) {
            noiseFloor->at(i) = std::max(noiseFloor->at(i), 0.001f);
        }
        
        return noiseFloor;
    }
    
    /**
     * Simple percentile-based noise floor (faster, less accurate)
     * Good for CW contest detection where many signals are present
     * 
     * @param magnitudes FloatArray of spectrum magnitudes
     * @param percentile Percentile for noise estimate (default 0.10 = 10th percentile)
     * @return Global noise floor value (single float)
     */
    inline float estimateNoiseFloorGlobal(const FloatArray& magnitudes, float percentile = 0.10f) {
        if (magnitudes->empty()) return 0.001f;
        
        // Copy and sort to find percentile
        std::vector<float> sorted = *magnitudes;
        std::sort(sorted.begin(), sorted.end());
        
        int idx = static_cast<int>(percentile * sorted.size());
        idx = std::max(0, std::min(idx, (int)sorted.size() - 1));
        
        return std::max(sorted[idx], 0.001f);
    }
    
    /**
     * Detect peaks in spectrum above noise floor
     * 
     * @param magnitudes Spectrum magnitudes
     * @param noiseFloor Per-bin noise floor estimates
     * @param thresholdMult Multiplier above noise (e.g., 5.0 = 5x noise floor)
     * @param minSnrDb Minimum SNR in dB
     * @param minSpacing Minimum spacing between peaks (in bins)
     * @return Vector of (bin_index, magnitude, snr_db) tuples for detected peaks
     */
    inline std::vector<std::tuple<int, float, float>> detectSpectrumPeaks(
        const FloatArray& magnitudes,
        const FloatArray& noiseFloor,
        float thresholdMult = 5.0f,
        float minSnrDb = 6.0f,
        int minSpacing = 3) {
        
        std::vector<std::tuple<int, float, float>> peaks;
        int fftSize = magnitudes->size();
        
        if (fftSize == 0 || noiseFloor->size() != fftSize) return peaks;
        
        // Find local maxima above threshold
        for (int i = 1; i < fftSize - 1; i++) {
            float mag = magnitudes->at(i);
            float noise = noiseFloor->at(i);
            float threshold = noise * thresholdMult;
            
            // Check if local maximum and above threshold
            if (mag > magnitudes->at(i-1) && 
                mag > magnitudes->at(i+1) &&
                mag > threshold &&
                noise > 0) {
                
                float snrLinear = mag / noise;
                float snrDb = 20.0f * std::log10(snrLinear);
                
                if (snrDb >= minSnrDb) {
                    peaks.push_back(std::make_tuple(i, mag, snrDb));
                }
            }
        }
        
        // Merge close peaks (keep strongest)
        if (minSpacing > 1 && !peaks.empty()) {
            std::sort(peaks.begin(), peaks.end(), 
                [](const auto& a, const auto& b) {
                    return std::get<1>(a) > std::get<1>(b);  // Sort by magnitude descending
                });
            
            std::vector<std::tuple<int, float, float>> merged;
            std::vector<bool> used(fftSize, false);
            
            for (const auto& peak : peaks) {
                int bin = std::get<0>(peak);
                bool tooClose = false;
                
                for (int i = std::max(0, bin - minSpacing); i <= std::min(fftSize - 1, bin + minSpacing); i++) {
                    if (used[i]) {
                        tooClose = true;
                        break;
                    }
                }
                
                if (!tooClose) {
                    merged.push_back(peak);
                    for (int i = std::max(0, bin - minSpacing); i <= std::min(fftSize - 1, bin + minSpacing); i++) {
                        used[i] = true;
                    }
                }
            }
            
            // Sort merged by frequency
            std::sort(merged.begin(), merged.end(),
                [](const auto& a, const auto& b) {
                    return std::get<0>(a) < std::get<0>(b);
                });
            
            return merged;
        }
        
        return peaks;
    }

} // namespace dsp::arrays
