//  _  ___  _   _____ _     _
// / |/ _ \/ | |_   _| |__ (_)_ __   __ _ ___
// | | | | | |   | | | '_ \| | '_ \ / _` / __|
// | | |_| | |   | | | | | | | | | | (_| \__ \.
// |_|\___/|_|   |_| |_| |_|_|_| |_|\__, |___/
//                                  |___/
//
// Copyright (c) Jonathan P Dawson 2025
// filename: cw_decoder_core.h
// description: CW Decoder using beam decoder with autocorrect
// License: MIT
//
// Ported for SDR++ Dawson CW Decoder

#ifndef __CW_DECODER_CORE_H__
#define __CW_DECODER_CORE_H__

#include "cw_classifier.h"
#include <string>
#include <vector>

// Observation structure for mark/space timing
struct s_observation {
    bool mark;      // true = tone present, false = silence
    float duration; // milliseconds
};

// Beam search candidate
struct s_candidate {
    std::string text;
    std::string word;
    std::string pattern;
    float logp;
};

static const int BEAM_WIDTH = 3;

class c_cw_decoder {
private:
    c_morse_timing_classifier classifier;
    s_candidate beam[BEAM_WIDTH];
    int items_in_beam;
    int m_channel_number;
    
public:
    c_cw_decoder(int channel_number);
    
    void decode(s_observation signal[], int num_observations);
    std::string get_text();
    std::string get_text_partial();
    void reset() { classifier.reset(); }
    float get_WPM() { return classifier.get_WPM(); }
    bool has_good_estimates() { return classifier.good_estimates; }
};

#endif
