/* The algorithms, source code, look-and-feel of WSJT-X and related
 * programs, and protocol specifications for the modes FSK441, FT8, JT4,
 * JT6M, JT9, JT65, JTMS, QRA64, ISCAT, MSK144, are Copyright © 2001-2017
 * by one or more of the following authors: Joseph Taylor, K1JT; Bill
 * Somerville, G4WJS; Steven Franke, K9AN; Nico Palermo, IV3NWV; Greg Beam,
 * KI7MT; Michael Black, W9MDB; Edson Pereira, PY2SDR; Philip Karn, KA9Q;
 * and other members of the WSJT Development Group.
 *
 * MSHV Q65 Decoder
 * Rewritten into C++ and modified by Hrisimir Hristov, LZ2HV 2015-2021
 * May be used under the terms of the GNU General Public License (GPL)
 */
#ifndef DECODERQ65_H
#define DECODERQ65_H

#include "mshv_support.h"

#include "q65_subs.h"
#include "decoderpom.h"
#include "gen65.h"
#include "gen_q65.h"

#define MAX_NCW 206

class DecoderQ65 
{
public:
    explicit DecoderQ65();
    ~DecoderQ65();
	
	void SetStMultiAnswerMod(bool);
    void SetPeriod(int);
    void SetStDecoderDeep(int);
    void SetStWords(QString,QString,int,int);
    void SetStHisCallGrid(QString c,QString g);
    void SetStDecode(QString,int);
    void SetStQSOProgress(int);
    void AutoClrAvgChanged(bool);
    void AvgDecodeChanged(bool);
    void SetSingleDecQ65(bool);
    void SetClearAvgQ65();
    void SetClearAvgQ65all();
    void SetTxFreq(double);
    void SetDecAftEMEDelay(bool);
    void q65_decode(double *dd,double f0a,double f0b,double fqso,int,bool &hdec);
    void SetStApDecode(bool);
    void SetMaxDrift(bool);

//signals:
    void EmitDecodetText(QStringList) {
        abort();
    }
    void EmitBackColor() {
        abort();
    }
    void EmitAvgSavesQ65(int,int) {
        abort();
    }

private:
	std::shared_ptr<F2a> f2a;
	PomAll pomAll;
    GenQ65 *TGenQ65;
    q65subs q65S;
    
    QString decoded;
    QString s_time;
    int s_ntrperiod;
    int s_ndepth;
    QString s_mycall;
    QString s_hiscall;
    QString s_hisgrid; 
    int ibwa,ibwb;
    double nfa,nfb;
    int npasses;      
    int s_mousebutton; 
    int s_nQSOprogress;      
    bool f_clravg_after_decode; 
    bool f_averaging; 
    bool f_single_decode;
    bool lclearave;
    double nftx;
    bool f_emedelay;
    int s_cont_id;
    int s_cont_type;
    int s_maxiters;
    bool s_lapon;   
    bool f_multi_answer_modq65;       
        
    void q65_clravg(); 
    double s1raw_[800][7000];
    void q65_snr(int *dat4,double dtdec,double f0dec,int mode_q65,double &snr2);//int nused,  
    void PrintMsg(QString tmm,double snrr,double dtt,int dff,QString mss,int frqq,QString,QString,
    				bool &bgc,bool &havdd);  
    bool isgrid4_rr73(QString s);    
    int nhist;
    int nf0[105];
    QString msg[105];
    void q65_hist(int if0,QString msg0,QString &dxcall,QString &dxgrid);//,bool f   
    int ncw; 
    int codewords_[MAX_NCW+5][86];//63   integer codewords(63,MAX_NCW)  
    int codewords_1da[14000];//MAX_NCW*63=12978         
    void q65_set_list(QString,QString,QString);   
    void smo121(double *x,int beg,int nz); // from jt65  
    void q65_symspec(double *iwave,int iz,int jz,double s1_[800][7000]);
    void q65_ccf_85(double s1_[800][7000],int iz,int jz,double nfqso,int ia,int ia2,
					int &ipk,int &jpk,double &f0,double &xdt/*,int &imsg_best*/,double &better/*,double *ccf1*/);	
	int fmaxloc_da_beg_to_end(float*a,int a_beg,int a_end);				
	int imaxval_da_beg_to_end(int*a,int a_beg,int a_end);				
	void q65_bzap(float *s3,int LL);				
	void q65_s1_to_s3(double s1_[800][7000],int iz,int jz,int ipk,int jpk,int LL,
								float *s3_1df);
	void SetArrayBits(int in,int in_bits,bool *ar,int &co);							
	void q65_dec1(float *s3_1fa,int nsubmode,float b90ts,float &esnodb,int &irc,
							int *dat4,QString &decoded);				
	void q65_dec_q3(double s1_[800][7000],int iz,int jz,float *s3_1df,int LL,
							int ipk,int jpk,double &snr2,int *dat4,int &idec,QString &decoded);								
	int ncand;
	double candidates_[2][20];
	int max_drift;	
	bool f_max_drift;
	double drift;
	double f0nd;
	double xdtnd;					
	void q65_ccf_22(double s1_[800][7000],int iz,int jz,double nfqso,int iavg,int &ipk,int &jpk,
							double &f0,double &xdt); /*,double *ccf2*/									
    //void q65_sync_curve(double *ccf1,int ia,int ib,double &rms1);
    int BinToInt32(bool*a,int b_a,int bits_sz);
    void q65_dec2(float *s3_1fa,int nsubmode,float b90ts,float &esnodb,int &irc,int *dat4);
    int apmask[20];//13
    int apsymbols[20];//13
    int apsym0[68];//58
    bool apmask1[80];//78 
    bool apsymbols1[80];//78
    int ncontest0;
    int mcq_q65[30];//29
    void q65_ap(int nQSOprogress,int ipass,int,int,bool lapcqonly,int &iaptype,int *,bool *,bool *);
    void q65_dec_q012(float *s3_1fa,double &snr2,int *dat4,int &idec,int,int,int);
        
    void ana64(double *iwave,int,std::complex<double> *c0);
    void q65apset(QString mycall12,QString hiscall12,int *apsym2);
    void twkfreq(std::complex<double> *c3,std::complex<double> *c4,int npts,double fsample,double *a);
    void spec64(std::complex<double> *c0,int nsps,int jpk,float *s3,int LL,int NN);
    void q65_loops(std::complex<double> *c00,int npts2,int nsps2,int nsubmode,int ndepth,int jpk0,
		double xdt0,double f0,int iaptype,double &xdt1,double &f1,double &snr2,int *dat4,int &idec,bool);
    
    bool lnewdat; 
    int nsmo;   
    int mode_q65;   
    int nsps;
    int istep;
    double df;
    double sync[90];//=85	
    int iseq;
    int LL0,iz0,jz0;
    int lag1,lag2;
    int i0,j0;
    double dtstep;
    int navg[2];//integer navg(0:1)
    double s1a_[2][800][7000];//s1a(iz,jz,0:1) = iz =6666 jz=733
    void q65_dec0(int iavg,double *iwave,double nfqso,
                  bool &lclearave,bool emedelay,double &xdt,double &f0,double &snr1,
                  int *dat4,double &snr2,int &idec,int,int,int,int);
};
#endif