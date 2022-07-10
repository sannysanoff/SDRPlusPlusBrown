//
// Created by san on 10/07/22.
//
#include <utils/dsputils.h>

void BackgroundNoiseCaltulator::reset() {
//    m_noise.clear();
}

void BackgroundNoiseCaltulator::addFrame(const std::vector<float>& fftFrame) {
    float minn = ERASED_SAMPLE;
    float maxx = -ERASED_SAMPLE;
    for(auto f : fftFrame) {
        if(f != ERASED_SAMPLE) {
            minn = std::min(minn, f);
            maxx = std::max(maxx, f);
        }
    }
    auto width = maxx - minn;
    const NBUCKETS = 100;
    std::vector<float> buckets(NBUCKETS, 0);
    for(auto f : fftFrame) {
        int bucket = (int)((f - minn)/ width);
        if (bucket < 0) {
            bucket = 0;
        } else if (bucket >= NBUCKETS) {
            bucket = NBUCKETS - 1;
        }
    }
}