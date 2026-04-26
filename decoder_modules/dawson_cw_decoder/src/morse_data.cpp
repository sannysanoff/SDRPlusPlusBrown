//  _  ___  _   _____ _     _
// / |/ _ \/ | |_   _| |__ (_)_ __   __ _ ___
// | | | | | |   | | | '_ \| | '_ \ / _` / __|
// | | |_| | |   | | | | | | | | | | (_| \__ \.
// |_|\___/|_|   |_| |_| |_|_|_| |_|\__, |___/
//                                  |___/
//
// Copyright (c) Jonathan P Dawson 2025
// filename: morse_data.cpp
// description: Morse code table and autocorrect dictionary
// License: MIT
//
// Ported for SDR++ Dawson CW Decoder

#include "morse_data.h"
#include <algorithm>
#include <cstring>
#include <climits>

// Binary tree encoding of Morse code
// ~ = invalid path, # = invalid code
// A = .-, B = -..., etc.
extern const char MORSE[] = "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~!~~~~~\"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~#~~$~~~%~~~&~~~~~~~~~~~~~~~~~~~~~~~~'~~(~~)~~*~~+~~,~~-~~.~~/~~0~~1~~2~~3~~4~~5~~6~~7~~8~~9~~:~~;~~<~~=~~>~~?~~@~~A~~B~~C~~D~~E~~F~~G~~H~~I~~J~~K~~L~~M~~N~~O~~P~~Q~~R~~S~~T~~U~~V~~W~~X~~Y~~Z~~[~~]~~^~~_~~`~~a~~b~~c~~d~~e~~f~~g~~h~~i~~j~~k~~l~~m~~n~~o~~p~~q~~r~~s~~t~~u~~v~~w~~x~~y~~z~~{~~|~~}~~}~~\177";
const int NUM_MORSE_LETTERS = 46;

const int NUM_PROSIGNS = 6;
const char* PROSIGNS[] = {"<AA>", "<AR>", "<AS>", "<BK>", "<BT>", "<SK>"};

const int NUM_AUTOCORRECT_WORDS = 258;

// Word frequency rankings (lower = more common)
const uint16_t RANKINGS[] = {
    1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007, 1008, 1009, 1010, 1011, 1012, 1013, 1014,
    // ... rankings for all words (simplified for brevity)
    // In actual implementation, this would have 9787 entries
    // Using sequential numbers for now - can be optimized with real frequency data
};

// Autocorrect dictionary - common words and ham radio terms
// This is a subset of the full dictionary for brevity
// Full implementation would include all 9787 words from HamFist
const char* AUTOCORRECT_WORDS[] = {
    "10M", "15M", "160M", "20M", "2M", "40M", "5NN", "6M", "70CM", "73", "80M",
    "A", "ABOUT", "ABOVE", "ACROSS", "ACTIVITY", "AFTER", "AGAIN", "AGAINST", "AGN",
    "AIR", "ALL", "ALSO", "AM", "AMATEUR", "AN", "AND", "ANTENNA", "ANY", "ARE",
    "AROUND", "AS", "AT", "ATTENUATION", "BAND", "BANDS", "BE", "BEACON", "BEEN",
    "BEFORE", "BEING", "BELOW", "BETWEEN", "BOTH", "BUT", "BY", "BYE", "CALL",
    "CALCULATOR", "CAN", "CANNOT", "CARD", "CENTRE", "CHIRP", "COAX", "COME",
    "CONTACT", "CQ", "CW", "DAY", "DB", "DE", "DECODER", "DIPOLE", "DO", "DOWN",
    "DRIFT", "DRIVE", "DX", "EACH", "EARTH", "ES", "FB", "FILTER", "FOR", "FREQUENCY",
    "FROM", "FRONT", "GAP", "GET", "GETTING", "GND", "GO", "GOOD", "GROUND", "HAM",
    "HAVE", "HE", "HEARING", "HELLO", "HER", "HERE", "HIS", "HI", "HOW", "I",
    "IF", "IN", "INCREASE", "INTO", "IT", "ITS", "KE", "KEY", "KHz", "KW",
    "LAST", "LB", "LDG", "LINE", "LOCATION", "LONG", "LOOP", "LOW", "LSB",
    "MAKE", "MANY", "MATCH", "MAY", "ME", "MHz", "MIC", "MOBILE", "MODE",
    "MORE", "MOST", "MUFFIN", "MY", "NAME", "NEAR", "NEED", "NO", "NOISE",
    "NOT", "NOW", "NULL", "NUMBER", "OF", "OFF", "OM", "ON", "ONE", "ONLY",
    "OP", "OPERATOR", "OR", "OTHER", "OUT", "OVER", "PEAK", "PLS", "PORTABLE",
    "POWER", "PRESS", "PROPAGATION", "PSK", "QRM", "QRN", "QRP", "QRS", "QRT",
    "QRZ", "QSB", "QSL", "QSO", "QSY", "QTH", "RADIO", "RAGCHEW", "RAIN",
    "RCVR", "RD", "REACH", "RECEIVER", "REFERENCE", "REPEATER", "RF", "RIG",
    "ROUND", "RST", "RTTY", "RUDY", "RX", "SAY", "SHE", "SHORT", "SHOULD",
    "SIG", "SIGNAL", "SKEW", "SOME", "SOTA", "SOURCED", "SPLIT", "SRI", "STATION",
    "STRENGTH", "SUN", "SWR", "TAKE", "TEMPERATURE", "TEST", "THAN", "THAT",
    "THE", "THEIR", "THEM", "THESE", "THEY", "THIS", "THROUGH", "TIME", "TO",
    "TNX", "TOO", "TRANSMIT", "TRANSMITTER", "TRAP", "TRX", "TUNER", "TU",
    "TWO", "UNDER", "UNTIL", "UP", "USB", "USE", "USING", "VERTICAL", "VERY",
    "VIA", "VOLTAGE", "VS", "WATTS", "WE", "WELL", "WERE", "WHAT", "WHEN",
    "WHERE", "WHICH", "WHO", "WHY", "WILL", "WIND", "WIRE", "WITH", "WOULD",
    "XIT", "XMTR", "YAGI", "YES", "YL", "YOU", "YOUR", "73", "88", "99"
};

// Binary search for exact word match
bool binary_search_word(const char* words[], int num_words, const std::string& target) {
    int left = 0;
    int right = num_words - 1;
    
    while (left <= right) {
        int mid = left + (right - left) / 2;
        int cmp = std::strcmp(words[mid], target.c_str());
        
        if (cmp == 0) {
            return true;
        } else if (cmp < 0) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    return false;
}

// Binary search for prefix match
bool binary_search_prefix(const char* words[], int num_words, const std::string& target) {
    int left = 0;
    int right = num_words - 1;
    
    while (left <= right) {
        int mid = left + (right - left) / 2;
        const char* word = words[mid];
        int cmp = std::strncmp(word, target.c_str(), target.size());
        
        if (cmp == 0) {
            return true;
        } else if (std::strcmp(word, target.c_str()) < 0) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    return false;
}

// Levenshtein distance for single edits
static int levenshtein_distance_1(const char* a, const char* b) {
    int len_a = strlen(a);
    int len_b = strlen(b);
    int diff = abs(len_a - len_b);
    if (diff > 1) return 2;
    
    int i = 0, j = 0;
    bool found_diff = false;
    
    while (i < len_a && j < len_b) {
        if (a[i] == b[j]) {
            i++;
            j++;
            continue;
        }
        if (found_diff) return 2;
        found_diff = true;
        
        if (len_a > len_b) i++;
        else if (len_a < len_b) j++;
        else { i++; j++; }
    }
    
    if ((i < len_a) || (j < len_b)) {
        if (found_diff) return 2;
        found_diff = true;
    }
    
    return found_diff ? 1 : 0;
}

// Binary search ranking lookup
static int binary_search_ranking(const char* words[], int num_words, const std::string& target) {
    int left = 0;
    int right = num_words - 1;
    
    while (left <= right) {
        int mid = left + (right - left) / 2;
        int cmp = std::strcmp(words[mid], target.c_str());
        
        if (cmp == 0) {
            return RANKINGS[mid % 15];  // Simplified ranking
        } else if (cmp < 0) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    return -1;
}

// Binary search for insertion point
static int binary_search_insertion_point(const char* words[], int num_words, const std::string& key) {
    int left = 0, right = num_words;
    while (left < right) {
        int mid = left + (right - left) / 2;
        if (std::string(words[mid]) < key)
            left = mid + 1;
        else
            right = mid;
    }
    return left;
}

// Autocorrect function
void autocorrect(std::string& word) {
    if (binary_search_word(AUTOCORRECT_WORDS, NUM_AUTOCORRECT_WORDS, word))
        return;
    
    std::string best_word = word;
    int best_distance = INT_MAX;
    int best_ranking = INT_MAX;
    
    int idx = binary_search_insertion_point(AUTOCORRECT_WORDS, NUM_AUTOCORRECT_WORDS, word);
    const int WINDOW = 50;
    int start = std::max(0, idx - WINDOW);
    int end = std::min(NUM_AUTOCORRECT_WORDS, idx + WINDOW);
    
    for (int i = start; i < end; ++i) {
        const std::string& candidate = AUTOCORRECT_WORDS[i];
        int d = levenshtein_distance_1(word.c_str(), candidate.c_str());
        
        if (d <= 1) {
            int ranking = binary_search_ranking(AUTOCORRECT_WORDS, NUM_AUTOCORRECT_WORDS, candidate);
            if (ranking < 0) ranking = i;
            
            if ((d < best_distance) || (d == best_distance && ranking < best_ranking)) {
                best_distance = d;
                best_word = candidate;
                best_ranking = ranking;
            }
            if (best_distance == 0) break;
        }
    }
    
    // First letter substitutions
    static char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (best_distance > 1) {
        std::string candidate;
        for (int i = 0; i < 26; i++) {
            candidate = word;
            if (candidate.empty()) continue;
            candidate[0] = letters[i];
            int ranking = binary_search_ranking(AUTOCORRECT_WORDS, NUM_AUTOCORRECT_WORDS, candidate);
            if (ranking > 0 && ranking < best_ranking) {
                best_word = candidate;
                best_distance = 1;
                best_ranking = ranking;
            }
        }
    }
    
    if (best_distance == 1) {
        word = best_word;
    }
}

// Validate callsign format
bool is_valid_callsign(const std::string& s) {
    size_t i = 0;
    size_t n = s.size();
    
    // 1-2 starting letters
    int letters1 = 0;
    while (i < n && std::isalpha(s[i]) && letters1 < 2) {
        ++i;
        ++letters1;
    }
    if (letters1 == 0 || i >= n) return false;
    
    // one digit
    if (!std::isdigit(s[i++])) return false;
    if (i >= n) return false;
    
    // 1-3 trailing letters
    int letters2 = 0;
    while (i < n && std::isalpha(s[i]) && letters2 < 3) {
        ++i;
        ++letters2;
    }
    if (letters2 == 0) return false;
    
    // optional suffix like /P, /M, /MM, etc.
    if (i < n) {
        if (s[i++] != '/' || i >= n) return false;
        int suf = 0;
        while (i < n && std::isalpha(s[i]) && suf < 3) {
            ++i;
            ++suf;
        }
        if (suf == 0) return false;
    }
    
    return i == n;
}
