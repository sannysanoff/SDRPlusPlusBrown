#pragma once
#include "base4_fft.h"
#include "head.h"
#include <iostream>
#include <fstream>
#include "Cordic.h"
#include "G_calculate.h"

using namespace std;

class LSA_denoise 
{
public:
	LSA_denoise();
	~LSA_denoise();
	short Initialize(short wlen);
	void setCompensation(float c) { Gc.setCompensation(c); }
	short Denoise_process(short* data_in, short* data_out, int blockInd);
	

private:
	short m_lerr_code = 0;
	G_calculate Gc;
	MY_B4_FFT MyN_fft;
	short m_lwlen;
	short m_linc13, m_linc23;
	int* m_ns_hn;
	Complex_num* m_winData;
};

