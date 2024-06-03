/* MSHV DecoderMs
 * Copyright 2015 Hrisimir Hristov, LZ2HV
 * May be used under the terms of the GNU General Public License (GPL)
 */
#ifndef DECODERMS_H
#define DECODERMS_H


#include "mshv_support.h"

#include "decoderpom.h"
#include "gen_ft4.h"
#include "gen_ft8.h"
#include "ctm.h"

#include <iostream>
#include <chrono>
#include <functional>
#include "mshv_support.h"

#include "decoderpom.h"
#include "gen_ft4.h"
#include "gen_ft8.h"
#include "ctm.h"

#include <iostream>
#include <chrono>
#include <functional>
#include <atomic>

// #include "../HvMsPlayer/libsound/HvGenFt8/gen_ft8.h"
//#include <QObject> //2.53
#define ALL_MSG_SNR 120 //2.63 from 100 to 120
#define MAXDEC 120
class DecoderFt8
{
    int outCount = 0;
public:
    explicit DecoderFt8(int id, std::shared_ptr<F2a> f2a);
    ~DecoderFt8();
    void SetStMultiAnswerMod(bool f);
    void SetStWords(QString,QString,int,int);
    void SetStHisCall(QString s1);
    void SetStDecode(QString time,int mousebutton,bool);
    void SetStDecoderDeep(int d);
    void SetStApDecode(bool f);
    void SetStQSOProgress(int i);
    void SetStTxFreq(double f);
    void Decode3intFt(bool);//2.39 remm
    void SetNewP(bool);
    //void SetResetPrevT(QString ptime);
    void ft8_decode(double *dd,int c_dd,double f0a,double f0b,double fqso,bool &f,int id3dec,double,double);
    void SetResultsCallback(std::function<void(const char *)> fun) {
        this->resultsCallback = fun;
    }


//signals:
    void EmitDecodedTextFt(QStringList lst);
    void EmitBackColor() {
//        abort();
    }

private:
    int decid;
    std::function<void(const char *)> resultsCallback;

    std::shared_ptr<F2a> f2a;
    PomAll pomAll;
    PomFt pomFt;
    GenFt8 *TGenFt8;
    double DEC_SAMPLE_RATE;
    double twopi;
    double pi;

    bool f_new_p;
    //QString s_time8_prev;
    int s_ndecodes;
    QString allmessages[ALL_MSG_SNR+20];
    //int allsnrs[ALL_MSG_SNR+20];
    double f1_save[ALL_MSG_SNR+20];
    double xdt_save[ALL_MSG_SNR+20];
    int itone_save[ALL_MSG_SNR+20][100];
    bool lsubtracted[ALL_MSG_SNR+20];
    int s_cou_dd1;
    double dd1[182600];

    bool first_sync8d;
    std::complex<double> csync_ft8_2[7][32];
    void sync8d(std::complex<double> *cd0,int i0,std::complex<double> *ctwk,int itwk,double &sync);

    bool first_ft8_downsample;
    double taper_ft8_ds[120];
    std::complex<double> cx_ft8[192100];//2.09 ->error //96000+100   //(0:NFFT1/2)  NFFT1=192000 // 96000
    void ft8_downsample(double *dd0,bool &newdat,double f1,std::complex<double> *cd0);

    double pulse_ft8_rx[5770];          //    !1920*3=5760
    std::complex<double> ctab8_[65536+10];
    void gen_ft8cwaveRx(int *i4tone,double f_tx,std::complex<double> *cwave);

    bool first_subsft8;
    std::complex<double> cw_subsft8[180300];//180000+300  15*12000
    double endcorrectionft8[2200]; //(NFILT/2+1)  NFILT=4000; 4000/2+1=2001
    double BestIdtft8(double *dd,double f0,double dt,double idt,std::complex<double> *cref,
                      std::complex<double> *cfilt,std::complex<double> *cw_subs,double *endcorr,
                      double *xdd,std::complex<double> *cx);
    void subtractft8(double *dd,int *itone,double f0,double dt,bool refdt);

    bool first_ft8b_2;
    int mcq_ft8_2[29];
    /*int mcqfd_ft8_2[29];
    int mcqru_ft8_2[29];
    int mcqtest_ft8_2[29];
    int mcqww_ft8_2[29];
    int mcqbu_ft8_2[29];
    int mcqft_ft8_2[29];*/
    int mrrr_ft8_2[19];
    int m73_ft8_2[19];
    int mrr73_ft8_2[19];
    int cont_id0_ft8_2;
    bool one_ft8_2[9][512];//(0:511,0:8);
    QString hiscall12_0_ft8_2;
    bool ft8_downs_sync_bmet(double *,bool ap7,bool &,double &,double &,int &,int &,double s8_[79][8],
                             double *,double *,double *,double *);
    void ft8b(double *dd,bool &newdat,int nQSOProgress,double nfqso,double nftx,int ndepth,bool n4pas3int,bool lapon,
              double napwid,bool lsubtract,bool nagain,int cid,int cty,int &iaptype,double &f1,double &xdt,
              double xbase,int *apsym,int &nharderrors,double &dmin,int &nbadcrc,QString &message,
              double &xsnr,QString hiscall12,int *it);

    void baseline(double *s,int nfa,int nfb,double *sbase);
    bool first_ft8sbl;
    double window_ft8sbl[3890];    //NFFT1=2*NSPS NSPS=1920 1920*2=3840
    void get_spectrum_baseline(double *dd,int nfa,int nfb,double *sbase);
    void sync8(double *dd,double nfa,double nfb,double syncmin,double nfqso,double s_[402][1970],double candidate[2][620],int &ncand,double *sbase);
    void ft8apset(QString mycall12,QString hiscall12,int *apsym2);//int &iaptype ,QString hisgrid6,bool bcontest,QString mygrid6,

    void PrintMsg(QString,int,double,double,QString,int,float,float,bool &,bool);
    QString nutc0;
    int c_zerop;
    QString msg0[2][2][MAXDEC+20];
    double dt0[2][2][MAXDEC+20];
    double f0[2][2][MAXDEC+20];
    int ndec[2][2];
    int jseq;
    bool isgrid4(QString);
    int ft8_even_odd(QString);
    void ft8_a7_save(QString,double,double,QString);
    void ft8_a7d(double *dd0,bool &newdat,QString call_1,QString call_2,QString grid4,
                 double &xdt,double &f1,double xbase,int &nharderrors,double &,QString &msg37,double &xsnr);

};

//#include "../HvMsPlayer/libsound/HvGenFt4/gen_ft4.h"
class DecoderFt4
{

    int outCount;

public:
    explicit DecoderFt4(int id, std::shared_ptr<F2a> f2a);
    ~DecoderFt4();
    void SetStTxFreq(double f);
    void SetStMultiAnswerMod(bool f);
    void SetStDecoderDeep(int d);
    void SetStApDecode(bool f);// only in mshv
    void SetStQSOProgress(int i);
    void SetStDecode(QString time,int mousebutton);
    void SetStWords(QString,QString,int,int);
    void SetStHisCall(QString c);
    void SetMAMCalls(QStringList ls);
    //void SetNewP(bool);
    //void SetResetPrevT(QString ptime);
    void ft4_decode(double *dd,double f0a,double f0b,double,double,double fqso,bool &f);
    void SetResultsCallback(std::function<void(const char *)> fun) {
        this->resultsCallback = fun;
    }


    //signals:
    void EmitDecodedTextFt(QStringList lst) {
        char buf[1000] ="";
        snprintf(buf+strlen(buf), sizeof(buf)-strlen(buf), "FT4_OUT\t%lld\t%02d", currentTimeMillis(), outCount++);
        for(int i=0; i<lst.count(); i++) {
            snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "\t{%d}\t%s", i, lst[i].str->c_str());
            //        std::cout << "{" << i << "}" << lst[i].str->c_str() << " ";
        }
        strcat(buf,"\n");
        // fwrite(buf, 1, strlen(buf), stdout);
        // fflush(stdout);
        decodeResultOutput(buf);
        if (resultsCallback) {
            resultsCallback(buf);
        }
    }
    void EmitBackColor() {
        //
    }

private:
    int decid;
    std::shared_ptr<F2a> f2a;
    PomAll pomAll;
    PomFt pomFt;
    GenFt4 *TGenFt4;
    double DEC_SAMPLE_RATE;
    double twopi;
    double pi;

    //bool f_new_p;
    void dshift1(double *a,int cou_a,int ish);//???

    bool first_ft4_ds;
    std::complex<double> cx_ft4_ds[80000];     //(0:NMAX/2)=36288   31104                 [NMAX] (NMAX=21*3456)=72576
    double window_ft4_ds[4096]; //(0:NFFT2-1) (NFFT2=NMAX/18)=4032     (0:NFFT2-1) (NFFT2=NMAX/16)=3888
    void ft4_downsample(double *dd,bool newdata,double f0,std::complex<double> *c);

    //void nuttal_window(double *win,int n);
    void ft4_baseline(double *s,int nfa,int nfb,double *sbase);

    bool first_ft4detcad;
    double window_ft4[2314];    //2304;//NFFT1=2048;
    void getcandidates4(double *dd,double fa,double fb,double,double,double syncmin,double nfqso,
                        int maxcand,double candidate[2][115],int &ncand);

    bool first_ft4_sync4d;
    std::complex<double> csynca_ft4_sync[70];//(2*NSS) 2*32=64
    std::complex<double> csyncb_ft4_sync[70];
    std::complex<double> csyncc_ft4_sync[70];
    std::complex<double> csyncd_ft4_sync[70];
    void sync4d(std::complex<double> *cd0,int i0,std::complex<double> *ctwk,int itwk,double &sync);

    double pulse_ft4_rx[1748];          //576*3=1728    !512*3=1536
    void gen_ft4cwaveRx(int *i4tone,double f_tx,std::complex<double> *cwave);

    bool first_subsft4;
    std::complex<double> cw_subsft4[72800];//72576   =62208
    void subtractft4(double *dd,int *itone,double f0,double dt);

    int count_eq_bits(bool *a,int b_a,bool *b,int c);

    bool first_ft4bm;
    bool one_ft4_2[8][256];//(0:255,0:7)
    void get_ft4_bitmetrics(std::complex<double> *cd,double bitmetrics_[3][220],bool &badsync);//2*NN=206

    bool first_ft4d;
    int mrrr_ft4[19];
    int m73_ft4[19];
    int mrr73_ft4[19];
    int mcq_ft4[29];
    /*int mcqru_ft4[29];
    int mcqfd_ft4[29];
    int mcqtest_ft4[29];
    int mcqww_ft4[29];
    int mcqbu_ft4[29];
    int mcqft_ft4[29];*/
    int cont_id0_ft4_2;
    QString mycall0_ft4;
    QString hiscall0_ft4;
    double fac_ft4_sync;
    std::complex<double> ctwk2_ft4_[41][70]; //ctwk2(2*NSS,-16:16) 2*32=64
    int apbits_ft4[174];//174
    int apmy_ru_ft4[28];
    int aphis_fd_ft4[28];

    std::function<void(const char *)> resultsCallback;
};

//#include <QObject>
//#include <QStringList>
//#include <QElapsedTimer>
#include <math.h>	//los
// #include <unistd.h> //usleep x86_64 pthread.h
/*
#include "../config.h"
#if defined _WIN32_
#include "../Hv_Lib_DirectX90c/dsound.h"
#endif
*/
//#include "../HvMsPlayer/libsound/HvGenMsk/genmesage_msk.h"
//#include "../HvMsPlayer/libsound/HvGen65/gen65.h"

#define STATIC_DAT_COUNT 12000*90 //1.35 jt65abc 90sec  max 60sec static dufer for decode thread
#define RECENT_CALLS_COU 6
#define HASH_CALLS_COUNT 36 //for any case manualy HV-> 6 reserved (6 + (RECENT_CALLS_COU*(RECENT_CALLS_COU-1)))
#define RECENT_SHMSGS_COU 50
#define MAX_DISP_DEC_COU 40      //max 40 labels for 30s and display_ms.h
#define MAX_RTD_DISP_DEC_COU 140 //max 140 lines for 30s and display_ms.h
#include "decoderq65.h"
#include "gen65.h"
#include "genmesage_msk.h"

class DecoderMs
{

public:
    DecoderMs();
    ~DecoderMs();

    void setMode(int);
    //void ResetCalsHashFileOpen(); //msk40 za po natatak ako ima oplakwane 1.31
    void SetCalsHashFileOpen(QString);
    void SetDecoderDeep(int);//1-fast 2-normal 3-deep
    ////jt65abc/////
    void SetMaxCandidats65(int max_cand);
    ////jt65abc/////
    ///  FT8 JT65 ////////////////////////
    void SetApDecode(bool);
    void SetVhfUhfFeatures(bool f);
    void DeepSearchChanged(bool);
    void AvgDecodeChanged(bool);
    void SetPerodTime(int);
    void SetMultiAnswerMod(bool);//for AP MAM ft8 2.03
    void SetThrLevel(int);
    void SetMsk144RxEqual(int);

//public slots:
    void Decode3intFt(bool);//2.39 remm
    void SetZapData(short*dat, int count);
    void SetDecode(short*,int,QString,int t_istart,int mousebutton,bool f_rtd,bool end_rtd,bool fopen);//1.27 psk rep   fopen bool true    false no file open
    void SetDfSdb(int sdb,int df);
    void SetWords(QStringList,int,int);
    void SetZap(bool f);
    void SetNexColor(bool);
    ///JTMSK SHORT////
    void SetCalsHash(QStringList);
    ///JTMSK SHORT////
    void SetShOpt(bool f);
    void SetSwlOpt(bool f);
    void SetResultsCallback(std::function<void(const char *)> fun);
    //void SetMyGridMsk144ContM(QString,bool);//for " R " in msg 1.31
    //void SetMsk144RxEqual(int);
    ///  JT56ABC  ////////////////////////
    //void AvgDecodeChanged(bool);
    //void DeepSearchChanged(bool);
    void SetClearAvg65();
    void SetRxFreqF0F1(double,double,double);
    //void SetVhfUhfFeatures(bool f);
    void SetAggresLevFtd(int val);
    void SetAggresLevDeepS(int val);
    ///  END JT56ABC  ////////////////////////
    void SetClearAvgPi4();
    void SetTxFreq(double);
    void Set65DeepSearchDb(QStringList);

    ///  FT8 and JT65 ////////////////////////
    void SetQSOProgress(int);
    void SetMAMCalls(QStringList);//2.00 mam hash
    void AutoClrAvgChanged(bool f);
    void SetSingleDecQ65(bool);
    void SetClearAvgQ65();
    void SetDecAftEMEDelay(bool);
    void SetMaxDrift(bool);
    //void SetMultiAnswerMod(bool);//for AP MAM ft8 2.03
    ///  END FT8  ////////////////////////

//signals:
    void EmitDecodetText(QStringList,bool,bool){
        abort();
    } //1.27 psk rep   fopen bool true    false no file open
    void EmitDecode(bool,int dec_state){
//        abort();
    };//dec_state no=0 dec=1 rtddec=2
    void EmitBackColor(bool){
        abort();
    };
    void EmitDecLinesPosToDisplay(int count,double pos,double pos_ping,QString p_time) { // 1.28 p_time for identif perood
        abort();
    }
    void EmitDecodeInProgresPskRep(bool) {};
    void EmitAvgSaves(int,int,int,int) {abort();};
    void EmitAvgSavesPi4(int,int) {abort();};
    void EmitAvgSavesQ65(int,int) {abort();};
    void EmitDecodedTextRxFreq(QStringList,bool,bool) {abort();};
    void EmitTimeElapsed(float) {abort();};//2.33
    bool IsWorking();

//private slots:
    //void SetDecodetTextFt(QStringList);
    //void SetDecodetTextQ65(QStringList);//bool
    void SetDecodetTextFtQ65(QStringList);
    void ThrSetBackColor();
    void SetBackColorQ65();

private:
    QString dup_amsgs_thr[140];
    int dup_afs_thr[140];
    int dup_camsgf_thr;
    void ResetDupThr();

    int s_thr_used;
    bool is_thrTime;
    void CreateStartTimerthr();
//    QElapsedTimer *thrTime;
    std::shared_ptr<F2a> f2a;
    PomAll pomAll;
    //PomFt pomFt;
    void EndRtdPeriod();
    bool s_fopen;
    int rtd_dupe_cou;
    double rtd_dupe_pos;

    int s_decoder_deep;//1-fast 2-normal 3-deep
    double DEC_SAMPLE_RATE;
    bool s_nzap;
    double twopi;
    double pi;
    int s_mod;
    int G_MinSigdB;
    int G_DfTolerance;
    bool G_ShOpt;
    bool G_SwlOpt;
    double a1_;
    double a2_;
    double a3_;
    double a4_;
    double s_basevb;
    QString s_MyCall;// in SetWords decoderms
    QString s_MyBaseCall;// in SetWords decoderms
    QString CharToQString(char*,int count);
    QString FormatFoldMsg(QString);
    QString AlignMsgSpecWord(QString msg, QString word, bool &f_align);
    QString RemBegEndWSpaces(QString);
    QString RemWSpacesInside(QString);
    QString FormatLongMsg(QString,int);
    void analytic(double*,int,int,int,double*,std::complex<double>*);
    void xfft(std::complex<double> *c,double *d,int nfft);
    void ssort(double *x,double *y,int n,int kflag);
    void sort(int nmax,double *tmp);
    double pctile(double *x,int begin_x,double *tmp,int nmax,double npct);
    void tweak1(std::complex<double> *ca,int jz,double f0,std::complex<double> *cb);
    void smooth(double *x,int nz);

    int ping(double *s,int nz,double dtbuf,int slim,double wmin,double pingdat_[100][3]);
    void indexx(int n,double *arr,int *indx);
    void set_double_to_d(double a,double*y,int n);
    void spec2d(double *data, int jz, double &sigma);
    void wsjt1_mtdecode(double*,int,bool);
    void bzap(double*dat,int jz,int nadd,int mode,double*fzap);
    //double gran(int iset);
    void dtrim(short*,int);
    void zero_int_beg_end(int*,int begin,int end);
    void add_da_da2_da(double *a,double b_[324][558],int b_beg,int b_row,double *c,int n);
    void flatten(double s2_[324][558],int nbins,int jz,double *psa,double *ref,double *birdie,double *variance);
    void move_da_to_da2(double *x,double y_[324][558],int b,int k,int n);
    void ps(double *dat,int dat_begin,int nfft,double*s);
    void avesp2(double *dat,int jza,int nadd,int mode,bool NFreeze,int MouseDF,int DFTolerance,double *fzap);
    void move_da_to_da(double*x,int begin_x,double*y,int begin_y,int n);
    bool f_back_color;
    void SetBackColor();
    //double s2[558][324];

    GenMsk *TGenMsk;
    /// MSK144 ///////////////
    //double pcoeffs_msk144_dec[3];
    bool first_dec_msk;
    //QString My_Grid_Loc;  // in SetMyGridMsk144ContM decodermsk144   //for " R " in msg 1.31
    char s_msk144_2s8[8];
    double pp_msk144[12];
    double rcw_msk144[12];
    std::complex<double> cb_msk144[42];
    bool f_first_msk144;
    double dt_msk144;
    double fs_msk144;
    //double df_msk144; //problem zavisi ot NFFT a toi se promenia
    void first_msk144();
    int ihlo_msk144;
    int ihhi_msk144;
    int illo_msk144;
    int ilhi_msk144;
    int i2000_msk144;
    int i4000_msk144;
    int last_ntol_msk144;
    double last_df_msk144;
    void dftool_msk144(int ntol, double nrxfreq,double fd);
    void cshift2(std::complex<double> *a,std::complex<double> *b,int cou,int ish);//HV for save vareable b in orginal and out is a
    void mplay_dca_dca_dca(std::complex<double> *a,int a_beg,int a_end,std::complex<double> *b,int b_beg,std::complex<double> *mp,int mp_b,int ord);
    void mplay_dca_dca_da(std::complex<double> *a,int a_beg,int a_end,std::complex<double> *b,int b_beg,double *mp,int mp_b,int ord);
    void mplay_da_da_i(double *a,int b_a,int e_a,double *b,int b_b,int mp);
    //void mplay_da_absdca_absdca(double *a,int b_a,int e_a,std::complex<double> *b,int b_b,std::complex<double> *mp,int mp_b);
    void mplay_da_absdca_absdca(double *a,int a_c,std::complex<double> *b,std::complex<double> *mp);
    void sum_dca_dca_dca(std::complex<double> *a,int a_cou,std::complex<double> *b,std::complex<double> *c);
    //void copy_int_ar(int*a,int a_beg,int a_odd,int*b,int b_beg,int b_end);
    void copy_double_ar_ainc(double*a,int a_beg,int a_inc,double*b,int b_beg,int b_end);
    void copy_dca_or_sum_max3dca(std::complex<double> *a,int a_cou, std::complex<double> *b, int b_beg,
                                 std::complex<double> *c=0, int c_beg=-1, std::complex<double> *d=0, int d_beg=-1);
    double sum_da(double*a,int a_beg,int a_end);
    int sum_ia(int*a,int a_beg,int a_end);
    //void set_ba(bool *a,int a_beg,int a_end,bool f);
    int maxloc_absdca_beg_to_end(std::complex<double>*a,int a_beg,int a_end);
    //int maxloc_da_end_to_beg(double*a,int a_beg,int a_end);
    //int maxloc_abs_dca(double  complex *a,int a_beg,int a_end);
    QString extractmessage144(char *decoded,int &nhashflag,char &ident);
    void msk144decodeframe_p(std::complex<double> *c,double *softbits,QString &msgreceived,int &nsuccess,char &ident,double phase0);
    void msk144decodeframe(std::complex<double> *c,double *softbits,QString &msg, int &nsuccess,char &ident,bool f_phase);
    ///rtd msk144/////
    bool s_f_rtd;
    bool s_end_rtd;
    //double tframe_msk144;
    double tsec0_rtd_msk;
    bool first_rtd_msk;
    double pnoise_rtd_msk;
    QString s_time_last;
    int s_nsnrlast;
    QString s_msglast;
    int s_nsnrlastswl;
    QString s_msglastswl;
    std::complex<double> dot_product_dca_dca(std::complex<double> *a,int b_a,std::complex<double> *b,int b_b,int count);
    std::complex<double> dot_product_dca_sum_dca_dca(std::complex<double> *a,int a_b,int b_b,std::complex<double> *c,int c_count);
    void msk144sync(std::complex<double> *cdat,int nframes,int ntol,double delf,int *navmask,int npeaks,double fc,double &fest,int *npklocs,int &nsuccess,std::complex<double> *c);
    void msk144spd(std::complex<double> *cdat,int np,int &nsuccess,QString &msgreceived,double fc,double &fest,double &tdec,char &ident,int &navg,std::complex<double> *ct,double *softbits);
    int navg_sq;
    std::complex<double> cross_avg_sq[864];
    double wt_avg_sq;
    double tlast_sq;
    QString trained_dxcall_sq;
    QString training_dxcall_sq;
    bool currently_training_sq;
    bool first_sq;
    QString s_HisCall;// in SetCalsHash decodermsk40
    void msk144signalquality(std::complex<double> *cframe,double snr,double freq,double t0,double *softbits,QString msg,
                             QString dxcall,int &nbiterrors,double &eyeopening,bool &trained,double *pcoeffs,bool f_calc_pcoeffs);
    double s_pcoeffs_msk144[3];
    bool s_trained_msk144;
    QStringList s_list_rpt_msk;
    double prev_pt_msk;
    double ping_width_msk;
    double prev_ping_t_msk;
    double last_rpt_snr_msk;
    bool one_end_ping_msk;
    bool is_new_rpt_msk;
    bool ss_msk144ms;
    QString GetStandardRPT(double width, double peak);
    void print_rtd_decode_text(QString msg,QString &smsg,int in_snr,int &s_snr,double ts,double fest,
                               double t0,int navg,int ncorrected,double eyeopening,char ident);
    QString str_round_20ms(double v);
    void msk_144_40_rtd(double *d2,int n,double s_istart,bool);
    void msk144_freq_search(std::complex<double> *cdat,double fc,int if1,int if2,double delf,int nframes,
                            int *navmask,double &xmax,double &bestf,std::complex<double> *cs,double *xccs);/*std::complex<double> *cdat2,*/
    ///rtd msk144 end/////
    double MskPingDuration(double *detmet_dur,int istp_real,int il,double level,int nstepsize,int nspm,double dt);
    void SetDecodetTextMsk2DL(QStringList);//2.46
    void detectmsk144(std::complex<double> *cdat,int npts,double s_istart,int &nmessages);
    //double maxval_da_beg_to_end(double*a,int a_beg,int a_end);
    void opdetmsk144(std::complex<double> *cdat,int npts,double s_istart,int &nmessages);
    /////////////////////////////////

    //double h_msk144[1024*1024];
    //int nfft0_msk144;
    //void analytic_msk144(double *d,int d_count_begin,int npts,int nfft,std::complex<double> *c);
    // new analytic /////////////////////////////
    bool s_msk144rxequal_s;
    bool s_msk144rxequal_d;
    std::complex<double> h_msk144_2[524500];  //new analytic 1024*1024/2 HV need for auto decode 30s=524288
    std::complex<double> s_corrs[524500];     //vazno hv 1.31 HV need for auto decode 30s=524288
    std::complex<double> s_corrd[524500];     //vazno hv 1.31 HV need for auto decode 30s=524288
    int nfft0_msk144_2;//new analytic
    double dpclast_msk144_2[3];
    //double spclast_msk144_2[3];
    //double saclast_msk144_2[5];
    bool any_not_and_save_in_a(double *a,double *b,int c);
    void analytic_msk144_2_init_s_corrs_full();
    void analytic_msk144_2(double *d,int d_count_begin,int npts,int nfft,std::complex<double> *c,double *dpc,bool bseq,bool bdeq);
    // end new analytic ////////////////////////////////
    void msk_144_40_decode(double *dat,int npts_in,double s_istart,bool);
    /// MSK144 end///////////////

    //double minval_da_beg_to_end(double*a,int a_beg,int a_end);
    /// MSK40 ///////////////
    typedef struct
    {
        int      hash;
        QString  calls;
    }
    call_;
    call_ hash_msk40_calls[HASH_CALLS_COUNT];
    QString recent_calls[RECENT_CALLS_COU];
    void update_recent_calls(QString call);
    bool check_hash_msk40(int hash_in,int rpt,QString &msg);
    //bool isValidCallsign(QString callsign);//2.00
    //QString FindBaseFullCallRemAllSlash(QString str);//2.00
    void hash_msk40_all_calls(int id, QString calls);
    bool check_hash_msk40_swl(int hash_in,int rpt,QString &msg);
    void hash_msk40_swl();
    QString recent_shmsgs[RECENT_SHMSGS_COU];
    bool update_recent_shmsgs(QString message);
    /*typedef struct
    {
        call_ hash_calls[6];
    }
    call_h40; 
    call_h40 hash_msk40_rpt[32];*/

    //int ig_msk32_[4096][24];  //integer ig(0:23,0:4095)
    //int nhashes_msk32[32];
    char s_msk40_2s8r[8];
    double pp_msk40[12];
    double rcw_msk40[12];
    std::complex<double> cbr_msk40[42];
    bool f_first_msk40;
    double dt_msk40;
    double fs_msk40;
    //double df_msk40; //problem zavisi ot NFFT a toi se promenia
    void first_msk40();
    int ihlo_msk40;
    int ihhi_msk40;
    int illo_msk40;
    int ilhi_msk40;
    int i2000_msk40;
    int i4000_msk40;
    int last_ntol_msk40;
    double last_df_msk40;
    void dftool_msk40(int ntol, double nrxfreq,double df);
    //int minloc_da_beg_to_end(double*a,int a_beg,int a_end);
    void detectmsk40(std::complex<double> *cdat,int npts,double s_istart);
    ///rtd MSK40///////////////
    void msk40decodeframe_p(std::complex<double> *c,double *softbits,double xsnr,QString &msgreceived,int &nsuccess,char &ident,double phase0);
    void msk40decodeframe(std::complex<double> *ct,double *softbits,double xsnr,QString &msg, int &nsuccess,char &ident,bool f_phase); //hv
    void msk40sync(std::complex<double> *cdat,int nframes,int ntol,double delf,int *navmask,int npeaks,double fc,double &fest,int *npklocs,int &nsuccess,std::complex<double> *c);
    void msk40spd(std::complex<double> *cdat,int np,int &nsuccess,QString &msgreceived,double fc,double &fest,double &tdec,char &ident,int &navg,std::complex<double> *ct,double *softbits);
    void msk40_freq_search(std::complex<double> *cdat,double fc,int if1,int if2,double delf,int nframes,
                           int *navmask,double &xmax,double &bestf,std::complex<double> *cs,double *xccs);//std::complex<double> *cdat2,
    ///rtd MSK40///////////////
    /// MSK40 end///////////////

    /// JTMSK ///////////////
    /// JTMS ///////////////
    double jtms_dfx;
    std::complex<double> cwb_jtms[56];
    std::complex<double> cw_jtms_[64][56];
    void setupms();
    double dot_product_da_da(double *a, double *b,int size,int offset_b);
    void hipass(double*y,int y_begin,int npts,int nwidth);
    double msdf(std::complex<double> *,int,int,int,double,int,int,int);
    int syncms(std::complex<double> *cdat,int npts,std::complex<double> *cwb,double *r);
    int lenms(double *r,int npts);
    void decodems(std::complex<double> *cdat,int npts,std::complex<double> cww_[64][56],int i1,int nchar,double s2_[400][64],char*msg);
    void foldms(double s2_[400][64],int msglen,int nchar,char *msg);
    void jtms(double*,int c_begin,int,int DFTol,double,int,int,int,bool pick,bool &f_only_one_color,int &disp_lines_dec_cou);
    /// JTMS ///////////////
    /// FSK441 ///////////////
    int only1_s_mode;
    short itone_s_fsk[84];
    int ndits_s;
    int noffset_fsk441_dfx;
    std::complex<double> cfrag_s[2100];
    double spec441(double*raw_in,int raw_in_c_begin,int count_in,double*ps);
    QString longx(double*raw_in,int raw_begin,int count_in,double*ps,int DFTol,int &msglen,double bauderr,int mode);
    double max_4double(double,double,double,double);
    int min_3int(int,int,int);
    void detect(double*data,int data_begin,int npts,double f,double*y,int mode);
    int sync(double*y1,double*y2,double*y3,double*y4,int npts,/*double baud,double &bauderr,*/int mode);
    double max_3double(double,double,double);
    int abc441(char*msg,int count_msg,short *itone);
    void gen441(short *itone,int ndits,std::complex<double> *cfrag,int mode);
    void smo(double*x,int x_begin,int npts,double*y,double nadd);
    int chk441(double *dat,int jz,double tstart,double width,int nfreeze,int mousedf,
               int dftolerance,bool pick,int mode,double &dfx_real_hv);
    /// FSK441 /////////////////
    ///// JT6M /////////////////////////////////
    //std::complex<double> c[1024*1024];
    void syncf0(double *data,int jz,int NFreeze,int NTol,int &jstart,double &f0);
    void add_da_da_to_da(double *a,double *b,double *c,int n);
    void add_da2_da_to_da2(double a_[6][128],double *b,double c_[6][128],int begin,int row,int n);
    void add_da_da2_to_da(double *a,double b_[646][44],int b_row,double *c,int n);
    void add_da2_da2_to_da2(double a_[646][44],int j,double b_[53][44],int k,double c_[53][44],int kk,int n);// hv v1.01 23 to 53
    void move_da2_to_da2(double x_[646][44],int j,double y_[646][44],int k,int n);
    void synct(double *data,int jz,int &jstart,double f0);
    void syncf1(double*data,int jz,int jstart,double &f0,int NFreeze,int DFTol,double*red);
    double max_2double(double,double);
    void decode6m(double *data,int d_start,int jz,int minSigdb,int NFixLen,double f0);//int npkept,double*yellow);
    void avemsg6m(double s2db_[646][44],int nz,int nslim,int NFixLen,double f0,bool);//,int npkept
    void wsjt1_jt6m(double*,int,double);
    ///// JT6M /////////////////////////////////
    ///// ISCAT /////////////////////////////////
    //double s0_[5601][289];
    //double s01_[289][5601];
    //double savg[289];
    int ana932(double *dat,int jz,std::complex<double> *cdat);
    void synciscat(std::complex<double>*cdat,int npts,double s0_[5601][289],int &jsym,int DFTolerance,
                   int mode4,double &xsync,double &sig,int &ndf0,int &msglen,
                   int &ipk,int &jpk,int &idf);
    /*int mousebutton,int nafc,double &df1*/
    void iscat(std::complex<double>*cdat0,int npts0,double t2,bool pick,int MinSigdB,int DFTolerance,int mode4);
    /*int mousebutton,int nafc,int nmore*/
    void wsjt1_iscat(double *dat,int count,int mode4,bool pick);
    ///// ISCAT /////////////////////////////////

    ///// JT65ABC /////////////////////////////////
    //QString App_Path;
    int s_aggres_lev_ftd;
    int s_aggres_lev_deeps;
    QStringList db_call_loc4_list;
    double s_nfqso_all;
    double s_f00;
    double s_f01;
    bool clearave_jt65;
    bool s_avg_jt65;
    bool s_deep_search_jt65;
    //void SetDepthAvg65(int);
    Gen65 *TGen65;
    double width_jt65;
    int nsave_jt65;
    bool first_symspec65;
    double thresh0_jt65;
    double ref_jt65[3413];
    double dfref_jt65;
    double w_symspec65[8192];
    double s1_jt65_[126][512+40];
    int s1_ofs_jt65;
    double s3a_jt65_[63][64];
    int mrs_jt65[63];
    int mrs2_jt65[63];
    int param_jt65[10]; // integer param(0:9)
    int nsum_jt65;
    int s_max65_cand_for_dec;

    /// avg ////
    bool first_avg65;
    int iutc_jt65[64];
    int nfsave_jt65[64];
    double dtdiff_jt65;
    double s1save_jt65_[64][126][512+40];  //double s1save(-255:256,126,MAXAVE)
    double s3save_jt65_[64][63][64]; //real s3save(64,63,MAXAVE)
    double syncsave_jt65[64];
    double dtsave_jt65[64];
    int nflipsave_jt65[64];
    char cused_jt65[64];
    int iused_jt65[64];
    char csync_jt65;
    int count_saved_avgs_1jt65;
    int count_saved_avgs_2jt65;
    int count_use_avgs_1jt65;
    int count_use_avgs_2jt65;
    //int count_saved_avgs_jt65;
    /// end avg ///

    typedef struct
    {
        double freq;
        double dt;
        double sync;
        double flip;
    }
    candidate_jt65;

    std::complex<double> cw_jt65[721000];//  60*12000+40
    bool first_subtract65;
    int correct_jt65[63];
    bool s_bVHF_jt65;
    //AP jt65
    QString mycall0_jt65ap;
    QString hiscall0_jt65ap;
    QString hisgrid0_jt65ap;
    int apsymbols_jt65[8][14];
    void hint65(double s3_[63][64],int nadd,int nflip,QString mycall,QString hiscall,
                QString hisgrid,double &qual,QString &decoded);
    void subtract65(double *dd,int npts,double f0,double dt);
    void getpp_(int *workdat,float &p);
    void ftrsd2_ap(int *mrsym, int *mrprob, int *mr2sym, int *mr2prob,
                   bool f_ap_d,int *ap,int ntrials0, int *correct, int *param, int *ntry);
    void chkhist(int *mrsym,int &nmax,int &ipk);
    void demod64a(double s3_[63][64],int nadd,int *mrsym,int *mrprob,
                  int *mr2sym,int *mr2prob,int &ntest,int &nlow);
    void extract(double s3_[63][64],int nadd,int mode65,int ntrials,int naggressive,
                 bool f_deep_search,int nflip,QString mycall_12,QString hiscall_12,QString hisgrid,
                 int &ncount,int &nhist,QString &decoded,bool &ltext,int &nft,double &qual,int nQSOProgress,bool ljt65apon);
    void decode65b(double s2_[126][66],int nflip,int nadd,int mode65,int ntrials,int naggressive,bool f_deep_search,
                   QString mycall,QString hiscall,QString hisgrid,int nqd,int &nft,double &qual,
                   int &nhist,QString &decoded,int nQSOProgress,bool ljt65apon);
    void smo121(double *x,int beg,int nz);
    void twkfreq65(std::complex<double> *c4aa,int n5,double *a);
    void ccf2(double *ss,int nz,int nflip,double &ccfbest,double &xlagpk);
    double fchisq65(std::complex<double> *cx,int npts,double fsample,int nflip,double *a,double &ccfmax,double &dtmax);
    void afc65b(std::complex<double> *cx,int npts,double fsample,int nflip,double *a,double &ccfbest,double &dtbest);
    //void afc65b(std::complex<double> *cx,int npts,double fsample,int nflip,int mod65,double *a,double &ccfbest,double &dtbest);
    void fil6521(std::complex<double> *c1,int n1,std::complex<double> *c2,int &n2);
    void sh65snr(double *x,int beg,int nz,double &snr);
    void sh65(std::complex<double> *cx,int n5,int mode65,int ntol,double &xdf,int &nspecial,double &snrdb,double &nstest);
    void filbig(double *dd,int npts,double f0,bool &newdat,std::complex<double> *c4a,int &n4,double &sq0);
    void decode65a(double *dd,int npts,bool &first_time,int nqd,double freq,int &nflip,
                   int mode65,int nvec,int naggressive,bool f_deep_search,int ntol,QString mycall,QString hiscall,
                   QString hisgrid,bool bVHF,double &sync2,double *a,double &dtx,int &nft,int &nspecial,
                   double &qual,int nsmo,QString &decoded,double &nstestn,int nQSOProgress,bool ljt65apon);
    void fqso_first(double nfqso,int ntol,candidate_jt65 *ca,int ncand);
    void slope(double *y,int beg, int npts,double xpk);
    void xcor(double ss_[3413][642],int ipk,int nsteps,int nsym,int lag1,int lag2,double *ccf,
              double &ccf0,int &lagpk,double &flip,double fdot,int nrobust);
    void sync65(double ss_[3413][642],double nfa,double nfb,/*int naggressive,int ntol,*/
                int nhsym,candidate_jt65 *ca,int &ncand,int nrobust,bool bVHF);
    double fchisq0(double *y,int npts,double *a);
    void lorentzian(double *y,int npts,double *a);
    void flat65(double ss_[3413][642],int nhsym,int NSZ,double *ref);
    void symspec65(double *dd,int npts,double ss_[3413][642],int &nhsym,double *savg);
    void avg65(int nutc,int &nsave,double snrsync,double dtxx,int nflip,int nfreq,int mode65,int ntol,
               bool f_deep_search,bool nagain,int ntrials,int naggressive,int neme,QString mycall,
               QString hiscall,QString hisgrid,int &nftt,QString &avemsg,double &qave,QString &deepave,
               int &nsum,int ndeepave,int nQSOProgress,bool ljt65apon);
    //void SetRxFreq65(double);
    void jt65_decode(double *dat,int dat_count,int mode65);
    ///// END JT65ABC /////////////////////////////////

    ///// single sync JT65ABC /////////////////////////////////
    //void sync65_single(double ss_[3413][552],double nfa,double nfb,int nhsym,double &dtx,double &dfx,
    //double &snrx,double &snrsync,double &flip,/*double &width,*/double *savg,int mode65);
    void sync65_single(double ss_[3413][642],double nfa,double nfb,int nhsym,double &dtx,double &dfx,
                       double &snrx,double &snrsync,double &flip,/*double &width,*/double *savg,int mode65,int nrob);//1.52+nrob
    ///// emd single sync JT65ABC /////////////////////////////////

    /////////// PI4 ///////////////////////////////////////////////////////////
    int count_use_avgs_pi4;
    int count_saved_avgs_pi4;
    bool clearave_pi4;
    bool first_pi4;
    int nsave_pi4;
    QString blank_pi4;
    int iutc_pi4[64];
    bool first_avgpi4;
    double nfsave_pi4[64];
    double dtdiff_pi4;
    double ppsave_pi4_[64][207];
    double rsymbol_pi4_[207];
    double dtsave_pi4[64];
    double syncsave_pi4[64];
    double flipsave_pi4[64];
    int iused_pi4[64];
    char cused_pi4[64];
    double nfreq0_pi4;
    bool first_xcorpi4;
    double pr2_pi4[207];
    bool first_extractpi4;
    int ndelta_pi4;
    int off_mettab_pi4; //130
    int mettab_pi4_[2][270];//(-128:127,0:1)
    bool first_interleavepi4;
    int j0_pi4[207];//(0:205)
    int ich1_pi4;
    int ich2_pi4;
    int nutc0_pi4;
    void fano232(char *symbol,int beg,int nbits,int maxcycles,unsigned char *dat,int &ncycles,int &ierr);
    void interleavepi4(char *id,int beg,int ndir);
    void getmetpi4();
    void extractpi4(double *sym0,int &ncount,QString &decoded);
    void decodepi4(double *dat,int npts,double dtx,double nfreq/*,double flip*/,int mode4,
                   QString &decoded,int &nfano);
    void xcorpi4(double s2_[770][1260],int ipk,int nsteps,int nsym,int lag1,int lag2,int mode4,
                 double *ccf,double &ccf0,int &lagpk,double &flip);
    void smo_pi4(double*x,int x_begin,int npts,double*y,double nadd);
    void flat1b(double *psavg,int nsmo,double s2_[770][1260],int nh,int nsteps);
    void pspi4(double *dat,int beg,int nfft,double *s);
    void syncpi4(double *dat,int jz,int ntol,int NFreeze,int MouseDF,int mode,int mode4,
                 double &dtx,double &dfx,double &snrx,double &snrsync,double *ccfblue,double *ccfred1,
                 double &flip,double &width/*,double *ps0*/);
    void print_msgpi4(int nsync,int nsnr,double dtx,int df,int width,QString decoded,QString csync,QString cflags,int nfreq);
    void avgpi4(int nutc,double snrsync,double dtxx,double flip,double nfreq,int ntol,
                QString &avemsg/*,double &qave*/,int &nfanoave);
    void mshvpi4(double *dat,int jz2,int nutc,int minsync,int ntol,
                 int mode4,double nfqso,int ndepth/*,int neme*/);
    //void resample_12_6(double *d2,int &npts,double *dd);
    void lpf1(double *dd,int jz,double *dat,int &jz2);
    void pi4_decode(double *dd,int npts);
    ///////////END PI4 ///////////////////////////////////////////////////////////

    /////////// FT8 ///////////////////////////////////////////////////////////
    bool s_lapon;
    int s_nQSOProgress;
    //bool f_multi_answer_mod;//for ap mam ft8;
    double s_nftx;
    //int s_ncontest_ft8_2;
    QString HisGridLoc;  // in SetCalsHash decodermsk40
    QString s_R1HisCall; //for fr8 MA QSO Foxs in SetCalsHash decodermsk40
    QString s_R2HisCall; //for fr8 MA QSO Foxs in SetCalsHash decodermsk40

    DecoderFt8 *DecFt8_0;
    DecoderFt8 *DecFt8_1;
    DecoderFt8 *DecFt8_2;
    DecoderFt8 *DecFt8_3;
    DecoderFt8 *DecFt8_4;
    DecoderFt8 *DecFt8_5;
    ///////////END FT8 ///////////////////////////////////////////////////////////
    /////////// FT4 ///////////////////////////////////////////////////////////
    DecoderFt4 *DecFt4_0;
    DecoderFt4 *DecFt4_1;
    DecoderFt4 *DecFt4_2;
    DecoderFt4 *DecFt4_3;
    DecoderFt4 *DecFt4_4;
    DecoderFt4 *DecFt4_5;
    /////////// FT4 end ///////////////////////////////////////////////////////////

    void TryEndThr();
    void StrtDec0();
    void StrtDec1();
    void StrtDec2();
    void StrtDec3();
    void StrtDec4();
    void StrtDec5();
    void StrtDecode();

    bool allq65;
    DecoderQ65 *DecQ65;

protected:
    bool thred_busy;
    int s_static_dat_count;
    double s_in_istart;
    int s_mousebutton;
    QString s_time;
    //double raw_in_s1[STATIC_DAT_COUNT];
    short raw_in_s[STATIC_DAT_COUNT];
    double static_dat0[STATIC_DAT_COUNT];
    double static_dat1[190000];//15,8s
    double static_dat2[190000];//15,8s
    double static_dat3[190000];//15,8s
    double static_dat4[190000];//15,8s
    double static_dat5[190000];//15,8s

    bool fromBufFt;
    int id3decFt;
    int c_stat_ftb[2];
    bool is_stat_ftb[2];
    int s_mousebftb[2];
    QString s_timeftb[2];
    bool s_fopenftb[2];
    short stat_1ftb[190000];//15,8s
    short stat_2ftb[190000];//15,8s
    bool is_ftBuff;
    void SETftBuff();

    void CalcZapDat();
    int s_zap_count;
    short s_zap_in[STATIC_DAT_COUNT];
    double s_zap_dat[STATIC_DAT_COUNT];

    pthread_t th0;
    static void *ThrDec0(void *);
    pthread_t th1;
    static void *ThrDec1(void *);
    pthread_t th2;
    static void *ThrDec2(void *);
    pthread_t th3;
    static void *ThrDec3(void *);
    pthread_t th4;
    static void *ThrDec4(void *);
    pthread_t th5;
    static void *ThrDec5(void *);
    pthread_t th;
    static void *ThreadDecode(void *);

};
#endif



