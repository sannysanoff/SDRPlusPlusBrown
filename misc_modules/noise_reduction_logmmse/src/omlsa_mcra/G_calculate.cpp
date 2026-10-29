#include "G_calculate.h"
#include<iostream>
#include"SearchChart.h"
#include"time.h"
#include<algorithm>
#include <sdrpp_export.h>

using namespace std;

#ifdef _WIN32
#define _WINSOCKAPI_    // stops windows.h including winsock.h
#endif
SDRPP_EXPORT char* sdrppResourcesDirectory;

G_calculate::G_calculate()
{
}
short G_calculate::Initialize(int wlen) {
	m_lwlen = wlen ;
	m_linc = m_lwlen >> 1;
	m_linc_move = log(m_linc) / log(2);
	N_wlen1 = m_lwlen + 1;
	m_num_mag = 128;       // 2^7
	m_num_mag_pow = 16384; // 2^14
	m_num_mag_pow2 = 268435456; //2^28
	m_amp_para = 7, m_amp_para_double = 14, m_amp_para_three = 21;  
	m_snpramin = 0.03*m_num_mag_pow;
	beta = 0.7*m_num_mag_pow;
	m_cosen_max = 0.8*m_num_mag_pow;  // %:0.1 : 10
	m_cosen_min = 0.1*m_num_mag_pow;  // :0.1:10  
	m_cosen_pmax = 5 * m_num_mag_pow;			// %ʵ��֤��Ҫ����1.3����ȷ�Ͻ�
	m_cosen_pmin = 1 * m_num_mag_pow;			// %1.0~1.1֮�����
	m_cosen_max_min = log(8) *m_num_mag_pow;
	m_w_global = 8; m_wt_global = 17;
	m_old_cosen_frame = 0;
	m_cosen_peak = m_cosen_pmin;

	m_arr_temp = new int[m_lwlen];
	if (!m_arr_temp) {
		return -19;
	}
	m_S_f = new int[m_lwlen];
	if (!m_S_f) {
		return -20;
	}
	m_abs_Y = new unsigned int[m_lwlen];
	if (!m_abs_Y) {
		return -21;
	}
	m_dataBuf = new short[m_lwlen];
	if (!m_dataBuf) {
		return -22;
	}
	m_init_S = new int[m_lwlen];
	if (!m_init_S) {
		return -23;
	}
	m_Gh1 = new int[m_lwlen];
	if (!m_Gh1) {
		return -24;
	}
	m_q = new int[m_lwlen];
	if (!m_q) {
		return -25;
	}
	m_pp = new int[m_lwlen];
	if (!m_pp) {
		return -26;
	}
	m_G = new int[m_lwlen];
	if (!m_G) {
		return -27;
	}
	m_M = new unsigned int[m_lwlen];
	if (!m_M) {
		return -28;
	}

	m_ns_storage = new short[m_lwlen];
	if (!m_ns_storage) {
		return -30;
	}
	m_cosen_local = new int[m_lwlen];
	if (!m_cosen_local) {
		return -31;
	}
	m_cosen_global = new int[m_lwlen];
	if (!m_cosen_local) {
		return -32;
	}
	m_plocal = new int[m_lwlen];
	if (!m_cosen_local) {
		return -33;
	}
	m_pglobal = new int[m_lwlen];
	if (!m_cosen_local) {
		return -34;
	}
	m_h_global = new short[m_wt_global];
	if (!m_cosen_local) {
		return -35;
	}
	m_old_cosen = new int[m_lwlen];
	if (!m_cosen_local) {
		return -36;
	}
	m_cosen = new int[m_lwlen];
	if (!m_cosen_local) {
		return -37;
	}
	m_lamda_d = new int[N_wlen1];
	if (!m_lamda_d) {
		return -38;
	}
	m_integra = new int[m_lwlen];
	if (!m_integra) {
		return -39;
	}
	m_init_S_min = new int[m_lwlen];
	if (!m_init_S_min) {
		return -40;
	}
	m_init_S_tmp = new int[m_lwlen];
	if (!m_init_S_tmp) {
		return -41;
	}
	m_init_p1 = new int[m_lwlen];
	if (!m_init_p1) {
		return -42;
	}
	m_EN_cos = new short[m_lwlen];
	if (!m_EN_cos) {
		return -43;
	}
	m_EN_sin = new short[m_lwlen];
	if (!m_EN_sin) {
		return -44;
	}
	m_post_SNR = new int[m_lwlen];
	if (!m_post_SNR) {
		return -45;
	}
	m_pr_SNR = new int[m_lwlen];
	if (!m_pr_SNR) {
		return -46;
	}
	m_E_pr_SNR = new int[m_lwlen];
	if (!m_E_pr_SNR) {
		return -47;
	}
	m_v = new int[m_lwlen];
	if (!m_v) {
		return -48;
	}
	
	for (int i = 1; i < (m_wt_global + 1); ++i)  //2^28=268435456 
		m_h_global[i - 1] = (0.5 - 0.5 * cos(2.0 * Pi*(i) / (m_wt_global + 1))) * 16384;//2^14=16384

	for (int i = 0; i < m_lwlen; i++) 
		m_pr_SNR[i] = 0.98*m_num_mag_pow;

	memset(m_old_cosen, 0, sizeof(int)*m_lwlen);
	memset(m_cosen, 0, sizeof(int)*m_lwlen);
	memset(m_dataBuf, 0, sizeof(short) * m_lwlen);
	memset(m_ns_storage, 0, sizeof(short)*m_lwlen);

	//��new���Ŀռ䷵��
	//const char* Fileexpint = "D:/exppow3.pcm";
	//m_int_value = file_read<int>(Fileexpint);  
   //const char* Fileexpsub = "D:/expsub1.pcm";
   //m_expsub_value = file_read<int>(Fileexpsub);



//	const char* FileexpG = "/home/san/Fun/OMLSA-MCRA/OM_LSA/Gvalue2.pcm";
    std::string full = std::string(sdrppResourcesDirectory) + "/cty/oomlsa_gcra_gvalue2.pcm";
#ifdef _WIN32
    std::replace(full.begin(), full.end(), '/', '\\');
    std::string::size_type pos = 0;
    while ((pos = full.find("\\\\", pos)) != std::string::npos) {
        full.replace(pos, 2, "\\");
        pos += 1; // Move to the next position after the replaced slash
    }
#endif

	m_G_value = file_read<int>(full.c_str());
	//cout << sizeof(m_G_value) << sizeof(m_G_value) / sizeof(m_G_value[0]);
	return 0;
}

void G_calculate::NoiseEstimation(int blockInd)
{
	int p;
	int L;
	//����ǰ10֡���ݣ�����L�Լӿ�������ĸ����ٶ�
	if (blockInd >= 10) {
		L = 50;
	}
	else {
		L = 1;
		if (blockInd == 0) {
			for (int i = 0; i <= m_linc; i++) {
				m_init_S[i] = m_abs_Y[i];  //��ȡ��ʼ��ֵ���ʼ��
				m_init_S_min[i] = m_init_S[i];
				m_init_S_tmp[i] = m_init_S[i];
				m_lamda_d[i] = m_init_S[i];
			}
			memset(m_init_p1, 0, sizeof(int)*(m_linc + 1));
		}
	}

	if ((blockInd + 1) % L == 0) {
		for (int k = 0; k <= m_linc; k++) {
			m_init_S_min[k] = min(m_init_S_tmp[k], m_init_S[k]);
			m_init_S_tmp[k] = m_init_S[k];
		}
	}

	for (int k = 0; k <= m_linc; k++)
	{
		m_init_S[k] = (m_init_S[k] >> 1) + (m_init_S[k] >> 2) + (m_init_S[k] >> 4) + (m_abs_Y[k] >> 3) + (m_abs_Y[k] >> 4);
		m_init_S_min[k] = min(m_init_S_min[k], m_init_S[k]);   // ����L�����˾ֲ���С�����ķֱ���
		m_init_S_tmp[k] = min(m_init_S_tmp[k], m_init_S[k]);

		if (m_init_S[k] > (m_init_S_min[k] >> 2) + (m_init_S_min[k] << 1))
			p = m_num_mag_pow;
		else
			p = 0;

		m_init_p1[k] = (m_init_p1[k] >> 2) + (p >> 1) + (p >> 2);
		m_lamda_d[k] = ((__int64)((m_lamda_d[k] >> 1) + (m_lamda_d[k] >> 2) + (m_lamda_d[k] >> 3) + (m_lamda_d[k] >> 4) + (m_abs_Y[k] >> 4))
			*(m_num_mag_pow - m_init_p1[k]) + (__int64)m_lamda_d[k] * m_init_p1[k]) >> m_amp_para_double;
		m_lamda_d[m_lwlen - k] = m_lamda_d[k];
	}
}
void G_calculate::SpeechAbsenceEstm()
{
	unsigned long long sum = 0, a = 0;
	int p_frame, mu, cosen_peak;
	int cosen_frame;

	for (int k = 0; k <= (m_linc + m_w_global); k++) {   //����ļ�����õ�
		m_cosen[k] = (m_old_cosen[k] >> 1) + (m_old_cosen[k] >> 2) + (m_E_pr_SNR[k] >> 2);
	}

	for (int k = 0; k <= m_linc; k++) {
		if (k <= m_w_global - 1) {  // ��һ֡������ͷȥβ
			m_cosen_global[k] = m_cosen[k];
			if (k == 0)
				m_cosen_local[k] = m_cosen[k];
			else
				m_cosen_local[k] = (m_cosen[k - 1] + (m_cosen[k] << 1) + m_cosen[k + 1]) >> 2;
		}
		else {
			a = 0;
			for (int j = 0; j < 2 * m_w_global + 1; j++)
				a = a + ((__int64)m_h_global[j] * m_cosen[k + m_w_global - j]);
			m_cosen_global[k] = ((a >> m_amp_para_double) / (m_w_global + 1));
			m_cosen_local[k] = (m_cosen[k - 1] + (m_cosen[k] << 1) + m_cosen[k + 1]) >> 2;
		}

		if (m_cosen_local[k] <= m_cosen_min)   //% (25)
			m_plocal[k] = 0;
		else if (m_cosen_local[k] >= m_cosen_max)
			m_plocal[k] = m_num_mag_pow; //��ȷ�Ŵ�10000��
		else
			m_plocal[k] = m_num_mag_pow2 * (log(m_cosen_local[k]) - log(m_cosen_min)) / m_cosen_max_min;

		if (m_cosen_global[k] <= m_cosen_min)   //% (25)
			m_pglobal[k] = 0;
		else if (m_cosen_global[k] >= m_cosen_max)
			m_pglobal[k] = m_num_mag_pow;
		else
			m_pglobal[k] = m_num_mag_pow2 * (log(m_cosen_global[k]) - log(m_cosen_min)) / m_cosen_max_min;
		sum += m_cosen[k];
	}

	// Keep frame SNR in Q14 int precision and remember its peak across frames.
	cosen_frame = sum / (m_linc + 1); //10000
	sum = 0;
	if (cosen_frame > m_old_cosen_frame)
		m_cosen_peak = (std::min)((std::max)(cosen_frame, m_cosen_pmin), m_cosen_pmax);
	cosen_peak = m_cosen_peak;//10000

	if (cosen_frame <= (((__int64)cosen_peak * m_cosen_min) >> m_amp_para_double))   // (27)
		mu = 0;
	else if (cosen_frame >= (((__int64)cosen_peak * m_cosen_max) >> m_amp_para_double))
		mu = m_num_mag_pow;
	else
		mu = m_num_mag_pow * log((double)cosen_frame * m_num_mag_pow / cosen_peak / m_cosen_min) / log((double)m_cosen_max / m_cosen_min);

	if (cosen_frame > m_cosen_min)
	{
		if (cosen_frame > m_old_cosen_frame)
			p_frame = m_num_mag_pow;
		else
		{
			p_frame = mu;  //�����õ�  ����������ȼ��˶��ӵ����
		}
	}
	else
	{
		p_frame = 0;
	}
	m_old_cosen_frame = cosen_frame;
	for (int k = 0; k <= m_linc; k++)
	{
		m_q[k] = m_num_mag_pow - ((__int64)m_plocal[k] * m_pglobal[k] * p_frame >> 28);//10000
		m_q[k] = min<int>(m_q[k], 0.95*m_num_mag_pow);
		m_old_cosen[k] = m_cosen[k];
		if (k > 0) m_old_cosen[m_lwlen - k] = m_old_cosen[k];
	}
}
short G_calculate::G_calculate_process(Complex_num* winData, int blockInd) {  // -49
	if (winData == NULL)return -49;

	int post_temp, w = 8;
	for (int i = 0; i <= m_linc + w; i++) {
		m_abs_Y[i] = sqrt(pow(winData[i].real, 2) + pow(winData[i].imag, 2));  //m_abs_Y����2^6��
		m_EN_cos[i] = ((__int64)winData[i].real * 4096) / (1 > m_abs_Y[i] ? 1 : m_abs_Y[i]);
		m_EN_sin[i] = ((__int64)winData[i].imag * 4096) / (1 > m_abs_Y[i] ? 1 : m_abs_Y[i]);
	}
	NoiseEstimation(blockInd);
	for (int i = 0; i <= m_linc; i++) {
		const int lam = std::max<int>(1, (int)(m_lamda_d[i] * m_compensation));
		// Clamp before converting to Q14: narrowing first can wrap a large SNR.
		const double ratio = (double)m_abs_Y[i] / lam;
		m_post_SNR[i] = (int)((std::min)(ratio * ratio, 4096.0) * m_num_mag_pow);
		post_temp = max<int>(m_post_SNR[i] - m_num_mag_pow, 0);
		m_E_pr_SNR[i] = min<int>(max<int>((m_pr_SNR[i] >> 1) + (m_pr_SNR[i] >> 2) + (m_pr_SNR[i] >> 3) + (post_temp >> 3), m_snpramin), 4096 << m_amp_para_double);
		if (i > 0) m_E_pr_SNR[m_lwlen - i] = m_E_pr_SNR[i];                                                // 0.0001 * (2^24=16777216) =1678  167772
		m_v[i] = max<__int64>(min<__int64>((__int64)((__int64)m_E_pr_SNR[i] * m_post_SNR[i] << 10) / (m_num_mag_pow + m_E_pr_SNR[i]), 15 << 24), 1678);
		m_integra[i] = expintpow_solution(m_v[i]);  // ����ֵ�Ŵ�14��  =exp(expint(v)/2)
		m_Gh1[i] = min<int>((__int64)m_E_pr_SNR[i] * m_integra[i] / (m_num_mag_pow + m_E_pr_SNR[i]), 70 << 14);
	}
	SpeechAbsenceEstm();
	for (int i = 0; i <= m_linc; i++) {
		const int lam = std::max<int>(1, (int)(m_lamda_d[i] * m_compensation));
		m_integra[i] = subexp_solution(m_v[i]);  // exp(-(double)m_v[i] / (1 << 24))
		m_arr_temp[i] = m_num_mag_pow + ((__int64)(m_num_mag_pow + m_E_pr_SNR[i]) * m_integra[i] * m_q[i] >> m_amp_para_double) / (m_num_mag_pow - m_q[i]);
		
		/*m_arr_temp[i] = (m_num_mag + ((int)((m_num_mag_pow + m_E_pr_SNR[i]) *m_q[i] * exp(-(double)m_v[i] / m_num_mag_pow)) >> 7)
			/ (m_num_mag_pow - m_q[i]));*/
		m_pp[i] = min(m_num_mag_pow2 / m_arr_temp[i], 1 << 14);
		m_G[i] = Gvalue_solution(m_Gh1[i], m_pp[i]); // Gh1^pp * 0.003^(1-pp)<<14
		//m_G[i] = pow((double)m_Gh1[i] / 16384, (double)m_pp[i] / 16384) * 16384* pow(0.003, (1 - (double)m_pp[i] / 16384));  //��16��14

		m_M[i] = ((__int64)m_G[i] * m_abs_Y[i]) >> m_amp_para_double;  //��ֵ
		winData[i].real = ((__int64)m_M[i] * m_EN_cos[i]) >> 12; 
		winData[i].imag = ((__int64)m_M[i] * m_EN_sin[i]) >> 12;
		const double cleanRatio = (double)m_M[i] / lam;
		m_pr_SNR[i] = (int)((std::min)(cleanRatio * cleanRatio, 4096.0) * m_num_mag_pow);  // 10000
		// DC and Nyquist are self-conjugate; DC has no bin at index N.
		if (i > 0 && i < m_linc) {
			winData[m_lwlen - i].real = winData[i].real;
			winData[m_lwlen - i].imag = -winData[i].imag;
		}
		else {
			winData[i].imag = 0;
		}
		aa[i] = m_G[i];
	}
	
	return 0;
}


G_calculate::~G_calculate()
{
	if (!m_arr_temp) {
		delete[] m_arr_temp;
		m_arr_temp = NULL;
	}
	if (!m_S_f) {
		delete[] m_S_f;
		m_S_f = NULL;
	}

	if (!m_abs_Y) {
		delete[] m_abs_Y;
		m_abs_Y = NULL;
	}
	if (!m_dataBuf) {
		delete[] m_dataBuf;
		m_dataBuf = NULL;
	}
	if (!m_init_S) {
		delete[] m_init_S;
		m_init_S = NULL;
	}
	if (!m_init_S_min) {
		delete[] m_init_S_min;
		m_init_S_min = NULL;
	}
	if (!m_init_S_tmp) {
		delete[] m_init_S_tmp;
		m_init_S_tmp = NULL;
	}
	if (!m_init_p1) {
		delete[] m_init_p1;
		m_init_p1 = NULL;
	}
	if (!m_EN_cos) {
		delete[] m_EN_cos;
		m_EN_cos = NULL;
	}
	if (!m_EN_sin) {
		delete[] m_EN_sin;
		m_EN_sin = NULL;
	}
	if (!m_post_SNR) {
		delete[] m_post_SNR;
		m_post_SNR = NULL;
	}
	if (!m_pr_SNR) {
		delete[] m_pr_SNR;
		m_pr_SNR = NULL;
	}
	if (!m_E_pr_SNR) {
		delete[] m_E_pr_SNR;
		m_E_pr_SNR = NULL;
	}
	if (!m_v) {
		delete[] m_v;
		m_v = NULL;
	}
	if (!m_Gh1) {
		delete[] m_Gh1;
		m_Gh1 = NULL;
	}
	if (!m_q) {
		delete[] m_q;
		m_q = NULL;
	}
	if (!m_pp) {
		delete[] m_pp;
		m_pp = NULL;
	}

	if (!m_G) {
		delete[] m_G;
		m_G = NULL;
	}

	if (!m_M) {
		delete[] m_M;
		m_M = NULL;
	}

	if (!m_ns_storage) {
		delete[] m_ns_storage;
		m_ns_storage = NULL;
	}
	if (!m_cosen_local) {
		delete[] m_cosen_local;
		m_cosen_local = NULL;
	}
	if (!m_cosen_global) {
		delete[] m_cosen_global;
		m_cosen_global = NULL;
	}
	if (!m_plocal) {
		delete[] m_plocal;
		m_plocal = NULL;
	}
	if (!m_pglobal) {
		delete[] m_pglobal;
		m_pglobal = NULL;
	}
	if (!m_h_global) {
		delete[] m_h_global;
		m_h_global = NULL;
	}
	if (!m_cosen) {
		delete[] m_cosen;
		m_cosen = NULL;
	}
	if (!m_old_cosen) {
		delete[] m_old_cosen;
		m_old_cosen = NULL;
	}
	if (!m_int_value) {
		delete[] m_int_value;
		m_int_value = NULL;
	}
	if (!m_expsub_value) {
		delete[] m_expsub_value;
		m_expsub_value = NULL;
	}
	if (!m_G_value) {
		delete[] m_G_value;
		m_G_value = NULL;
	}
	if (!m_lamda_d) {
		delete[] m_lamda_d;
		m_lamda_d = NULL;
	}
}
