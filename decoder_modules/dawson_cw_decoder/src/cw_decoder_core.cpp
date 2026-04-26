//  _  ___  _   _____ _     _
// / |/ _ \/ | |_   _| |__ (_)_ __   __ _ ___
// | | | | | |   | | | '_ \| | '_ \ / _` / __|
// | | |_| | |   | | | | | | | | | | (_| \__ \.
// |_|\___/|_|   |_| |_| |_|_|_| |_|\__, |___/
//                                  |___/
//
// Copyright (c) Jonathan P Dawson 2025
// filename: cw_decoder_core.cpp
// description: CW Decoder using beam decoder with autocorrect
// License: MIT
//
// Ported for SDR++ Dawson CW Decoder

#include "cw_decoder_core.h"
#include "morse_data.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstring>
#include <numeric>

// #define LOGGING

#ifdef LOGGING
#include <cstdio>
#define DEBUG_PRINTF(...) std::printf(__VA_ARGS__)
#else
#define DEBUG_PRINTF(...)
#endif

// Gaussian log-likelihood
static float log_gaussian(float x, float mu, float sigma) {
    return -0.5f * pow((x - mu) / sigma, 2.0f);
}

// Check if pattern is start of valid Morse code
static bool is_start_of_code(std::string& pattern) {
    static const char* prefixes[] = {
        ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....",
        "..", ".---", "-.-", ".-..", "--", "-.", "---", ".--.",
        "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-",
        "-.--", "--..", ".----", "..---", "...--", "....-", ".....",
        "-....", "--...", "---..", "----.", "-----",
        ".-.-.-", "--..--", "..--..", ".----.", "-.-.--", "-..-.",
        "-.--.", "-.--.-", ".-...", "---...", "-.-.-.", "-...-",
        ".-.-.", "-....-", "..--.-", ".-..-.", "...-..-", ".--.-.",
        nullptr
    };
    for(int i=0; prefixes[i]; i++){
        std::string s(prefixes[i]);
        if(s.find(pattern) == 0) return true;
    }
    return false;
}

static bool is_code(std::string& pattern) {
    static const char* codes[] = {
        ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....",
        "..", ".---", "-.-", ".-..", "--", "-.", "---", ".--.",
        "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-",
        "-.--", "--..", ".----", "..---", "...--", "....-", ".....",
        "-....", "--...", "---..", "----.", "-----",
        ".-.-.-", "--..--", "..--..", ".----.", "-.-.--", "-..-.",
        "-.--.", "-.--.-", ".-...", "---...", "-.-.-.", "-...-",
        ".-.-.", "-....-", "..--.-", ".-..-.", "...-..-", ".--.-.",
        nullptr
    };
    for(int i=0; codes[i]; i++){
        if(pattern == codes[i]) return true;
    }
    return false;
}

static char get_letter_from_code(std::string& pattern) {
    struct { const char* code; char letter; } table[] = {
        {".-", 'A'}, {"-...", 'B'}, {"-.-.", 'C'}, {"-..", 'D'},
        {".", 'E'}, {"..-.", 'F'}, {"--.", 'G'}, {"....", 'H'},
        {"..", 'I'}, {".---", 'J'}, {"-.-", 'K'}, {".-..", 'L'},
        {"--", 'M'}, {"-.", 'N'}, {"---", 'O'}, {".--.", 'P'},
        {"--.-", 'Q'}, {".-.", 'R'}, {"...", 'S'}, {"-", 'T'},
        {"..-", 'U'}, {"...-", 'V'}, {".--", 'W'}, {"-..-", 'X'},
        {"-.--", 'Y'}, {"--..", 'Z'},
        {".----", '1'}, {"..---", '2'}, {"...--", '3'}, {"....-", '4'},
        {".....", '5'}, {"-....", '6'}, {"--...", '7'}, {"---..", '8'},
        {"----.", '9'}, {"-----", '0'},
        {".-.-.-", '.'}, {"--..--", ','}, {"..--..", '?'}, {".----.", '\''},
        {"-.-.--", '!'}, {"-..-.", '/'}, {"-.--.", '('}, {"-.--.-", ')'},
        {".-...", '&'}, {"---...", ':'}, {"-.-.-.", ';'}, {"-...-", '='},
        {".-.-.", '+'}, {"-....-", '-'}, {"..--.-", '_'}, {".-..-.", '"'},
        {"...-..-", '$'}, {".--.-.", '@'},
        {nullptr, 0}
    };
    for(int i=0; table[i].code; i++){
        if(pattern == table[i].code) return table[i].letter;
    }
    return '~';
}

// Get log probability of word prefix
static float word_prefix_log_prob(std::string& word) {
    if (word.size() < 2) return 0;
    if (binary_search_prefix(AUTOCORRECT_WORDS, NUM_AUTOCORRECT_WORDS, word))
        return 1.0f;
    return 0;
}

// Get log probability of complete word
static float language_log_prob(std::string& word) {
    if (binary_search_word(AUTOCORRECT_WORDS, NUM_AUTOCORRECT_WORDS, word))
        return 4.0f;
    if (is_valid_callsign(word))
        return 2.0f;
    return 0;
}

// Replace all occurrences
static void replace_all(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}

// Replace prosigns with display format
static void replace_prosigns(std::string& str) {
    for (int i = 0; i < NUM_PROSIGNS; ++i) {
        char char_to_replace[] = {static_cast<char>(0x80 + i), 0};
        std::string from = std::string(char_to_replace);
        std::string to = std::string(PROSIGNS[i]);
        replace_all(str, from, to);
    }
}

// Autocorrect text
static void autocorrect_text(std::string& text) {
    std::string word = "";
    std::string new_text = "";
    
    for (size_t i = 0; i < text.size(); ++i) {
        if (std::isalpha(text[i])) {
            word += text[i];
        } else {
            if (word.size() > 2)
                autocorrect(word);
            new_text += word + text[i];
            word = "";
        }
    }
    if (word.size() > 2)
        autocorrect(word);
    new_text += word;
    
    text = new_text;
}

// Constructor
c_cw_decoder::c_cw_decoder(int channel_number)
    : classifier(channel_number), m_channel_number(channel_number) {
    beam[0] = {"", "", "", 0.0f};
    items_in_beam = 1;
}

// Get partial decode (in progress)
std::string c_cw_decoder::get_text_partial() {
    std::string letter = std::string(1, get_letter_from_code(beam[0].pattern));
    std::string text = beam[0].word + letter;
    replace_prosigns(text);
    return text;
}

// Get committed text
std::string c_cw_decoder::get_text() {
    std::string text = beam[0].text;
    
    // Prune candidates that don't share the same prefix
    int filtered_candidate_index = 1;
    for (int candidate_index = 1; candidate_index < items_in_beam; ++candidate_index) {
        if (beam[candidate_index].text == beam[0].text) {
            beam[filtered_candidate_index] = beam[candidate_index];
            beam[filtered_candidate_index].text = "";
            filtered_candidate_index++;
        }
    }
    beam[0].text = "";
    items_in_beam = filtered_candidate_index;
    
    autocorrect_text(text);
    replace_prosigns(text);
    return text;
}

// Pre-filter observations to remove obvious errors
static void pre_filter_observations(s_observation signal[], int& num_observations) {
    // Hard limits
    float dot_min_ms = 30.0f;
    float dash_max_ms = 720.0f;
    float min_hard = std::max(dot_min_ms * 0.5f, 8.0f);
    float max_hard = dash_max_ms * 2.0f;
    
    // Remove short gaps
    int filtered_index = 0;
    for (int index = 0; index < num_observations; ++index) {
        signal[filtered_index] = signal[index];
        while ((index + 2 < num_observations) && (!signal[index + 1].mark) &&
               (signal[index + 1].duration < min_hard)) {
            signal[filtered_index].duration += signal[index + 1].duration + signal[index + 2].duration;
            index += 2;
        }
        filtered_index++;
    }
    num_observations = filtered_index;
    
    // Remove short marks
    filtered_index = 0;
    for (int index = 0; index < num_observations; ++index) {
        signal[filtered_index] = signal[index];
        while ((index + 2 < num_observations) && (signal[index + 1].mark) &&
               (signal[index + 1].duration < min_hard)) {
            signal[filtered_index].duration += signal[index + 1].duration + signal[index + 2].duration;
            index += 2;
        }
        filtered_index++;
    }
    num_observations = filtered_index;
    
    // Remove long marks
    filtered_index = 0;
    for (int index = 0; index < num_observations; ++index) {
        signal[filtered_index] = signal[index];
        while ((index + 2 < num_observations) && (signal[index + 1].mark) &&
               (signal[index + 1].duration > max_hard)) {
            signal[filtered_index].duration += signal[index + 1].duration + signal[index + 2].duration;
            index += 2;
        }
        filtered_index++;
    }
    num_observations = filtered_index;
    
    // Remove marks in the middle of two long spaces
    filtered_index = 0;
    for (int index = 0; index < num_observations; ++index) {
        signal[filtered_index] = signal[index];
        while ((index + 2 < num_observations) && (signal[index + 1].mark) &&
               (signal[index].duration > max_hard) && (signal[index + 2].duration > max_hard)) {
            signal[filtered_index].duration += signal[index + 1].duration + signal[index + 2].duration;
            index += 2;
        }
        filtered_index++;
    }
    num_observations = filtered_index;
}

// Main decode function
void c_cw_decoder::decode(s_observation signal[], int num_observations) {
    DEBUG_PRINTF("decoding channel %u\n", m_channel_number);
    
    // Pre-filter
    pre_filter_observations(signal, num_observations);
    
    // Separate on and off durations
    float on_durations[100];  // Max observations
    float off_durations[100];
    int on_count = 0;
    int off_count = 0;
    
    for (int idx = 0; idx < num_observations; ++idx) {
        if (signal[idx].mark)
            on_durations[on_count++] = signal[idx].duration;
        else
            off_durations[off_count++] = signal[idx].duration;
    }
    
    // Update classifier models
    classifier.update_on_model(on_durations, on_count);
    if (!classifier.good_estimates) return;
    
    classifier.update_off_model(off_durations, off_count);
    
    // Process each observation
    for (int i = 0; i < num_observations; ++i) {
        float duration = signal[i].duration;
        
        float logp_dot, logp_dash, logp_dotdot, logp_dotdash, logp_dashdash;
        classifier.prescale_histograms();
        classifier.classify_on(duration, logp_dot, logp_dash, logp_dotdot, logp_dotdash, logp_dashdash);
        
        float logp[3];
        classifier.classify_off(duration, logp);
        classifier.postscale_histograms();
        float logp_gap1 = logp[0];
        float logp_gap3 = logp[1];
        float logp_gap7 = logp[2];
        
        s_candidate candidates[BEAM_WIDTH * 6];
        int num_candidates = 0;
        
        for (int j = 0; j < items_in_beam; j++) {
            std::string& text = beam[j].text;
            std::string& word = beam[j].word;
            std::string& pattern = beam[j].pattern;
            float& logp_beam = beam[j].logp;
            
            if (signal[i].mark) {
                char letter = get_letter_from_code(pattern);
                bool pattern_is_code = letter != '#' && letter != '~';
                
                // Case 1: dot
                std::string dot_pattern = pattern + '.';
                if (is_start_of_code(dot_pattern)) {
                    candidates[num_candidates++] = {text, word, pattern + '.', logp_beam + logp_dot};
                } else if (pattern_is_code) {
                    candidates[num_candidates++] = {text, word + letter, ".", logp_beam + logp_dot};
                }
                
                // Case 2: dash
                std::string dash_pattern = pattern + '-';
                if (is_start_of_code(dash_pattern)) {
                    candidates[num_candidates++] = {text, word, pattern + '-', logp_beam + logp_dash};
                } else if (pattern_is_code) {
                    candidates[num_candidates++] = {text, word + letter, "-", logp_beam + logp_dash};
                }
                
                // Case 3: dotdash
                std::string dotdash_pattern = pattern + ".-";
                if (is_start_of_code(dotdash_pattern)) {
                    candidates[num_candidates++] = {text, word, pattern + ".-", logp_beam + logp_dotdash};
                } else if (pattern_is_code) {
                    candidates[num_candidates++] = {text, word + letter, ".-", logp_beam + logp_dotdash};
                }
                
                // Case 4: dashdot
                std::string dashdot_pattern = pattern + "-.";
                if (is_start_of_code(dashdot_pattern)) {
                    candidates[num_candidates++] = {text, word, pattern + "-.", logp_beam + logp_dotdash};
                } else if (pattern_is_code) {
                    candidates[num_candidates++] = {text, word + letter, "-.", logp_beam + logp_dotdash};
                }
                
                // Case 5: dashdash
                std::string dashdash_pattern = pattern + "--";
                if (is_start_of_code(dashdash_pattern)) {
                    candidates[num_candidates++] = {text, word, pattern + "--", logp_beam + logp_dashdash};
                } else if (pattern_is_code) {
                    candidates[num_candidates++] = {text, word + letter, "--", logp_beam + logp_dashdash};
                }
                
                // Case 6: dotdot
                std::string dotdot_pattern = pattern + "..";
                if (is_start_of_code(dotdot_pattern)) {
                    candidates[num_candidates++] = {text, word, pattern + "..", logp_beam + logp_dotdot - 2};
                } else if (pattern_is_code) {
                    candidates[num_candidates++] = {text, word + letter, "..", logp_beam + logp_dotdot - 2};
                }
                
            } else {
                // Gap cases
                candidates[num_candidates++] = {text, word, pattern, logp_beam + logp_gap1};
                
                char letter = get_letter_from_code(pattern);
                bool pattern_is_code = letter != '#' && letter != '~';
                
                if (pattern_is_code) {
                    std::string last_word = word + letter;
                    float language_bonus = language_log_prob(last_word);
                    float prefix_bonus = word_prefix_log_prob(last_word);
                    
                    // Letter gap
                    candidates[num_candidates++] = {text, word + letter, "", 
                                                     logp_beam + logp_gap3 + prefix_bonus};
                    
                    // Word gap
                    candidates[num_candidates++] = {text + last_word + ' ', "", "",
                                                     logp_beam + logp_gap7 + language_bonus};
                }
            }
        }
        
        // Select best candidates
        items_in_beam = std::min(BEAM_WIDTH, num_candidates);
        std::vector<int> best_indices(num_candidates);
        std::iota(best_indices.begin(), best_indices.end(), 0);
        std::partial_sort(
            best_indices.begin(), best_indices.begin() + items_in_beam, best_indices.end(),
            [&](const int a, const int b) { return candidates[a].logp > candidates[b].logp; });
        
        for (int idx = 0; idx < items_in_beam; ++idx) {
            beam[idx] = candidates[best_indices[idx]];
        }
    }
}
