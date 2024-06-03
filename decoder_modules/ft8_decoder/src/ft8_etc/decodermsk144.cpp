/* The algorithms, source code, look-and-feel of WSJT-X and related
 * programs, and protocol specifications for the modes FSK441, FT8, JT4,
 * JT6M, JT9, JT65, JTMS, QRA64, ISCAT, MSK144, are Copyright © 2001-2017
 * by one or more of the following authors: Joseph Taylor, K1JT; Bill
 * Somerville, G4WJS; Steven Franke, K9AN; Nico Palermo, IV3NWV; Greg Beam,
 * KI7MT; Michael Black, W9MDB; Edson Pereira, PY2SDR; Philip Karn, KA9Q;
 * and other members of the WSJT Development Group.
 *
 * MSHV MSK144 Decoder
 * Rewritten into C++ and modified by Hrisimir Hristov, LZ2HV 2015-2017
 * May be used under the terms of the GNU General Public License (GPL)
 */

//#include "../config_213.h"
#include "decoderms.h"
#include "nhash.h"
//2.28 included for small exe size
static const char s8ms[8] =
    {
        0,1,0,0,1,1,1,0
    }
    ; //ok 4  78 invers msk40   14s
static const char s8[8]   =
    {
        0,1,1,1,0,0,1,0
    };

//#include <QtGui>

/*
void DecoderMs::SetMyGridMsk144ContM(QString s,bool c_msg) //for " R " in msg 1.31  (bool msk144cont_m)
{
    My_Grid_Loc = s; //qDebug()<<"DECODER="<<My_Grid_Loc;
    msk144_ft8_cont_msg = c_msg;
}
*/
void DecoderMs::first_msk144()
{
    if (f_first_msk144)
        return;
    //qDebug()<<"f_first_msk144="<<f_first_msk144;
    //int NSPM=864;
    //int NFFT=NSPM;  //for msk 144 ->  NPSM = 864
    double cbi[42],cbq[42];

    //write(78,*) "after init_ldpc"
    //! define half-sine pulse and raised-cosine edge window
    fs_msk144=DEC_SAMPLE_RATE;
    dt_msk144=1.0/fs_msk144;
    //df_msk144=(double)fs_msk144/(double)NFFT; //qDebug()<<"df_144="<<df_144;
    //tframe_msk144=NSPM/fs_msk144;

    for (int i = 0; i<12; i++)
    {//do i=1,12
        double angle=(i)*pi/12.0;//angle=(i-1)*pi/12.0
        pp_msk144[i]=sin(angle);
        rcw_msk144[i]=(1.0-cos(angle))/2.0;//rcw(i)=(1-cos(angle))/2
    }

    //! define the sync word waveform
    // tic const  {0,1,1,1,0,0,1,0}; //msk144  114
    // tic const  {1,0,1,1,0,0,0,1}; //msk40   177

    //char s8ms1[8]={0,1,0,0,1,1,1,0}; //ok 4   78 invers msk40   14s
    //ar s8ms1[8]={0,1,0,0,1,1,0,0}; //ok 5   76    14s  ok ok ok
    //ar s8ms1[8]={0,0,0,0,1,1,0,0}; //ok 6   12    11s
    for (int i = 0; i<8; i++)
    {
        if (ss_msk144ms)
            s_msk144_2s8[i]=2*s8ms[i]-1;            //s8=2*s8-1
        else
            s_msk144_2s8[i]=2*s8[i]-1;
    }

    mplay_da_da_i(cbq,0,6,pp_msk144,6,s_msk144_2s8[0]);//cbq(1:6)=pp(7:12)*s8_[1]
    mplay_da_da_i(cbq,6,18,pp_msk144,0,s_msk144_2s8[2]);//cbq(7:18)=pp*s8_(3]
    mplay_da_da_i(cbq,18,30,pp_msk144,0,s_msk144_2s8[4]);//cbq(19:30)=pp*s8_(5]
    mplay_da_da_i(cbq,30,42,pp_msk144,0,s_msk144_2s8[6]);//cbq(31:42)=pp*s8_(7]
    mplay_da_da_i(cbi,0,12,pp_msk144,0,s_msk144_2s8[1]);//cbi(1:12)=pp*s8_(2]
    mplay_da_da_i(cbi,12,24,pp_msk144,0,s_msk144_2s8[3]);//cbi(13:24)=pp*s8_(4]
    mplay_da_da_i(cbi,24,36,pp_msk144,0,s_msk144_2s8[5]);//cbi(25:36)=pp*s8_(6]
    mplay_da_da_i(cbi,36,42,pp_msk144,0,s_msk144_2s8[7]);//cbi(37:42)=pp(1:6)*s8_(8]

    for (int i = 0; i<42; i++)
        cb_msk144[i]=cbi[i] + cbq[i]*complex_i;   //cb=cmplx(cbi,cbq)

    f_first_msk144=true;
    /*qDebug()<<"f_first_msk144="<<creal(cb_msk144[3])<<creal(cb_msk144[8])<<(int)s_msk144_2s8[2];
    QString sss = "";
    for (int i= 0; i < 42; i++)//2 pove4e
    {
        sss.append(QString("%1").arg(creal(cb_msk144[i]),0,'f',1 ));
        sss.append(",");
    }
    qDebug()<<"f="<<sss;*/
}
void DecoderMs::dftool_msk144(int ntol, double nrxfreq,double df)
{
    ///for all msk144///
    if (last_ntol_msk144==ntol && last_df_msk144==df)
        return; //qDebug()<<"dftool_msk144="<<ntol<<df;

    last_ntol_msk144=ntol;
    last_df_msk144=df;

    double nfhi=2.0*(nrxfreq+500.0);
    double nflo=2.0*(nrxfreq-500.0);
    ihlo_msk144=(nfhi-(double)2.0*ntol)/df;
    ihhi_msk144=(nfhi+(double)2.0*ntol)/df;
    illo_msk144=(nflo-(double)2.0*ntol)/df;
    ilhi_msk144=(nflo+(double)2.0*ntol)/df;
    i2000_msk144=nflo/df;
    i4000_msk144=nfhi/df;
    //qDebug()<<ihlo_msk144<<ihhi_msk144<<illo_msk144<<ilhi_msk144<<i2000_msk144<<i4000_msk144;
}
/*
void DecoderMs::analytic_msk144(double *d,int d_count_begin,int npts,int nfft,std::complex<double> *c)
{
    int nh=nfft/2;
    double fac=(double)2.0/(double)nfft;
    //int NFFTMAX=1024*1024;
    //double *h = new double[NFFTMAX/2];

    //qDebug()<<d[2]<<d[3];

    //int nfft0 = 0;
    //data nfft0/0/
    //save nfft0,h

    double df=(double)DEC_SAMPLE_RATE/(double)nfft;
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (nfft!=nfft0_msk144)
    {
        double t=1.0/2000.0;
        double beta=0.1;//0.6=jtmsk 0.1=msk144
        for (int i = 0; i<nh+1; i++)
        {//do i=1,nh+1
            double ff=(i-0)*df;//ff=(i-1)*df
            double f=ff-1500.0;
            h_msk144[i]=0.0;
            if (fabs(f)<=(1.0-beta)/(2.0*t)) h_msk144[i]=1.0;
            if (fabs(f)>(1.0-beta)/(2.0*t) && fabs(f)<=(1.0+beta)/(2.0*t))
            {
                h_msk144[i]=0.5*(1.0+cos((pi*t/beta )*(fabs(f)-(1.0-beta)/(2.0*t))));
            }
            //h[i]=sqrt(h[i]);//h[i]=sqrt(h[i]); jtmsk
        }
        nfft0_msk144=nfft;
    }

    for (int i = 0; i<npts; i++)
        c[i]=fac*d[i+d_count_begin];

    //for (int j = npts+1; j<nfft; j++)
    for (int j = npts; j<nfft; j++)
        c[j]=0.0+0.0*complex_i;

    four2a_compex_to_cmplex(c,nfft,1,-1,1);               //!Forward c2c FFT

    //c(1:nh+1)=h(1:nh+1)*c(1:nh+1)
    for (int i = 0; i<nh+1; i++)
        c[i]=h_msk144[i]*c[i];

    c[0]=0.5*c[0];
    //qDebug()<<nh;
    //for (int y = nh+2; y<nfft; y++)
    for (int y = nh+1; y<nfft; y++)
        c[y]=0.0+0.0*complex_i;

    four2a_compex_to_cmplex(c,nfft,1,1,1);                //!Inverse c2c FFT

    //delete h;
    //c[1] = 4.9+6*complex_i;
    //c[2] = 6.7+6*complex_i;
    //s[1]= 56.7;
}
*/
void DecoderMs::analytic_msk144_2_init_s_corrs_full()
{
    int nfft = 524290;  //524288
    int nh=nfft/2;
    double df=(double)DEC_SAMPLE_RATE/(double)nfft;
    double spc[3]={-0.952,0.768,-0.565}; //hv ne se promeniat za oprostiavane //! baseline phase coeffs for TS2000
    double sac[5]={1.0,0.05532,0.11438,0.12918,0.09274}; //hv ne se promeniat za oprostiavane //! amp coeffs for TS2000

    for (int i = 0; i<nh+1; i++)
    {//do i=1,nh+1
        double ff=(double)i*df;//ff=(i-1)*df
        double f=ff-1500.0;
        double fp=f/1000.0;
        double ps=fp*fp*(spc[0]+fp*(spc[1]+fp*spc[2]));
        double amp=sac[0]+fp*(sac[1]+fp*(sac[2]+fp*(sac[3]+fp*sac[4])));
        s_corrs[i]=amp*(cos(ps)+sin(ps)*complex_i);
    }
}
bool DecoderMs::any_not_and_save_in_a(double *a,double *b,int c)
{
    //ako ima dori edno razli4no
    bool res = false;
    for (int i = 0; i<c; i++)
    {
        if (a[i]!=b[i])
        {
            res = true;
            break;
        }
    }

    if (res)
    {
        for (int i = 0; i<c; i++)
            a[i]=b[i];
        //qDebug()<<"KKKK"<<b[0]<<b[1]<<b[2];
    }
    return res;
}
void DecoderMs::analytic_msk144_2(double *d,int d_count_begin,int npts,int nfft,std::complex<double> *c,
                                  double *dpc,bool bseq,bool bdeq)
{
    //parameter (NFFTMAX=1024*1024)
    double fac=(double)2.0/(double)nfft;
    int nh=nfft/2;
    double df=(double)DEC_SAMPLE_RATE/(double)nfft;

    // if new nfft ?, for now no.  if differenf nfft need to be first and in to this->if (nfft!=nfft0_msk144_2) HV
    // if spc and sac static no need to calculate and any_not() agen HV
    if (nfft!=nfft0_msk144_2 || any_not_and_save_in_a(dpclast_msk144_2,dpc,3))
    {
        for (int i = 0; i<nh+1; i++)
        {//do i=1,nh+1
            double ff=(double)i*df;//ff=(i-1)*df
            double f=ff-1500.0;
            double fp=f/1000.0;
            double pd=fp*fp*(dpc[0]+fp*(dpc[1]+fp*dpc[2]));
            s_corrd[i]=cos(pd)+sin(pd)*complex_i;
        }
        //qDebug()<<"s_corrd="<<s_corrd[0]<<s_corrd[1]<<s_corrd[2];
    }

    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (nfft!=nfft0_msk144_2) //|| any_not(alast_msk144,a,5)
    {
        //qDebug()<<"nfft="<<nfft<<"nh+1="<<nh+1;
        double t=1.0/2000.0;
        double beta=0.1;//0.6=jtmsk 0.1=msk144
        for (int i = 0; i<nh+1; i++)
        {//do i=1,nh+1
            double ff=(double)i*df;//ff=(i-1)*df
            double f=ff-1500.0;

            h_msk144_2[i]=1.0; // new 1.0
//            h_msk144_2[i]=0.01; //from old HV max_res->0.01;

//            if (fabs(f)<=(1.0-beta)/(2.0*t)) h_msk144_2[i]=1.0; // from old HV max_res->1.0;
            if (fabs(f)>(1.0-beta)/(2.0*t) && fabs(f)<=(1.0+beta)/(2.0*t))
            {
                h_msk144_2[i]=h_msk144_2[i]*0.5*(1.0+cos((pi*t/beta )*(fabs(f)-(1.0-beta)/(2.0*t))));   // new
//                h_msk144_2[i]=0.5*(1.0+cos((pi*t/beta )*(fabs(f)-(1.0-beta)/(2.0*t)))); // from old HV
            }
            else if ( fabs(f) > (1.0+beta)/(2.0*t) )
                h_msk144_2[i]=0.0;
        }
        nfft0_msk144_2=nfft;
    }

    for (int i = 0; i<npts; i++)
        c[i]=fac*d[i+d_count_begin];

    //for (int j = npts+1; j<nfft; j++)
    for (int j = npts; j<nfft; j++)
        c[j]=0.0+0.0*complex_i;

    f2a->four2a_c2c(c,nfft,-1,1);               //!Forward c2c FFT

    //int button = (bdeq<<1 | bseq);
    //qDebug()<<"button"<<button;

    if (!bseq && !bdeq)///tuk normal
    {
        for (int i = 0; i<nh+1; i++)
            c[i]=h_msk144_2[i]*c[i];
    }
    else if (bseq && !bdeq)//only s_corrs->from TS2000
    {
        for (int i = 0; i<nh+1; i++)
            c[i]=h_msk144_2[i]*s_corrs[i]*c[i];
    }
    else if (!bseq && bdeq)//tuk from s_corrd
    {
        for (int i = 0; i<nh+1; i++)
            c[i]=h_msk144_2[i]*s_corrd[i]*c[i];
    }
    else if (bseq && bdeq)//tuk s_corrd & s_corrs->from TS2000
    {
        for (int i = 0; i<nh+1; i++)
            c[i]=h_msk144_2[i]*s_corrs[i]*s_corrd[i]*c[i];
    }

    c[0]=0.5*c[0];  //c(1)=0.5*c(1)                            //!Half of DC term
    for (int y = nh+1; y<nfft; y++)
        c[y]=0.0+0.0*complex_i; //c(nh+2:nfft)=0.

    f2a->four2a_c2c(c,nfft,1,1);                //!Inverse c2c FFT
}
/*
void DecoderMs::analytic_msk144_2(double *d,int d_count_begin,int npts,int nfft,std::complex<double> *c,
                                  double *dpc,bool bseq,bool bdeq)
{
    //parameter (NFFTMAX=1024*1024)
    double fac=(double)2.0/(double)nfft;
    int nh=nfft/2;
    double df=(double)DEC_SAMPLE_RATE/(double)nfft;
    double spc[3]={-0.952,0.768,-0.565}; //hv ne se promeniat za oprostiavane //! baseline phase coeffs for TS2000
    double sac[5]={1.0,0.05532,0.11438,0.12918,0.09274}; //hv ne se promeniat za oprostiavane //! amp coeffs for TS2000

    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (nfft!=nfft0_msk144_2) //|| any_not(alast_msk144,a,5)
    {
        //qDebug()<<"nfft="<<nfft<<"nh+1="<<nh+1;
        double t=1.0/2000.0;
        double beta=0.1;//0.6=jtmsk 0.1=msk144
        for (int i = 0; i<nh+1; i++)
        {//do i=1,nh+1
            double ff=(i-0)*df;//ff=(i-1)*df
            double f=ff-1500.0;

            h_msk144_2[i]=1.0; // new 1.0
//            h_msk144_2[i]=0.01; //from old HV max_res->0.01;

//            if (fabs(f)<=(1.0-beta)/(2.0*t)) h_msk144_2[i]=1.0; // from old HV max_res->1.0;
            if (fabs(f)>(1.0-beta)/(2.0*t) && fabs(f)<=(1.0+beta)/(2.0*t))
            {
                h_msk144_2[i]=h_msk144_2[i]*0.5*(1.0+cos((pi*t/beta )*(fabs(f)-(1.0-beta)/(2.0*t))));   // new
//                h_msk144_2[i]=0.5*(1.0+cos((pi*t/beta )*(fabs(f)-(1.0-beta)/(2.0*t)))); // from old HV
            }
            else if ( fabs(f) > (1.0+beta)/(2.0*t) )
                h_msk144_2[i]=0.0;
        }
        nfft0_msk144_2=nfft;
    }

    // if new nfft ?, for now no.  if differenf nfft need to be first and in to this->if (nfft!=nfft0_msk144_2) HV
    // if spc and sac static no need to calculate and any_not() agen HV
    if (any_not(spclast_msk144_2,spc,3) || any_not(saclast_msk144_2,sac,5) || any_not(dpclast_msk144_2,dpc,3))
    {
        for (int i = 0; i<3; i++)
        {
            spclast_msk144_2[i]=spc[i];
            dpclast_msk144_2[i]=dpc[i];
        }
        for (int i = 0; i<5; i++)
            saclast_msk144_2[i]=sac[i];

        for (int i = 0; i<nh+1; i++)
        {//do i=1,nh+1
            double ff=(i-0)*df;//ff=(i-1)*df
            double f=ff-1500.0;
            double fp=f/1000.0;
            double ps=fp*fp*(spc[0]+fp*(spc[1]+fp*spc[2]));
            double amp=sac[0]+fp*(sac[1]+fp*(sac[2]+fp*(sac[3]+fp*sac[4])));
            s_corrs[i]=amp*(cos(ps)+sin(ps)*complex_i);
            double pd=fp*fp*(dpc[0]+fp*(dpc[1]+fp*dpc[2]));
            s_corrd[i]=cos(pd)+sin(pd)*complex_i;
        }
    }

    for (int i = 0; i<npts; i++)
        c[i]=fac*d[i+d_count_begin];

    //for (int j = npts+1; j<nfft; j++)
    for (int j = npts; j<nfft; j++)
        c[j]=0.0+0.0*complex_i;

    four2a_compex_to_cmplex(c,nfft,1,-1,1);               //!Forward c2c FFT

    //int button = (bdeq<<1 | bseq);
    //qDebug()<<"button"<<button;

    if (!bseq && !bdeq)///tuk normal
    {
        for (int i = 0; i<nh+1; i++)
            c[i]=h_msk144_2[i]*c[i];
    }
    else if (bseq && !bdeq)//only s_corrs->from TS2000
    {
        for (int i = 0; i<nh+1; i++)
            c[i]=h_msk144_2[i]*s_corrs[i]*c[i];
    }
    else if (!bseq && bdeq)//tuk from s_corrd
    {
        for (int i = 0; i<nh+1; i++)
            c[i]=h_msk144_2[i]*s_corrd[i]*c[i];
    }
    else if (bseq && bdeq)//tuk s_corrd & s_corrs->from TS2000
    {
        for (int i = 0; i<nh+1; i++)
            c[i]=h_msk144_2[i]*s_corrs[i]*s_corrd[i]*c[i];
    }

    c[0]=0.5*c[0];  //c(1)=0.5*c(1)                            //!Half of DC term
    for (int y = nh+1; y<nfft; y++)
        c[y]=0.0+0.0*complex_i; //c(nh+2:nfft)=0.

    four2a_compex_to_cmplex(c,nfft,1,1,1);                //!Inverse c2c FFT
}
*/
double DecoderMs::sum_da(double*a,int a_beg,int a_end)
{
    double res = 0.0;
    for (int i = a_beg; i < a_end; i++)
    {
        res+=a[i];
    }
    return res;
}
int DecoderMs::sum_ia(int*a,int a_beg,int a_end)
{
    int res = 0;
    for (int i = a_beg; i < a_end; i++)
    {
        res+=a[i];
    }
    return res;
}
void DecoderMs::mplay_da_da_i(double *a,int b_a,int e_a,double *b,int b_b,int mp)
{
    for (int i = 0; i < (e_a-b_a); i++)
    {
        a[i+b_a]=b[i+b_b]*mp;
    }
}
void DecoderMs::mplay_dca_dca_dca(std::complex<double> *a,int a_beg,int a_end,std::complex<double> *b,int b_beg,std::complex<double> *mp,int mp_beg,int ord)
{
    int c =0;
    int c_mp = mp_beg;
    for (int i = a_beg; i < a_end; i++)
    {
        a[i]=b[c+b_beg]*mp[c_mp];
        c++;
        c_mp = c_mp + ord;
    }
}
void DecoderMs::mplay_dca_dca_da(std::complex<double> *a,int a_beg,int a_end,std::complex<double> *b,int b_beg,double *mp,int mp_beg,int ord)
{
    //int c =0;
    //int c_mp = mp_beg;
    for (int i = a_beg; i < a_end; i++)
    {
        a[i]=b[b_beg]*mp[mp_beg];
        //c++;
        b_beg++;
        //qDebug()<<"b_beg="<<b_beg;
        mp_beg = mp_beg + ord;
    }
}
//void DecoderMs::mplay_da_absdca_absdca(double *a,int a_b,int a_e,std::complex<double> *b,int b_b,std::complex<double> *mp,int mp_b)
void DecoderMs::mplay_da_absdca_absdca(double *a,int a_c,std::complex<double> *b,std::complex<double> *mp)
{
    //mplay_da_absdca_absdca(tonespec,0,6000,ctmp,0,ctmp,0);//tonespec=abs(ctmp)**2
    /*int c =0;
    for (int i = a_b; i < a_e; i++)
    {
        a[i]=cabs(b[c+b_b])*cabs(mp[c+mp_b]);
        c++;
    }*/
    for (int i = 0; i < a_c; ++i)//1.68 ++i ok 100ms
    {
        a[i]=cabs(b[i])*cabs(mp[i]);
    }
}

void DecoderMs::copy_double_ar_ainc(double*a,int a_beg,int a_inc,double*b,int b_beg,int b_end)
{
    //int c = 0;
    for (int i = b_beg; i < b_end; i++)
    {
        a[a_beg]=b[i];
        a_beg = a_beg + a_inc;
    }
}

void DecoderMs::copy_dca_or_sum_max3dca(std::complex<double> *a,int a_cou, std::complex<double> *b, int b_beg,
                                        std::complex<double> *c, int c_beg, std::complex<double> *d, int d_beg)
{
    //c=cdat2(1:NSPM)+cdat2(NSPM+1:2*NSPM)+cdat2(2*NSPM+1:npts)
    int c1 = b_beg;
    int c2 = c_beg;
    int c3 = d_beg;
    for (int i = 0; i <  a_cou; i++)
    {
        if	(c_beg == -1 && d_beg == -1)
            a[i]=b[c1];
        else if (d_beg == -1)
            a[i]=b[c1]+c[c2];
        else
            a[i]=b[c1]+c[c2]+d[c3];

        c1++;
        c2++;
        c3++;
    }
}
/*
int DecoderMs::maxloc_da_end_to_beg(double*a,int a_beg,int a_end)
{
    double max = a[a_end];
    int loc = a_end;
    for (int i = a_end-1; i >= a_beg; i--)
    {
        if (a[i]>max)
        {
            loc = i;
            max = a[i];
        }
    }
    return loc;
}
*/
int DecoderMs::maxloc_absdca_beg_to_end(std::complex<double>*a,int a_beg,int a_end)
{
    double max = cabs(a[a_beg]);
    int loc = a_beg;
    for (int i = a_beg; i < a_end; i++)
    {
        if (cabs(a[i])>max)
        {
            loc = i;
            max = cabs(a[i]);
        }
    }
    //qDebug()<<"Rmax-="<<max;
    return loc;
}
void DecoderMs::sum_dca_dca_dca(std::complex<double> *a,int a_cou,std::complex<double> *b,std::complex<double> *c)
{
    for (int i = 0; i < a_cou; i++)
        a[i]=b[i]+c[i];
}
QString DecoderMs::GetStandardRPT(double width, double peak)
{
    int nrpt=0;
    int nwidth=0;

    if (width>=0.0) nwidth=2;     //lz2hv duration Hanbook 7.00
    if (width>=0.5) nwidth=3;
    if (width>=1.0) nwidth=4;
    if (width>=5.0) nwidth=5;     // jt coding problem maz 49

    int nstrength=6;
    if (peak>=5.0) nstrength=7;    //lz2hv S-unit Hanbook 7.00
    if (peak>=11.0) nstrength=8;   //be6e 10
    if (peak>=15.0) nstrength=9;

    nrpt=10*nwidth + nstrength;
    return QString("%1").arg(nrpt);
}

double DecoderMs::MskPingDuration(double *detmet_dur,int istp_real,int il,double level,int nstepsize,int nspm,double dt)
{
    double start_time = 0.0;
    double end_time = 0.0;
    for (int i = il; i>=0; i--)
    {
        if (detmet_dur[i] > level)
            start_time = (double)(i*nstepsize+nspm)*dt;//(double)((i)*nstepsize+NSPM/2)*dt_msk144;
        else
            break;
    }
    for (int i = il; i<istp_real; i++)
    {
        if (detmet_dur[i] > level)
            end_time = (double)(i*nstepsize+nspm)*dt;
        else
            break;
    }
    //qDebug()<<"Get============="<<start_time<<end_time<<end_time-start_time<<il<<istp_real<<detmet_dur[il];
    return end_time - start_time;
}
void DecoderMs::SetDecodetTextMsk2DL(QStringList list)//2.46
{
    QString tstr = list.at(6);
    tstr.remove("<");
    tstr.remove(">");
    QStringList tlist = tstr.split(" ");
    for (int x = 0; x<tlist.count(); ++x)
    {
        if (tlist.at(x)==s_MyBaseCall || tlist.at(x)==s_MyCall)
        {
            EmitDecodedTextRxFreq(list,true,true);
            break;
        }
    }
    EmitDecodetText(list,s_fopen,true);//1.27 psk rep   fopen bool true    false no file open
}
void DecoderMs::detectmsk144(std::complex<double> *cbig,int n,double s_istart,int &nmessages)
{
    //parameter (NSPM=864, NPTS=3*NSPM, MAXSTEPS=1700, NFFT=NSPM, MAXCAND=16)
    char ident;
    //double fest = 0.0;

    QString msgreceived;
    bool f_only_one_color = true;

    const int MAXCAND=16;//16 20; if 20 more diferent pings decodet HV
    const int NSPM=864;
    const int NPTS=3*NSPM; //  parameter (NSPM=864, NPTS=3*NSPM)
    const int NFFT=864;//NSPM; //da e 4islo 1.46

    std::complex<double> cdat[NPTS];                    //!Analytic signal
    std::complex<double> cdat2[NPTS];
    std::complex<double> c[NSPM+10];

    //int nfft_144 = 6000;
    std::complex<double> ctmp[NFFT+10];

    const int MAXSTEPS=1700;
    double detmet_plus[MAXSTEPS+12];
    double *detmet = &detmet_plus[6];         //real detmet(-2:nstep+3)
    double detmet2_plus[MAXSTEPS+12];
    double *detmet2 = &detmet2_plus[6];
    for (int i = -5; i<MAXSTEPS+5; i++)
    {
        detmet[i]=0.0;
        detmet2[i]=0.0;
    }

    double detfer[MAXSTEPS];
    int indices[MAXSTEPS];
    //double locate[MAXSTEPS];//hv
    //real hannwindow(NPTS)

    //double ferr = 0.0;
    double tonespec[NFFT];
    double times[MAXCAND];
    double ferrs[MAXCAND];
    double snrs[MAXCAND];
    //std::complex<double> cc[NPTS];
    std::complex<double> cc1[NPTS];
    std::complex<double> cc2[NPTS];
    double dd[NPTS];
    int ipeaks[10];
    std::complex<double> bb[6];
    std::complex<double> cfac,cca,ccb;
    //double phase0;
    double softbits[144];
    //int hardbits[144];
    //double lratio[128];
    //double llr[128];
    //char decoded[80];

    QString allmessages[20];
    int ntol=G_DfTolerance;
    double nrxfreq = 1500.0;

    //int ncorrected = 0;
    //double eyeopening = 0.0;

    first_msk144();
    double df = (double)fs_msk144/(double)NFFT;
    dftool_msk144(ntol,nrxfreq,df);

    //int nmessages=0;
    int istp_real = 0;
    int nstepsize = 216;
    int nstep=(n-NPTS)/nstepsize; //nstep=(n-NPTS)/216  ! 72ms/4=18ms steps//qDebug()<<"nstep="<<nstep;
    for (int i = 0; i<MAXSTEPS; i++)
        detfer[i]=-999.99;

    for (int istp = 0; istp<nstep; ++istp)//1.68 ++istp ima efect 100ms
    {//do istp=1,nstep
        double ferr = 0.0;
        int ns=0+nstepsize*(istp-0);//ns=1+256*(istp-1)
        int ne=ns+NSPM-0;    //ne=ns+NSPM-1    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        if ( ne > n ) break;//exit

        pomAll.zero_double_comp_beg_end(ctmp,0,NFFT);//ctmp=cmplx(0.0,0.0)
        int c_c=0;
        for (int i = ns; i<ne; i++)
        {
            ctmp[c_c]=cbig[i];//cdat=cbig(ns:ne)
            c_c++;
        }

        //! Coarse carrier frequency sync - seek tones at 2000 Hz and 4000 Hz in
        //! squared signal spectrum.
        //! search range for coarse frequency error is +/- 100 Hz

        mplay_dca_dca_dca(ctmp,0,NFFT,ctmp,0,ctmp,0,1);//ctmp=ctmp**2
        mplay_dca_dca_da(ctmp,0,12,ctmp,0,rcw_msk144,0,1);//ctmp(1:12)=ctmp(1:12)*rcw
        mplay_dca_dca_da(ctmp,NSPM-12,NSPM,ctmp,NSPM-12,rcw_msk144,11,-1);//ctmp(NSPM-11:NSPM)=ctmp(NSPM-11:NSPM)*rcw(12:1:-1)
        f2a->four2a_c2c(ctmp,NFFT,-1,1);//call four2a(ctmp,nfft,1,-1,1)
        mplay_da_absdca_absdca(tonespec,NFFT,ctmp,ctmp);//tonespec=abs(ctmp)**2

        //qDebug()<<"IIIIII="<<i3800<<i4200;
        int ihpk = pomAll.maxloc_da_beg_to_end(tonespec,ihlo_msk144,ihhi_msk144);//! high tone search window
        double deltah=-creal( (ctmp[ihpk-1]-ctmp[ihpk+1]) / (2.0*ctmp[ihpk]-ctmp[ihpk-1]-ctmp[ihpk+1]) );//deltah=-real( (ctmp(ihpk-1)-ctmp(ihpk+1)) / (2*ctmp(ihpk)-ctmp(ihpk-1)-ctmp(ihpk+1)) )
        double ah=tonespec[ihpk]; //ah=tonespec(ihpk)

        double ahavp = 0.0;//ahavp=(sum(tonespec,ismask)-ah)/count(ismask)
        for (int i = ihlo_msk144; i<ihhi_msk144; i++)
            ahavp+=tonespec[i];
        ahavp = (ahavp-ah)/(double)(ihhi_msk144-ihlo_msk144);
        double trath=ah/(ahavp+0.01);//trath=ah/(ahavp+0.01)

        int ilpk = pomAll.maxloc_da_end_to_beg(tonespec,illo_msk144,ilhi_msk144);//! window for low tone
        double deltal=-creal( (ctmp[ilpk-1]-ctmp[ilpk+1]) / (2.0*ctmp[ilpk]-ctmp[ilpk-1]-ctmp[ilpk+1]) );//deltal=-real( (ctmp(ilpk-1)-ctmp(ilpk+1)) / (2*ctmp(ilpk)-ctmp(ilpk-1)-ctmp(ilpk+1)) )
        double al=tonespec[ilpk];

        double alavp = 0.0;//alavp=(sum(tonespec,ismask)-al)/count(ismask)
        for (int i = illo_msk144; i<ilhi_msk144; i++)
            alavp+=tonespec[i];
        alavp = (alavp-al)/(double)(ilhi_msk144-illo_msk144);
        double tratl=al/(alavp+0.01);//tratl=al/(alavp+0.01)

        //double fdiff=(ihpk+deltah-ilpk-deltal)*df_144;//fdiff=(ihpk+deltah-ilpk-deltal)*df
        double ferrh=((double)ihpk+deltah-(double)i4000_msk144)*df/2.0;//ferrh=(ihpk+deltah-i4000)*df/2.0
        double ferrl=((double)ilpk+deltal-(double)i2000_msk144)*df/2.0;//ferrl=(ilpk+deltal-i2000)*df/2.0
        //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        //if( abs(fdiff-2000) <= 25.0 )  //!    if( abs(fdiff-2000) .le. 25.0 ) then
        //{
        if ( ah >= al )
            ferr=ferrh;
        else
            ferr=ferrl;

        detmet[istp]=fmax(ah,al);
        detmet2[istp]=fmax(trath,tratl);
        detfer[istp]=ferr;
        istp_real++;
        //qDebug()<<"SNR="<<(int)(12.0*log10(detmet[istp])/2.0-9.0);
        //qDebug()<<"ah,al="<<ah<<al;
    }
    //qDebug()<<"istp="<<istp;
    if (istp_real>0)//v1.29
        pomAll.indexx_msk(detmet,istp_real-1,indices);//call indexx(detmet,nstep,indices) !find median of detection metric vector
    //indexx(nstep-0,detmet,indices);
    //qDebug()<<"nstep-1="<<nstep-1;

    //double xmed=detmet[indices[istp/2]];//<-pri men e taka xmed=detmet(indices(nstep/2))
    double xmed=detmet[indices[istp_real/4]];//<-pri men e taka xmed=detmet(indices(nstep/2))
    if (xmed==0.0)//no devide by zero
        xmed=1.0;

    double duration[MAXCAND];
    double *detmet_dur = new double[istp_real+5];//double detmet_dur[istp_real+5];
    double *detmet_dur2 = new double[istp_real+5];//double detmet_dur2[istp_real+5];

    for (int i = 0; i<istp_real; i++)
    {
        detmet[i]=detmet[i]/xmed; //! noise floor of detection metric is 1.0
        detmet_dur[i]=detmet[i];
        detmet_dur2[i]=detmet2[i];
    }


    int ndet=0;
    for (int ip = 0; ip<MAXCAND; ip++)
    {//do ip=1,20 //! use something like the "clean" algorithm to find candidates
        //iloc=maxloc(detmet)
        int il = pomAll.maxloc_da_beg_to_end(detmet,0,istp_real);//il=iloc(1)  //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE
        //qDebug()<<"Idetmet="<<il;
        //int il = maxloc_beg_to_end(locate,0,istp);

        if ( detmet[il] < 3.5 ) break; ///*tested 3.5<-hv 4.0*/if( (detmet(il) .lt. 4.0) ) exit
        if ( fabs(detfer[il]) <= (double)(ntol) ) //if( abs(detfer(il)) .le. 100.0 ) then
        {
            //inportant no(il-1)<-out of array HV tested->(il-0) times(ndet)=((il-1)*256+NPTS/2)*dt
            times[ndet]=(double)((il-0)*nstepsize+NSPM/2)*dt_msk144;//times(ndet)=((il-1)*216+NSPM/2)*dt
            ferrs[ndet]=detfer[il];
            //snrs[ndet]=10.0*log10(detmet[il])/2.0-5.0; //snrs(ndet)=10.0*log10(detmet(il))/2-5.0 !/2 because detmet is a 4th order moment
            snrs[ndet]=12.0*log10(detmet[il])/2.0-9.0;    //snrs(ndet)=12.0*log10(detmet(il))/2-9.0

            if (ss_msk144ms)
                duration[ndet]=MskPingDuration(detmet_dur,istp_real,il,3.5,nstepsize,(NSPM/2),dt_msk144);
            //qDebug()<<"Get============="<<times[ndet]<<start_time<<end_time<<end_time-start_time;

            ndet++;
        }

        //for (int i = il-3; i<il+3+1; i++)// -3 +4 lost good locations
        //for (int i = il-3; i<il+4; i++)
        //detmet[i]=0.0;  //detmet(il-3:il+3)=0.0 !    detmet(max(1,il-1):min(nstep,il+1))=0.0
        detmet[il]=0.0;
        //for (int i = il-3; i<il+4; i++)
        //locate[i]=0.0;  //detmet(il-3:il+3)=0.0
    }
    delete [] detmet_dur;
    //qDebug()<<"1111"<<ndet;
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE
    //ndet = 3;
    if ( ndet < 3 )//for Tropo/ES  if( ndet .lt. 3 ) then
    {
        //qDebug()<<"ndet============================="<<ndet;
        for (int ip = 0; ip<MAXCAND-ndet; ip++)
        {//do ip=1,MAXCAND-ndet //! Find candidates
            int il = pomAll.maxloc_da_beg_to_end(detmet2,0,istp_real);//iloc=maxloc(detmet2(1:nstep))

            if ( detmet2[il] < 12.0 ) break; //if( (detmet2(il) .lt. 12.0) ) exit
            if ( fabs(detfer[il]) <= (double)(ntol) )//if( abs(detfer(il)) .le. ntol ) then
            {
                //ndet=ndet+1
                times[ndet]=(double)((il-0)*nstepsize+NSPM/2)*dt_msk144;//times(ndet)=((il-1)*216+NSPM/2)*dt
                ferrs[ndet]=detfer[il];//ferrs(ndet)=detfer(il)
                snrs[ndet]=12.0*log10(detmet2[il])/2.0-9.0; //snrs(ndet)=12.0*log10(detmet2(il))/2-9.0

                if (ss_msk144ms)
                    duration[ndet]=MskPingDuration(detmet_dur2,istp_real,il,12.0,nstepsize,(NSPM/2),dt_msk144);

                ndet++;
            }
            //!detmet2(max(1,il-1):min(nstep,il+1))=0.0
            detmet2[il]=0.0;
        }
    }
    delete [] detmet_dur2;

    //qDebug()<<"2222"<<ndet;
    if (ndet>0)
        pomAll.indexx_msk(times,ndet-1,indices);//HV sprt pings in one scan by time

    //qDebug()<<"---------ndet="<<ndet<<istp_real;
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE




    for (int iip = 0; iip < ndet; iip++)//! Try to sync/demod/decode each candidate.
    {//do ip=1,ndet

        int ip = indices[iip];//indices[MAXSTEPS]->1700
        int imid=(int)((double)times[ip]*fs_msk144);
        //int a_beg = imid-NPTS/2;
        //if( a_beg < 0 ) a_beg=0;
        //int a_end = imid+NPTS/2;
        //if( a_end > n ) a_end=n;
        //qDebug()<<"Get="<<(times[ip]+dt_msk144*(s_istart))<<ip<<ndet;

        if ( imid < NPTS/2 ) imid=NPTS/2;//if( imid .lt. NPTS/2 ) imid=NPTS/2
        if ( imid > n-NPTS/2 ) imid=n-NPTS/2;//if( imid .gt. n-NPTS/2 ) imid=n-NPTS/2

        double t0=times[ip] + dt_msk144*(s_istart);
        //qDebug()<<"times="<<t0+ dt_msk_144*(s_istart);
        int c_dd = 0;
        for (int i = imid-NPTS/2; i<imid+NPTS/2; i++)//cdat=cbig(imid-NPTS/2+1:imid+NPTS/2)
        {
            cdat[c_dd]=cbig[i];
            c_dd++;
        }

        double ferr=ferrs[ip];
        //double nsnr=snrs[ip];   //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        double nsnr=2.0*int(snrs[ip]/2.0);
        if ( nsnr < -4.0 ) nsnr=-4.0;
        if ( nsnr > 24.0 ) nsnr=24.0;
        //if ( nsnr < -5.0 ) nsnr=-5.0;
        //if ( nsnr > 25.0 ) nsnr=25.0;
        double p_duration = 0.0;
        if (ss_msk144ms)
            p_duration = duration[ip];

        //! remove coarse freq error - should now be within a few Hz
        tweak1(cdat,NPTS,-(nrxfreq+ferr),cdat);//call tweak1(cdat,NPTS,-(1500+ferr),cdat)
        //qDebug()<<"ferr="<<ferr;

        //! attempt frame synchronization
        //! correlate with sync word waveforms
        //zero_double_comp_beg_end(cc,0,NPTS);//cc=0
        pomAll.zero_double_comp_beg_end(cc1,0,NPTS);    //cc1=0
        pomAll.zero_double_comp_beg_end(cc2,0,NPTS);    //cc2=0

        for (int i = 0; i<NPTS-(56*6+(41+1)); i++)
        {//do i=1,NPTS-(56*6+41)
            cc1[i]=pomAll.sum_dca_mplay_conj_dca(cdat,i,i+42,cb_msk144);//cc1[i]=sum(cdat(i:i+41)*conjg(cb))
            cc2[i]=pomAll.sum_dca_mplay_conj_dca(cdat,i+56*6,i+56*6+42,cb_msk144); //cc2[i]=sum(cdat(i+56*6:i+56*6+41)*conjg(cb))
            //qDebug()<<"IIIIIIIIIIIIII="<<i; i=2214
        }
        //sum_dca_dca_dca(cc,NPTS,cc1,cc2);//cc=cc1+cc2
        mplay_da_absdca_absdca(dd,NPTS,cc1,cc2);//dd=abs(cc1)*abs(cc2)

        //! Find 6 largest peaks
        for (int ipk = 0; ipk<6; ipk++)
        {//do ipk=1,5
            // HV Good work cc ic1 no dd and ic2
            //int ic1 = maxloc_absdca_beg_to_end(cc,0,NPTS);//iloc=maxloc(abs(cc))
            //ipeaks[ipk]=ic1;//ipeaks(ipk)=ic1
            //zero_double_comp_beg_end(cc,(int)fmax(0,ic1-7),(int)fmin(NPTS-56*6-(41+1),ic1+7+0)); //cc(max(1,ic1-7):min(NPTS-56*6-41,ic1+7))=0.0

            int ic2 = pomAll.maxloc_da_beg_to_end(dd,0,NPTS);//ic2=iloc(1)//iloc=maxloc(dd)
            ipeaks[ipk]=ic2;
            pomAll.zero_double_beg_end(dd,(int)fmax(0,ic2-7),(int)fmin(NPTS-56*6-(41+1),ic2+7+0));//dd(max(1,ic2-7):min(NPTS-56*6-41,ic2+7))=0.0

        }

        //int ndither= 0;//-99;
        for (int ipk = 0; ipk<6; ipk++)
        {//do ipk=1,6

            //! we want ic to be the index of the first sample of the frame
            int ic0=ipeaks[ipk];//ic0=ipeaks(ipk)

            //! fine adjustment of sync index
            //! bb lag used to place the sampling index at the center of the eye
            for (int i = 0; i<6; i++)
            {//do i=1,6   //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
                int cd_b = ic0+i;//hv tested ok -0 HV
                if ( ic0+(11+0)+NSPM < NPTS )    //if( ic0+11+NSPM .le. npts ) then
                {
                    //bb(i) = sum( ( cdat(ic0+i-1+6:ic0+i-1+6+NSPM:6) * conjg( cdat(ic0+i-1:ic0+i-1+NSPM:6) ) )**2 )
                    std::complex<double> sum_1 = 0.0+0.0*complex_i;
                    int b_c = cd_b;
                    for (int x = cd_b+6; x < cd_b+6+NSPM; x+=6)
                    {
                        std::complex<double> ss = (cdat[x]*conj(cdat[b_c]));
                        sum_1+=ss*ss;
                        b_c+=6;
                    }
                    bb[i] = sum_1;
                }
                else
                {
                    //bb(i) = sum( ( cdat(ic0+i-1+6:NPTS:6) * conjg( cdat(ic0+i-1:NPTS-6:6) ) )**2 )
                    std::complex<double> sum_1 = 0.0+0.0*complex_i;
                    int b_c = cd_b;
                    for (int x = cd_b+6; x < NPTS; x+=6)
                    {
                        std::complex<double> ss = (cdat[x]*conj(cdat[b_c]));
                        sum_1+=ss*ss;
                        b_c+=6;
                    }
                    bb[i] = sum_1;
                }
            }

            //iloc=maxloc(abs(bb))
            int ibb = maxloc_absdca_beg_to_end(bb,0,6);//ibb=iloc(1)
            //bba=abs(bb(ibb)) //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
            //bbp=atan2(-imag(bb(ibb)),-real(bb(ibb)))/(2*twopi*6*dt)
            //qDebug()<<"ibb="<<ibb;

            //if ( ibb > 2 ) ibb=ibb-6;
            if ( ibb <= 2 ) ibb=ibb-1;//hv tested ok
            if ( ibb > 2 ) ibb=ibb-7;//hv tested ok
            //if( ibb .le. 3 ) ibb=ibb-1
            //if( ibb .gt. 3 ) ibb=ibb-7

            for (int id = 0; id<3; id++)
            {//do id=1,3            // ! slicer dither
                int is = 0;
                //if ( id == 0 ) is=0;
                if ( id == 1 ) is=-1;
                if ( id == 2 ) is=1;

                //if ( id == 3 ) is=-2;
                //if ( id == 4 ) is=2;
                //if ( id == 5 ) is=-3;
                //if ( id == 6 ) is=3;

                //! Adjust frame index to place peak of bb at desired lag
                int ic=ic0+ibb+is;//ic=ic0+ibb+is
                //qDebug()<<"ic0 ibb is="<<ic0<<ibb<<is<<ic;
                if ( ic < 0 ) ic=ic+NSPM; //if( ic .lt. 1 ) ic=ic+864

                //! Estimate fine frequency error.
                //! Should a larger separation be used when frames are averaged?
                double ferr2=0.0;
                cca=pomAll.sum_dca_mplay_conj_dca(cdat,ic,ic+42,cb_msk144);//cca=sum(cdat(ic:ic+41)*conjg(cb))
                if ( ic+56*6+42 < NPTS ) //if( ic+56*6+41 .le. NPTS ) then
                {
                    ccb=pomAll.sum_dca_mplay_conj_dca(cdat,ic+56*6,ic+56*6+42,cb_msk144);//ccb=sum(cdat(ic+56*6:ic+56*6+41)*conjg(cb))
                    cfac=ccb*conj(cca);//cfac=ccb*conjg(cca)
                    ferr2=atan2(cimag(cfac),creal(cfac))/(twopi*56*6*dt_msk144);//ferr2=atan2(imag(cfac),real(cfac))/(twopi*56*6*dt)
                    //qDebug()<<"V1=";
                }
                else
                {
                    ccb=pomAll.sum_dca_mplay_conj_dca(cdat,ic-88*6,ic-88*6+42,cb_msk144);//ccb=sum(cdat(ic-88*6:ic-88*6+41)*conjg(cb))
                    cfac=ccb*conj(cca);//cfac=cca*conjg(ccb)
                    ferr2=atan2(cimag(cfac),creal(cfac))/(twopi*88*6*dt_msk144);//ferr2=atan2(imag(cfac),real(cfac))/(twopi*88*6*dt)
                    //qDebug()<<"V2=";
                }

                //! Final estimate of the carrier frequency - returned to the calling program
                double fest=(int)(nrxfreq+ferr+ferr2);//fest=1500+ferr+ferr2
                //qDebug()<<"ferr ferr2="<<ferr<<ferr2;

                for (int idf = 0; idf < 5; idf++)// do idf=0,4   ! frequency jitter
                {//do idf=0,6   do idf=0,10   ! frequency jitter
                    double deltaf = 0.0;
                    if ( idf == 0 )
                        deltaf=0.0;
                    else if ( fmod(idf,2) == 0 )
                        deltaf=(double)idf;//deltaf=(double)idf/2.0; //deltaf=idf
                    else
                        deltaf=-((double)idf+1.0); //deltaf=-((double)idf+1.0)/2.0; //deltaf=-(idf+1)
                    //qDebug()<<"deltaf="<<deltaf;

                    //! Remove fine frequency error
                    tweak1(cdat,NPTS,-(ferr2+deltaf),cdat2);// call tweak1(cdat,NPTS,-ferr2,cdat2)
                    //qDebug()<<"-ferr2="<<-ferr2;

                    //! place the beginning of frame at index NSPM+1
                    pomAll.cshift1(cdat2,NPTS,ic-(NSPM+0));//cdat2=cshift(cdat2,ic-(NSPM+1))
                    //qDebug()<<"cshift="<<ic-(NSPM+1);

                    for (int iav = 0; iav<8; iav++)//! Hopefully we can eliminate some of these after looking at more examples
                    {//do iav=1,7 ! try each of 7 averaging patterns, hope that one works

                        if ( iav == 0 )
                        {
                            //c(1:NSPM)=cdat2(NSPM+1:2*NSPM)  !avg 1 frame to the right of ic
                            copy_dca_or_sum_max3dca(c,NSPM,cdat2,NSPM);
                            //qDebug()<<"INAV0";
                        }
                        else if ( iav == 1 )
                        {
                            //c=cdat2(NSPM-431:NSPM+432)      !1 frame centered on ic
                            copy_dca_or_sum_max3dca(c,NSPM,cdat2,NSPM-432);
                            pomAll.cshift1(c,NSPM,-432);//c=cshift(c,-432)
                            //qDebug()<<"INAV1";
                        }
                        else if ( iav == 2 )
                        {
                            //c=cdat2(2*NSPM-431:2*NSPM+432)
                            copy_dca_or_sum_max3dca(c,NSPM,cdat2,2*NSPM-432);
                            pomAll.cshift1(c,NSPM,-432);//c=cshift(c,-432)
                            //qDebug()<<"INAV2";
                        }
                        else if ( iav == 3 )
                        {
                            //c=cdat2(1:NSPM)  // !1 frame to the left of ic
                            copy_dca_or_sum_max3dca(c,NSPM,cdat2,0);
                            //qDebug()<<"INAV3";
                        }
                        else if ( iav == 4 )
                        {
                            //c=cdat2(2*NSPM+1:NPTS)
                            copy_dca_or_sum_max3dca(c,NSPM,cdat2,2*NSPM);
                            //c=cdat2(NSPM+432:NSPM+432+863)  !1 frame beginning 36ms to the right of ic
                            //copy_dca_or_sum_max3dca(c,NSPM,cdat2,NSPM+432);//432
                            //cshift(c,NSPM,432);//c=cshift(c,432)
                            //qDebug()<<"INAV4";
                        }
                        else if ( iav == 5 )
                        {
                            //c=cdat2(1:NSPM)+cdat2(NSPM+1:2*NSPM)
                            copy_dca_or_sum_max3dca(c,NSPM,cdat2,0,cdat2,NSPM);
                            //qDebug()<<"INAV5";
                        }
                        else if ( iav == 6 )
                        {
                            //c=cdat2(NSPM+1:2*NSPM)+cdat2(2*NSPM+1:NPTS)
                            copy_dca_or_sum_max3dca(c,NSPM,cdat2,NSPM,cdat2,2*NSPM);
                            //qDebug()<<"INAV6";
                        }
                        else if ( iav == 7 )
                        {
                            //c=cdat2(1:NSPM)+cdat2(NSPM+1:2*NSPM)+cdat2(2*NSPM+1:NPTS)
                            copy_dca_or_sum_max3dca(c,NSPM,cdat2,0,cdat2,NSPM,cdat2,2*NSPM);
                            //qDebug()<<"INAV7";
                        }

                        int nsuccess = 0;
                        msk144decodeframe(c,softbits,msgreceived,nsuccess,ident,true);
                        if (nsuccess > 0)
                        {
                            int ndupe=0;
                            for (int im = 0; im<nmessages; im++)
                            {//do im=1,nmessages
                                if ( allmessages[im] == msgreceived ) ndupe=1;
                            }
                            //qDebug()<<"msgreceived=---------->"<<msgreceived<<ip;
                            int ncorrected = 0;
                            double eyeopening = 0.0;
                            msk144signalquality(c,nsnr,fest,t0,softbits,msgreceived,s_HisCall,ncorrected,
                                                eyeopening,s_trained_msk144,s_pcoeffs_msk144,false);//false no calc pcoeffs

                            if ( ndupe == 0 && nmessages<20 )
                            {
                                if (f_only_one_color)
                                {
                                    f_only_one_color = false;
                                    SetBackColor();
                                }

                                allmessages[nmessages]=msgreceived;

                                int df_hv = fest-nrxfreq;//int df_hv = freq2-nrxfreq+idf1;
                                //if (msk144_ft8_cont_msg)//1.69
                                //msgreceived = TGenMsk->fix_contest_msg(My_Grid_Loc,msgreceived);//for " R " in msg 1.31

                                QStringList list;
                                list <<s_time<<QString("%1").arg(t0,0,'f',1)<<""<<
                                QString("%1").arg((int)nsnr)<<""<<QString("%1").arg((int)df_hv)
                                <<msgreceived<<QString("%1").arg(iav+1)  //navg +1 za tuk  no ne 0 hv 1.31
                                <<QString("%1").arg(ncorrected)<<QString("%1").arg(eyeopening,0,'f',1)
                                <<QString(ident)+" "+QString("%1").arg((int)fest);

                                //qDebug()<<"Msk144msgreceived="<<msgreceived<<"durat=="<<p_duration;
                                if (ss_msk144ms)
                                {
                                    //int rpt = GetStandardRPT(p_duration,nsnr);//  (double width, double peak)
                                    //list.replace(2,QString("%1").arg((int)(p_duration*1000.0)));
                                    list.replace(2,str_round_20ms(p_duration));
                                    list.replace(4,GetStandardRPT(p_duration,nsnr));
                                }

                                //emit EmitDecodetText(list,s_fopen,true);//1.27 psk rep   fopen bool true    false no file open
                                SetDecodetTextMsk2DL(list);//2.46

                                nmessages++;

                                if (s_mousebutton == 0) // && t2!=0.0 1.32 ia is no real 0.0   mousebutton Left=1, Right=3 fullfile=0 rtd=2
                                    EmitDecLinesPosToDisplay(nmessages,t0,t0,s_time);
                            }
                            goto c999;
                        }

                    }    //! frame averaging loop
                }        //! frequency dithering loop
            }            //! sample-time dither loop
        }                //! peak loop - could be made more efficient
        msgreceived="";  //msgreceived=' '
        //ndither=-98;
c999:
        //continue;
        if ( nmessages >= 3 )// nai mnogo 3 razli4ni
            return; //exit
    }
}

void DecoderMs::cshift2(std::complex<double> *a,std::complex<double> *b,int cou,int ish)
{
    //HV for save vareable b in orginal and out is a
    //Left Shift 	ISHFT 	ISHFT(N,M) (M > 0) 	<< 	n<<m 	n shifted left by m bits
    //Right Shift 	ISHFT 	ISHFT(N,M) (M < 0) 	>> 	n>>m 	n shifted right by m bits

    if (ish>0)
    {
        for (int i = 0; i <  cou; i++)
        {
            if (i+ish<cou)
                a[i]=b[i+ish];
            else
                a[i]=b[i+ish-cou];
        }
    }
    if (ish<0)
    {
        for (int i = 0; i <  cou; i++)
        {
            if (i+ish<0)
                a[i]=b[i+ish+cou];
            else
                a[i]=b[i+ish];
        }
    }
}
/*
void cshift_int(int *a,int cou_a,int ish)
{
    //Left Shift 	ISHFT 	ISHFT(N,M) (M > 0) 	<< 	n<<m 	n shifted left by m bits
    //Right Shift 	ISHFT 	ISHFT(N,M) (M < 0) 	>> 	n>>m 	n shifted right by m bits

    int t[cou_a];
    for (int i=0; i< cou_a; i++)
        t[i]=a[i];

    if (ish>0)
    {
        for (int i = 0; i <  cou_a; i++)
        {
            if (i+ish<cou_a)
                a[i]=t[i+ish];
            else
                a[i]=t[i+ish-cou_a];
        }
    }
    if (ish<0)
    {
        for (int i = 0; i <  cou_a; i++)
        {
            if (i+ish<0)
                a[i]=t[i+ish+cou_a];
            else
                a[i]=t[i+ish];
        }
    }
}
void cshift_int2(int *a,int *b,int cou,int ish)
{
    //Left Shift 	ISHFT 	ISHFT(N,M) (M > 0) 	<< 	n<<m 	n shifted left by m bits
    //Right Shift 	ISHFT 	ISHFT(N,M) (M < 0) 	>> 	n>>m 	n shifted right by m bits

    if (ish>0)
    {
        for (int i = 0; i <  cou; i++)
        {
            if (i+ish<cou)
                a[i]=b[i+ish];
            else
                a[i]=b[i+ish-cou];
        }
    }
    if (ish<0)
    {
        for (int i = 0; i <  cou; i++)
        {
            if (i+ish<0)
                a[i]=b[i+ish+cou];
            else
                a[i]=b[i+ish];
        }
    }
}
*/

void DecoderMs::opdetmsk144(std::complex<double> *cbig,int n,double s_istart,int &nmessages)
{
    //int uu[5]={1,2,3,4,5};
    //int uu2[5];
    //qDebug()<<uu[0]<<uu[1]<<uu[2]<<uu[3]<<uu[4];
    //qDebug()<<uu[0]<<uu[1]<<uu[2]<<uu[3]<<uu[4];
    //cshift_int(uu,5,4);
    // memmove( uu +3, uu, sizeof( uu ) -sizeof( uu[ 0 ] ) );
    // qDebug()<<uu[0]<<uu[1]<<uu[2]<<uu[3]<<uu[4];
    //qDebug()<<uu2[0]<<uu2[1]<<uu2[2]<<uu2[3]<<uu2[4];

    //parameter (NSPM=864, NPTS=7*NSPM, MAXCAND=16)
    //int MAXCAND=16;//16 20; if 20 more diferent pings decodet HV
    const int NSPM=864;
    //int NFFT=NSPM;
    const int NAVG=7;//7
    const int NPTS=NAVG*NSPM;
    int NSTEP=6000;

    QString allmessages[20];

    std::complex<double> cdat[NPTS];
    std::complex<double> cdat2[NPTS];

    std::complex<double> c[NSPM];
    //std::complex<double> cr_[NAVG][NSPM];
    //std::complex<double> cc1[NSPM];//cc1(0:NSPM-1)
    //std::complex<double> cc2[NSPM];//cc2(0:NSPM-1)
    std::complex<double> cc[NSPM]; //complex cc(0:NSPM-1)
    std::complex<double> csum;

    std::complex<double> ct[NSPM+10];   //complex ct(NSPM)
    double ccm[NSPM];//ccm(0:NSPM-1)
    double ccms[NSPM];//(0:NSPM-1)
    //double dd[NSPM];//real dd(0:NSPM-1)
    std::complex<double> cs[NSPM];
    int ipeaks[10];
    //std::complex<double> cfac,cca,ccb;
    double softbits[144];
    //int hardbits[144];
    //double lratio[128];
    //double llr[128];
    //char decoded[80];
    QString msgreceived;
    char ident; //text->'$' codet->'*' short rpt msk40->'#';
    bool f_only_one_color = true;


    int ntol=G_DfTolerance;

    double nrxfreq = 1500.0;

    //int ncorrected = 0;
    //double eyeopening = 0.0;

    first_msk144();

    double trec=(double)NPTS/12000.0;  //! Duration of the data record

    //if ( n < 24000 || n > 49000) return;//26pix 2s  4s  //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if ( n < NPTS || n > 181000) return;//0,5s-15s if( n .lt. NPTS .or. n .gt. 181000) return
    int nsteps=2*n/NSTEP-0;
    double nsnr=0.0; //HV 1.31 be6e -4.0

    //double bestf=0.0;
    //qDebug()<<"nsteps="<<nsteps;//  144
    for (int istep = 0; istep<nsteps; istep++)
    {//do istep=1,nsteps
        int ib=(istep)*NPTS/2+0; //ib=(istep-1)*NPTS/2+1
        int ie=ib+NPTS-0;  //ie=ib+NPTS-1
        if ( ie > n ) //if( ie .gt. n ) then
        {
            ie=n; //ie=n
            ib=n-NPTS+0; //ib=n-NPTS+1
        }

        double t0=dt_msk144*(s_istart)+(ib-0)/12000.0+trec/2.0;// t0=t00+(ib-1)/12000.0+trec/2

        int c_c=0;
        //qDebug()<<ib<<ie<<n;
        for (int i = ib; i<ie; i++)//cdat=cbig(ib:ib+NPTS-1)
        {
            cdat[c_c]=cbig[i];
            c_c++;
        }

        double xmax=0.0;
        double bestf=0.0;
        for (int ifr = -ntol; ifr < ntol; ifr++)
        {//do if=-ntol,ntol   //! search for frequency that maximizes sync correlation
            double ferr=(double)ifr;

            //! shift analytic signal to baseband
            tweak1(cdat,NPTS,-(nrxfreq+ferr),cdat2);//call tweak1(cdat,NPTS,-(1500+ferr),cdat2)

            //cr=reshape(cdat2,(/NSPM,NAVG/))//  complex cr(NSPM,NAVG)   864,7
            //c=sum(cr,2)
            pomAll.zero_double_comp_beg_end(c,0,NSPM);//c=0
            for (int i = 0; i < NAVG; i++)//for (int i = 0; i < 7; i++)
            {
                for (int j = 0; j < NSPM; j++)
                    c[j]+=cdat2[j+NSPM*i];
            }

            /*for (int i = 0; i < 7; i++)
            {//do i=1,7
                int ib1=(i-0)*NSPM+0;//ib=(i-1)*NSPM+1
                //int ie=ib1+NSPM-0;//ie=ib1+NSPM-1
                for (int j = 0; j < NSPM; j++)
                    c[j]=c[j]+cdat2[j+ib1];//c(1:NSPM)=c(1:NSPM)+cdat2(ib1:ie)
            }*/

            pomAll.zero_double_comp_beg_end(cc,0,NSPM);//cc=0
            //zero_double_comp_beg_end(cc1,0,NSPM);//cc1=0
            //zero_double_comp_beg_end(cc2,0,NSPM);//cc2=0
            for (int ish = 0; ish < NSPM; ish++)
            {//do ish=0,NSPM-1
                cshift2(ct,c,NSPM,ish);//ct=cshift(c,ish)
                // if(ish==NSPM-1)
                // qDebug()<<creal(ct[0])<<creal(ct[1])<<creal(ct[2])<<creal(ct[3]);

                //cc(ish)=sum(ct(1:42)*conjg(cb))+sum(ct(56*6:56*6+41)*conjg(cb))
                //cc[ish]+=sum_dca_mplay_conj_dca(ct,0,0+42,cb_msk_op144);
                //cc[ish]+=sum_dca_mplay_conj_dca(ct,(56*6-1),(56*6-1+42),cb_msk_op144);//hv -1

                csum=0.0+0.0*complex_i;
                for (int j = 0; j < 42; j++)
                {//do j=1,42
                    //csum=csum+(ct(j)+ct(56*6+j-1))*conjg(cb(j))
                    csum+=(ct[j]+ct[56*6+j-1])*conj(cb_msk144[j]);//hv -0 moze bi
                }
                cc[ish]=csum;
                //cc[ish]=dot_product_dca_sum_dca_dca(ct,ish,ct,336-1+ish,cb_msk144,42);
                //cc1[ish]=sum_dca_mplay_conj_dca(ct,0,0+42,cb_msk_op144);//0 = ish hv cc1(ish)=sum(ct(1:42)*conjg(cb))
                //cc2[ish]=sum_dca_mplay_conj_dca(ct,0+56*6-1,0+56*6+42-1,cb_msk_op144); //0 = ish hv  cc2(ish)=sum(ct(56*6:56*6+41)*conjg(cb))
                ccm[ish]=cabs(cc[ish]);//ccm=abs(cc) v1.33 optimisation
            }

            //for (int i = 0; i< NSPM; i++)
            //    ccm[i]=cabs(cc[i]);//ccm=abs(cc)

            //ccm[i]=cabs(cc1[i]+cc2[i]);//ccm=abs(cc1+cc2)
            //mplay_da_absdca_absdca(dd,NSPM,cc1,cc2);//dd=abs(cc1)*abs(cc2)
            double xb=pomAll.maxval_da_beg_to_end(ccm,0,NSPM);//xb=maxval(ccm)
            if ( xb > xmax ) //then //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
            {
                //qDebug()<<"ferr=>"<<ferr;
                xmax=xb;
                bestf=ferr;
                for (int x = 0; x< NSPM; x++)
                {
                    cs[x]=c[x];
                    ccms[x]=ccm[x];
                }
                //if(ferr==81)
                //break;
            }
        }

        double fest=nrxfreq+bestf;
        //if(fest == 1581)
        //qDebug()<<"msgreceived=---------->"<<fest;

        for (int x = 0; x< NSPM; x++)
        {
            c[x]=cs[x];//c=cs
            ccm[x]=ccms[x];//ccm=ccms
        }

        //! Find 2 largest peaks
        for (int ipk = 0; ipk<2; ipk++)
        {//do ipk=1, 2
            //iloc=maxloc(ccm)
            int ic2=pomAll.maxloc_da_beg_to_end(ccm,0,NSPM);//ic2=iloc(1)
            ipeaks[ipk]=ic2;
            // ccm(max(0,ic2-7):min(NSPM-1,ic2+7))=0.0
            pomAll.zero_double_beg_end(ccm,(int)fmax(0,ic2-7),(int)fmin(NSPM-1,ic2+7));
        }
        for (int ipk = 0; ipk<2; ipk++)
        {//do ipk=1,2
            for (int is = 0; is<3; is++)//bezsmisleno HV
            {//do is=1,3

                int ic0=ipeaks[ipk];
                if ( is==1 ) ic0=fmax(0,ic0-1);//if( is.eq.2 ) ic0=max(1,ic0-1)
                if ( is==2 ) ic0=fmin(NSPM-1,ic0+1);//if( is.eq.3 ) ic0=min(NSPM,ic0+1)
                //for (int i = 0; i< NSPM; i++)
                //ct[i]=c[i];
                cshift2(ct,c,NSPM,ic0);//ct=cshift(c,ic0-1)

                int nsuccess = 0;
                msk144decodeframe(ct,softbits,msgreceived,nsuccess,ident,false);
                //qDebug()<<"msgreceived=---------->"<<msgreceived<<nsuccess<<ic0;
                if (nsuccess > 0)
                {
                    int ndupe=0;
                    for (int im = 0; im<nmessages; im++)
                    {//do im=1,nmessages
                        if ( allmessages[im] == msgreceived ) ndupe=1;
                    }
                    //qDebug()<<"msgreceived=---------->"<<msgreceived<<ip;

                    int ncorrected = 0;
                    double eyeopening = 0.0;

                    msk144signalquality(ct,nsnr,fest,t0,softbits,msgreceived,s_HisCall,ncorrected,
                                        eyeopening,s_trained_msk144,s_pcoeffs_msk144,false);//no calc pcoeffs

                    if ( ndupe == 0 && nmessages<10 )
                    {

                        if (f_only_one_color)
                        {
                            f_only_one_color = false;
                            SetBackColor();
                        }

                        allmessages[nmessages]=msgreceived;

                        ident = '^';

                        int df_hv = fest-nrxfreq;//int df_hv = freq2-nrxfreq+idf1;
                        //if (msk144_ft8_cont_msg)//1.69
                        //msgreceived = TGenMsk->fix_contest_msg(My_Grid_Loc,msgreceived);//for " R " in msg 1.31

                        QStringList list;
                        list <<s_time<<QString("%1").arg(t0,0,'f',1)<<""<<
                        QString("%1").arg((int)nsnr)<<""<<QString("%1").arg((int)df_hv)
                        <<msgreceived<<QString("%1").arg(NAVG)  //navg   za tuk si e 7  no ne 0 hv 1.31
                        <<QString("%1").arg(ncorrected)<<QString("%1").arg(eyeopening,0,'f',1)
                        <<QString(ident)+" "+QString("%1").arg((int)fest);
                        //qDebug()<<"OPd"<<msgreceived;
                        if (ss_msk144ms)
                        {
                            double p_duration = 0.140;
                            //int rpt = GetStandardRPT(p_duration,nsnr);//  (double width, double peak)
                            //list.replace(2,QString("%1").arg((int)(p_duration*1000.0)));
                            list.replace(2,str_round_20ms(p_duration));
                            list.replace(4,GetStandardRPT(p_duration,nsnr));
                        }

                        //emit EmitDecodetText(list,s_fopen,true);//1.27 psk rep   fopen bool true    false no file open
                        SetDecodetTextMsk2DL(list);//2.46

                        nmessages++;//nmessages=nmessages+1
                    }
                    goto c999;
                }
                else
                {
                    msgreceived="";
                }
            } //! slicer dither
        }    // ! peak loop
    }
    msgreceived="";
    //ndither=-98
c999:
    return;
    //continue;
}

QString DecoderMs::extractmessage144(char *decoded,int &nhashflag,char &ident)
{
    QString msgreceived;
    unsigned char i1Dec8BitBytes[10];
    int ihashdec;
    unsigned char i1hash[4]={0};
    int i1hashdec;
    //int i4Dec6BitWords[12];

    //! The decoder found a codeword - compare decoded hash with calculated
    //! Collapse 80 decoded bits to 10 bytes. Bytes 1-9 are the message, byte 10 is the hash
    //Left Shift 	ISHFT 	ISHFT(N,M) (M > 0) 	<< 	n<<m 	n shifted left by m bits
    //Right Shift 	ISHFT 	ISHFT(N,M) (M < 0) 	>> 	n>>m 	n shifted right by m bits
    for (int ibyte = 0; ibyte<10; ibyte++)
    {//do ibyte=1,10
        int itmp=0;
        for (int ibit = 0; ibit<8; ibit++)
        {//do ibit=1,8
            itmp=(itmp << 1)+(1 & decoded[(ibyte-0)*8+ibit]);//itmp=ishft(itmp,1)+iand(1,decoded((ibyte-1)*8+ibit))
            //qDebug()<<(int)decoded[(ibyte-0)*7+ibit];
        }
        i1Dec8BitBytes[ibyte]=itmp;
    }

    //! Calculate the hash using the first 9 bytes.
    ihashdec=nhash(i1Dec8BitBytes,9,146);//ihashdec=nhash(c_loc(i1Dec8BitBytes),int(9,c_size_t),146)
    ihashdec=2*(ihashdec & 255);
    memcpy(i1hash, (unsigned char*)&ihashdec, 4);
    //qDebug()<<"ihashdec="<<ihashdec;

    //! Compare calculated hash with received byte 10 - if they agree, keep the message.
    i1hashdec=i1hash[0];
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    //qDebug()<<"i1hashdec="<<i1hashdec;
    if ( i1hashdec == i1Dec8BitBytes[9] )
    {
        int i4Dec6BitWords[12];
        //! Good hash --- unpack 72-bit message
        for (int ibyte = 0; ibyte<12; ibyte++)
        {//do ibyte=1,12
            int itmp=0;
            for (int ibit = 0; ibit<6; ibit++)
            {//do ibit=1,6
                itmp=(itmp << 1)+(1 & decoded[(ibyte-0)*6+ibit]);//itmp=ishft(itmp,1)+iand(1,decoded((ibyte-1)*6+ibit))
            }
            i4Dec6BitWords[ibyte]=itmp;
        }
        //msgreceived = TGenMsk->unpackmsg(i4Dec6BitWords,ident); //call unpackmsg(i4Dec6BitWords,msgreceived)
        QString c1,c2;
        msgreceived = TGenMsk->unpackmsg144(i4Dec6BitWords,ident,c1,c2,ss_msk144ms);

        //2.00 no hash from mskms
        /*if (!s_fopen && c1.mid(0,2) != "CQ" && c1.mid(0,2) != "73" && !c1.isEmpty())
            update_recent_calls(c1);   //HvTxW::FindBaseCallRemAllSlash(QString str)
        if (!s_fopen && c2.mid(0,2) != "CQ" && c2.mid(0,2) != "73" && !c2.isEmpty()) //c2.mid(0,2) != "73" &&  73 is problem
            update_recent_calls(c2);   */

        //qDebug()<<"c1,c2" <<c1<<c2;

        //for (int i = 0; i<10; i++)
        //qDebug()<<"recent_calls[i]"<<recent_calls[i]<<i;

        //qDebug()<<"c1,c2="<<c1<<c2;
        nhashflag=1;
        //qDebug()<<"DECODET="<<msgreceived<<"ndet==========="<<ip;
    }
    else
    {
        msgreceived="";
        nhashflag=-1;
    }
    return msgreceived;
}

void DecoderMs::msk144decodeframe_p(std::complex<double> *c,double *softbits,QString &msgreceived,int &nsuccess,char &ident,double phase0)
{
    int NSPM = 864;
    //char decoded[80];
    std::complex<double> cfac;
    //double softbits[144];
    int hardbits[144];
    double lratio[128];
    double llr[128];

    cfac=(cos(phase0)+sin(phase0)*complex_i);//cfac=cmplx(cos(phase0),sin(phase0))
    for (int i = 0; i<NSPM; i++)
        c[i]=c[i]*conj(cfac);//c=c*conjg(cfac)

    softbits[0] = 0.0;
    softbits[1] = 0.0;
    //zero_double_beg_end(softbits,0,144);
    for (int i = 0; i<6; i++)
    {
        softbits[0] += cimag(c[i])*pp_msk144[i+6];
        softbits[0] += cimag(c[i+(NSPM-5-1)])*pp_msk144[i];
    }
    //softbits(2)=sum(real(c(1:12))*pp)
    for (int i = 0; i<12; i++)
        softbits[1] += creal(c[i])*pp_msk144[i];

    zero_int_beg_end(hardbits,0,144);//v1.33 0 here important hardbits=0

    for (int i = 1; i<72; i++)
    {//do i=2,72
        //softbits(2*i-1)=sum(imag(c(1+(i-1)*12-6:1+(i-1)*12+5))*pp)
        double sum01 = 0.0;
        for (int j = 0; j<12; j++)
            sum01 += cimag(c[((i)*12-6)+j])*pp_msk144[j];
        softbits[2*i-0]=sum01;

        //softbits(2*i)=sum(real(c(7+(i-1)*12-6:7+(i-1)*12+5))*pp)
        sum01 = 0.0;
        for (int j = 0; j<12; j++)
            sum01 += creal(c[(6+(i)*12-6)+j])*pp_msk144[j];
        softbits[2*i+1]=sum01;

        if ( softbits[2*i] >= 0.0 )//v1.33 if( softbits(i) .ge. 0.0 ) then
            hardbits[2*i]=1;
        if ( softbits[2*i+1] >= 0.0 )//v1.33 if( softbits(i) .ge. 0.0 ) then
            hardbits[2*i+1]=1;
    }
    if ( softbits[0] >= 0.0 )//2.06 lost first 2bits
        hardbits[0]=1;
    if ( softbits[1] >= 0.0 )//2.06 lost first 2bits
        hardbits[1]=1;

    //zero_int_beg_end(hardbits,0,144);// 0 here important hardbits=0
    /*
    for (int i = 0; i<144; i++)
    {//do i=1,144
        if ( softbits[i] >= 0.0 )// if( softbits(i) .ge. 0.0 ) then
            hardbits[i]=1;
    }
    */
    //nbadsync1=(8-sum( (2*hardbits(1:8)-1)*s8 ) )/2
    //nbadsync2=(8-sum( (2*hardbits(1+56:8+56)-1)*s8 ) )/2
    int nbadsync1 = 0;
    int nbadsync2 = 0;
    for (int i = 0; i<8; i++)
    {
        nbadsync1+=(2*hardbits[i]-1)*s_msk144_2s8[i];
        nbadsync2+=((2*hardbits[i+57-1]-1)*s_msk144_2s8[i]);
    }
    nbadsync1 = (8-nbadsync1)/2;
    nbadsync2 = (8-nbadsync2)/2;
    /* stop v1.33 cycles optimisation
    //nbadsync2=(8-sum( (2*hardbits(1+56:8+56)-1)*s8 ) )/2
    int nbadsync2 = 0;
    for (int i = 0; i<8; i++)
        nbadsync2+=((2*hardbits[i+57-1]-1)*s_msk144_2s8[i]);
    nbadsync2 = (8-nbadsync2)/2;
    */
    int nbadsync=nbadsync1+nbadsync2;//nbadsync=nbadsync1+nbadsync2

    //qDebug()<<"nbadsync="<<nbadsync;
    if ( nbadsync > 4 ) return;//if( nbadsync .gt. 4 ) cycle
    //if ( nbadsync < 1 )
    //qDebug()<<"DEC==============="<<nbadsync;

    //! normalize the softsymbols before submitting to decoder
    double sav = 0.0;
    double s2av = 0.0;
    for (int i = 0; i<144; i++)
    {
        sav+=softbits[i];//sav=sum(softbits)/144
        s2av+=(softbits[i]*softbits[i]);//s2av=sum(softbits*softbits)/144
    }
    sav = sav/144.0;
    s2av = s2av/144.0;

    double ssig=sqrt(s2av-(sav*sav));//ssig=sqrt(s2av-sav*sav)
    if (ssig==0.0)//no devide by zero
        ssig=1.0;
    for (int i = 0; i<144; i++)//softbits=softbits/ssig
        softbits[i]=softbits[i]/ssig;

    double sigma=0.60;//2.06 tested 0.60 ok new v2 sigma=0.60  my old v1 sigma=0.75
    if (ss_msk144ms)
        sigma=0.75;//2.06 for MSKMS my old v1 sigma=0.75

    copy_double_ar_ainc(lratio,0,1,softbits,8,9+47);//lratio(1:48)=softbits(9:9+47)
    copy_double_ar_ainc(lratio,48,1,softbits,64,65+80-1);//lratio(49:128)=softbits(65:65+80-1)
    //llr=2.0*lratio/(sigma*sigma)
    //lratio=exp(2.0*lratio/(sigma*sigma))
    for (int i = 0; i<128; i++)
    {
        llr[i]=2.0*lratio[i]/(sigma*sigma);
        //qDebug()<<i<<llr[i];
        //lratio[i]=exp(2.0*lratio[i]/(sigma*sigma));
    }

    int max_iterations=10;//max_iterations=10
    //int niterations= -1;


    if (ss_msk144ms)
    {
        int niterations= -1;
        char decoded[80];
        TGenMsk->bpdecode144(llr,max_iterations,decoded,niterations);

        //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        //qDebug()<<"niterations=---------->"<<niterations;
        if ( niterations >= 0 )//if( niterations .ge. 0.0 ) then
        {
            int nhashflag = -1;
            msgreceived = extractmessage144(decoded,nhashflag,ident);
            if ( nhashflag > 0 ) //then  ! CRCs match, so print it
                nsuccess=1;
        }
    }
    else
    {
        //const int N=128;
        //bool apmask[N];//apmask(N)apmask=0;  //N=128, K=90, M=N-K=38
        //bool cw[N];//cw(N)
        //for (int i = 0; i<128; ++i)
        //apmask[i]=0;//2.06 fatal error in v2.05
        bool decoded77[120];
        int nharderror = -1;

        TGenMsk->bpdecode128_90(llr,max_iterations,decoded77,nharderror);//cw,apmask,niterations
        //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        //qDebug()<<nharderror;
        if ( nharderror >= 0 && nharderror < 18 )
        {
            ident = '*';//normal->'*' SH msk40='#'  freetxt -> ident = '$'  deep='^';
            nsuccess=1;
            //write(c77,'(77i1)') decoded77
            //read(c77(72:77),'(2b3)'),n3,i3
            int i3,n3;
            n3=4*decoded77[71] + 2*decoded77[72] + decoded77[73];//??? check need
            i3=4*decoded77[74] + 2*decoded77[75] + decoded77[76];//??? check need
            //if( (i3.eq.0.and.(n3.eq.1 .or. n3.eq.3 .or. n3.eq.4 .or. n3.gt.5)) .or. i3.eq.3 .or. i3.gt.4 ) then
            //qDebug()<<"i3="<<i3<<"n3="<<n3;
            //if ((i3==0 && (n3==1 || n3==3 || n3==4 || n3>5)) || i3==3 || i3>4)
            if ((i3==0 && (n3==1 || n3==3 || n3==4 || n3>5)) || i3==3 || i3>5)//2.51 i3>4 to i3>5
                nsuccess=0;          	
            else
            {
                bool unpk77_success;
                msgreceived = TGenMsk->unpack77(decoded77,unpk77_success); //qDebug()<<"msgreceived="<<msgreceived<<unpk77_success;
                if (!unpk77_success)
                    nsuccess=0;
                else
                {
                    QString c1;// = "";
                    QString c2;// = "";
                    TGenMsk->Get_C1_C2_RX_Calls(c1,c2);
                    if (!s_fopen && !c1.isEmpty() && c1.mid(0,2) != "CQ" && c1.mid(0,2) != "73")
                        update_recent_calls(c1);
                    if (!s_fopen && !c2.isEmpty() && c2.mid(0,2) != "CQ" && c2.mid(0,2) != "73")
                        update_recent_calls(c2);
                    //qDebug()<<"Hash Cals="<<c1<<c2;
                }
            }
        }
    }
}

void DecoderMs::msk144decodeframe(std::complex<double> *c,double *softbits,QString &msgreceived,int &nsuccess,char &ident,bool f_phase)
{
    std::complex<double> cca,ccb;
    double phase0;
    nsuccess=0;

    //first_msk144();// no needed HV

    //! Estimate final frequency error and carrier phase.
    cca=pomAll.sum_dca_mplay_conj_dca(c,0,42,cb_msk144);//cca=sum(c(1:1+41)*conjg(cb))
    ccb=pomAll.sum_dca_mplay_conj_dca(c,0+56*6,0+56*6+42,cb_msk144);//ccb=sum(c(1+56*6:1+56*6+41)*conjg(cb))
    //cfac=ccb*conj(cca);//cfac=ccb*conjg(cca)
    //ffin=atan2(imag(cfac),real(cfac))/(twopi*56*6*dt)
    phase0=atan2(cimag(cca+ccb),creal(cca+ccb));////phase0=atan2(imag(cca+ccb),real(cca+ccb))

    if (f_phase)
    {
        //for (int ipha = 0; ipha < 1; ++ipha)//2.12 ne minava nikoga r_lz2hv
        //{//do ipha=1,1
        //if ( ipha == 1 ) phase0=phase0+30.0*pi/180.0; //if( ipha.eq.2 ) phase0=phase0+30*pi/180.0
        //if ( ipha == 2 ) phase0=phase0-30.0*pi/180.0;//if( ipha.eq.3 ) phase0=phase0-30*pi/180.0
        msk144decodeframe_p(c,softbits,msgreceived,nsuccess,ident,phase0);

        if ( nsuccess > 0 ) //then  ! CRCs match, so print it
            return;
        //}
    }
    else
        msk144decodeframe_p(c,softbits,msgreceived,nsuccess,ident,phase0);
}

///mskrtd/////
std::complex<double> DecoderMs::dot_product_dca_dca(std::complex<double> *a,int b_a,std::complex<double> *b,int b_b,int count)
{
    std::complex<double> sum = 0.0+0.0*complex_i;
    //double sum = 0.0;
    //If the vectors are COMPLEX -> DOT_PRODUCT(VECTOR_A, VECTOR_B) = SUM(CONJG(VECTOR_A)*VECTOR_B)
    for (int i = 0; i < count; i++)
    {
        sum += a[i+b_a] * conj(b[i+b_b]);
    }
    return sum;
}
std::complex<double> DecoderMs::dot_product_dca_sum_dca_dca(std::complex<double> *a,int a_b,int b_b,std::complex<double> *c,int c_count)
{
    std::complex<double> sum = 0.0+0.0*complex_i;
    //double sum = 0.0;
    //If the vectors are COMPLEX -> DOT_PRODUCT(VECTOR_A, VECTOR_B) = SUM(CONJG(VECTOR_A)*VECTOR_B)
    for (int i = 0; i < c_count; i++)
    {
        sum += (a[i+a_b]+a[i+b_b]) * conj(c[i]);
    }
    return sum;
}
void DecoderMs::msk144_freq_search(std::complex<double> *cdat,double fc,int if1,int if2,double delf,int nframes,
                                   int *navmask,double &xmax,double &bestf,std::complex<double> *cs,double *xccs)/*std::complex<double> *cdat2,*/
{
    const int NSPM=864;
    const int c_cdat2 = NSPM*8;//NSPM*nframes nframes=max=8
    int navg;
    std::complex<double> c[NSPM];
    std::complex<double> ct2[2*NSPM];
    std::complex<double> cc[NSPM];
    double xcc[NSPM];

    std::complex<double> cdat2[c_cdat2];
    //zero_double_comp_beg_end(cdat2,0,c_cdat2);

    navg = sum_ia(navmask,0,nframes); //=1.68->nframes no 3   navg=sum(navmask)
    int n=nframes*NSPM; //n=nframes*NSPM
    //qDebug()<<nframes;
    double fac=1.0/(48.0*sqrt((double)navg));//fac=1.0/(48.0*sqrt(float(navg)))

    for (int ifr = if1; ifr < if2; ++ifr)
    {//do ifr=if1,if2            //!Find freq that maximizes sync
        double ferr=(double)ifr*delf; //ferr=ifr*delf
        tweak1(cdat,n,-(fc+ferr),cdat2);//call tweak1(cdat,n,-(fc+ferr),cdat2)
        pomAll.zero_double_comp_beg_end(c,0,NSPM);//c=0

        for (int i = 0; i < nframes; ++i)
        {//do i=1,nframes
            int ib=i*NSPM;//ib=(i-1)*NSPM+1
            //int ie=ib+NSPM-0;//ie=ib+NSPM-1
            if ( navmask[i] == 1 )//HV->1 then  if( navmask(i) .eq. 1 ) then
            {
                for (int z = 0; z < NSPM; ++z)
                    c[z]+=cdat2[z+ib];//c=c+cdat2(ib:ie)
            }
        }

        //zero_double_comp_beg_end(cc,0,NSPM);//2.28 cc=0
        for (int i = 0; i < NSPM; ++i)
        {
            ct2[i]=c[i];          //ct2(1:NSPM)=c
            ct2[i+NSPM]=c[i];     //ct2(NSPM+1:2*NSPM)=c
            cc[i]=0.0+0.0*complex_i;
        }
        double xb = 0.0;
        for (int ish = 0; ish < NSPM; ++ish)
        {//do ish=0,NSPM-1
            //cc(ish)=dot_product(ct2(1+ish:42+ish)+ct2(337+ish:378+ish),cb(1:42))
            cc[ish]=dot_product_dca_sum_dca_dca(ct2,ish,335+ish,cb_msk144,42);//2.06 336-1 tested
            xcc[ish]=cabs(cc[ish]);         //xcc=abs(cc)

            double xb1 = xcc[ish]*fac;//2.28
            if (xb1>xb) xb=xb1;
        }

        //for (int i = 0; i < NSPM; i++)
        //xcc[i]=cabs(cc[i]);         //xcc=abs(cc)

        //double xb=maxval_da_beg_to_end(xcc,0,NSPM)*fac;   //xb=maxval(xcc)*fac
        if (xb>xmax) //then//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        {
            xmax=xb;
            bestf=ferr;//qDebug()<<ferr;
            for (int i = 0; i < NSPM; ++i)
            {
                cs[i]=c[i];                //cs=c
                xccs[i]=xcc[i];            //xccs=xcc
            }
        }
    }
}

void DecoderMs::msk144sync(std::complex<double> *cdat,int nframes,int ntol,double delf,int *navmask,int npeaks,double fc,double &fest,int *npklocs,int &nsuccess,std::complex<double> *c)
{
    const int NSPM=864;
    //double xcc[NSPM];                //real xcc(0:NSPM-1)

    //const int c_cdat2_ = NSPM*8;//NSPM*nframes nframes=max=8

    /*std::complex<double> cdat2_[8][c_cdat2_]; //nframes=max=8 complex cdat2(NSPM*nframes,8)
    double xccs_[8][NSPM];           //real xccs(0:NSPM-1,8)
    std::complex<double> cs_[8][NSPM];       //complex cs(NSPM,8) //complex cs(NSPM)
    double xm[8];
    double bf[8];
    zero_double_beg_end(xm,0,8);
    zero_double_beg_end(bf,0,8);*/

    //std::complex<double> cdat2_[c_cdat2_];
    //std::complex<double> cs_[NSPM];
    double xccs_[NSPM];
    double xm = 0.0;
    double bf = 0.0;
    //zero_double_comp_beg_end(cdat2_,0,c_cdat2_);
    //zero_double_comp_beg_end(cs_,0,NSPM);
    //zero_double_beg_end(xccs_,0,NSPM);

    //integer OMP_GET_NUM_THREADS
    first_msk144();

    int nfreqs=2*(int)(ntol/delf) + 1;

    int nthreads=1;//1;
    //!$ nthreads=min(8,int(OMP_GET_MAX_THREADS(),4))
    int nstep=nfreqs/nthreads;

    //!$OMP PARALLEL NUM_THREADS(nthreads) PRIVATE(id,if1,if2)
    int id=1;//1;
    //!$ id=OMP_GET_THREAD_NUM() + 1            !Thread id = 1,2,...
    int if1=-(int)(ntol/delf) + (id-1)*nstep;
    int if2=if1+nstep-1;//-1
    if (id==nthreads) if2=(int)(ntol/delf);
    //call msk144_freq_search(cdat,fc,if1,if2,delf,nframes,navmask,cb,    &
    //cdat2(1,id),xm(id),bf(id),cs(1,id),xccs(1,id))
    //if(ntol!=8)
    //qDebug()<<fc<<if1<<if2<<delf<<ntol;
    //msk144_freq_search(cdat,fc,if1,if2,delf,nframes,navmask,cdat2_[id-1],xm[id-1],bf[id-1],cs_[id-1],xccs_[id-1]);
    //msk144_freq_search(cdat,fc,if1,if2,delf,nframes,navmask,xm,bf,cs_,xccs_);/*cdat2_,*/
    msk144_freq_search(cdat,fc,if1,if2,delf,nframes,navmask,xm,bf,c,xccs_);

    //double xmax=xm[0];// xmax=xm(1)
    //fest=fc+bf[0]; //fest=fc+bf(1)
    //double xmax=xm;
    fest=fc+bf;
    //if(fest>=1499 && fest<=1501)
    //qDebug()<<"XXXXX"<<fest<<xmax;

    /*for (int i = 0; i < NSPM; i++)
    {
        //c[i]=cs_[0][i];        //c=cs(1:NSPM,1)
        //xcc[i]=xccs_[0][i];    //xcc=xccs(0:NSPM-1,1)
        c[i]=cs_[i];
        //xcc[i]=xccs_[i];
    }*/

    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    /*if (nthreads>1) //then
    {
        for (int i = 1; i < nthreads; i++)
        {//do i=2,nthreads
            if (xm[i]>xmax) //then
            {
                xmax=xm[i];
                fest=fc+bf[i];
                for (int j = 0; j < NSPM; j++)
                {
                    c[j]=cs_[i][j];        //c=cs(1:NSPM,i)
                    xcc[j]=xccs_[i][j];   // xcc=xccs(0:NSPM-1,i)
                }
            }
        }
    }*/

    //! Find npeaks largest peaks
    for (int ipk = 0; ipk < npeaks; ++ipk)
    {//do ipk=1,npeaks
        //iloc=maxloc(xcc)
        //ic2=iloc(1)
        int ic2 = pomAll.maxloc_da_beg_to_end(xccs_,0,NSPM);
        npklocs[ipk]=ic2;
        //pkamps[ipk]=xcc[ic2-1]; //pkamps(ipk)=xcc(ic2-1)

        //xcc(max(0,ic2-7):min(NSPM-1,ic2+7))=0.0
        //for (int i = fmax(0,ic2-7); i < fmin(NSPM-0,ic2+7); i++)
        //xcc[i]=0.0;
        // ccm(max(0,ic2-7):min(NSPM-1,ic2+7))=0.0
        pomAll.zero_double_beg_end(xccs_,(int)fmax(0,ic2-7),(int)fmin(NSPM-1,ic2+7));
    }

    nsuccess=0;
    //1.3   HV 3.0 maybe
    if ( xm >= 1.3 ) nsuccess=1;

}
void DecoderMs::msk144spd(std::complex<double> *cbig,int n,int &nsuccess,QString &msgreceived,double fc,double &fret,
                          double &tret,char &ident,int &navg,std::complex<double> *ct, double *softbits)
{
    //data tpat/1.5,0.5,2.5,1.0,2.0,1.5/
    //save df,first,fs,pi,twopi,dt,tframe,rcw*/
    const int NSPM=864;
    const int MAXSTEPS=100;
    const int NFFT=864;//NSPM;//da e 4islo 1.46
    const int MAXCAND=7;// org->5 HV from msk40 tested->7
    int NPATTERNS=6; // zaradi dwoen array na staro gcc

    double ferr = 0.0;
    double tonespec[NFFT];
    std::complex<double> ctmp[NFFT+10];
    std::complex<double> cdat[3*NSPM];                    //!Analytic signal
    int navmask[8];//=1.68->nframes 3 to 8

    //int NPATTERNS=6; // zaradi dwoen array na staro gcc
    int navpatterns_[6][3] =
        {
            {
                0,1,0
            },
            {1,0,0},
            {0,0,1},
            {1,1,0},
            {0,1,1},
            {1,1,1}
        };

    std::complex<double> c[NSPM];
    //std::complex<double> ct[NSPM];
    int npkloc[10];

    double detmet_plus[MAXSTEPS+12];
    double *detmet = &detmet_plus[6];         //real detmet(-2:nstep+3)
    double detmet2_plus[MAXSTEPS+12];
    double *detmet2 = &detmet2_plus[6];
    for (int i = -5; i<MAXSTEPS+5; i++)
    {
        detmet[i]=0.0;
        detmet2[i]=0.0;
    }

    double detfer[MAXSTEPS];
    int ntol=G_DfTolerance;

    int nstart[MAXCAND];
    int indices[MAXSTEPS];
    double ferrs[MAXCAND];
    //double snrs[MAXCAND];

    first_msk144();
    double df = (double)fs_msk144/(double)NFFT;
    dftool_msk144(ntol,fc,df);

    //! fill the detmet, detferr arrays
    int istp_real = 0;
    int nstepsize = 216;
    int nstep=(n-NSPM)/nstepsize;  //! 72ms/4=18ms steps

    for (int i = 0; i<MAXSTEPS; i++)
        detfer[i]=-999.99;

    //qDebug()<<"nstep="<<nstep;
    for (int istp = 0; istp<nstep; ++istp)//1.68 ++istp ima efect 100ms
    {//do istp=1,nstep
        int ns=0+nstepsize*(istp-0);//ns=1+256*(istp-1)
        int ne=ns+NSPM-0;    //ne=ns+NSPM-1    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        if ( ne > n ) break;//exit

        pomAll.zero_double_comp_beg_end(ctmp,0,NFFT);//ctmp=cmplx(0.0,0.0)
        int c_c=0;
        for (int i = ns; i<ne; i++)
        {
            ctmp[c_c]=cbig[i];//cdat=cbig(ns:ne)
            c_c++;
        }

        mplay_dca_dca_dca(ctmp,0,NFFT,ctmp,0,ctmp,0,1);//ctmp=ctmp**2
        mplay_dca_dca_da(ctmp,0,12,ctmp,0,rcw_msk144,0,1);//ctmp(1:12)=ctmp(1:12)*rcw
        mplay_dca_dca_da(ctmp,NSPM-12,NSPM,ctmp,NSPM-12,rcw_msk144,11,-1);//ctmp(NSPM-11:NSPM)=ctmp(NSPM-11:NSPM)*rcw(12:1:-1)
        f2a->four2a_c2c(ctmp,NFFT,-1,1);//call four2a(ctmp,nfft,1,-1,1)
        mplay_da_absdca_absdca(tonespec,NFFT,ctmp,ctmp);//tonespec=abs(ctmp)**2
        //qDebug()<<ihlo_msk144<<ihhi_msk144;
        int ihpk = pomAll.maxloc_da_beg_to_end(tonespec,ihlo_msk144,ihhi_msk144);//! high tone search window
        double deltah=-creal( (ctmp[ihpk-1]-ctmp[ihpk+1]) / (2.0*ctmp[ihpk]-ctmp[ihpk-1]-ctmp[ihpk+1]) );//deltah=-real( (ctmp(ihpk-1)-ctmp(ihpk+1)) / (2*ctmp(ihpk)-ctmp(ihpk-1)-ctmp(ihpk+1)) )
        double ah=tonespec[ihpk]; //ah=tonespec(ihpk)

        double ahavp = 0.0;//ahavp=(sum(tonespec,ismask)-ah)/count(ismask)
        for (int i = ihlo_msk144; i<ihhi_msk144; i++)
            ahavp+=tonespec[i];
        ahavp = (ahavp-ah)/(double)(ihhi_msk144-ihlo_msk144);
        double trath=ah/(ahavp+0.01);//trath=ah/(ahavp+0.01)
        //qDebug()<<illo_msk144<<ilhi_msk144;
        int ilpk = pomAll.maxloc_da_end_to_beg(tonespec,illo_msk144,ilhi_msk144);//! window for low tone
        double deltal=-creal( (ctmp[ilpk-1]-ctmp[ilpk+1]) / (2.0*ctmp[ilpk]-ctmp[ilpk-1]-ctmp[ilpk+1]) );//deltal=-real( (ctmp(ilpk-1)-ctmp(ilpk+1)) / (2*ctmp(ilpk)-ctmp(ilpk-1)-ctmp(ilpk+1)) )
        double al=tonespec[ilpk];

        double alavp = 0.0;//alavp=(sum(tonespec,ismask)-al)/count(ismask)
        for (int i = illo_msk144; i<ilhi_msk144; i++)
            alavp+=tonespec[i];
        alavp = (alavp-al)/(double)(ilhi_msk144-illo_msk144);
        double tratl=al/(alavp+0.01);//tratl=al/(alavp+0.01)

        //double fdiff=(ihpk+deltah-ilpk-deltal)*df_144;//fdiff=(ihpk+deltah-ilpk-deltal)*df
        double ferrh=((double)ihpk+deltah-(double)i4000_msk144)*df/2.0;//ferrh=(ihpk+deltah-i4000)*df/2.0
        double ferrl=((double)ilpk+deltal-(double)i2000_msk144)*df/2.0;//ferrl=(ilpk+deltal-i2000)*df/2.0
        //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        //if( abs(fdiff-2000) <= 25.0 )  //!    if( abs(fdiff-2000) .le. 25.0 ) then
        //{
        if ( ah >= al )
            ferr=ferrh;
        else
            ferr=ferrl;

        detmet[istp]=fmax(ah,al);
        detmet2[istp]=fmax(trath,tratl);
        detfer[istp]=ferr;
        istp_real++;
        //if(trath > 12.0 || tratl > 12.0)
        //qDebug()<<"TTTT="<<trath<<ah<<tratl<<al;
    } //! end of detection-metric and frequency error estimation loop

    //qDebug()<<"istp_real-1"<<istp_real-1;
    if (istp_real>0)//1.29
        pomAll.indexx_msk(detmet,istp_real-1,indices);//call indexx(detmet,nstep,indices) !find median of detection metric vector

    //double xmed=detmet[indices[istp/2]];//<-pri men e taka xmed=detmet(indices(nstep/2))
    double xmed=detmet[indices[istp_real/4]];//<-pri men e taka xmed=detmet(indices(nstep/2))
    if (xmed==0.0)//no devide by zero
        xmed=1.0;
    for (int i = 0; i<istp_real; i++)
        detmet[i]=detmet[i]/xmed; //! noise floor of detection metric is 1.0

    int ndet=0;
    for (int ip = 0; ip<MAXCAND; ip++)
    {//do ip=1,20 //! use something like the "clean" algorithm to find candidates
        //iloc=maxloc(detmet)
        int il = pomAll.maxloc_da_beg_to_end(detmet,0,istp_real);//il=iloc(1)  //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE

        if ( detmet[il] < 2.6 ) break; ///*tested 2.6<-hv 3.0*/ if( (detmet(il) .lt. 3.0) ) exit
        if ( fabs(detfer[il]) <= (double)(ntol) ) //if( abs(detfer(il)) .le. 100.0 ) then
        {
            //inportant no(il-1)<-out of array HV tested->(il-0) times(ndet)=((il-1)*256+NPTS/2)*dt
            nstart[ndet]=1+(il-0)*nstepsize+1;                        //nstart(ndet)=1+(il-1)*216+1
            ferrs[ndet]=detfer[il];
            //snrs[ndet]=12.0*log10(detmet[il])/2.0-9.0;    //snrs(ndet)=12.0*log10(detmet(il))/2-9.0
            ndet++;
        }
        //for (int i = il-3; i<il+3+1; i++)// -3 +4 lost good locations
        //for (int i = il-3; i<il+4; i++)
        //detmet[i]=0.0;  //detmet(il-3:il+3)=0.0 !    detmet(max(1,il-1):min(nstep,il+1))=0.0
        detmet[il]=0.0;
        //for (int i = il-3; i<il+4; i++)
        //locate[i]=0.0;  //detmet(il-3:il+3)=0.0
    }

    /*double sss = 0;
    for (int k = 0; k<istp_real; k++)
         sss += detmet2[k];
         sss = sss/istp_real;
    qDebug()<<"mid="<< sss;*/

    if ( ndet < 3 )//for Tropo/ES  if( ndet .lt. 3 ) then
    {
        for (int ip = 0; ip<MAXCAND-ndet; ip++)
        {//do ip=1,MAXCAND-ndet //! Find candidates
            int il = pomAll.maxloc_da_beg_to_end(detmet2,0,istp_real);//iloc=maxloc(detmet2(1:nstep))

            if ( detmet2[il] < 12.0 ) break; //if( (detmet2(il) .lt. 12.0) ) exit  //22
            if ( fabs(detfer[il]) <= (double)(ntol) )//if( abs(detfer(il)) .le. ntol ) then
            {
                nstart[ndet]=1+(il-0)*nstepsize+1;                        //nstart(ndet)=1+(il-1)*216+1
                ferrs[ndet]=detfer[il];//ferrs(ndet)=detfer(il)
                //snrs[ndet]=12.0*log10(detmet2[il])/2.0-9.0; //snrs(ndet)=12.0*log10(detmet2(il))/2-9.0
                ndet++;
            }
            //!detmet2(max(1,il-1):min(nstep,il+1))=0.0
            detmet2[il]=0.0;
        }
        //qDebug()<<"3333333ndet="<<ndet<<xmed;
    }
    //qDebug()<<"ndet="<<ndet<<xmed;
    //if (ndet>0)
    //indexx_msk(times,ndet-1,indices);//HV sprt pings in one scan by time

    nsuccess=0;
    msgreceived="";
    int npeaks=2;
    int ntol0=8;//8;
    double deltaf=2.0;


    for (int icand = 0; icand<ndet; icand++)
    {//do icand=1,ndet  //! Try to sync/demod/decode each candidate.
        int ib=fmax(0,nstart[icand]-NSPM);//ib=max(1,nstart(icand)-NSPM)
        int ie=ib-0+3*NSPM;     //ie=ib-1+3*NSPM
        if ( ie > n ) //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        {
            ie=n;
            ib=ie-3*NSPM+0; //ib=ie-3*NSPM+1
        }

        int c_dd = 0;
        for (int i = ib; i<ie; i++)// cdat=cbig(ib:ie)
        {
            cdat[c_dd]=cbig[i];
            c_dd++;
        }

        double fo=fc+ferrs[icand];
        //qDebug()<<"ib ie="<<fo<<icand;

        for (int iav = 0; iav<NPATTERNS; iav++)
        {//do iav=1,NPATTERNS

            for (int z = 0; z<3; z++)
                navmask[z]=navpatterns_[iav][z];// navmask=navpatterns(1:3,iav)

            int nsyncsuccess = 0;
            double fest = 0.0;
            msk144sync(cdat,3,ntol0,deltaf,navmask,npeaks,fo,fest,npkloc,nsyncsuccess,c);
            //qDebug()<<"nsyncsuccess"<<nsyncsuccess<<fest<<iav;

            if ( nsyncsuccess == 0 ) continue; //cycle


            for (int ipk = 0; ipk<npeaks; ipk++)
            {//do ipk=1,npeaks
                for (int is = 0; is<3; is++)
                {//do is=1,3
                    int ic0=npkloc[ipk];    //qDebug()<<"ic0="<<ic0;
                    if ( is==1) ic0=fmax(0,ic0-1);//if( is.eq.2) ic0=max(1,ic0-1)
                    if ( is==2) ic0=fmin(NSPM-1,ic0+1);//if( is.eq.3) ic0=min(NSPM,ic0+1)

                    //for (int i = 0; i< NSPM; i++)//ct=cshift(c,ic0-1)
                    //ct[i]=c[i];
                    cshift2(ct,c,NSPM,ic0);// opasno r_lz2hv

                    int ndecodesuccess = 0;
                    //call msk144decodeframe(ct,msgreceived,ndecodesuccess)
                    msk144decodeframe(ct,softbits,msgreceived, ndecodesuccess,ident,false);

                    if ( ndecodesuccess > 0 )
                    {
                        //qDebug()<<"ns"<<fo<<msgreceived;
                        tret=(nstart[icand]+NSPM/2)/fs_msk144;//tret=(nstart[icand]+NSPM/2)/fs
                        fret=fest;
                        //!write(*,*) icand, iav, ipk, is, tret, fret, msgreceived
                        navg = sum_ia(navmask,0,3);  //navg=sum(navmask)//za msk40
                        nsuccess=1;
                        //qDebug()<<"msgreceived=========="<<msgreceived<<"==="<<ic0<<ipk;
                        return;
                    }
                }
            }
        }
    }       //! candidate loop
}

void DecoderMs::SetMsk144RxEqual(int i)
{
    if (i==0)//all off
    {
        s_msk144rxequal_s = false;
        s_msk144rxequal_d = false;
    }
    if (i==1)//s
    {
        s_msk144rxequal_s = true;
        s_msk144rxequal_d = false;
    }
    if (i==2)//d
    {
        s_msk144rxequal_s = false;
        s_msk144rxequal_d = true;
    }
    if (i==3)//d+s
    {
        s_msk144rxequal_s = true;
        s_msk144rxequal_d = true;
    }

    //lz2hv reset s_corrd from begining 1.31
    /*for (int i = 0; i<524300; i++)//decoderms.h max 524500 need for auto decode 30s=524288
    {
        //s_corrs[i]=1.0+0.01*complex_i;
        s_corrd[i]=1.0+0.01*complex_i;
    }
    zero_double_beg_end(dpclast_msk144_2,0,3);*/
}
/*template<typename T>
bool is_nan_hv(const T &value)
{
    // True if nan
    return value != value;
}*/
/*
QStringList s_list_rpt_msk;
double ping_width_msk = 0.0;
double prev_ping_t_msk = 0.0;
double last_rpt_snr_msk = 0.0;
bool one_end_ping_msk = false;
bool is_new_rpt_msk = false;
bool use_rpt_db_msk = true;
*/

/*
void DecoderMs::SetPerodTime(int t)
{
	s_period_time = t;
	qDebug()<<"PeriodTime="<<((double)s_period_time-1.5);
}
*/
QString DecoderMs::str_round_20ms(double v)
{
    int val = 0;
    /*     value   round   floor   ceil    trunc
           -----   -----   -----   ----    -----
           2.3     2.0     2.0     3.0     2.0
           3.8     4.0     3.0     4.0     3.0
           5.5     6.0     5.0     6.0     5.0
           -2.3    -2.0    -3.0    -2.0    -2.0
           -3.8    -4.0    -4.0    -3.0    -3.0
           -5.5    -6.0    -6.0    -5.0    -5.0 */

    /// round 10ms
    v = v * 100.0;// in ms
    val = 10*(int)round(v);
    ///end round 10ms

    /// round 20ms
    int k = val / 20;
    val = 20 * k;
    ///end round 20ms

    //val = v *1000.0;//real val

    if (val<72)
        val=72;

    return QString("%1").arg(val);
}
bool f_only_one_color_rtd_msk = true;
void DecoderMs::msk_144_40_rtd(double *dat,int,double s_istart,bool rpt_db_msk)
{
    //qDebug()<<"msk_144_40_rtd"<<ccc<<s_istart;
    if (ss_msk144ms != rpt_db_msk)
        f_first_msk144 = false;
    ss_msk144ms = rpt_db_msk;

    double rms;
    const int NSPM=864;              //!Number of samples per message frame
    //int NFFT=NSPM;
    int NZ= 7168;               // !Block size
    int NFFT1=8192;             //!FFT size for making analytic signal
    int NFFT2=8192*2; //v1.28 my be crash for this +1024
    int NPATTERNS=4;  // zaradi dwoen array na staro gcc          //  !Number of frame averaging patterns to try
    std::complex<double> *cdat = new std::complex<double>[NFFT2+10];               // !Analytic signal
    int ndecodesuccess = 0;
    double tdec = 0.0;
    char ident = 'N';
    int nsnr;
    double snr0;
    double pow[8];

    QString msgreceived = "";

    double t0=0.0;
    //double df_hv=0.0;
    double fest=0.0;
    //QStringList list;
    //bool f_only_one_color = true;

    int iavpatterns_[4][8] = //NPATTERNS=4; zaradi dwoen array na staro gcc
        {//
            {
                1,1,1,1,0,0,0,0
            },
            {0,0,1,1,1,1,0,0},
            {1,1,1,1,1,0,0,0},
            {1,1,1,1,1,1,1,0}
        };

    int iavmask[8];
    int ntol=G_DfTolerance;
    int npkloc[10];
    std::complex<double> ct[NSPM+10];
    std::complex<double> c[NSPM+10];
    double xmc[4] =
        {
            2.0,4.5,2.5,3.5
        }
        ;//NPATTERNS=4; zaradi dwoen array na staro gcc        // !Used to label decode with time at center of averaging mask
    double tframe=(double)NSPM/DEC_SAMPLE_RATE;
    int npat;
    double pmax=-99.0;
    double pavg;
    int np;
    double fc;

    double tsec = s_istart/DEC_SAMPLE_RATE; // vazno zaradi parwia ping

    //1<-ne se polzwa '^'for,2<- dva cikalana '^'for,3<-4cikala na '^'for
    int ndepth = s_decoder_deep;
    int navg = 0; // za msk40 i nadolu

    //qDebug()<<"s_decoder_deep"<<s_decoder_deep;

    int ncorrected = 0;
    double eyeopening = 0.0;
    double softbits[144];
    bool f_calc_pcoeffs = true;
    //bool f_only_one_color = true;

    if (first_rtd_msk)
    {
        tsec0_rtd_msk=tsec;
        s_time_last=s_time;
        pnoise_rtd_msk = -1.0;
        pomAll.zero_double_beg_end(s_pcoeffs_msk144,0,3);

        s_trained_msk144=false;
        s_msglast="";
        s_nsnrlast=-99;
        s_msglastswl="";
        s_nsnrlastswl=-99;

        first_rtd_msk=false;
    }

    //!!! Dupe checking should probaby be moved to mainwindow.cpp
    if ( s_time_last != s_time || tsec < tsec0_rtd_msk ) //if(nutc00.ne.nutc0 .or. tsec.lt.tsec0) then ! reset dupe checker
    {   // reset per period HV
        s_msglast="";
        s_nsnrlast=-99;
        s_msglastswl="";
        s_nsnrlastswl=-99;
        s_time_last=s_time;

        prev_pt_msk = 0.0;
        ping_width_msk = 0.0001;//0.0001 inportent for right ping duration
        prev_ping_t_msk = 0.0;
        one_end_ping_msk = false;
        is_new_rpt_msk = false;
        s_list_rpt_msk.clear();
        last_rpt_snr_msk = 0.0;
        f_only_one_color_rtd_msk = true;
        //pnoise_rtd_msk = -1.0; ???? need to test
        //qDebug()<<"Rrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrr";
    }
    double fac;

    rms=sqrt(dot_product_da_da(dat,dat,NZ,0)/(double)NZ);//rms=sqrt(sum(d(1:NZ)*d(1:NZ))/NZ)
    if (rms<1.0) goto end; //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    //qDebug()<<"RMS>1.0"<<rms;

    /*for (int i = 0; i < NZ; i++)
        dat[i]=dat[i]/(rms/2.0);//2.0*/
    fac=1.0/rms;
    for (int i = 0; i < NZ; ++i)
        dat[i]=fac*dat[i];

    pomAll.zero_double_beg_end(dat,NZ,NFFT1);//d(NZ+1:NFFT1)=0.

    //if(!s_msk144rxequal_s && !s_msk144rxequal_d)
    //analytic_msk144(dat,0,NZ,NFFT1,cdat);      //!Convert to analytic signal and filter
    //else
    analytic_msk144_2(dat,0,NZ,NFFT1,cdat,s_pcoeffs_msk144,s_msk144rxequal_s,s_msk144rxequal_d);      //call analytic(d,NZ,NFFT1,cdat,pcoeffs,.true.) !Convert to analytic signal and filter

    //qDebug()<<s_pcoeffs_msk144[0]<<s_pcoeffs_msk144[1]<<s_pcoeffs_msk144[2];

    pmax=-99;
    for (int i = 0; i < 8; i++)
    {//do i=1,8
        int ib=i*NSPM;
        int ie=ib+NSPM;
        pow[i]=creal(dot_product_dca_dca(cdat,ib,cdat,ib,ie-ib))*(rms*rms);//pow(i)=real(dot_product(cdat(ib:ie),cdat(ib:ie)))*rms**2

        //if ( pow[i] > pmax ) //then
        //pmax=pow[i];
        pmax=fmax(pmax,pow[i]);
    }

    pavg = sum_da(pow,0,8)/8.0;//pavg=sum(pow)/8.0;

    //if (tsec<0.1) pavg = sum_da(pow,5,8)/3.0; //if(tsec.lt.0.1) pavg=sum(pow(6:8))/3.0

    //! Short ping decoder uses squared-signal spectrum to determine where to
    //! center a 3-frame analysis window and attempts to decode each of the
    //! 3 frames along with 2- and 3-frame averages.

    fc = 1500.0;
    np=8*NSPM;
    msk144spd(cdat,np,ndecodesuccess,msgreceived,fc,fest,tdec,ident,navg,ct,softbits);

    //qDebug()<<"End msk_144_40_rtd";
    //if(ndecodesuccess.eq.0 .and. (bshmsg.or.bswl)) then
    if ( ndecodesuccess == 0 && (G_ShOpt || G_SwlOpt))
    {
        //call msk40spd(cdat,np,ntol,mycall,hiscall,nsuccess,msgreceived,fc,fest,tdec,navg)
        msk40spd(cdat,np,ndecodesuccess,msgreceived,fc,fest,tdec,ident,navg,ct,softbits);
        if ( ndecodesuccess >= 1 )  //>=  //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
            f_calc_pcoeffs = false;
        //qDebug()<<"ndecodesuccess3"<<ndecodesuccess ;
    }

    if ( ndecodesuccess >= 1 )
    {
        t0=tsec + tdec;//tdec=tsec+tdec
        //ident='&';
        //if ( equalized_msk144_rtd ) ident='E';// bezsmisleno
        //qDebug()<<"ndecodesuccess3"<<ndecodesuccess ;
        goto c900;
    }

    //! If short ping decoder doesn't find a decode,
    //! Fast - short ping decoder only.
    //! Normal - try 4-frame averages
    //! Deep - try 4-, 5- and 7-frame averages.
    npat=NPATTERNS;
    if ( ndepth == 1 ) npat=0;
    if ( ndepth == 2 ) npat=2;
    for (int iavg = 0; iavg < npat; iavg++)
    {//do iavg=1,npat
        for (int j = 0; j < 8; j++)
            iavmask[j]=iavpatterns_[iavg][j];//iavpatterns(1:8,iavg)

        navg = sum_ia(iavmask,0,8);     //navg=sum(iavmask)

        if (navg==0)//no devide by zero
            navg=1;

        //double deltaf=7.0/(double)navg;//hv probvano 1.68=7.9<-super 1.67=7.0 ne=10.0 deltaf=7.0/real(navg) //! search increment for frequency sync
        double deltaf=10.0/(double)navg;//2.06 for v2 tested
        int npeaks=2;//2
        int nsyncsuccess = 0;

        msk144sync(cdat,8,ntol,deltaf,iavmask,npeaks,fc,fest,npkloc,nsyncsuccess,c);

        if ( nsyncsuccess == 0 ) continue;//cycle
        //qDebug()<<"SSSS"<<nsyncsuccess ;

        for (int ipk = 0; ipk < npeaks; ipk++)
        {//do ipk=1,npeaks
            for (int is = 0; is < 3; is++)
            {//do is=1,3
                int ic0=npkloc[ipk];
                if (is==1) ic0=fmax(0,ic0-1);//if(is.eq.2) ic0=max(1,ic0-1)
                if (is==2) ic0=fmin(NSPM-1,ic0+1);//if(is.eq.3) ic0=min(NSPM,ic0+1)

                cshift2(ct,c,NSPM,ic0);// opasno r_lz2hv

                ndecodesuccess = 0;
                //call msk144decodeframe(ct,msgreceived,ndecodesuccess)
                msk144decodeframe(ct,softbits,msgreceived,ndecodesuccess,ident,false);

                if (ndecodesuccess > 0)
                {
                    t0=tsec+xmc[iavg]*tframe;//tdec=tsec+xmc[iavg]*tframe
                    ident='^';
                    //if ( equalized_msk144_rtd ) ident='E';//bezsmisleno
                    //if ( equalized_msk144_rtd && is != 1 ) ident='!'; //bezsmisleno //!help decide if is dither is needed
                    goto c900;
                }
            }                        //!Slicer dither
        }                            //!Peak loop
    }

    msgreceived="";

    //! no decode - update noise level used for calculating displayed snr.
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if ( pnoise_rtd_msk < 0.0 ) //then         //! initialize noise level
        pnoise_rtd_msk=pavg;
    else if ( pavg > pnoise_rtd_msk ) //then  //! noise level is slow to rise
        pnoise_rtd_msk=0.9*pnoise_rtd_msk+0.1*pavg;
    else if ( pavg < pnoise_rtd_msk )// then  //! and quick to fall
        pnoise_rtd_msk=pavg;
    //qDebug()<<"pnoise_rtd_msk="<<pnoise_rtd_msk<<pavg;

    goto end;

c900:

    //! Successful decode - estimate snr
    if ( pnoise_rtd_msk > 0.0 ) //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {
        double nozero = pmax/pnoise_rtd_msk-1.0;
        if (nozero<0.000001) nozero=0.000001;
        snr0=10.0*log10(nozero);
    }
    else
        snr0=0.0;
    //if (is_nan_hv(snr0))// inportent exception 1.31
    //snr0=-10.0;
    nsnr=(int)snr0;

    //if (s_msk144rxequal_s && !s_trained_msk144 && s_msk144rxequal_d) ident='^';  //s+d - changes
    if (s_msk144rxequal_s && s_trained_msk144 && s_msk144rxequal_d)  ident='$';  //s+d + changes
    if (!s_msk144rxequal_s && s_trained_msk144 && s_msk144rxequal_d) ident='@';  //d   + changes

    //if(s_pcoeffs_msk144[0]!=0.0)
    //ident='E';
    //qDebug()<<"NN1111="<<s_trained_msk144<<s_pcoeffs_msk144[0];

    ncorrected = 0;
    eyeopening = 0.0;
    msk144signalquality(ct,snr0,fest,t0,softbits,msgreceived,s_HisCall,ncorrected,eyeopening,
                        s_trained_msk144,s_pcoeffs_msk144,f_calc_pcoeffs);//f_calc_pcoeffs

    //qDebug()<<"NN222="<<s_trained_msk144<<s_pcoeffs_msk144[0];
    /*if (s_msk144rxequal_s && !s_trained_msk144 && s_msk144rxequal_d) ident='^';  //s+d - changes
    if (s_msk144rxequal_s && s_trained_msk144 && s_msk144rxequal_d)  ident='$';  //s+d + changes
    if (!s_msk144rxequal_s && s_trained_msk144 && s_msk144rxequal_d) ident='@';  //d   + changes*/

    if ( nsnr < -8 ) nsnr=-8;
    if ( nsnr > 24 ) nsnr=24;


    if (ndecodesuccess==1)
    {
        //qDebug()<<"msk144="<<msgreceived;
        print_rtd_decode_text(msgreceived,s_msglast,nsnr,s_nsnrlast,tsec,fest,t0,navg,
                              ncorrected,eyeopening,ident);
    }
    else if (G_SwlOpt && ndecodesuccess>=2) //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {
        //bool seenb4=false;
        /*for (int i = 0; i < RECENT_SHMSGS_COU; i++)
        {//do i=1,nshmem
            if ( msgreceived == recent_shmsgs[i] )
            {
                seenb4=true;
                break;
            }
        }*/
        bool seenb4 = update_recent_shmsgs(msgreceived);

        if (seenb4 && nsnr>-6)
        {
            //qDebug()<<"msk40="<<msgreceived;
            print_rtd_decode_text(msgreceived,s_msglastswl,nsnr,s_nsnrlastswl,tsec,fest,t0,navg,
                                  ncorrected,eyeopening,ident);
        }
    }

//end:
    //qDebug()<<"pnoise_rtd_msk="<<pnoise_rtd_msk;
    if (ss_msk144ms && pnoise_rtd_msk > 0.0)
    {
        for (int i = 0; i < 8; i++)
        {
            //MSK40="<<f_calc_pcoeffs; false -> msk40 but no posyblr to go from beg to end of stack
            double nozero = pow[i]/pnoise_rtd_msk-1.0;
            if (nozero<0.000001) nozero=0.000001;
            int isnr0=(int)(10.0*log10(nozero));
            double now_pt = tsec+((double)i*tframe);
            if (now_pt>prev_pt_msk)
            {
                if (isnr0>-5)// && now_pt>prev_ping_t_msk)//limit -2db
                {
                    one_end_ping_msk = true;
                    //double now_pt = tsec+((double)i*tframe);

                    if (prev_ping_t_msk>0.0)
                        ping_width_msk += now_pt - prev_ping_t_msk;
                    else if (prev_ping_t_msk==0.0)
                        ping_width_msk = 0.002; //0.002 inportent for right ping duration

                    prev_ping_t_msk = now_pt;
                    //qDebug()<<"isnr0="<<now_pt<<isnr0;
                }
                else
                {
                    //qDebug()<<"out111111="<<one_end_ping_msk<<is_new_rpt_msk<<ping_width_msk;
                    if (one_end_ping_msk && is_new_rpt_msk && ping_width_msk>0.001)//0.001 inportent for right ping duration
                    {
                        if (f_only_one_color_rtd_msk)
                        {
                            f_only_one_color_rtd_msk = false;
                            SetBackColor();
                        }
                        //int rpt = GetStandardRPT(ping_width_msk,last_rpt_snr_msk);//  (double width, double peak)
                        //s_list_rpt_msk.replace(2,QString("%1").arg((int)(ping_width_msk*1000.0)));
                        s_list_rpt_msk.replace(2,str_round_20ms(ping_width_msk));
                        s_list_rpt_msk.replace(4,GetStandardRPT(ping_width_msk,last_rpt_snr_msk));
                        //emit EmitDecodetText(s_list_rpt_msk,s_fopen,true);
                        SetDecodetTextMsk2DL(s_list_rpt_msk);//2.46
                        one_end_ping_msk = false;
                        is_new_rpt_msk = false;
                        //qDebug()<<"pingEnd==="<<now_pt<<"width="<<ping_width_msk<<s_list_rpt_msk.at(4);
                    }
                    prev_ping_t_msk = 0.0;  //0.0 inportent for right ping duration
                    ping_width_msk = 0.0001;//0.0001 inportent for right ping duration
                }
                prev_pt_msk = now_pt - 0.0001;//prev_pt_msk <- find ping if have new part (parts over lap)
            }
            //else
            //qDebug()<<"NO yet P"<<i;
        }
    }

end:

    tsec0_rtd_msk=tsec;
    delete [] cdat;
}
void DecoderMs::EndRtdPeriod()
{
    //qDebug()<<"11111EndRtdPeriod()end_rtd="<<is_new_rpt_msk<<ping_width_msk;
    if (ss_msk144ms && is_new_rpt_msk && ping_width_msk>0.001)//0.001 inportent for right ping duration
    {
        //qDebug()<<"2222EndRtdPeriod()=";
        if (f_only_one_color_rtd_msk)
        {
            f_only_one_color_rtd_msk = false;
            SetBackColor();
        }
        //int rpt = GetStandardRPT(ping_width_msk,last_rpt_snr_msk);//  (double width, double peak)
        //s_list_rpt_msk.replace(2,QString("%1").arg((int)(ping_width_msk*1000.0)));
        s_list_rpt_msk.replace(2,str_round_20ms(ping_width_msk));
        s_list_rpt_msk.replace(4,GetStandardRPT(ping_width_msk,last_rpt_snr_msk));
        //emit EmitDecodetText(s_list_rpt_msk,s_fopen,true);
        SetDecodetTextMsk2DL(s_list_rpt_msk);//2.46
        one_end_ping_msk = false;
        is_new_rpt_msk = false;
        //qDebug()<<"End==============================="<<prev_ping_t_msk<<s_list_rpt_msk.at(4)<<ping_width_msk;
    }
}
void DecoderMs::print_rtd_decode_text(QString msgreceived,QString &s_msg_,int nsnr,int &s_nsnr_,
                                      double in_tsec,double fest,double t0,int navg,int ncorrected,
                                      double eyeopening,char ident)
{
    //bool f_only_one_color = true;
    //double fc = 1500.0;
    //QStringList list;
    //qDebug()<<"rpt====="<<msgreceived;

    if (msgreceived != s_msg_ || nsnr > s_nsnr_ || in_tsec < tsec0_rtd_msk)
    {
        double fc = 1500.0;
        //qDebug()<<"NEWWWWWWW==="<<msgreceived<<s_msg_<<nsnr<<s_nsnr_<<in_tsec<<tsec0_rtd_msk;
        s_msg_=msgreceived;
        s_nsnr_=nsnr;

        //if (s_msk144rxequal_s && !s_trained_msk144 && s_msk144rxequal_d) ident='^';  //s+d - changes
        //if (s_msk144rxequal_s && s_trained_msk144 && s_msk144rxequal_d)  ident='$';  //s+d + changes
        //if (!s_msk144rxequal_s && s_trained_msk144 && s_msk144rxequal_d) ident='@';  //d   + changes
        //if (msk144_ft8_cont_msg)//1.69
        //msgreceived = TGenMsk->fix_contest_msg(My_Grid_Loc,msgreceived);//for " R " in msg 1.31

        double df_hv = fest-fc;
        is_new_rpt_msk = true;

        last_rpt_snr_msk = nsnr;
        //if (use_rpt_db_msk)
        //rpt = GetStandardRPT(ping_width_msk,nsnr);//  (double width, double peak)

        //qDebug()<<"rpt====="<<msgreceived<<is_new_rpt_msk<<ping_width_msk;
        if (G_ShOpt || G_SwlOpt)
            msgreceived = RemBegEndWSpaces(msgreceived);//for msk40


        s_list_rpt_msk.clear();
        s_list_rpt_msk <<s_time<<QString("%1").arg(t0,0,'f',1)<<""<<
        QString("%1").arg((int)nsnr)<<""<<QString("%1").arg((int)df_hv)
        <<msgreceived<<QString("%1").arg(navg)  //navg  za tuk e iz4isleno no ne 0 hv 1.31
        <<QString("%1").arg(ncorrected)<<QString("%1").arg(eyeopening,0,'f',1)
        <<QString(ident)+" "+QString("%1").arg((int)fest);

        if (!ss_msk144ms)
        {
            if (f_only_one_color_rtd_msk)
            {
                f_only_one_color_rtd_msk = false;
                SetBackColor();
            }
            //emit EmitDecodetText(s_list_rpt_msk,s_fopen,true); //1.27 psk rep   fopen bool true    false no file open
            SetDecodetTextMsk2DL(s_list_rpt_msk);//2.46
        }

        rtd_dupe_cou = 0;
        rtd_dupe_pos = t0;
        rtd_dupe_cou++;
        EmitDecLinesPosToDisplay(rtd_dupe_cou,rtd_dupe_pos,t0,s_time);//1.28 s_time for identif perood
    }
    else
    {
        //is_new_rpt_msk = false;
        if (rtd_dupe_cou < MAX_RTD_DISP_DEC_COU)// max 0-139
        {
            rtd_dupe_cou++;
            EmitDecLinesPosToDisplay(rtd_dupe_cou,rtd_dupe_pos,t0,s_time);//1.28 s_time for identif perood
        }
    }
}
///mskrtd end/////

void DecoderMs::msk_144_40_decode(double *dat,int npts_in,double s_istart,bool rpt_db_msk)
{
    if (ss_msk144ms != rpt_db_msk)
        f_first_msk144 = false;
    ss_msk144ms = rpt_db_msk;

    int NMAX = 30*DEC_SAMPLE_RATE;
    int NFFTMAX=512*1024;// org arrayc
    int NFFTMAX2=NFFTMAX+4096;//v1.28 NFFTMAX2 for any case
    //int NSPM_MSK144=864;//parameter (NSPM=864)               !Samples per MSK144 long message
    std::complex<double> *c = new std::complex<double>[NFFTMAX2+10]; //v1.28 NFFTMAX2 for any case //!Complex (analytic) data
    pomAll.zero_double_comp_beg_end(c,0,NFFTMAX);
    int nmessages=0;
    double rms;
    int nfft,n;

    int npts=fmin(npts_in,NMAX);

    rms=sqrt(dot_product_da_da(dat,dat,npts,0)/(double)npts);//rms=sqrt(dot_product(d(0:npts-1),d(0:npts-1))/npts);
    if (rms==0.0)// no div by zero
        rms=1.0;
    for (int i = 0; i < npts; i++)
        dat[i]=dat[i]/(rms/2.0);//(rms/2.0);v1.16 //50 i na 3 ne se 42te

    n=log((double)npts)/log(2.0) + 1.0;
    nfft=fmin(pow(2,n),(1024.0*1024.0));//    nfft=min(2**n,1024*1024)                            !FFT length

    if (first_dec_msk)
    {
        pomAll.zero_double_beg_end(s_pcoeffs_msk144,0,3);
        first_dec_msk = false;
    }

    //analytic_msk144(dat,0,npts,nfft,c);//!Convert to analytic signal
    //qDebug()<<nfft;

    //bool cb_rxequa = false;
    //s_msk144rxequal
    analytic_msk144_2(dat,0,npts,nfft,c,s_pcoeffs_msk144,false,false);//s_msk144rxequal ako e true ne decodira

    //analytic_jtmsk(dat,0,npts,nfft,c);//!Convert to analytic signal
    //double *s = new double[1024*1024];
    //analytic(dat,0,npts,nfft,s,c);
    //char ident;//text->'$' codet->'*' short_rpt->'#' rtd->& ->^;
    //double freq2 = 0.0;  //ok1xjd
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.

    detectmsk144(c,npts,s_istart,nmessages);

    //mousebutton Left=1, Right=3 fullfile=0 rtd=2
    if ( nmessages == 0 && (s_mousebutton == 1 || s_mousebutton == 3)) //&& s_decoder_deep>1 if( nline .eq. 0 .and. t0 .gt. 0 ) then  !Operator detected signal - try averaging 7 frames
        opdetmsk144(c,npts,s_istart,nmessages);//opdetmsk144(c,npts,line,nline,nutc,ntol,t0)

    if (nmessages == 0 && G_ShOpt)
        detectmsk40(c,npts,s_istart);

    //qDebug()<<"DXCall="<<dxcall_sq;
    //c900:
    delete [] c;
}


