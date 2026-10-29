// Standalone DSP regression: run with tests/omlsa_mcra/run.sh <resources-directory>.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#define private public
#include "G_calculate.h"
#undef private
#include "Datablock_Read.h"
#include "utils/flog.h"

char* sdrppResourcesDirectory;
// The standalone test uses the real DSP implementation without the GUI/core library.
namespace flog {
std::string formatString(const char* fmt, const std::vector<std::string>&) { return fmt; }
void __log__(Type, const char*, const std::vector<std::string>&) {}
}

void testSpectrum(int n) {
    G_calculate g;
    assert(g.Initialize(n) == 0);
    assert(g.m_G_value);
    std::vector<Complex_num> spectrum(n + 1, {6400, 0});
    spectrum[n] = {12345, -6789};
    assert(g.G_calculate_process(spectrum.data(), 0) == 0);
    assert(spectrum[n].real == 12345 && spectrum[n].imag == -6789);
    assert(spectrum[0].imag == 0 && spectrum[n / 2].imag == 0);

    // A strong frame must keep its Q14 SNR above the old signed-short limit.
    std::fill_n(g.m_old_cosen, n, 0);
    std::fill_n(g.m_E_pr_SNR, n, 20 * 16384);
    g.SpeechAbsenceEstm();
    assert(g.m_old_cosen_frame == 5 * 16384);
    assert(g.m_cosen_peak == 5 * 16384);
    // On decay the frame probability uses the remembered peak, reducing gain.
    std::fill_n(g.m_E_pr_SNR, n, 0);
    g.SpeechAbsenceEstm();
    assert(g.m_old_cosen_frame == 61440);
    assert(g.m_q[n / 4] > 0 && g.m_q[n / 4] < 15564);

    // Force a tiny frozen noise estimate followed by a large signed spectrum.
    std::fill_n(g.m_lamda_d, n + 1, 1);
    std::fill_n(g.m_init_p1, n, 16384);
    std::fill_n(g.m_init_S, n, 1000000);
    std::fill_n(g.m_init_S_min, n, 1);
    std::fill_n(g.m_init_S_tmp, n, 1);
    std::fill_n(spectrum.data(), n, Complex_num{-1000000, -1000000});
    assert(g.G_calculate_process(spectrum.data(), 11) == 0);
    for (int k = 0; k <= n / 2; ++k) {
        assert(g.m_post_SNR[k] == 4096 * 16384);
        assert(g.m_pr_SNR[k] >= 0 && g.m_pr_SNR[k] <= 4096 * 16384);
        assert(g.m_pp[k] >= 0 && g.m_pp[k] <= 16384);
        if (k > 0 && k < n / 2) {
            assert(spectrum[n - k].real == spectrum[k].real);
            assert(spectrum[n - k].imag == -spectrum[k].imag);
        }
    }
}

void testFrameCounter() {
    Datablock_Read d(48000, 1, 10000);
    assert(d.m_derr_code == 0);
    // Crossing 32767 used to wrap the short counter and restart noise estimation.
    std::vector<short> in(3 * d.m_wlen15, 0), out(in.size() + d.m_wlen, 0);
    int count = 0;
    assert(d.Data_procese(in.data(), out.data(), in.size(), count) == 0);
    d.m_blockInd = 32766;
    assert(d.Data_procese(in.data(), out.data(), in.size(), count) == 0);
    assert(d.m_blockInd > 32767 && count > 0);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    sdrppResourcesDirectory = argv[1];
    testSpectrum(256);
    testSpectrum(1024);
    testFrameCounter();
    std::cout << "OMLSA spectrum, SNR, probability and frame-counter regressions passed\n";
}
