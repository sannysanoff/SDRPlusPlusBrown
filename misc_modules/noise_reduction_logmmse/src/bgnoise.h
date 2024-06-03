//
// Created by san on 10/07/22.
//
#pragma once

#include <vector>

class BackgroundNoiseCaltulator {

    double lastNoise = ERASED_SAMPLE;
    static constexpr auto NBUCKETS = 1000;
    static constexpr auto SKIP_FRAMES = 10;
    std::vector<int> buckets;
    std::vector<float> logFrame;
    int frameCount = 0;

public:

    static constexpr auto ERASED_SAMPLE = 1e9f;

    void reset() {
        lastNoise = ERASED_SAMPLE;
        frameCount = 0;
    }

    float addFrame(const std::vector<float> &fftFrame) {
        if (frameCount > 0 && frameCount % SKIP_FRAMES != 0) {
            frameCount++;
            return lastNoise;
        }
        frameCount++;
        float minn = ERASED_SAMPLE;
        float maxx = -ERASED_SAMPLE;
        logFrame.clear();
        logFrame.reserve(fftFrame.size());
        for(float q : fftFrame) {
            if(q != ERASED_SAMPLE) {
                q = log10(q);
                minn = std::min<float>(minn, q);
                maxx = std::max<float>(maxx, q);
                logFrame.push_back(q);
            }
        }
        auto width = maxx - minn;
        buckets.resize(NBUCKETS);
        memset(buckets.data(), 0, sizeof(int) * NBUCKETS);
        for(auto f : logFrame) {
            int bucket = (int) (NBUCKETS * ((f - minn) / width));
            buckets[bucket]++;
        }
        auto ix = std::max_element(buckets.begin(), buckets.end()) - buckets.begin();
        double maxf = pow(10, ((((double)ix)/NBUCKETS) * width + minn));
        if (lastNoise == ERASED_SAMPLE) {
            lastNoise = maxf;
        } else {
            lastNoise = 0.9 * lastNoise + 0.1 * maxf;
        }
        return lastNoise;

    }

};