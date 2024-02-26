#pragma clang diagnostic push
#pragma ide diagnostic ignored "misc-no-recursion"

#include "cwdecoder.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric> // for std::iota
#include <chrono>
#include <fstream>
#include <ctm.h>

#include <dsp/types.h>
#include <dsp/window/blackman.h>
#include <../src/utils/arrays.h>

struct MorseCode {
    const char* sequence; // Morse code sequence
    const char* decoded;  // Decoded character
};

struct Matrix2D : public std::vector<std::vector<float>> {
    Matrix2D() = default;
    Matrix2D(int major, int minor) : std::vector<std::vector<float>>(minor, std::vector<float>(major, 0)) {

    }
};

typedef std::shared_ptr<Matrix2D> Matrix2DPtr;

// todo: 2 separate transmissions, pause between, split and calc stat separately.

// Initialize the vector with Morse code mapping
std::vector<MorseCode> morseTable = {
    { ".-", "A" },
    { "-...", "B" },
    { "-.-.", "C" },
    { "-..", "D" },
    { ".", "E" },
    { "..-.", "F" },
    { "--.", "G" },
    { "....", "H" },
    { "..", "I" },
    { ".---", "J" },
    { "-.-", "K" },
    { ".-..", "L" },
    { "--", "M" },
    { "-.", "N" },
    { "---", "O" },
    { ".--.", "P" },
    { "--.-", "Q" },
    { ".-.", "R" },
    { "...", "S" },
    { "-", "T" },
    { "..-", "U" },
    { "...-", "V" },
    { ".--", "W" },
    { "-..-", "X" },
    { "-.--", "Y" },
    { "--..", "Z" },
    { "-----", "0" },
    { ".----", "1" },
    { "..---", "2" },
    { "...--", "3" },
    { "....-", "4" },
    { ".....", "5" },
    { "-....", "6" },
    { "--...", "7" },
    { "---..", "8" },
    { "----.", "9" },
    // Punctuation
    { ".-.-.-", "." }, // Full stop
    { "--..--", "," }, // Comma
    { "..--..", "?" }, // Question mark
    { "-.-.--", "!" }, // Exclamation mark
    { "-....-", "-" }, // Hyphen
    { "-..-.", "/" },  // Slash
    { ".--.-.", "@" }, // At sign
    { "-.--.", "(" },  // Left parenthesis
    { "-.--.-", ")" }, // Right parenthesis
    { "---...", ";" }, // Semicolon
    { "-.-.-.", ";" }, // Colon
    { "-...-", "=" },  // Equals sign
    { ".-.-.", "+" },  // Plus sign
    // Known prosigns
    { ".-...", "&" },          // Wait (AS)
    { "...-.-", "SK" },        // End of work
    { ".-.-.-", "{OUT}" },     // AR (out)
    { "-.-.-", "KA" },         // Beginning of message
    { "...-.-", "VA" },        // Understood (VE)
    { ".-.-.", "AR" },         // End of message
    { "-.-.--", "SN" },        // Understood (SN)
    { "........", "{corr}" },  // correction
    { ".......", "{corr}" },   // correction
    { ".........", "{corr}" }, // correction
    { "...-.-", "{SK}" },      // silent key (end of trx)
};


Matrix2DPtr read_csv(const char* filename) {
    auto rv = std::make_shared<Matrix2D>();
    FILE* file = fopen(filename, "r"); // Open the file for reading
    if (!file) {
        perror("Failed to open file");
        return rv; // Return empty data if file opening fails
    }

    char line[100000]; // Buffer to hold each line, up to 100,000 bytes
    while (fgets(line, sizeof(line), file)) {
        std::vector<float> row;
        char* token = strtok(line, ","); // Tokenize the line by comma
        while (token) {
            row.emplace_back(strtof(token, nullptr)); // Convert token to float and add to row
            token = strtok(nullptr, ",");             // Get next token
        }
        rv->emplace_back(row); // Add the row to the main data vector
    }

    fclose(file); // Close the file
    return rv;  // Return the populated data structure
}

[[maybe_unused]]
bool write_csv(const char* filename, const std::vector<std::vector<float>>& data) {
    std::ofstream file(filename); // Open the file for writing

    if (!file.is_open()) {
        perror("Failed to open file");
        return false;
    }

    for (const auto& row : data) {
        std::stringstream ss;
        for (size_t i = 0; i < row.size(); ++i) {
            ss << row[i];
            if (i != row.size() - 1) {
                ss << ","; // Add comma as a delimiter
            }
        }
        file << ss.str() << "\n"; // Write the line to the file
    }

    file.close();
    return true;
}

struct DecodedLetter {
    int localOffset;
    const char* letter;
    double score;
};

struct DecodedRun {
    std::vector<DecodedLetter> letters;
    double score = 0;

    [[nodiscard]] std::string toString() const {
        std::string rv;
        rv += std::to_string((int)score);
        rv += ": ";
        for (auto& letter : letters) {
            rv += letter.letter;
        }
        return rv;
    }
};

struct Decoded {
    std::vector<DecodedRun> variations;
};

struct SingleRun {
    float value;
    int len;
    int start;
};

struct TemporalSettings {
    int sigShort;
    int sigLong;
    int spaceShort;
    int spaceLong;

    [[nodiscard]] float qualifyLongSignal(int len) const {
        return 1 - qualifyShortSignal(len);
    }

    [[nodiscard]] float qualifyShortSignal(int len) const {
        if (len <= sigShort) {
            return 1;
        }
        if (len >= sigLong) {
            return 0;
        }
        return float(sigLong - len) / float(sigLong - sigShort);
    }

    [[nodiscard]] float qualifyShortSpace(int len) const {
        if (len < spaceShort) {
            return 1;
        }
        if (len >= spaceLong) {
            return 0;
        }
        return float(spaceLong - len) / float(spaceLong - spaceShort);
    }

    [[nodiscard]] float qualifyLongSpace(int len) const {
        return 1 - qualifyShortSpace(len);
    }
};


enum DecodedSegmentType {
    ROOT = 1,
    DIT = 2,
    DA = 3,
    END_LETTER = 4,
    END_WORD = 5,
    IGNORE = 6, // just filler
};


struct SegmentMeaning {
    DecodedSegmentType segtype = ROOT;
    double score = 0;
    int localOffset = 0;
    double value = 0;

    SegmentMeaning() = default;

    SegmentMeaning(DecodedSegmentType segtype, double score, int localOffset, double value)
        : segtype(segtype), score(score), localOffset(localOffset), value(value) {}
};

template <typename T>
T rootNode();

template <>
SegmentMeaning rootNode<SegmentMeaning>() {
    return SegmentMeaning{ ROOT, 0, -1, 0 };
}

template <typename T>
struct Node {
    int leftNodeIndex;
    int rightNodeIndex;
    int parentIndex;
    int depth;
    T value;

    explicit Node(T value) : value(value), leftNodeIndex(-1), rightNodeIndex(-1), parentIndex(-1), depth(0) {
        this->value = value;
    }
};

template <typename T>
struct BinaryTree {
    std::vector<Node<T>> nodes;

    BinaryTree() {
        nodes.emplace_back(Node(rootNode<T>()));
    }

    void withEachLeaf(const std::function<void(BinaryTree<T>&, int)>& fun) {
        if (!nodes.empty()) {
            visitNode(0, fun); // Start from the root node, which is at index 0
        }
    }

    void visitNode(int nodeIndex, const std::function<void(BinaryTree<T>&, int)>& fun) {
        if (nodeIndex >= nodes.size()) {
            abort();
        }
        Node<T>* node = &nodes[nodeIndex];

        // Check if this is a leaf node
        if (node->leftNodeIndex == -1 && node->rightNodeIndex == -1) {
            fun(*this, nodeIndex); // Apply the function on the leaf node
        }
        else {
            // Recursively visit the left child if it exists
            if (node->leftNodeIndex != -1) {
                visitNode(node->leftNodeIndex, fun);
            }

            node = &nodes[nodeIndex]; // reread value.
            // Recursively visit the right child if it exists
            if (node->rightNodeIndex != -1) {
                visitNode(node->rightNodeIndex, fun);
            }
        }
    }

    void addNode(int where, T value) {
        auto rv = nodes.size();
        Node d = Node(value);
        d.parentIndex = where;
        d.depth = nodes[where].depth + 1;
        nodes.emplace_back(d);
        if (nodes[where].leftNodeIndex == -1) {
            nodes[where].leftNodeIndex = rv;
        }
        else if (nodes[where].rightNodeIndex == -1) {
            nodes[where].rightNodeIndex = rv;
        }
        else {
            abort();
        }
    }
};

void segmentMeaningToWords(const std::vector<SegmentMeaning>& src, std::vector<DecodedRun>& dest) {
    char result[1024];
    const SegmentMeaning* seg[1024];
    double scoreMultipliers[1024]; // letter regularity adjustment (mix of 2 signals with diff levels -> filter out)
    if (src.size() >= 1024) {
        return; // no wai
    }
    memset(scoreMultipliers, 0, src.size() * sizeof scoreMultipliers[0]);
    int nresult = 0;
    double score = 0;
    int nscore = 0;
    int countDits = 0;
    int countDas = 0;
    int countEndLetters = 0;
    int countBadSigns = 0;
    DecodedRun dr;
    bool found;
    for (int q = 0; q < src.size(); q++) {
        switch (src[q].segtype) {
        case DIT:
            result[nresult] = '.';
            seg[nresult] = &src[q];
            nresult++;
            countDits++;
            break;
        case DA:
            result[nresult] = '-';
            seg[nresult] = &src[q];
            nresult++;
            countDas++;
            break;
        case IGNORE:
            continue;
        case END_LETTER:
        case END_WORD:
            if (nresult > 0) {
                result[nresult] = 0;
                found = false;
                for (auto & i : morseTable) {
                    if (!strcmp(result, i.sequence)) {
                        double localScore = 0.0;
                        double minValue = seg[0]->value;
                        double maxValue = seg[0]->value;
                        for (int qq = 0; qq < nresult; qq++) {
                            localScore += seg[qq]->score;
                            if (seg[qq]->value > maxValue) {
                                maxValue = seg[qq]->value;
                            }
                            if (seg[qq]->value < minValue) {
                                minValue = seg[qq]->value;
                            }
                        }
                        double irregularity = 1 - minValue / maxValue;
                        irregularity = irregularity * irregularity;
                        double scoreMultiplier = 1 - irregularity;
                        localScore /= nresult;
                        localScore *= scoreMultiplier;
                        for (int qq = 0; qq < nresult; qq++) {
                            auto index = seg[qq] - src.data();
                            // all signals for this decoded letter get this multipler. If letter is not regular, it goes down.
                            scoreMultipliers[index] = scoreMultiplier;
                        }
                        if (src[q].segtype == END_LETTER && src[q].score == 0) {
                            localScore *= 0.5; // unterminated letter, samples ended.
                        }
                        dr.letters.emplace_back(
                            DecodedLetter{ seg[0]->localOffset, i.decoded, localScore });
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    dr.letters.emplace_back(DecodedLetter{ seg[0]->localOffset, "#", 0 });
                    if (dr.letters.size() != 1) {
                        // first bad sign => don't count
                        countBadSigns++;
                    }
                }
                nresult = 0;
                countEndLetters++;
            }
            if (src[q].segtype == END_WORD) {
                dr.letters.emplace_back(DecodedLetter{ src[q].localOffset, " ", src[q].score });
            }
            break;
        case ROOT:
            break;
        }
    }
    for (int q = 0; q < src.size(); q++) {
        switch (src[q].segtype) {
        case DIT:
        case DA:
            score += src[q].score * scoreMultipliers[q];
            nscore++;
        default:
            continue;
        }
    }
    if (countDits == 0 || countDas == 0 || countEndLetters == 0) {
        // statistically ugly.
        return;
    }

    dr.score = (int)(1000000 * (score / nscore));
    for (int q = 0; q < countBadSigns; q++) {
        dr.score = dr.score * 0.95;
    }
    dest.emplace_back(dr);
}


std::vector<DecodedRun> decodeLetters(const std::vector<SingleRun>& run, const TemporalSettings& ts, int totalSamples) {
    std::vector<DecodedRun> rv;
    auto q = 0;
    BinaryTree<SegmentMeaning> bt;
    auto startEdgeScore = 1.0;
    while (q < run.size()) {
        auto segment = run[q];
        if (q == 0 && run[q].start < ts.spaceLong) {
            startEdgeScore = 0.5; // unsure if complete word.
        }
        auto dit = ts.qualifyShortSignal(segment.len);
        auto da = ts.qualifyLongSignal(segment.len);
        double sureScore = fabs(dit - da);
        if (sureScore > 0.3) {
            SegmentMeaning value(dit > da ? DIT : DA, sureScore, segment.start, segment.value);
            bt.withEachLeaf([&](auto& tree, int nodeIndex) {
                tree.addNode(nodeIndex, value);
            });
        }
        else {
            bt.withEachLeaf([&](auto& tree, int nodeIndex) {
                tree.addNode(nodeIndex, SegmentMeaning(DIT, sureScore + (dit > da ? 0.05 : 0),
                                                       segment.start, segment.value));
                tree.addNode(nodeIndex,
                             SegmentMeaning(DA, sureScore + (da > dit ? 0.05 : 0),
                                            segment.start, segment.value));
            });
        }
        // space measurement
        int followingSpace;
        if (q == run.size() - 1) {
            followingSpace = totalSamples - (segment.start + segment.len);
        }
        else {
            auto nsegment = run[q + 1];
            followingSpace = nsegment.start - (segment.start + segment.len);
        }
        if (followingSpace > ts.spaceLong * 2) {
            bt.withEachLeaf([&](auto& tree, int nodeIndex) {
                tree.addNode(nodeIndex, SegmentMeaning(END_WORD, 1,
                                                       segment.start +
                                                           segment.len,
                                                       0));
            });
            //            if (followingSpace < ts.spaceLong * 4) {
            //                bt.withEachLeaf([&](auto &tree, int nodeIndex) {
            //                    tree.addNode(nodeIndex, SegmentMeaning(END_WORD, 0.9,
            //                                                           segment.start +
            //                                                           segment.len));
            //                });
            //            }
        }
        else {
            auto shorter = ts.qualifyShortSpace(followingSpace);
            auto longer = ts.qualifyLongSpace(followingSpace);
            sureScore = fabs(shorter - longer);
            if (sureScore > 0.5) {
                if (shorter > longer) {
                    if (q == run.size() - 1) {
                        // flush last letter
                        bt.withEachLeaf([&](auto& tree, int nodeIndex) {
                            tree.addNode(nodeIndex,
                                         SegmentMeaning(END_LETTER, 0, segment.start + segment.len, 0));
                        });
                    }
                    // nothing
                }
                else {
                    bt.withEachLeaf([&](auto& tree, int nodeIndex) {
                        tree.addNode(nodeIndex,
                                     SegmentMeaning(END_LETTER, sureScore, segment.start + segment.len, 0));
                    });
                }
            }
            else {
                bt.withEachLeaf([&](auto& tree, int nodeIndex) {
                    tree.addNode(nodeIndex, SegmentMeaning(IGNORE, sureScore + (shorter < longer ? 0.05 : 0),
                                                           segment.start + segment.len, 0));
                    tree.addNode(nodeIndex,
                                 SegmentMeaning(END_LETTER, sureScore + (longer > shorter ? 0.05 : 0),
                                                segment.start + segment.len, 0));
                });
            }
        }
        q++;
    }
    bt.withEachLeaf([&](BinaryTree<SegmentMeaning>& tree, int nodeIndex) {
        std::vector<SegmentMeaning> sm;
        while (nodeIndex != 0) {
            sm.insert(sm.begin(), tree.nodes[nodeIndex].value);
            nodeIndex = tree.nodes[nodeIndex].parentIndex;
        }
        segmentMeaningToWords(sm, rv);
    });
    for (auto & rvi : rv) {
        auto& letters = rvi.letters;
        letters[0].score *= startEdgeScore; // first letter maybe from the middle
                                            //        if (!strcmp(letters.back().letter," ")) {
                                            //            letters.resize(letters.size()-1);
                                            //        } else {
                                            //            letters.back().score *= 0.5; // looks like incomplete
                                            //        }
    }
    return rv;
}

std::vector<SingleRun> convertToRuns(const std::vector<float>& vector);

#define COUNTLEN 100

void addHit(float (&counts)[COUNTLEN], int where, float weight) {
    if (where >= 0 && where < COUNTLEN) {
        counts[where] += weight;
    }
}

struct Peak {
    int peakIndex;
};

std::vector<Peak> findPeaks(const std::vector<SingleRun>& run) {
    float counts[COUNTLEN] = { 0 };
    float countsma[COUNTLEN] = { 0 };
    for (auto& r : run) {
        addHit(counts, r.len, 1.0);
        addHit(counts, r.len + 1, 0.9);
        addHit(counts, r.len - 1, 0.9);
        addHit(counts, r.len + 2, 0.75);
        addHit(counts, r.len - 2, 0.75);
        addHit(counts, r.len + 3, 0.5);
        addHit(counts, r.len - 3, 0.5);
    }
    for (int q = 3; q < COUNTLEN - 3; q++) {
        auto summ = 0.0f;
        for (int j = -3; j <= 3; j++) {
            summ += counts[q + j];
        }
        countsma[q] = summ / 7.0f;
    }
    std::vector<Peak> rv;
    for (int q = 0; q < 5; q++) {
        auto mx = 0.0;
        auto mxi = -1;
        for (int z = 0; z < COUNTLEN; z++) {
            if (countsma[z] > mx) {
                mx = countsma[z];
                mxi = z;
            }
        }
        if (mxi == -1) {
            break;
        }
        rv.emplace_back(Peak{ mxi});
        auto scan = mxi + 1;
        while (scan < COUNTLEN && countsma[scan + 1] < countsma[scan]) {
            countsma[scan] = 0;
            scan++;
        }
        scan = mxi - 1;
        while (scan > 0 && countsma[scan - 1] < countsma[scan]) {
            countsma[scan] = 0;
            scan--;
        }
        countsma[mxi] = 0;
    }
    return rv;
}

Decoded decodeFrame(const std::vector<float>& frame) {
    Decoded rv;
    std::vector<SingleRun> runs = convertToRuns(frame);
    std::vector<SingleRun> spaces;
    for (int i = 0; i < runs.size() - 1; i++) {
        auto& r1 = runs[i];
        auto& r2 = runs[i + 1];
        spaces.emplace_back(SingleRun{ 0, r2.start - (r1.start + r1.len), r1.start + r1.len });
    }
    auto signalPeaks = findPeaks(runs);
    auto silencePeaks = findPeaks(spaces);
    std::sort(signalPeaks.begin(), signalPeaks.end(), [](auto& a, auto& b) {
        return a.peakIndex <= b.peakIndex;
    });
    std::sort(silencePeaks.begin(), silencePeaks.end(), [](auto& a, auto& b) {
        return a.peakIndex <= b.peakIndex;
    });
    for (auto & signalPeak : signalPeaks) {
        for (auto & silencePeak : silencePeaks) {
            auto ts = TemporalSettings{ signalPeak.peakIndex, signalPeak.peakIndex * 3,
                                        silencePeak.peakIndex, silencePeak.peakIndex * 3 };
            for (auto& t : decodeLetters(runs, ts, (int)frame.size())) {
                rv.variations.emplace_back(t);
            }
        }
        auto ts = TemporalSettings{ signalPeak.peakIndex, signalPeak.peakIndex * 3, signalPeak.peakIndex,
                                    signalPeak.peakIndex * 3 };
        for (auto& t : decodeLetters(runs, ts, (int)frame.size())) {
            rv.variations.emplace_back(t);
        }
    }
    std::sort(rv.variations.begin(), rv.variations.end(),
              [](const DecodedRun& a, const DecodedRun& b) { return a.score > b.score; });
    return rv;
}

std::vector<SingleRun> convertToRuns(const std::vector<float>& vector) {
    auto rv = std::vector<SingleRun>();
    float prev = 0;
    auto start = -1;
    for (int i = 0; i < vector.size(); i++) {
        if (vector[i] != prev) {
            if (vector[i] != 0) {
                start = i;
                prev = vector[i];
            }
            else {
                // end run
                rv.emplace_back(SingleRun{ prev, i - start, start });
                prev = vector[i];
            }
        }
    }
    for (int i = 0; i < rv.size() - 1; i++) {
        auto mean = rv[i].value;
        auto inQuestion = rv[i + 1].value;
        auto distance = rv[i + 1].start - rv[i].start - rv[i].len;
        auto twolen = rv[i].len * 2;
        if (i < vector.size() - 2) {
            mean += rv[i + 2].value;
            mean /= 2;
            distance = rv[i + 2].start - rv[i].start - rv[i].len;
            twolen = rv[i].len + rv[i + 2].len;
        }
        if (inQuestion < mean * 0.25) {
            if (distance < twolen * 5) {
                rv.erase(rv.begin() + i + 1); // weak signal between strong
                i--;
            }
        }
    }
    return rv;
}

void decodeChannels(std::vector<std::vector<float>>& res) {
    long long total = 0;
    for (int i = 0; i < res.size(); i++) {
        printf("SIGNAL %d (line %d):\n", i, i + 1);
        auto t1 = currentTimeNanos();
        auto decodeResult = decodeFrame(res[i]);
        t1 = currentTimeNanos() - t1;
        total += t1;
        for (int q = 0; q < decodeResult.variations.size() && q < 10; i++) {
            auto s = decodeResult.variations[q].toString();
            printf("     %s\n", s.c_str());
        }
    }
    printf("Total time: %f microsec\n", (double)total / 1000.0);
}

float sumVector(const std::vector<float>& vec) {
    float sum = 0.0f;
    for (float value : vec) {
        sum += value;
    }
    return sum;
}

std::vector<float> addConstantToVector(const std::vector<float>& vec, float constant) {
    std::vector<float> result;
    result.reserve(vec.size()); // Pre-allocate space for efficiency

    for (float value : vec) {
        result.push_back(value + constant);
    }
    return result;
}

float sumMatrix(const std::vector<std::vector<float>>& matrix) {
    float sum = 0.0f;
    for (const std::vector<float>& row : matrix) {
        sum += sumVector(row);
    }
    return sum;
}

Matrix2DPtr addConstantToMatrix(const std::vector<std::vector<float>>& matrix, float constant) {
    auto result = std::make_shared<Matrix2D>();
    result->reserve(matrix.size()); // Pre-allocate outer vector

    for (const std::vector<float>& row : matrix) {
        result->push_back(addConstantToVector(row, constant));
    }
    return result;
}

float meanOfVector(const std::vector<float>& vec) {
    float sum = sumVector(vec);
    return sum / (float)vec.size();
}

// Mean of a matrix
float meanOfMatrix(const std::vector<std::vector<float>>& matrix) {
    float sum = sumMatrix(matrix);
    int totalElements = 0;

    for (const std::vector<float>& row : matrix) {
        totalElements += (int)row.size();
    }

    return sum / (float)totalElements;
}

float calculateCorrelation(const std::vector<float>& X, const std::vector<float>& Y) {
    if (X.size() != Y.size() || X.empty()) {
        throw std::invalid_argument("Vectors have different sizes or are empty");
    }

    auto n = (float)X.size();
    float sum_X = std::accumulate(X.begin(), X.end(), 0.0f);
    float sum_Y = std::accumulate(Y.begin(), Y.end(), 0.0f);
    float sum_XY = std::inner_product(X.begin(), X.end(), Y.begin(), 0.0f);
    float squareSum_X = std::inner_product(X.begin(), X.end(), X.begin(), 0.0f);
    float squareSum_Y = std::inner_product(Y.begin(), Y.end(), Y.begin(), 0.0f);

    float corr = (n * sum_XY - sum_X * sum_Y) /
                 (sqrt((n * squareSum_X - sum_X * sum_X) * (n * squareSum_Y - sum_Y * sum_Y)));

    return corr;
}


template <typename T>
std::vector<T> rollmax(const std::vector<T>& data, int window_size) {
    std::vector<T> result;
    for (int i = 0; i < data.size() - window_size; ++i) {
        T current_max = data[i];
        for (int j = i; j < i + window_size; ++j) {
            if (data[j] > current_max) {
                current_max = data[j];
            }
        }
        result.push_back(current_max);
    }
    return result;
}

template <typename T>
std::vector<T> rollmin(const std::vector<T>& data, int window_size) {
    std::vector<T> result;
    for (int i = 0; i < data.size() - window_size; ++i) {
        T current_min = data[i];
        for (int j = i; j < i + window_size; ++j) {
            if (data[j] < current_min) {
                current_min = data[j];
            }
        }
        result.push_back(current_min);
    }
    return result;
}

std::vector<float> rollmean(const std::vector<float>& data, int window_size) {
    std::vector<float> result;
    float sum = 0;
    for (int i = 0; i < window_size; i++) {
        sum += data[i];
    }
    for (int i = 0; i < data.size() - window_size; ++i) {
        result.push_back(sum / (float)window_size);
        sum = sum - data[i];
        if (i + window_size < data.size()) {
            sum = sum + data[i + window_size];
        }
    }
    return result;
}

std::vector<float> xrollmean(const std::vector<float>& data, int window_size) {
    auto rm = rollmean(data, window_size);
    auto beg = window_size / 2;
    float valb = rm.front();
    float vale = rm.back();
    for (int i = 0; i < beg; i++) {
        rm.insert(rm.begin(), valb);
    }
    for (int i = 0; i < window_size - beg; i++) {
        rm.insert(rm.end(), vale);
    }
    return rm;
}

std::vector<float> xrollmax(const std::vector<float>& data, int window_size) {
    auto rm = rollmax(data, window_size);
    auto beg = window_size / 2;
    float valb = rm.front();
    float vale = rm.back();
    for (int i = 0; i < beg; i++) {
        rm.insert(rm.begin(), valb);
    }
    for (int i = 0; i < window_size - beg; i++) {
        rm.insert(rm.end(), vale);
    }
    return rm;
}

std::vector<float> xrollmin(const std::vector<float>& data, int window_size) {
    auto rm = rollmin(data, window_size);
    auto beg = window_size / 2;
    float valb = rm.front();
    float vale = rm.back();
    for (int i = 0; i < beg; i++) {
        rm.insert(rm.begin(), valb);
    }
    for (int i = 0; i < window_size - beg; i++) {
        rm.insert(rm.end(), vale);
    }
    return rm;
}

std::vector<float> normalize(std::vector<float>& src) {
    std::vector<float> rv = src;
    auto minn = src[0];
    auto maxx = src[0];
    for (int i = 1; i < src.size(); i++) {
        if (src[i] > maxx) {
            maxx = src[i];
        }
        if (src[i] < minn) {
            minn = src[i];
        }
    }
    for (float & i : rv) {
        i = (i - minn) / (maxx - minn);
    }
    return rv;
}

struct OffDur {
    int offs = 0;
    int dur = 0;
};

std::vector<OffDur> toOffDur(const std::vector<float>& signal) {
    std::vector<OffDur> rv;
    int started = -1;

    for (int i = 1; i < signal.size(); ++i) {
        if (signal[i - 1] < signal[i]) { // Begin
            started = i;
        }
        else if (signal[i - 1] > signal[i]) { // End
            if (started > -1) {
                OffDur segment;
                segment.offs = started;
                segment.dur = i - started;
                rv.push_back(segment);
                started = -1;
            }
        }
    }

    return rv;
}


float minAllowLength(float x) {
    // magic polynome (not best)
    return 45.2121f - 14.0818f * x + 1.7897f * x * x - 0.0763636f * x * x * x;
}

Matrix2DPtr samplesToData(const dsp::arrays::ComplexArray &samples, int framerate, int hz, float offSec, float durSec) {
    int win = framerate / hz;
    auto window = dsp::arrays::npzeros_c(win);
    for (int i = 0; i < win; i++) {
        (*window)[i].re = (float)dsp::window::blackman(i, win);
    }
    auto plan = dsp::arrays::allocateFFTWPlan(false, win);
    auto part = dsp::arrays::npzeros_c(win);
    int outFrames = (int)((float)hz * durSec);
    int offsetFrames = (int)((float)hz * offSec);
    auto rv = std::make_shared<Matrix2D>(outFrames, win);
    for (int i = 0; i < outFrames; i++) {
        for (int j = 0; j < win; j++) {
            part->at(j) = samples->at((i + offsetFrames) * win + j) * (*window)[j];
        }
        dsp::arrays::npfftfft(part, plan);
        auto carr = plan->getOutput()->data();
        std::vector<float> dest(win);
        auto di = win / 2;
        for (int j = 0; j < win; j++, di++) {
            auto zz = 10 * log10(carr[j].amplitude());
            (*rv)[di % win][i] = zz;
        }
    }
    return rv;
}

Matrix2DPtr getBand(const dsp::arrays::ComplexArray &samples, float startSec, float lengthSec) {
    std::vector<std::vector<float>> rv;
//    if (false) {
//        return read_csv("/Users/san/Fun/morse/band.txt");
//    }

    auto framerate = 192000;
    // auto dur = 4.0;


    auto herz = 250;
    int win = framerate / herz;
    auto fftframes = samplesToData(samples, framerate, herz, startSec, lengthSec);

    //    write_csv("/Users/san/Fun/morse/c_frames.csv", fftframes);
    return fftframes;
}

struct DecodingState {

    static int calcFrequency(int sampleRate, int middleFrequency, int bucket, int nBuckets) {
        int startFrequency = middleFrequency - sampleRate / 2;
        return startFrequency + (bucket * sampleRate) / nBuckets;
    }

    static void decodeInterval(float globalOffset, const Matrix2DPtr &band, int sampleRate, int middleFrequency) {
        auto data = addConstantToMatrix(*band, -meanOfMatrix(*band));

        std::vector<float> correlations;

        // Efficiently iterate over column indices
        for (auto i = 0; i < data->size() - 1; i++) {
            auto v = calculateCorrelation((*data)[i], (*data)[i + 1]);
            correlations.emplace_back(v);
        }

        auto corr2 = correlations;
        corr2.insert(corr2.begin(), 0.0);
        corr2.insert(corr2.begin(), 0.0);
        corr2.insert(corr2.end(), 0.0);
        corr2.insert(corr2.end(), 0.0);
        corr2 = rollmax(corr2, 5);
        for (int i = 0; i < correlations.size(); i++) {
            if (correlations[i] < 0.5 || correlations[i] != corr2[i]) {
                correlations[i] = 0;
            }
        }
        correlations.emplace_back(0);

        std::vector<std::vector<float>> channels;
        std::vector<int> channelFrequencies;

        for (int i = 0; i < correlations.size() - 1; i++) {
            if ((i + 1) <= 240 || (i + 1) >= 520) { // temporarily
                continue;
            }
            if (correlations[i] != 0) {
                channels.emplace_back((*data)[i + 1]);
                int args = calcFrequency(sampleRate, middleFrequency, i, (int)data->size());
                channelFrequencies.emplace_back(args);
            }
        }

        std::vector<std::vector<float>> signals;
        for (auto src : channels) {
            auto mean6 = xrollmean(src, 7);
            auto bgnoise = xrollmin(mean6, 100);
            auto noiseslow = xrollmean(bgnoise, 100);
            for (int j = 0; j < src.size(); j++) {
                src[j] -= noiseslow[j];
            }
            auto mean3 = xrollmean(src, 3);
            auto mean5 = xrollmean(src, 5);
            auto env = xrollmax(mean3, 100);
            env = xrollmean(env, 100);
            std::vector<float> sig(env.size(), 0);
            for (int j = 0; j < src.size(); j++) {
                if (mean5[j] > env[j] * 0.5 || src[j] > env[j] * 0.75) {
                    sig[j] = 1.0;
                }
            }
            const auto bits = toOffDur(sig);
            for (auto bit : bits) {
                if ((float)bit.dur < minAllowLength(env[bit.offs])) {
                    // wipe out too short signals for this env
                    for (int q = bit.offs; q < bit.offs + bit.dur; q++) {
                        sig[q] = 0;
                    }
                }
                else {
                    float mean = 0.0;
                    for (int q = bit.offs; q < bit.offs + bit.dur; q++) {
                        mean += src[q];
                    }
                    mean /= (float)bit.dur;
                    for (int q = bit.offs; q < bit.offs + bit.dur; q++) {
                        sig[q] = mean;
                    }
                }
            }
            sig = normalize(sig);
            signals.emplace_back(sig);
        }

        printf("done.\n");


        //   signals = read_csv("/Users/san/Fun/morse/decoded.txt");
        decodeChannels(signals);

    }
};

struct SourceData {
    dsp::arrays::ComplexArray allSamples;

    [[nodiscard]] Matrix2DPtr getFrames(float secondsOffset, float secondsLength) const {
        return  getBand(allSamples, secondsOffset, secondsLength);
    }

    static int getSampleRate() {
        return 192000;
    }

    static int getFrequency() {
        return 14034485;
    }
};

std::shared_ptr<SourceData> getSourceData() {
    auto samples0 =  read_csv("/Users/san/Fun/morse/ffdata.csv");
    auto samples = dsp::arrays::npzeros_c(samples0->size());
    for (int i = 0; i < samples0->size(); i++) {
        (*samples)[i] = dsp::complex_t{ (*samples0)[i][0], (*samples0)[i][1] };
    }
    auto rv = std::make_shared<SourceData>();
    rv->allSamples = samples;
    return rv;
}

void cw_test() {

    auto sourceData = getSourceData();

    float secondsOffset = 0;

    auto band = sourceData->getFrames(secondsOffset, 4);

    DecodingState ds;
    ds.decodeInterval(secondsOffset, band, sourceData->getSampleRate(), sourceData->getFrequency());

}

#pragma clang diagnostic pop