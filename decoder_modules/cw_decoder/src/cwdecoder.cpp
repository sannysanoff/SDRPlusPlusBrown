#pragma clang diagnostic push
#pragma ide diagnostic ignored "misc-no-recursion"

#include "cwdecoder.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric> // for std::iota
#include <fstream>
#include <ctm.h>

#include <dsp/types.h>
#include <unordered_set>
#include <dsp/window/blackman.h>
#include <../src/utils/arrays.h>

// todo: check 14019430 (freq 812@100hz) CQ WAE IR2D (second time is wors on 100 hz)

static std::string strprintf(const char *format, ...) {
    va_list args;
    va_start(args, format);

    // Determine required length (with some extra space)
    int size = vsnprintf(nullptr, 0, format, args) + 10;
    std::string buffer(size, '\0');

    // Format the string into the buffer
    vsnprintf(&buffer[0], size, format, args);

    va_end(args);
    buffer.resize(strlen(buffer.data()));

    return buffer;
}

constexpr int hertz = 100;

struct LayoutKey {
    int offset;
    int length;

    bool operator==(const LayoutKey &other) const {
        return offset == other.offset && length == other.length;
    }
};

struct CompareLayoutKey {
    bool operator()(const LayoutKey &a, const LayoutKey &b) const {
        if (a.offset != b.offset) return a.offset < b.offset;
        return a.length < b.length;
    }
};

// namespace std {
template<>
struct std::hash<LayoutKey> {
    size_t operator()(const LayoutKey &key) const noexcept {
        // Combine the hash of individual members using a method similar to boost::hash_combine
        size_t hash1 = std::hash<int>()(key.offset);
        size_t hash2 = std::hash<int>()(key.length);
        return hash1 ^ (hash2 << 1); // Shift and combine hashes
    }
};

// }

std::vector<float> normalize(const std::vector<float> &src) {
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
    for (float &i: rv) {
        i = (i - minn) / (maxx - minn);
    }
    return rv;
}

struct MorseCode {
    const char *sequence; // Morse code sequence
    const char *decoded; // Decoded character
};

struct Matrix2D : public std::vector<std::vector<float> > {
    Matrix2D() = default;

    Matrix2D(int major, int minor) : std::vector<std::vector<float> >(minor, std::vector<float>(major, 0)) {
    }
};

typedef std::shared_ptr<Matrix2D> Matrix2DPtr;

// todo: 2 separate transmissions, pause between, split and calc stat separately.

// Initialize the vector with Morse code mapping
std::vector<MorseCode> morseTable = {
    {".-", "A"},
    {"-...", "B"},
    {"-.-.", "C"},
    {"-..", "D"},
    {".", "E"},
    {"..-.", "F"},
    {"--.", "G"},
    {"....", "H"},
    {"..", "I"},
    {".---", "J"},
    {"-.-", "K"},
    {".-..", "L"},
    {"--", "M"},
    {"-.", "N"},
    {"---", "O"},
    {".--.", "P"},
    {"--.-", "Q"},
    {".-.", "R"},
    {"...", "S"},
    {"-", "T"},
    {"..-", "U"},
    {"...-", "V"},
    {".--", "W"},
    {"-..-", "X"},
    {"-.--", "Y"},
    {"--..", "Z"},
    {"-----", "0"},
    {".----", "1"},
    {"..---", "2"},
    {"...--", "3"},
    {"....-", "4"},
    {".....", "5"},
    {"-....", "6"},
    {"--...", "7"},
    {"---..", "8"},
    {"----.", "9"},
    // Punctuation
    {".-.-.-", "."}, // Full stop
    {"--..--", ","}, // Comma
    {"..--..", "?"}, // Question mark
    {"-.-.--", "!"}, // Exclamation mark
    {"-....-", "-"}, // Hyphen
    {"-..-.", "/"}, // Slash
    {".--.-.", "@"}, // At sign
    {"-.--.", "("}, // Left parenthesis
    {"-.--.-", ")"}, // Right parenthesis
    {"---...", ";"}, // Semicolon
    {"-.-.-.", ";"}, // Colon
    {"-...-", "="}, // Equals sign
    {".-.-.", "+"}, // Plus sign
    // Known prosigns
    {".-...", "&"}, // Wait (AS)
    {"...-.-", "SK"}, // End of work
    {".-.-.-", "{OUT}"}, // AR (out)
    {"-.-.-", "KA"}, // Beginning of message
    {"...-.-", "VA"}, // Understood (VE)
    {".-.-.", "AR"}, // End of message
    {"-.-.--", "SN"}, // Understood (SN)
    {"........", "{corr}"}, // correction
    {".......", "{corr}"}, // correction
    {".........", "{corr}"}, // correction
    {"...-.-", "{SK}"}, // silent key (end of trx)
};


Matrix2DPtr read_complex_array(const char *filename) {
    Matrix2DPtr matrix = std::make_shared<Matrix2D>();

    printf("%lld begin read file...\n", currentTimeMillis());
    std::ifstream file(filename, std::ios::in | std::ios::binary);

    if (!file.is_open()) {
        // Handle the error: file could not be opened
        return nullptr;
    }

    // Determine the size of the matrix
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    size_t num_elements = file_size / (2 * sizeof(double)); // Two floats per complex number
    size_t major_dim = 2; // Assuming square shape for simplicity
    size_t minor_dim = num_elements;
    file.seekg(0, std::ios::beg);

    matrix->resize(minor_dim, std::vector<float>(major_dim, 0));

    double real_part, imag_part;
    for (int i = 0; i < minor_dim; ++i) {
        file.read(reinterpret_cast<char *>(&real_part), sizeof(double));
        file.read(reinterpret_cast<char *>(&imag_part), sizeof(double));
        (*matrix)[i][0] = (float) real_part;
        (*matrix)[i][1] = (float) imag_part;
    }

    file.close();
    printf("%lld end read file...\n", currentTimeMillis());
    return matrix;
}

Matrix2DPtr read_csv(const char *filename) {
    auto rv = std::make_shared<Matrix2D>();
    printf("Loading file: %s...\n", filename);
    FILE *file = fopen(filename, "r"); // Open the file for reading
    if (!file) {
        perror("Failed to open file");
        return rv; // Return empty data if file opening fails
    }

    char line[100000]; // Buffer to hold each line, up to 100,000 bytes
    char buf[1000000]; // Buffer to hold each line, up to 100,000 bytes
    setvbuf(file, buf, _IOFBF, sizeof(buf));
    while (fgets(line, sizeof(line), file)) {
        rv->emplace_back();
        std::vector<float> &row = rv->back();
        if (rv->size() > 1) {
            row.reserve(rv->at(0).size());
        }
        char *token = strtok(line, ","); // Tokenize the line by comma
        while (token) {
            row.emplace_back(strtof(token, nullptr)); // Convert token to float and add to row
            token = strtok(nullptr, ","); // Get next token
        }
        if (rv->size() > 1000 * 768 * 15) {
            break;
        }
    }

    fclose(file); // Close the file
    return rv; // Return the populated data structure
}

[[maybe_unused]] bool write_csv(const char *filename, const std::vector<std::vector<float> > &data) {
    std::ofstream file(filename); // Open the file for writing

    if (!file.is_open()) {
        perror("Failed to open file");
        return false;
    }

    for (const auto &row: data) {
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
    int duration;
    const char *letter;
    double score;
    double level;
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

    std::string toString() {
        return strprintf("TS{%d,%d,%d,%d}", sigShort, sigLong, spaceShort, spaceLong);
    }
};

struct DecodedRun {
    std::vector<DecodedLetter> letters;
    double score = 0;
    TemporalSettings ts;
    bool debugIt = false;
    int generationIndex = 0;
    std::string debugLog;

    [[nodiscard]] std::string toString() const {
        std::string rv;
        rv += std::to_string((int) (score * 10000));
        rv += ": ";
        for (auto &letter: letters) {
            rv += letter.letter;
        }
        return rv;
    }
};

struct Decoded {
    int globalOffset = -1;
    int exactFrequency = -1;
    long long decodeTime = 0;
    std::vector<DecodedRun> variations;

    void adjustScores(int nsamples) {
        std::vector<float> scores;
        scores.reserve(variations.size());
        for (auto &variation: variations) {
            scores.emplace_back(variation.score);
        }
        scores = normalize(scores);
        for (int q = 0; q < variations.size(); q++) {
            variations[q].score = scores[q];
            for (auto &l: variations[q].letters) {
                // range 0.9..1
                float closeToMiddleOfFrame = abs((float) l.localOffset - (float) nsamples / 2.0f) / (
                                                 (float) nsamples / 2.0f) / 10.0f + 0.9f;
                l.score *= closeToMiddleOfFrame;
            }
        }
        // 0.9 ... 1 now
    }
};

struct SingleRun {
    float value;
    int len;
    int start;
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
    SingleRun run = {0, 0, 0};

    SegmentMeaning() = default;

    SegmentMeaning(DecodedSegmentType segtype, double score, const SingleRun run)
        : segtype(segtype), score(score), run(run) {
    }

    const char *toString() const {
        switch (segtype) {
            case ROOT:
                return "R";
            case IGNORE:
                return "I";
            case DIT:
                return ".";
            case DA:
                return "-";
            case END_LETTER:
                return "/";
            case END_WORD:
                return " ";
        }
    }
};

template<typename T>
T rootNode();

template<>
SegmentMeaning rootNode<SegmentMeaning>() {
    return SegmentMeaning{ROOT, 0, SingleRun{0, 0, 0}};
}

template<typename T>
struct Node {
    int leftNodeIndex;
    int rightNodeIndex;
    int parentIndex;
    int depth;
    T value;

    explicit Node(T value) : leftNodeIndex(-1), rightNodeIndex(-1), parentIndex(-1), depth(0), value(value) {
        this->value = value;
    }
};

template<typename T>
struct BinaryTree {
    std::vector<Node<T> > nodes;

    int scanFrom = 0;

    BinaryTree() {
        nodes.emplace_back(Node(rootNode<T>()));
    }

    void withEachLeaf(const std::function<void(BinaryTree<T> &, int)> &fun) {
        int savedSize = nodes.size();
        for (int i = scanFrom; i < savedSize; i++) {
            Node<T> *node = &nodes[i];
            if (node->leftNodeIndex == -1 && node->rightNodeIndex == -1) {
                fun(*this, i); // Apply the function on the leaf node
            }
        }
        if (nodes.size() != savedSize) {
            scanFrom = savedSize;
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
        } else if (nodes[where].rightNodeIndex == -1) {
            nodes[where].rightNodeIndex = rv;
        } else {
            abort();
        }
    }
};

void segmentMeaningToWords(const std::vector<SegmentMeaning> &src, std::vector<DecodedRun> &dest) {
    char result[1024];
    const SegmentMeaning *seg[1024];
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
    for (int q = 0; q < src.size(); q++) {
        auto sq = src[q];
        auto segtype = sq.segtype;
        switch (segtype) {
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
                    bool found = false;
                    for (auto &i: morseTable) {
                        if (!strcmp(result, i.sequence)) {
                            double localScore = 0.0;
                            double minValue = seg[0]->run.value;
                            double maxValue = seg[0]->run.value;
                            for (int qq = 0; qq < nresult; qq++) {
                                localScore += seg[qq]->score;
                                if (seg[qq]->run.value > maxValue) {
                                    maxValue = seg[qq]->run.value;
                                }
                                if (seg[qq]->run.value < minValue) {
                                    minValue = seg[qq]->run.value;
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
                            if (segtype == END_LETTER && (sq.score == 0 || q == src.size() - 1)) {
                                localScore *= 0.5; // unterminated letter, samples ended.
                            }
                            if (q == 1000000) {
                                printf("%d", (int) src.size()); // i want it here in debug scope
                            }
                            dr.letters.emplace_back(
                                DecodedLetter{
                                    seg[0]->run.start, src[q - 1].run.start + src[q - 1].run.len - seg[0]->run.start,
                                    i.decoded, localScore
                                });
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        dr.letters.emplace_back(DecodedLetter{
                            seg[0]->run.start, src[q - 1].run.start + src[q - 1].run.len - seg[0]->run.start, "#", 0
                        });
                        if (dr.letters.size() != 1) {
                            // first bad sign => don't count
                            countBadSigns++;
                        }
                    }
                    nresult = 0;
                    countEndLetters++;
                }
                if (src[q].segtype == END_WORD) {
                    dr.letters.emplace_back(DecodedLetter{src[q].run.start, src[q].run.len, " ", src[q].score});
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

    dr.score = (int) (1000000 * (score / nscore));
    for (int q = 0; q < countBadSigns; q++) {
        dr.score = dr.score * 0.95;
    }
    if (dr.score > 0) {
        dest.emplace_back(dr);
    }
}

int globalCount = 0;
int decodeLettersCount = 0;

std::vector<DecodedRun> decodeLetters(
    const std::vector<SingleRun> &run,
    const TemporalSettings &ts,
    int totalSamples,
    bool debugIt)
//
{
    std::vector<DecodedRun> rv;
    auto q = 0;
    BinaryTree<SegmentMeaning> bt;
    std::string debugLog;

    double startEdgeScore = 1.0;
    float lastValue = -1;
    bool lastEndWord = false;
    int ones = 0, twos = 0, dits = 0, das = 0;

    auto addStats = [&](const SegmentMeaning &opt) {
        switch (opt.segtype) {
            case DA:
                das++;
                break;
            case DIT:
                dits++;
                break;
            default:
                break;
        }
    };

    auto appendOne = [&](const SegmentMeaning &opt) {
        ones++;
        addStats(opt);
        bt.withEachLeaf([&](auto &tree, int nodeIndex) {
            tree.addNode(nodeIndex, opt);
        });
        debugLog += opt.toString();
    };

    auto appendTwo = [&](const SegmentMeaning &opt1, const SegmentMeaning &opt2) {
        twos++;
        addStats(opt1);
        addStats(opt2);
        bt.withEachLeaf([&](auto &tree, int nodeIndex) {
            tree.addNode(nodeIndex, opt1);
            tree.addNode(nodeIndex, opt2);
        });
        debugLog += "[";
        if (opt1.score > opt2.score) {
            debugLog += opt1.toString();
            debugLog += opt2.toString();
        } else {
            debugLog += opt2.toString();
            debugLog += opt1.toString();
        }
        debugLog += "]";
    };

    while (q < run.size()) {
        if (ones + twos > 20) {
            if (float(das) / float(dits) < 0.2) {
                debugLog += "early discard 1";
                return rv;
            }
            if (float(twos) / float(twos + ones) > 0.25) {
                debugLog += "early discard 2";
                return rv;
            }
        }
        auto segment = run[q];
        if (q == 0 && run[q].start < ts.spaceLong) {
            startEdgeScore = 0.5; // unsure if complete word.
        }
        auto dit = ts.qualifyShortSignal(segment.len), da = ts.qualifyLongSignal(segment.len);
        double sureScore = fabs(dit - da);
        if (segment.value < lastValue / 1.5f && !lastEndWord) {
            // special case when noise follows the legit letters
            auto dummy = run[q];
            dummy.len = 0;
            appendTwo(SegmentMeaning(IGNORE, 0.5, dummy), SegmentMeaning(END_WORD, 0.5, dummy));
            // proceed as usually
        }
        if (sureScore > 0.3) {
            appendOne(SegmentMeaning(dit > da ? DIT : DA, sureScore, segment));
        } else {
            appendTwo(SegmentMeaning(DIT, sureScore + (dit > da ? 0.05 : 0), segment),
                      SegmentMeaning(DA, sureScore + (da > dit ? 0.05 : 0), segment));
        }
        // space measurement
        lastEndWord = false;
        int spaceLength, spaceStart = segment.start + segment.len;
        if (q == run.size() - 1) {
            spaceLength = totalSamples - (segment.start + segment.len);
        } else {
            auto nsegment = run[q + 1];
            spaceLength = nsegment.start - (segment.start + segment.len);
        }
        if (spaceLength > ts.spaceLong * 2) {
            appendOne(SegmentMeaning(END_WORD, 1, SingleRun{0, spaceLength, spaceStart}));
            lastEndWord = true;
        } else {
            auto shorter = ts.qualifyShortSpace(spaceLength), longer = ts.qualifyLongSpace(spaceLength);
            sureScore = fabs(shorter - longer);
            if (sureScore > 0.5) {
                if (shorter > longer) {
                    if (q == run.size() - 1) {
                        // flush last letter
                        appendOne(SegmentMeaning(END_LETTER, 0, SingleRun{0, spaceLength, spaceStart}));
                    }
                    // nothing
                } else {
                    appendOne(SegmentMeaning(END_LETTER, sureScore, SingleRun{0, spaceLength, spaceStart}));
                }
            } else {
                appendTwo(
                    SegmentMeaning(IGNORE, sureScore + (shorter < longer ? 0.05 : 0),
                                   SingleRun{0, spaceLength, spaceStart}),
                    SegmentMeaning(END_LETTER, sureScore + (longer > shorter ? 0.05 : 0),
                                   SingleRun{0, spaceLength, spaceStart}));
            }
        }
        lastValue = segment.value;
        q++;
    }
    bt.withEachLeaf([&](const BinaryTree<SegmentMeaning> &tree, int nodeIndex) {
        std::vector<SegmentMeaning> sm;
        while (nodeIndex != 0) {
            sm.insert(sm.begin(), tree.nodes[nodeIndex].value);
            nodeIndex = tree.nodes[nodeIndex].parentIndex;
        }
        segmentMeaningToWords(sm, rv);
    });
    for (auto &rvi: rv) {
        auto &letters = rvi.letters;
        rvi.generationIndex = decodeLettersCount++;
        rvi.debugLog = debugLog;
        letters[0].score *= startEdgeScore; // first letter maybe from the middle
        //        if (!strcmp(letters.back().letter," ")) {
        //            letters.resize(letters.size()-1);
        //        } else {
        //            letters.back().score *= 0.5; // looks like incomplete
        //        }
        rvi.ts = ts;
    }
    return rv;
}

std::vector<SingleRun> convertToRuns(const std::vector<float> &vector);

#define COUNTLEN 100

void addHit(float (&counts)[COUNTLEN], int where, float weight) {
    if (where >= 0 && where < COUNTLEN) {
        counts[where] += weight;
    }
}

struct Peak {
    int peakIndex;
};

std::vector<Peak> findPeaks(const std::vector<SingleRun> &run) {
    float counts[COUNTLEN] = {0};
    float countsma[COUNTLEN] = {0};
    for (auto &r: run) {
        addHit(counts, r.len, 1.0);
        addHit(counts, r.len + 1, 0.9);
        addHit(counts, r.len - 1, 0.9);
        addHit(counts, r.len + 2, 0.75);
        addHit(counts, r.len - 2, 0.75);
        addHit(counts, r.len + 3, 0.5);
        addHit(counts, r.len - 3, 0.5);
    }
    // for (int q = 3; q < COUNTLEN - 3; q++) {
    //     auto summ = 0.0f;
    //     for (int j = -2; j <= 2; j++) {
    //         summ += counts[q + j];
    //     }
    //     countsma[q] = summ / 5.0f;
    // }
    memcpy(countsma, counts, sizeof(counts));
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
        if (mxi > 1) {
            rv.emplace_back(Peak{mxi});
        }
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

std::shared_ptr<Decoded> decodeFrame(const std::vector<float> &frame) {
    auto rv = std::make_shared<Decoded>();
    auto t1 = currentTimeNanos();
    std::vector<SingleRun> runs = convertToRuns(frame);
    if (runs.size() > 1) {
        // filter definite rubbish
        std::vector<SingleRun> spaces;
        for (int i = 0; i < runs.size() - 1; i++) {
            auto &r1 = runs[i];
            auto &r2 = runs[i + 1];
            spaces.emplace_back(SingleRun{0, r2.start - (r1.start + r1.len), r1.start + r1.len});
        }
        auto signalPeaks = findPeaks(runs);
        auto silencePeaks = findPeaks(spaces);
        std::sort(signalPeaks.begin(), signalPeaks.end(), [](auto &a, auto &b) {
            return a.peakIndex <= b.peakIndex;
        });
        std::sort(silencePeaks.begin(), silencePeaks.end(), [](auto &a, auto &b) {
            return a.peakIndex <= b.peakIndex;
        });
        decodeLettersCount = 0;
        for (auto &signalPeak: signalPeaks) {
            for (auto &silencePeak: silencePeaks) {
                if (signalPeak.peakIndex != silencePeak.peakIndex) {
                    // will be done anyway
                    auto ts = TemporalSettings{
                        signalPeak.peakIndex, signalPeak.peakIndex * 3,
                        silencePeak.peakIndex, silencePeak.peakIndex * 3
                    };
                    for (auto &t: decodeLetters(runs, ts, (int) frame.size(), false)) {
                        rv->variations.emplace_back(t);
                    }
                }
            }
            auto ts = TemporalSettings{
                signalPeak.peakIndex, signalPeak.peakIndex * 3, signalPeak.peakIndex,
                signalPeak.peakIndex * 3
            };
            const auto dl = decodeLetters(runs, ts, (int) frame.size(), false);
            for (auto &t: dl) {
                rv->variations.emplace_back(t);
            }
        }
        globalCount++;
        std::sort(rv->variations.begin(), rv->variations.end(),
                  [](const DecodedRun &a, const DecodedRun &b) { return a.score > b.score; });
        if (globalCount == -1) {
            rv->variations[0].debugIt = true;
        }
    }
    rv->decodeTime = currentTimeNanos() - t1;
    return rv;
}

std::vector<SingleRun> convertToRuns(const std::vector<float> &vector) {
    auto rv = std::vector<SingleRun>();
    float prev = 0;
    auto start = -1;
    for (int i = 0; i < vector.size(); i++) {
        if (vector[i] != prev) {
            if (vector[i] != 0) {
                start = i;
                prev = vector[i];
            } else {
                // end run
                rv.emplace_back(SingleRun{prev, i - start, start});
                prev = vector[i];
            }
        }
    }
    if (rv.size() > 1) {
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
    }
    return rv;
}

float sumVector(const std::vector<float> &vec) {
    float sum = 0.0f;
    for (float value: vec) {
        sum += value;
    }
    return sum;
}

std::vector<float> addConstantToVector(const std::vector<float> &vec, float constant) {
    std::vector<float> result;
    result.reserve(vec.size()); // Pre-allocate space for efficiency

    for (float value: vec) {
        result.push_back(value + constant);
    }
    return result;
}

float sumMatrix(const std::vector<std::vector<float> > &matrix) {
    float sum = 0.0f;
    for (const std::vector<float> &row: matrix) {
        sum += sumVector(row);
    }
    return sum;
}

Matrix2DPtr addConstantToMatrix(const std::vector<std::vector<float> > &matrix, float constant) {
    auto result = std::make_shared<Matrix2D>();
    result->reserve(matrix.size()); // Pre-allocate outer vector

    for (const std::vector<float> &row: matrix) {
        result->push_back(addConstantToVector(row, constant));
    }
    return result;
}

// Mean of a matrix
float meanOfMatrix(const std::vector<std::vector<float> > &matrix) {
    float sum = sumMatrix(matrix);
    int totalElements = 0;

    for (const std::vector<float> &row: matrix) {
        totalElements += (int) row.size();
    }

    return sum / (float) totalElements;
}

float calculateCorrelation(const std::vector<float> &X, const std::vector<float> &Y) {
    if (X.size() != Y.size() || X.empty()) {
        throw std::invalid_argument("Vectors have different sizes or are empty");
    }

    auto n = (float) X.size();
    float sum_X = std::accumulate(X.begin(), X.end(), 0.0f);
    float sum_Y = std::accumulate(Y.begin(), Y.end(), 0.0f);
    float sum_XY = std::inner_product(X.begin(), X.end(), Y.begin(), 0.0f);
    float squareSum_X = std::inner_product(X.begin(), X.end(), X.begin(), 0.0f);
    float squareSum_Y = std::inner_product(Y.begin(), Y.end(), Y.begin(), 0.0f);

    float corr = (n * sum_XY - sum_X * sum_Y) /
                 (sqrt((n * squareSum_X - sum_X * sum_X) * (n * squareSum_Y - sum_Y * sum_Y)));

    return corr;
}


template<typename T>
std::vector<T> rollmax(const std::vector<T> &data, int window_size) {
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

template<typename T>
std::vector<T> rollmin(const std::vector<T> &data, int window_size) {
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

std::vector<float> rollmean(const std::vector<float> &data, int window_size) {
    std::vector<float> result;
    float sum = 0;
    for (int i = 0; i < window_size; i++) {
        sum += data[i];
    }
    for (int i = 0; i < data.size() - window_size; ++i) {
        result.push_back(sum / (float) window_size);
        sum = sum - data[i];
        if (i + window_size < data.size()) {
            sum = sum + data[i + window_size];
        }
    }
    return result;
}

std::vector<float> xrollmean(const std::vector<float> &data, int window_size) {
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

std::vector<float> xrollmax(const std::vector<float> &data, int window_size) {
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

std::vector<float> xrollmin(const std::vector<float> &data, int window_size) {
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


struct OffDur {
    int offs = 0;
    int dur = 0;
};

std::vector<OffDur> toOffDur(const std::vector<float> &signal) {
    std::vector<OffDur> rv;
    int started = -1;

    for (int i = 1; i < signal.size(); ++i) {
        if (signal[i - 1] < signal[i]) {
            // Begin
            started = i;
        } else if (signal[i - 1] > signal[i]) {
            // End
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
    return (45.2121f - 14.0818f * x + 1.7897f * x * x - 0.0763636f * x * x * x) / 2.5;
}

Matrix2DPtr samplesToData(const dsp::arrays::ComplexArray &samples, int framerate, int hz, float offSec, float durSec) {
    int win = framerate / hz;
    auto window = dsp::arrays::npzeros_c(win);
    for (int i = 0; i < win; i++) {
        (*window)[i].re = (float) dsp::window::blackman(i, win);
    }
    auto plan = dsp::arrays::allocateFFTWPlan(false, win);
    auto part = dsp::arrays::npzeros_c(win);
    int outFrames = (int) ((float) hz * durSec);
    int offsetFrames = (int) ((float) hz * offSec);
    auto rv = std::make_shared<Matrix2D>(outFrames, win);
    for (int i = 0; i < outFrames; i++) {
        for (int j = 0; j < win; j++) {
            part->at(j) = samples->at((i + offsetFrames) * win + j) * (*window)[j];
        }
        npfftfft(part, plan);
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


struct DecodingState {
    struct DecodesOnFrequency {
        std::vector<std::shared_ptr<Decoded> > decodes;
    };

    std::vector<std::shared_ptr<DecodesOnFrequency> > allDecodes;

    static int calcFrequency(int sampleRate, int middleFrequency, int bucket, int nBuckets) {
        int startFrequency = middleFrequency - sampleRate / 2;
        return startFrequency + (bucket * sampleRate) / nBuckets;
    }

    void addDecodedVariant(const std::shared_ptr<Decoded> &d, int freq) {
        bool found = false;
        d->exactFrequency = freq;
        for (auto &variation: d->variations) {
            for (auto &l: variation.letters) {
                l.localOffset += d->globalOffset;
            }
        }
        int minIndex = -1;
        int minDist = 9999999;
        int index = 0;
        for (const auto &freqChannel: allDecodes) {
            std::vector<std::shared_ptr<Decoded> > &decodes = freqChannel->decodes;
            int checkFrequency = decodes.front()->exactFrequency;
            int dist = abs(checkFrequency - freq);
            if (dist < minDist) {
                minIndex = index;
                minDist = dist;
            }
            index++;
        }
        if (minDist <= 3) {
            auto &dest = allDecodes[minIndex]->decodes;
            dest.insert(dest.begin(), d);
        } else {
            auto newChannel = std::make_shared<DecodesOnFrequency>();
            newChannel->decodes.emplace_back(d);
            allDecodes.emplace_back(newChannel);
        }
    }

    [[clang::noinline]] void decodeChannels(const std::vector<std::vector<float> > &res, const std::vector<int> &freqs,
                                            int globalOffset, int nsamples) {
        for (int i = 0; i < res.size(); i++) {
            if (abs(freqs[i] - 811) > 2 ) {   // FREQFILTER
                continue;
            }
            auto decodeResult = decodeFrame(res[i]);
            auto drp = decodeResult.get();
            printf("[%d] SIGNAL %d (line %d) - freq %d - variations %d:\n",
                   globalOffset, i, i + 1, freqs[i], (int) drp->variations.size());
            if (decodeResult->variations.empty()) {
                continue; // nothing here
            }
            printf(" sequence: %s\n", decodeResult->variations.front().debugLog.c_str());
            decodeResult->adjustScores(nsamples);
            int topn = 10;
            auto newSize = std::min<int>((int) drp->variations.size(), topn);
            auto debugIndex = -1;
            for (int z = 0; z < drp->variations.size(); z++) {
                if (drp->variations[z].debugIt) {
                    debugIndex = z;
                    break;
                }
            }
            if (debugIndex >= topn) {
                std::swap(drp->variations[topn - 1], drp->variations[debugIndex]);
            }
            drp->variations.resize(newSize);
            for (auto &variation: drp->variations) {
                auto s = variation.toString();
                printf("     %s - %s: ", variation.ts.toString().c_str(), s.c_str());
                for (auto z: variation.letters) {
                    printf("%d ", (int) (100 * z.score));
                }
                printf(" <> ");
                for (auto z: variation.letters) {
                    printf("%d..%d ", z.localOffset, z.localOffset + z.duration - 1);
                }
                printf("\n");
            }
            drp->globalOffset = globalOffset;
            addDecodedVariant(decodeResult, freqs[i]);
        }
    }

    void decodeInterval(float globalOffsetSeconds, const Matrix2DPtr &band, int sampleRate, int middleFrequency) {
        auto data = addConstantToMatrix(*band, -meanOfMatrix(*band));


        int globalOffset = (int) (globalOffsetSeconds * hertz);

        std::vector<float> energy;
        for (const auto &q: *data) {
            energy.emplace_back(sumVector(q) / (float) q.size());
        }
        auto e = xrollmin(energy, 10);
        auto t = xrollmax(energy, 3);
        std::vector<std::vector<float> > channels;
        std::vector<int> channelFrequencies;
        std::string logline;
        for (int b = 0; b < e.size(); b++) {
            if (energy[b] == t[b] && energy[b] - e[b] > 1) {
                channels.emplace_back((*data)[b]);
                int args = calcFrequency(sampleRate, middleFrequency, b, (int) data->size());
                channelFrequencies.emplace_back(b);
                logline += strprintf("%d ", b);
            }
        }
        printf("[%d] chans: %s\n", globalOffset, logline.c_str());


        std::vector<std::vector<float> > signals;
        for (auto src: channels) {
            auto mean6 = xrollmean(src, 4);
            auto bgnoise = xrollmin(mean6, 40);
            auto noiseslow = xrollmean(bgnoise, 40);
            for (int j = 0; j < src.size(); j++) {
                src[j] -= noiseslow[j];
            }
            auto mean3 = xrollmean(src, 2);
            auto mean5 = xrollmean(src, 2);
            auto env = xrollmax(mean3, 30);
            env = xrollmean(env, 30);
            std::vector<float> sig(env.size(), 0);
            for (int j = 0; j < src.size(); j++) {
                if (mean5[j] > env[j] * 0.5 || src[j] > env[j] * 0.75) {
                    sig[j] = 1.0;
                }
            }
            const auto bits = toOffDur(sig);
            for (auto bit: bits) {
                if ((float) bit.dur < minAllowLength(env[bit.offs])) {
                    // wipe out too short signals for this env
                    for (int q = bit.offs; q < bit.offs + bit.dur; q++) {
                        sig[q] = 0;
                    }
                } else {
                    float mean = 0.0;
                    for (int q = bit.offs; q < bit.offs + bit.dur; q++) {
                        mean += src[q];
                    }
                    mean /= (float) bit.dur;
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
        decodeChannels(signals, channelFrequencies, globalOffset, (int) band->at(0).size());
    }


    struct LetterAndScore {
        DecodedLetter *letter;
        DecodedRun *run;
    };

    static bool intersects(int off1, int len1, int off2, int len2) {
        int end1 = off1 + len1;
        int end2 = off2 + len2;

        // Check if either interval starts after the other one ends
        if (off1 >= end2 || off2 >= end1) {
            return false; // No overlap
        } else {
            return true; // Overlap exists
        }
    }

    static bool fits(int smallerOffset, int smallerLen, int spaceStart, int spaceEnd) {
        return smallerOffset + smallerLen <= spaceEnd && smallerOffset >= spaceStart;
    }

    void dumpState() const {
        std::vector<std::string> shortest;
        std::vector<long long> durations;
        for (auto &ad: allDecodes) {
            // recombination
            std::unordered_map<LayoutKey, LetterAndScore> heap;
            std::string totalResult;
            long long dur = 0;
            for (auto &dec: ad->decodes) {
                long limitDistance = 20 * hertz; // 20 seconds
                long minTime = -1;
                long maxTime = -1;
                dur += dec->decodeTime;
                for (auto &vari: dec->variations) {
                    for (auto &lette: vari.letters) {
                        if (minTime == -1 || lette.localOffset < minTime) {
                            minTime = lette.localOffset;
                        }
                        if (maxTime == -1 || lette.localOffset > maxTime) {
                            maxTime = lette.localOffset;
                        }
                        if (maxTime - lette.localOffset > limitDistance) {
                            break;
                        }
                        LayoutKey lk{lette.localOffset, lette.duration};
                        auto existing = heap.find(lk);
                        if (existing != heap.end()) {
                            if (lette.score > existing->second.letter->score) {
                                // printf("UPD: [%f] new score: %f   existing score %f   new letter %s    old letter %s    offset %d   len %d\n", vari.score, lette.score, existing->second.letter->score, lette.letter, existing->second.letter->letter, lk.offset, lk.length);
                                heap[lk] = LetterAndScore{&lette, &vari};
                            }
                            //
                        } else {
                            // printf("INS: [%f] new score: %f    new letter %s      offset %d   len %d\n", vari.score, lette.score, lette.letter, lk.offset, lk.length);
                            heap[lk] = LetterAndScore{&lette, &vari};
                        }
                    }
                }
            }
            std::vector<LayoutKey> sorted;
            sorted.reserve(heap.size());
            for (auto [k, v]: heap) {
                sorted.emplace_back(k);
            }
            std::sort(sorted.begin(), sorted.end(), CompareLayoutKey());
            printf("Decode: freq %d\n", ad->decodes[0]->exactFrequency);
            DecodedLetter *lastLetter = nullptr;
            std::vector<LayoutKey> skipped;
            for (auto k: sorted) {
                auto best = heap[k].letter;
                if (lastLetter != nullptr && intersects(best->localOffset, best->duration, lastLetter->localOffset,
                                                        lastLetter->duration)) {
                    // printf("  --- discarding/drawn: %0.7f  off=%05d  len=%02d  value=`%s`\n", best->score, best->localOffset, best->duration, best->letter);
                    continue;
                }
                if (skipped.size() > 0 && best->localOffset >= skipped[0].offset + skipped[0].length) {
                    for (int si = 0; si < skipped.size(); si++) {
                        // in order alreay
                        auto attempt = heap[skipped[si]].letter;
                        if (fits(attempt->localOffset, attempt->duration,
                                 lastLetter ? (lastLetter->localOffset + lastLetter->duration) : 0,
                                 best->localOffset)) {
                            printf("%s", attempt->letter);
                            printf("  - was skipped - score: %0.7f  off=%05d  len=%02d  value=`%s`\n", attempt->score,
                                   attempt->localOffset, attempt->duration, attempt->letter);
                            totalResult += attempt->letter;
                            lastLetter = attempt;
                            skipped.erase(skipped.begin(), skipped.begin() + (si + 1));
                            si = -1;
                        }
                    }
                }
                for (auto z: sorted) {
                    auto check = heap[z].letter;
                    if (intersects(best->localOffset, best->duration, z.offset, z.length) && best->score < check->
                        score) {
                        printf(
                            "  --- discarding this: %0.7f  off=%05d  len=%02d value=`%s`, better one: %0.7f  off=%05d  len=%02d value=`%s` \n",
                            best->score, best->localOffset, best->duration, best->letter,
                            check->score, check->localOffset, check->duration, check->letter);
                        skipped.emplace_back(k);
                        goto cont0;
                    }
                }
                printf("%s", best->letter);
                printf("  score: %0.7f  off=%05d  len=%02d  value=`%s`\n", best->score,           best->localOffset, best->duration, best->letter);

                totalResult += best->letter;
                lastLetter = best;
                skipped.clear();

            cont0:;
            }
            printf("\n");
            auto buf = strprintf("DECODED: freq %d dur %lldms text: %s", ad->decodes[0]->exactFrequency, dur / 1000000,
                                 totalResult.c_str());
            printf("%s\n", buf.c_str());
            shortest.emplace_back(buf);
            durations.emplace_back(dur);
        }
        printf("SUMMARY: --------------------\n");
        for (auto &sh: shortest) {
            printf("%s\n", sh.c_str());
        }
        printf("-----------------\nEnd dump state\n");
        exit(0);
    }
};

struct SourceData {
    dsp::arrays::ComplexArray allSamples;

    [[nodiscard]] static Matrix2DPtr
    getBand(const dsp::arrays::ComplexArray &samples, float startSec, float lengthSec) {
        std::vector<std::vector<float> > rv;
        // if (false) {
        //     return read_csv("/Users/san/Fun/morse/band.txt");
        // }

        auto framerate = 192000;
        auto fftframes = samplesToData(samples, framerate, hertz, startSec, lengthSec);
        return fftframes;
    }

    [[nodiscard]] Matrix2DPtr getFrames(float secondsOffset, float secondsLength) const {
        return getBand(allSamples, secondsOffset, secondsLength);
    }


    static int getSampleRate() {
        return 192000;
    }

    static int getFrequency() {
        return 14034485;
    }
};

std::shared_ptr<SourceData> getSourceData() {
    auto samples0 = read_complex_array("/Users/san/Fun/morse/ffdata.bin");
    auto samples = dsp::arrays::npzeros_c((int) samples0->size());
    for (int i = 0; i < samples0->size(); i++) {
        (*samples)[i] = dsp::complex_t{(*samples0)[i][0], (*samples0)[i][1]};
    }
    auto rv = std::make_shared<SourceData>();
    rv->allSamples = samples;
    printf("%lld end complex convert.\n", currentTimeMillis());
    return rv;
}

void cw_test() {
    auto sourceData = getSourceData();
    DecodingState ds;
    for (float secondsOffset = 0; secondsOffset < 20; secondsOffset += 2) {
        long long total = 0;
        auto t1 = currentTimeNanos();
        auto band = sourceData->getFrames(secondsOffset, 4);
        ds.decodeInterval(secondsOffset, band, sourceData->getSampleRate(), sourceData->getFrequency());
        t1 = currentTimeNanos() - t1;
        total += t1;
        printf("Total time: %f microsec\n", (double) total / 1000.0);
    }
    ds.dumpState();
    exit(0);
}

#pragma clang diagnostic pop
