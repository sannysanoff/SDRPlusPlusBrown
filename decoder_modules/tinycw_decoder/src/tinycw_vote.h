// SPDX-License-Identifier: MIT
// Copyright (C) 2026 DeepFist integration for SDR++ Brown
//
// Statistical text: windows slide by 1 s, so every moment is decoded by about
// windowSec windows. Each window votes for the characters it sees at absolute
// times; the text is the vote, and it stays open while windows still cover it.
//
// 1. Slots: the characters of all windows, sorted by time, join the current
//    slot while the gap to its previous member is <= kVoteTolSec and the
//    slot has no character of that window yet. FastCW moves a character by a
//    frame or two between windows, so frame-level voting would split it. Two
//    neighbouring characters stay apart: every window has both.
// 2. Weight: a character near a window edge has little context. Its weight is
//    the distance to the nearer edge / kVoteEdgeSec, clamped to [0.15, 1].
// 3. Coverage: the summed weight of all windows that cover the slot time.
//    "No character" is a vote too: the slot shows only if the character
//    weight is >= half the coverage.
// 4. Symbol: the id with the largest weight. Confidence = its weight /
//    coverage (0..1), drawn as brightness.
// 5. Freeze: a slot before the newest window start (minus the tolerance) can
//    get no more votes. It moves to the frozen text with its confidence.
#pragma once

#include <algorithm>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace tinycw {

constexpr double kVoteTolSec = 0.10;    // gap between members of one slot (jitter is +-1-2 frames)
constexpr double kVoteEdgeSec = 1.0;
constexpr double kVoteMinWeight = 0.15;
constexpr double kVotePauseSec = 2.0;   // longer silence -> "  " between transmissions

// Text with one confidence (0..1) per byte.
struct ConfText {
    std::string text;
    std::vector<float> conf;
    void append(const std::string& s, float c) {
        text += s;
        conf.insert(conf.end(), s.size(), c);
    }
    void keepLast(size_t n) {
        if (text.size() <= n) { return; }
        const size_t cut = text.size() - n;
        text.erase(0, cut);
        conf.erase(conf.begin(), conf.begin() + (long)cut);
    }
};

class VoteText {
public:
    struct Slot {
        int id;
        double sec;
        float conf;
    };

    // One window: blank-free CTC ids with their output frame, window start t0.
    // Returns the number of characters frozen by this call.
    int add(const std::vector<int>& ids, const std::vector<int>& frames, double t0,
            double framePeriod, double windowSec) {
        Win w;
        w.t0 = t0;
        w.t1 = t0 + windowSec;
        for (size_t i = 0; i < ids.size() && i < frames.size(); i++) {
            const double sec = t0 + (double)frames[i] * framePeriod;
            w.items.push_back({ ids[i], sec, edgeWeight(w, sec) });
        }
        wins_.push_back(std::move(w));
        newestT0_ = std::max(newestT0_, t0);

        // Freeze the slots that no later window can reach.
        const double freezeBefore = newestT0_ - kVoteTolSec;
        std::vector<Cluster> cl = clusters();
        int frozen = 0;
        double edge = frozenSec_;
        for (const Cluster& c : cl) {
            if (c.first >= freezeBefore) { break; }
            Slot s;
            if (vote(c, s)) {
                frozenSlots_.push_back(s);
                frozen++;
            }
            edge = std::max(edge, c.last + 1e-6);
        }
        // Open slots start at or after freezeBefore; a frozen slot may end later.
        frozenSec_ = std::max(edge, freezeBefore);
        while (!wins_.empty() && wins_.front().t1 < frozenSec_ - 1.0) { wins_.pop_front(); }
        return frozen;
    }

    // Frozen slots not yet taken by the caller (in time order).
    std::vector<Slot> takeFrozen() {
        std::vector<Slot> out;
        out.swap(frozenSlots_);
        return out;
    }

    // Current vote of the open (not frozen) part.
    std::vector<Slot> open() const {
        std::vector<Slot> out;
        for (const Cluster& c : clusters()) {
            Slot s;
            if (vote(c, s)) { out.push_back(s); }
        }
        return out;
    }

    double frozenSec() const { return frozenSec_; }

private:
    struct Item {
        int id;
        double sec;
        double w;
    };
    struct Win {
        double t0 = 0.0, t1 = 0.0;
        std::vector<Item> items;
    };
    struct Cluster {
        double first = 0.0, last = 0.0;
        std::vector<std::pair<int, const Item*>> members;   // (window index, item)
    };

    static double edgeWeight(const Win& w, double sec) {
        const double d = std::min(sec - w.t0, w.t1 - sec);
        return std::clamp(d / kVoteEdgeSec, kVoteMinWeight, 1.0);
    }

    std::vector<Cluster> clusters() const {
        std::vector<std::pair<int, const Item*>> all;
        for (size_t wi = 0; wi < wins_.size(); wi++) {
            for (const Item& it : wins_[wi].items) {
                if (it.sec >= frozenSec_) { all.push_back({ (int)wi, &it }); }
            }
        }
        std::stable_sort(all.begin(), all.end(),
                         [](const auto& a, const auto& b) { return a.second->sec < b.second->sec; });
        std::vector<Cluster> out;
        for (const auto& m : all) {
            bool join = !out.empty() && m.second->sec - out.back().last <= kVoteTolSec;
            if (join) {
                for (const auto& o : out.back().members) {
                    if (o.first == m.first) { join = false; break; }
                }
            }
            if (!join) {
                out.push_back(Cluster());
                out.back().first = m.second->sec;
            }
            out.back().members.push_back(m);
            out.back().last = m.second->sec;
        }
        return out;
    }

    bool vote(const Cluster& c, Slot& s) const {
        double charW = 0.0, secW = 0.0;
        std::map<int, double> by;
        for (const auto& m : c.members) {
            charW += m.second->w;
            secW += m.second->w * m.second->sec;
            by[m.second->id] += m.second->w;
        }
        if (charW <= 0.0) { return false; }
        const double sec = secW / charW;
        double cover = 0.0;
        for (const Win& w : wins_) {
            if (sec >= w.t0 && sec < w.t1) { cover += edgeWeight(w, sec); }
        }
        if (cover <= 0.0 || charW < 0.5 * cover) { return false; }
        int best = -1;
        double bestW = -1.0;
        for (const auto& [id, wt] : by) {
            if (wt > bestW) { bestW = wt; best = id; }
        }
        s.id = best;
        s.sec = sec;
        s.conf = (float)std::min(1.0, bestW / cover);
        return true;
    }

    std::deque<Win> wins_;
    std::vector<Slot> frozenSlots_;
    double frozenSec_ = -1e18;
    double newestT0_ = -1e18;
};

// Renders slots as text: skips a space after a space, and inserts "  " for a
// pause longer than kVotePauseSec. lastSec/lastSpace carry the state across calls.
inline void renderSlots(const std::vector<VoteText::Slot>& slots, const std::vector<std::string>& tok,
                        ConfText& out, double& lastSec, bool& lastSpace) {
    for (const VoteText::Slot& s : slots) {
        if (s.id <= 0 || s.id >= (int)tok.size()) { continue; }
        const std::string& sym = tok[(size_t)s.id];
        if (sym == " ") {
            if (out.text.empty() || lastSpace) { continue; }
            out.append(" ", s.conf);
            lastSpace = true;
            continue;
        }
        if (lastSec > 0.0 && s.sec - lastSec > kVotePauseSec && !out.text.empty() && !lastSpace) {
            out.append("  ", 1.0f);
        }
        out.append(sym, s.conf);
        lastSec = s.sec;
        lastSpace = false;
    }
}

} // namespace tinycw
