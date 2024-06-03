#pragma once

#include <functional>
#include <string>
#include <vector>
#include <cmath>
#include "memory.h"

inline void coalesceSplit(std::vector<std::string> &x) {
    for(int q=((int)x.size())-1; q>=0; q--) {
        if (x[q] == "") {
            x.erase(x.begin()+q);
        }
    }
}

inline void splitString(const std::string & str, const char *sep, const std::function<void(const std::string&)> &callback) {
    const char *c = str.data();
    size_t limit = str.length();
    if (limit == 0) {
        return;
    }
    long long start = 0;
    for (long long i = 0; i < limit; i++) {
        if (i == limit - 1) {
            if (strchr(sep, c[i]) != nullptr) {
                callback(std::string(c + start, i - start));
            } else {
                callback(std::string(c + start, i - start + 1));
            }
            break;
        }
        if (strchr(sep, c[i]) != nullptr) {
            callback(std::string(c + start, i - start));
            start = i + 1;
        }
    }
}



inline void splitStringV(const std::string & str, const char *sep, std::vector<std::string> &dest) {
    dest.clear();
    splitString(str,sep,[&](const std::string &str) {
        dest.emplace_back(str);
    });
}

inline std::string joinStringV(const char *sep, std::vector<std::string> &src) {
    std::string ret;
    int ix = 0;
    for(auto q:src) {
        if (ix > 0){
            ret += sep;
        }
        ret += q;
        ix++;
    }
    return ret;
}


inline void removeSubstrings(std::string& input, const std::string& substring) {
    size_t pos = input.find(substring);
    while (pos != std::string::npos) {
        input.erase(pos, substring.length());
        pos = input.find(substring, pos);
    }
}


inline void replaceSubstrings(std::string& input, const std::string& substring, const std::string& replacement) {
    size_t pos = input.find(substring);
    while (pos != std::string::npos) {
        input.replace(pos, substring.length(), replacement);
        pos = input.find(substring, pos + replacement.length());
    }
}


inline void trimString(std::string & line) {
    while (!line.empty() && std::isspace(line.back())) {
        line.pop_back();
    }
    while (!line.empty() && std::isspace(line.front())) {
        line.erase(line.begin());
    }
}

namespace percentile {


    template<typename T>
    int partition(T *arr, int l, int r)
    {
        int x = arr[r], i = l;
        for (int j = l; j <= r - 1; j++) {
            if (arr[j] <= x) {
                std::swap(arr[i], arr[j]);
                i++;
            }
        }
        std::swap(arr[i], arr[r]);
        return i;
    }

    template<typename T>
    T kthSmallest(T *arr, int l, int r, int k)
    {
        recur:
        // If k is smaller than number of
        // elements in array
        if (k > 0 && k <= r - l + 1) {

            // Partition the array around last
            // element and get position of pivot
            // element in sorted array
            int index = partition(arr, l, r);

            // If position is same as k
            if (index - l == k - 1)
                return arr[index];

            // If position is more, recur
            // for left subarray
            if (index - l > k - 1) {
                r = index - 1;
                goto recur;
            }

            // Else recur for right subarray
            k = k - index + l - 1;
            l = index + 1;
            goto recur;
        }

        // If k is more than number of
        // elements in array
        return arr[0];
    }


    template<typename T>
    T percentile(std::vector<T>& arr, double p) {
        int n = arr.size();
        if (n == 0) {
            return 0;
        }
        double k = (n - 1) * p;
        return kthSmallest(arr.data(), 0, n-1, (int)k);
    }

    template<typename T>
    T percentile_sampling(std::vector<T>& arr, double p) {
        int n = arr.size();
        if (n == 0) {
            return 0;
        }
        int targetPoints = 100;
        T *data = arr.data();
        std::vector<T> sampled;
        if (n > 2 * targetPoints) {
            // sample array
            float step = (n - 1) / (float)targetPoints;
            sampled.resize(targetPoints);
            for(int z=0; z<targetPoints; z++) {
                sampled[z] = data[(int)std::floor(step * z)];
            }
            data = sampled.data();
            n = sampled.size();
        }
        double k = (n - 1) * p;
        return kthSmallest(data, 0, n-1, (int)k);
    }


}