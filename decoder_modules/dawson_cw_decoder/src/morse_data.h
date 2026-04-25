//  _  ___  _   _____ _     _
// / |/ _ \/ | |_   _| |__ (_)_ __   __ _ ___
// | | | | | |   | | | '_ \| | '_ \ / _` / __|
// | | |_| | |   | | | | | | | | | | (_| \__ \.
// |_|\___/|_|   |_| |_| |_|_|_| |_|\__, |___/
//                                  |___/
//
// Copyright (c) Jonathan P Dawson 2025
// filename: morse_data.h
// description: CW related data including autocorrect dictionary
// License: MIT
//
// Ported for SDR++ Dawson CW Decoder

#ifndef __MORSE_DATA_H__
#define __MORSE_DATA_H__

#include <cstdint>
#include <string>

// Morse code letter structure
struct s_morse {
    char letter;
    const char* code;
};

// Morse code table (binary tree encoding)
// Index calculation: dot=+1, dash=+span/2
extern const char MORSE[];
extern const int NUM_MORSE_LETTERS;

// Autocorrect dictionary
extern const int NUM_AUTOCORRECT_WORDS;
extern const char* AUTOCORRECT_WORDS[];
extern const uint16_t RANKINGS[];

// Prosigns (Morse procedural signals)
extern const int NUM_PROSIGNS;
extern const char* PROSIGNS[];

// Dictionary lookup functions
bool binary_search_word(const char* words[], int num_words, const std::string& target);
bool binary_search_prefix(const char* words[], int num_words, const std::string& target);
void autocorrect(std::string& word);
bool is_valid_callsign(const std::string& s);

#endif
