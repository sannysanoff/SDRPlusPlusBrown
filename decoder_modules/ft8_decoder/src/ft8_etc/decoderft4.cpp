/* The algorithms, source code, look-and-feel of WSJT-X and related
 * programs, and protocol specifications for the modes FSK441, FT8, JT4,
 * JT6M, JT9, JT65, JTMS, QRA64, ISCAT, MSK144, are Copyright © 2001-2017
 * by one or more of the following authors: Joseph Taylor, K1JT; Bill
 * Somerville, G4WJS; Steven Franke, K9AN; Nico Palermo, IV3NWV; Greg Beam,
 * KI7MT; Michael Black, W9MDB; Edson Pereira, PY2SDR; Philip Karn, KA9Q;
 * and other members of the WSJT Development Group.
 *
 * MSHV FT4 Decoder
 * Rewritten into C++ and modified by Hrisimir Hristov, LZ2HV 2015-2020
 * May be used under the terms of the GNU General Public License (GPL)
 */

#include "decoderms.h"
//#include "../HvMsPlayer/libsound/genpom.h"
#include "ft_all_ap_def.h"
//#include <QtGui>

static const int nappasses_4[6]=
    {
        2,2,2,2,2,3
    };
static const int naptypes_4[6][4]=
    {//gjhghghj
        {1,2,0,0},{2,3,0,0},{2,3,0,0},{3,6,0,0},{3,6,0,0},{3,1,2,0}
    };
//! iaptype
//!------------------------
//!   1        CQ     ???    ???           (29 ap bits)
//!   2        MyCall ???    ???           (29 ap bits)
//!   3        MyCall DxCall ???           (58 ap bits)
//!   4        MyCall DxCall RRR           (77 ap bits)
//!   5        MyCall DxCall 73            (77 ap bits)
//!   6        MyCall DxCall RR73          (77 ap bits)
//!********
DecoderFt4::DecoderFt4(int id, std::shared_ptr<F2a> f2a) : f2a(f2a)
{

    pomFt.initPomFt();
    pomAll.initPomAll();//2.66 for pctile_shell
    decid = id;
    TGenFt4 = new GenFt4(true);//f_dec_gen = dec=true gen=false
    gen_pulse_gfsk_(pulse_ft4_rx,864.0,1.0,576);  //576
    first_ft4_ds = true;
    first_ft4detcad = true;
    first_ft4_sync4d = true;
    first_subsft4 = true;
    first_ft4bm = true;
    first_ft4d = true;
    DEC_SAMPLE_RATE = 12000.0;
    twopi=8.0*atan(1.0);
    pi=4.0*atan(1.0);
    cont_id0_ft4_2 = 0;
    //f_new_p = true;
}
DecoderFt4::~DecoderFt4() {
    delete TGenFt4;
}
static double s_nftx4 = 1200.0;
void DecoderFt4::SetStTxFreq(double f)
{
    s_nftx4 = f;
}
static bool f_multi_answer_mod4 = false;
void DecoderFt4::SetStMultiAnswerMod(bool f)
{
    f_multi_answer_mod4 = f;
}
static int s_decoder_deep4 = 1;
void DecoderFt4::SetStDecoderDeep(int d)
{
    s_decoder_deep4 = d;
    //qDebug()<<"s_decoder_deep="<<s_decoder_deep;
}
static bool s_lapon4 = false;// only in mshv
void DecoderFt4::SetStApDecode(bool f)
{
    s_lapon4 = f;
}
static int s_nQSOProgress4 = 0;
void DecoderFt4::SetStQSOProgress(int i)
{
    s_nQSOProgress4 = i;
}
static QString s_time4 = "0.0";
static int s_mousebutton4 = 0;
//static bool s_fopen4 = false;
void DecoderFt4::SetStDecode(QString time,int mousebutton)
{
    s_time4 = time;
    s_mousebutton4 = mousebutton;//mousebutton Left=1, Right=3 fullfile=0 rtd=2
    //s_fopen4 = ffopen;
}
static QString s_MyCall4 = "NOT__EXIST";
//static QString s_MyBaseCall4 = "NOT__EXIST";
static int s_id_cont_ft4_28 = 0;
static int s_ty_cont_ft4_28 = 0;
void DecoderFt4::SetStWords(QString s1,QString,int cq3,int ty4)
{
    s_MyCall4 = s1;
    s_id_cont_ft4_28 = cq3;
    s_ty_cont_ft4_28 = ty4;
}
static QString s_HisCall4 = "NOCALL";
void DecoderFt4::SetStHisCall(QString c)
{
    s_HisCall4 = c;
}
void DecoderFt4::SetMAMCalls(QStringList ls)
{
    TGenFt4->save_hash_call_mam(ls);
}
/*static QString s_cqstr_ft4 = "CQ";
void DecoderFt4::SetStCqStr(QString s)
{
    s_cqstr_ft4 = s;
}*/
void DecoderFt4::dshift1(double *a,int cou_a,int ish)
{
    //HV for single shift vareable
    //Left Shift 	ISHFT 	ISHFT(N,M) (M > 0) 	<< 	n<<m 	n shifted left by m bits
    //Right Shift 	ISHFT 	ISHFT(N,M) (M < 0) 	>> 	n>>m 	n shifted right by m bits

    //std::complex<double> t[cou_a];  //garmi hv v1.42
    //std::complex<double> t[cou_a*2+ish+50];  //garmi hv v1.43 ok
    double *t = new double[cou_a+100]; //garmi pri goliam count hv v1.43 correct ok
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
    delete [] t;
}
void DecoderFt4::ft4_downsample(double *dd,bool newdata,double f0,std::complex<double> *c)
{
    //! Input: real data in dd() at sample rate 12000 Hz
    //! Output: Complex data in c(), sampled at 1200 Hz
    const int NMAX = 72576; //(NMAX=21*3456)=72576;
    const int NDOWN = 18;
    const int NFFT2 = NMAX/NDOWN; //=4032
    const int NSPS = 576;
    std::vector<double> x(NMAX+50);
    std::vector<std::complex<double>> c1(NFFT2+100);//(0:NFFT2-1)

    double df=DEC_SAMPLE_RATE/(double)NMAX;
    double baud=DEC_SAMPLE_RATE/(double)NSPS;
    if (first_ft4_ds)
    {
        double bw_transition = 0.5*baud;
        double bw_flat = 4.0*baud;
        int iwt = (bw_transition/df);
        int iwf = (bw_flat/df);
        int z = iwt-1;
        for (int i = 0; i < iwt; ++i)
            window_ft4_ds[i] = 0.5*(1.0+cos(pi*(double)(z-i)/(double)iwt));//window(0:iwt-1) = 0.5*(1+cos(pi*(/(i,i=iwt-1,0,-1)/)/iwt))
        for (int i = iwt; i < iwt+iwf; ++i)
            window_ft4_ds[i]=1.0;//window(iwt:iwt+iwf-1)=1.0
        for (int i = 0; i < iwt; ++i)
            window_ft4_ds[i+iwt+iwf] = 0.5*(1.0+cos(pi*(double)(i)/(double)iwt));//window(iwt+iwf:2*iwt+iwf-1) = 0.5*(1+cos(pi*(/(i,i=0,iwt-1)/)/iwt))
        for (int i = 2*iwt+iwf; i < NFFT2; ++i)
            window_ft4_ds[i]=0.0;//window(2*iwt+iwf:)=0.0

        int iws = (int)(baud/df);
        dshift1(window_ft4_ds,NFFT2,iws);//window_ft4_ds=cshift(window_ft4_ds,iws)  window=cshift(window,iws)
        first_ft4_ds=false;
    }

    if (newdata)
    {
        for (int i = 0; i < NMAX; ++i)
            x[i]=dd[i]*0.01;
        f2a->four2a_d2c(cx_ft4_ds,x.data(),NMAX,-1,0,decid);//four2a(x,NMAX,1,-1,0)            // !r2c FFT to freq domain
    }
    int i0=(int)(f0/df);
    for (int i = 0; i < NFFT2; ++i)
        c[i]=0.0+0.0*complex_i;
    c1[0]=cx_ft4_ds[i0];
    for (int i = 1; i < NFFT2/2; ++i)//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {//do i=1,NFFT2/2
        if (i0+i<NMAX/2) c1[i]=cx_ft4_ds[i0+i];//if(i0+i.le.NMAX/2) c1(i)=cx(i0+i)
        if (i0-i>=0) c1[NFFT2-i]=cx_ft4_ds[i0-i];//if(i0-i.ge.0) c1(NFFT2-i)=cx(i0-i)
    }

    for (int i = 0; i < NFFT2; ++i)
        c1[i]*=window_ft4_ds[i]/(double)NFFT2;//c1=c1*window/NFFT2

    f2a->four2a_c2c(c1.data(),NFFT2,1,1,decid);//call four2a(c1,NFFT2,1,1,1)            //!c2c FFT back to time domain

    for (int i = 0; i < NMAX/NDOWN; ++i)
        c[i]=c1[i];//c[]=c1(0:NMAX/NDOWN-1)
}

void DecoderFt4::ft4_baseline(double *s,int nfa,int nfb,double *sbase)
{
    //! Fit baseline to spectrum
    //! Input:  s(npts)         Linear scale in power
    //! Output: sbase(npts)    Baseline

    const int NFFT1=2304;
    const int NH1=NFFT1/2;//=1152
    double df=DEC_SAMPLE_RATE/(double)NFFT1;                    //!3.125 Hz
    int ia=fmax((int)(200.0/df),nfa);
    int ib=fmin(NH1,nfb);
    int nseg = 10;
    int npct = 10;
    double t_s[1252];//NH1/2
    //double *t_s = new double[1920+20];
    double x[1010];
    double y[1010];
    double a[8];

    for (int i = ia; i<ib; ++i)
    {//do i=ia,ib
        if (s[i]<0.000001) s[i]=0.000001;
        s[i]=10.0*log10(s[i]);            //!Convert to dB scale
    }

    int nterms=5;
    int nlen=(ib-ia+0)/nseg;                 //!Length of test segment
    int i0=(ib-ia+0)/2;                      //!Midpoint
    int k=0;//???

    for (int n = 0; n<nseg; ++n)
    {//do n=1,nseg                         //!Loop over all segments
        int ja=ia + (n-0)*nlen;
        int jb=ja+nlen-0;

        for (int z = 0; z<NH1-ja; ++z)//NH1=1152
            t_s[z] = s[z+ja];
        //qDebug()<<"ja jb"<<ja<<jb<<nlen<<1920-ja;
        double base = pomAll.pctile_shell(t_s,nlen,npct);//pctile(s(ja),nlen,npct,base); //!Find lowest npct of points
        //qDebug()<<"base"<<base;
        for (int i = ja; i<jb; ++i)
        {//do i=ja,jb
            if (s[i]<=base)// then //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
            {
                //if (k<1000) k++;       //!Save all "lower envelope" points
                x[k]=i-i0;
                y[k]=s[i];
                if (k<999)
                    k++;
            }
        }
    }

    int kz=k;
    //if(kz<32) kz=32;

    pomAll.zero_double_beg_end(a,0,6);//a=0.
    //call polyfit(x,y,y,kz,nterms,0,a,chisqr)  //!Fit a low-order polynomial
    double chisqr = 0.0;
    pomAll.polyfit(x,y,y,kz,nterms,0,a,chisqr);
    //qDebug()<<"a"<<a[0]<<a[1]<<a[2]<<a[3]<<a[4]<<i0;
    for (int i = ia; i<ib; ++i)
    {//do i=ia,ib
        double t=i-i0;
        sbase[i]=a[0]+t*(a[1]+t*(a[2]+t*(a[3]+t*(a[4])))) + 0.65;
        //!     write(51,3051) i*df,s(i),sbase(i)
        //!3051 format(3f12.3)
        sbase[i]=pow(10,(sbase[i]/10.0)); //sbase[i]=10**(sbase[i]/10.0)  nt=pow(2,2*nsym);//nt=2**(2*nsym)
    }
}

void DecoderFt4::getcandidates4(double *dd,double fa,double fb,double fa1,double fb1,double syncmin,double nfqso,
                                int maxcand,double candidate[2][115],int &ncand)
{
    const int NFFT1=2304;
    const int NSTEP=576;
    const int NMAX=21*3456;//=72576 !Samples in iwave
    const int NHSYM=((NMAX-NFFT1)/NSTEP); //=122 153hv !Number of symbol spectra (1/4-sym steps)
    const int NH1=NFFT1/2;
    // for small stack, using dynamic allocations
    DYNAMIC_ARRAY(double, x, NFFT1+10)
    std::complex<double> cx[NFFT1+50];
    std::vector <double> s_0((NHSYM + 10)*(NH1 + 10)); //(NH1,NHSYM)
    auto s_ = new double[NHSYM + 10][NH1 + 10];
    deferred s_cleanup([&] { delete[] s_; });
    DYNAMIC_ARRAY(double, savsm, NH1 + 50)
    //int indx[NH1+50];
    DYNAMIC_ARRAY(double, sbase, NH1 + 50)
    DYNAMIC_ARRAY(double, savg, NH1 + 50)
    auto candidatet = new double [2][115];
    deferred candidatet_cleanup([&] { delete[] candidatet; });
    //qDebug()<<NHSYM;

    if (first_ft4detcad)
    {
        first_ft4detcad=false;
        for (int i = 0; i < NFFT1; ++i)
            window_ft4[i]=0.0;
        pomFt.nuttal_window(window_ft4,NFFT1);
    }

    //! Compute symbol spectra, stepping by NSTEP steps.
    for (int i = 0; i < NH1; ++i)
    {
        //sbase[i]=0.0;
        savg[i]=0.0;
    }

    //tstep=NSTEP/12000.0
    double df=12000.0/(double)NFFT1;
    double fac=1.0/300.0;
    for (int j = 0; j < NHSYM; ++j)//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {//do j=1,NHSYM
        int ia=j*NSTEP;//ia=(j-1)*NSTEP + 1
        int ib=ia+NFFT1;   //ib=ia+NFFT1-1
        if (ib>NMAX) break;// if(ib.gt.NMAX) exit
        for (int z = 0; z < NFFT1; ++z)
            x[z]=fac*dd[ia+z]*window_ft4[z]*0.01;
        f2a->four2a_d2c(cx,x,NFFT1,-1,0,decid);              //!r2c FFT
        for (int i = 0; i < NH1; ++i)
        {//do i=1,NH1
            //s_[j][i]=pomAll.ps_hv(cx[i]);//old -> s(i,j)=real(cx(i))**2 + aimag(cx(i))**2
            s_[j][i]=cabs(cx[i])*cabs(cx[i]);//fin s(1:NH1,j)=abs(cx(1:NH1))**2
            savg[i]+=s_[j][i];
        }
        //savg=savg + s(1:NH1,j)                   !Average spectrum
    }
    for (int i = 0; i < NH1; ++i)
        savsm[i]=0.0;
    for (int i = 7; i < NH1-7; ++i)
    {//do i=8,NH1-7
        double sum = 0.0;
        for (int j = -7; j < 8; ++j)
            sum+=savg[i-j];
        savsm[i]=sum/15.0;//savsm(i)=sum(savg(i-7:i+7))/15.
    }

    //double corrt=0.0;//corr for threads
    //if (decid>0) corrt=100.0;
    //int nfa=(int)(fa-corrt)/df;
    int nfa=(int)fa/df;
    if (nfa<38) nfa=38;
    int nfb=(int)(fb)/df;
    if (nfb>942) nfb=942;

    int nfa1 = (int)fa1/df;//nfa;//
    if (nfa1<38) nfa1=38;
    int nfb1 = (int)fb1/df;//nfb;//
    if (nfb1>942) nfb1=942;

    ncand=0;
    //qDebug()<<200.0/df<<4910.0/df;// 38.3   942.7
    //1000/df=192=96 1200/df=230=115 1500/df=288=144 2000/df=384=192 3000/df=576=288 HV correction
    if ((fb1-fa1)<2000)
    {
        nfb1 = nfa1 + 384;
        if (nfb1>942)
        {
            nfb1 = 942; //5000.0/df=960
            //nfa1 = 960 - 384;
        }
    }
    //qDebug()<<decid<<nfa<<nfb<<"---"<<nfa1<<nfb1;
    ft4_baseline(savg,nfa1,nfb1,sbase);
    //ft4_baseline(savg,nfa,nfb,sbase);
    //ft4_baseline(savg,200.0/df,3500.0/df,sbase);//2.22 important

    bool any_is0 = false;
    for (int i = nfa; i < nfb; ++i)
    {
        if (sbase[i]<=0.0)
        {
            any_is0 = true;
            break;
        }
    }
    if (any_is0) return;
    //qDebug()<<"--------------"<<any_is0;

    /*double tdel = 0.0;
    int ctdel = 0;
    for (int i = nfa1+10; i < nfb1-10; i+=5)
    {
    	tdel += sbase[i];
    	ctdel++;
    }
    tdel /= (double)ctdel;
    if (tdel<=0.0) tdel = 0.001;*/

    for (int i = nfa; i < nfb; ++i)
    {
        savsm[i] /= sbase[i];
        //savsm[i] /= tdel;
    }
    //qDebug()<<decid<<nfa<<nfb<<"---"<<nfa1<<nfb1<<" dots="<<ctdel;

    double f_offset = -1.5*12000.0/(double)NSTEP;//-4.0;//no in 2.29 tested -> +0.3 for 32/64bit tested - 0.5
    for (int i = 0; i < 2; ++i)
    {
        for (int j = 0; j < 100; ++j)
            candidatet[i][j]=0.0;
    }
    int nq=0;
    for (int i = nfa+1; i < nfb-1; ++i) //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {//do i=nfa+1,nfb-1
        //if (i*df>810 && i*df<880) qDebug()<<"Pickkkk===="<<decid<<(int)i*df<<savsm[i];
        if (savsm[i]>=savsm[i-1] && savsm[i]>=savsm[i+1] && savsm[i]>=syncmin)
        {
            //if (i*df>810 && i*df<880) qDebug()<<"Pickkkk===="<<decid<<(int)i*df<<savsm[i]<<"innnnn";
            double den=savsm[i-1]-2.0*savsm[i]+savsm[i+1];//den=savsm(i-1)-2*savsm(i)+savsm(i+1);
            double del=0.0;
            if (den!=0.0) del=0.5*(savsm[i-1]-savsm[i+1])/den;
            double fpeak=((double)i+del)*df+f_offset;
            if (fpeak<100.0 || fpeak>4910.0) continue;//cycle
            double speak=savsm[i]-0.25*(savsm[i-1]-savsm[i+1])*del;

            candidatet[0][ncand]=fpeak;//candidate(1,ncand)=fpeak
            candidatet[1][ncand]=speak;//candidate(3,ncand)=speak


            if (fabs(fpeak-nfqso)<=20.0) nq++;//int nq=count(fabs(candidatet(1,1:ncand)-nfqso)<=20.0)

            if (ncand<(maxcand-1))
                ncand++;
            else
                break;
            //if(ncand.eq.maxcand) exit
        }
    }
    //qDebug()<<"test"<<ncand; //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    int n1=0;//1;
    int n2=nq;//nq+1;
    for (int i = 0; i < ncand; ++i)
    {//do i=1,ncand
        if (fabs(candidatet[0][i]-nfqso)<=20.0) //if(abs(candidatet(1,i)-nfqso)<=20.0) then
        {
            candidate[0][n1]=candidatet[0][i];//candidate(1:2,n1)=candidatet(1:2,i)
            candidate[1][n1]=candidatet[1][i];
            n1++;//n1=n1+1
        }
        else
        {
            candidate[0][n2]=candidatet[0][i];
            candidate[1][n2]=candidatet[1][i];
            n2++;
        }
    }
}
void DecoderFt4::sync4d(std::complex<double> *cd0,int i0,std::complex<double> *ctwk,int itwk,double &sync)
{
    const int NDOWN=18;
    const int NSPS=576;//512;
    const int NSS=NSPS/NDOWN;  //576/18=32
    const int NMAX=21*3456;//=72576
    const int NP=NMAX/NDOWN;//=4032
    std::complex<double> csync2[2*NSS+20];//=64
    int icos4a[4]=
        {
            0,1,3,2
        };
    int icos4b[4]=
        {
            1,0,2,3
        };
    int icos4c[4]=
        {
            2,3,1,0
        };
    int icos4d[4]=
        {
            3,2,0,1
        };

    if (first_ft4_sync4d)
    {
        int k=0; //k=1;
        double phia=0.0;
        double phib=0.0;
        double phic=0.0;
        double phid=0.0;
        for (int i = 0; i < 4; ++i)
        {//do i=0,3
            double dphia=2.0*twopi*(double)icos4a[i]/(double)NSS;
            double dphib=2.0*twopi*(double)icos4b[i]/(double)NSS;
            double dphic=2.0*twopi*(double)icos4c[i]/(double)NSS;
            double dphid=2.0*twopi*(double)icos4d[i]/(double)NSS;
            for (int j = 0; j < NSS/2; ++j)
            {//do j=1,NSS/2
                csynca_ft4_sync[k]=cos(phia)+sin(phia)*complex_i;
                csyncb_ft4_sync[k]=cos(phib)+sin(phib)*complex_i;
                csyncc_ft4_sync[k]=cos(phic)+sin(phic)*complex_i;
                csyncd_ft4_sync[k]=cos(phid)+sin(phid)*complex_i;
                phia=fmod(phia+dphia,twopi);
                phib=fmod(phib+dphib,twopi);
                phic=fmod(phic+dphic,twopi);
                phid=fmod(phid+dphid,twopi);
                k++;//k=k+1
            }
        }
        first_ft4_sync4d=false;
        fac_ft4_sync=1.0/(double)(2.0*NSS);
    }

    int i1=i0;                            //!four Costas arrays
    int i2=i0+33*NSS;
    int i3=i0+66*NSS;
    int i4=i0+99*NSS;

    std::complex<double> z1=0.0+0.0*complex_i;
    std::complex<double> z2=0.0+0.0*complex_i;
    std::complex<double> z3=0.0+0.0*complex_i;
    std::complex<double> z4=0.0+0.0*complex_i;  //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.

    if (itwk==1) //csync2=ctwk*csynca      !Tweak the frequency
    {
        for (int i = 0; i < 2*NSS; ++i)
            csync2[i]=ctwk[i]*csynca_ft4_sync[i];
    }
    if (i1>=0 && i1+4*NSS-1<=NP-1) //z1=sum(cd0(i1:i1+4*NSS-1:2)*conjg(csync2))
    {
        int z =0;
        for (int i = 0; i < 2*NSS; ++i)
        {
            z1+=cd0[i1+z]*conj(csync2[i]);
            z+=2;
        }
    }
    else if (i1<0)
    {
        int npts=(i1+4*NSS-1)/2;//    npts=(i1+4*NSS-1)/2
        if (npts<=16)// if(npts.le.16) then
            z1=0.0+0.0*complex_i;
        else
        {
            //z1=sum(cd0(0:i1+4*NSS-1:2)*conjg(csync2(2*NSS-npts:)))
            int z =0;
            for (int i = 2*NSS-npts; i < 2*NSS; ++i)//NSS=32 cd0[4032];
            {
                z1+=cd0[z]*conj(csync2[i]);
                z+=2;
            }
            //qDebug()<<"cd0="<<z<<"csync2="<<2*NSS-1;
        }
    }

    if (itwk==1) //csync2=ctwk*csyncb      //!Tweak the frequency
    {
        for (int i = 0; i < 2*NSS; ++i)
            csync2[i]=ctwk[i]*csyncb_ft4_sync[i];
    }
    if (i2>=0 && i2+4*NSS-1<=NP-1) //z2=sum(cd0(i2:i2+4*NSS-1:2)*conjg(csync2))
    {
        int z =0;
        for (int i = 0; i < 2*NSS; ++i)
        {
            z2+=cd0[i2+z]*conj(csync2[i]);
            z+=2;
        }
    }

    if (itwk==1) //csync2=ctwk*csyncc      !Tweak the frequency
    {
        for (int i = 0; i < 2*NSS; ++i)
            csync2[i]=ctwk[i]*csyncc_ft4_sync[i];
    }
    if (i3>=0 && i3+4*NSS-1<=NP-1) //z3=sum(cd0(i3:i3+4*NSS-1:2)*conjg(csync2))
    {
        int z =0;
        for (int i = 0; i < 2*NSS; ++i)
        {
            z3+=cd0[i3+z]*conj(csync2[i]);
            z+=2;
        }
    }

    if (itwk==1) //csync2=ctwk*csyncd      !Tweak the frequency
    {
        for (int i = 0; i < 2*NSS; ++i)
            csync2[i]=ctwk[i]*csyncd_ft4_sync[i];
    }
    if (i4>=0 && i4+4*NSS-1<=NP-1) //z4=sum(cd0(i4:i4+4*NSS-1:2)*conjg(csync2))
    {
        int z =0;
        for (int i = 0; i < 2*NSS; ++i)
        {
            z4+=cd0[i4+z]*conj(csync2[i]);
            z+=2;
        }
    }//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    else if (i4+4*NSS-1>NP-1) //else if( i4+4*NSS-1.gt.NP-1 ) then
    {
        int npts=(NP-1-i4+1)/2;//npts=(NP-1-i4+1)/2;
        if (npts<=16) //if(npts.le.16) then
            z4=0.0+0.0*complex_i;
        else
        {
            //z4=sum(cd0(i4:i4+2*npts-1:2)*conjg(csync2(1:npts)))
            int z =0;
            for (int i = 0; i < npts; ++i)//NSS=32  cd0[4032];
            {
                z4+=cd0[i4+z]*conj(csync2[i]);
                z+=2;
            }
            //qDebug()<<"cd0="<<i4<<i4+(npts-1)<<"csync2="<<npts-1;
        }
    }
    //old p(z1)=real(z1*fac)**2 + aimag(z1*fac)**2
    //new p(z1)=(real(z1*fac)**2 + aimag(z1*fac)**2)**0.5
    //sync = ps_hv(z1*fac_ft4_sync) + ps_hv(z2*fac_ft4_sync) + ps_hv(z3*fac_ft4_sync) + ps_hv(z4*fac_ft4_sync);
    sync = pow(pomAll.ps_hv(z1*fac_ft4_sync),0.5) + pow(pomAll.ps_hv(z2*fac_ft4_sync),0.5) +
           pow(pomAll.ps_hv(z3*fac_ft4_sync),0.5) + pow(pomAll.ps_hv(z4*fac_ft4_sync),0.5);
}
void DecoderFt4::gen_ft4cwaveRx(int *i4tone,double f_tx,std::complex<double> *cwave)
{
    //////////////////////// GFSK MODULATOR ////////////////////////////////////////////
    int nsym=103;
    int nsps=576; //=576      for 12000=512
    int nwave=(nsym+2)*nsps;//max rx=60480   max rx=53760
    //double dphi[240000+10];  //real dphi(0:240000-1)
    double *dphi=new double[60540];  //max rx=60480
    double hmod=1.0;
    double dt=1.0/12000.0;// for RX=12000 for tx=48000

    //! Compute the smoothed frequency waveform.
    //! Length = (nsym+2)*nsps samples, zero-padded (nsym+2)*nsps TX=215040 RX=53760
    double dphi_peak=twopi*hmod/(double)nsps;
    for (int i= 0; i < 60500; ++i)//max rx=60480
        dphi[i]=0.0;

    for (int j= 0; j < nsym; ++j)
    {
        int ib=j*nsps;
        for (int i= 0; i < 3*nsps; ++i)//=1728
            dphi[i+ib] += dphi_peak*pulse_ft4_rx[i]*(double)i4tone[j];
    }

    double ofs = twopi*f_tx*dt;
    double phi=0.0;
    for (int j= 0; j < nwave; ++j)//rx=60480   rx=53760
    {
        cwave[j]=cos(phi)+sin(phi)*complex_i;
        phi=fmod(phi+dphi[j]+ofs,twopi);
    }
    //qDebug()<<"nsamp="<<nsamp<<nwave;
    for (int i = 0; i < nsps; ++i)
        cwave[i]*=(1.0-cos(twopi*(double)i/(2.0*nsps)))/2.0;
    int k2=(nsym+1)*nsps+1; //=2047 before stop
    for (int i = 0; i < nsps; ++i)
        cwave[i+k2]*=(1.0+cos(twopi*(double)i/(2.0*nsps)))/2.0;
    //qDebug()<<"nsamp="<<k2+nsps-1;
    delete [] dphi;
}
void DecoderFt4::subtractft4(double *dd,int *itone,double f0,double dt)
{
    //const int NSPS=576;
    const int NFRAME=(103+2)*576;//60480
    const int NMAX=21*3456;//=72576
    const int NFILT=1400;
    const int NFFT=NMAX;
    int offset_w = NFILT/2+25;
    int nstart=dt*12000.0+1.0-576.0;

    std::complex<double> *cref = new std::complex<double>[62481];//max rx=60481 +ramp
    std::complex<double> *cfilt= new std::complex<double>[72800];//72576     NMAX+100
    //double dd66[72800];// __attribute__((aligned(16)));
    //pomAll.zero_double_beg_end(dd66,0,72600);
    //double *dd66 = new double[72700]; // <- slow w10

    pomAll.zero_double_comp_beg_end(cfilt,0,(NMAX+25));
    //pomAll.zero_double_comp_beg_end(cref,0,NFRAME+25);

    gen_ft4cwaveRx(itone,f0,cref); //gen_ft4wave(itone,nsym,nsps,fs,f0,cref,xjunk,icmplx,NFRAME);

    if (first_subsft4)
    {
        //! Create and normalize the filter
        double window[NFILT+100] 
#ifndef _WIN32
            __attribute__((aligned(16)))
#endif
            ;
        double fac=1.0/double(NFFT);
        double sum=0.0;
        for (int j = -NFILT/2; j < NFILT/2; ++j)
        {//do j=-NFILT/2,NFILT/2
            window[j+offset_w]=cos(pi*(double)j/(double)NFILT)*cos(pi*(double)j/(double)NFILT);
            sum+=window[j+offset_w];
        }
        pomAll.zero_double_comp_beg_end(cw_subsft4,0,NMAX+25);
        if (sum<=0.0) // no devide by zero
            sum=0.01;
        for (int i = 0; i < NFILT+1; ++i)
            cw_subsft4[i]=window[i+offset_w-NFILT/2]/sum;//cw(1:NFILT+1)=window/sum

        pomAll.cshift1(cw_subsft4,NMAX,(NFILT/2+1));    //cw=cshift(cw,NFILT/2+1);

        f2a->four2a_c2c(cw_subsft4,NFFT,-1,1,decid);//call four2a(cw,nfft,1,-1,1)
        for (int i = 0; i < NMAX; ++i)
            cw_subsft4[i]*=fac;
        first_subsft4=false;
    }

    for (int i = 0; i < NFRAME; ++i)
    {
        int id=nstart+i-1;//0 -1
        if (id>=0 && id<NMAX)
        {
            cfilt[i]=dd[id]*conj(cref[i]);//camp[i];//cfilt(1:nframe)=camp(1:nframe)
        }
    }

    f2a->four2a_c2c(cfilt,NFFT,-1,1,decid);//call four2a(cfilt,nfft,1,-1,1)
    for (int i = 0; i < NFFT; ++i)//cfilt(1:nfft)=cfilt(1:nfft)*cw(1:nfft)
        cfilt[i]*=cw_subsft4[i];
    f2a->four2a_c2c(cfilt,NFFT,1,1,decid);//call four2a(cfilt,nfft,1,1,1)

    //! Subtract the reconstructed signal
    for (int i = 0; i < NFRAME; ++i)//if(j.ge.1 .and. j.le.NMAX) dd(j)=dd(j)-2*REAL(cfilt(i)*cref(i))
    {//do i=1,nframe
        int j=nstart+i-1;//0 -1
        if (j>=0 && j<NMAX)
            dd[j]-=1.94*creal(cfilt[i]*cref[i]);//2.35 1.94   2.18 1.93
    }
    /*int kstop = 0;
    for (int i = 0; i < NFRAME; ++i)//if(j.ge.1 .and. j.le.NMAX) dd(j)=dd(j)-2*REAL(cfilt(i)*cref(i))
    {
        int j=nstart+i-1;//0 -1
        if (j>=0 && j<NMAX)
        {
            dd66[j]=1.94*creal(cfilt[i]*cref[i]);//2.35 1.94   2.26 1.93 no->2.0  //2.07 1.92<-tested    1.5,1.6  ,1.7ok,
            kstop=j;
        }
    }
    int kstart = nstart-1;
    if (kstart<0) kstart=0;
    for (int i = kstart; i < kstop+1; ++i)
        dd[i]-=dd66[i];*/

    delete [] cref;
    delete [] cfilt;
}
int DecoderFt4::count_eq_bits(bool *a,int b_a,bool *b,int c)
{
    int ns1=0;
    for (int x = 0; x < c; ++x)
        if (a[x+b_a]==b[x]) ns1++;
    return ns1;
}
void DecoderFt4::get_ft4_bitmetrics(std::complex<double> *cd,double bitmetrics_[3][220],bool &badsync)
{
    const int NN=103;
    const int NSPS=576;
    const int NDOWN=18;
    const int NSS=NSPS/NDOWN;//=32
    std::complex<double> csymb[NSS+10];//complex csymb(NSS)
    std::complex<double> cs_[NN+10][4];//complex cs(0:3,NN)
    double s4_[NN+10][4];// real s4(0:3,NN)
    double s2[256+20];
    int graymap[4]=
        {
            0,1,3,2
        };
    int icos4a[4]=
        {
            0,1,3,2
        };
    int icos4b[4]=
        {
            1,0,2,3
        };
    int icos4c[4]=
        {
            2,3,1,0
        };
    int icos4d[4]=
        {
            3,2,0,1
        };

    if (first_ft4bm)
    {
        for (int i = 0; i < 8; ++i)//one_ft4_2[8][256];
        {//do i=0,255
            for (int j = 0; j < 256; ++j)
            {//do j=0,7
                //if(iand(i,2**j).ne.0) one(i,j)=.true.
                if ((j & (int)pow(2,i))!=0)
                    one_ft4_2[i][j]=true;
                else
                    one_ft4_2[i][j]=false;
            }
        }
        first_ft4bm = false;
    }

    for (int k = 0; k < NN; ++k)//NN=103
    {//do k=1,NN
        int i1=k*NSS;//i1=(k-1)*NSS
        for (int z = 0; z < NSS; ++z)//NSS=32
            csymb[z]=cd[z+i1];      //csymb=cd(i1:i1+NSS-1)
        f2a->four2a_c2c(csymb,NSS,-1,1,decid);//four2a(csymb,NSS,1,-1,1)
        for (int x = 0; x < 4; ++x)
        {
            cs_[k][x]=csymb[x];      //cs(0:3,k)=csymb(1:4)
            s4_[k][x]=cabs(csymb[x]);//s4(0:3,k)=abs(csymb(1:4))
        }
    }

    //! Sync quality check
    int is1=0;
    int is2=0;
    int is3=0;
    int is4=0;
    badsync=false;
    for (int k = 0; k < 4; ++k)//s4_[103][4] s4(0:3,NN) NN=103
    {//do k=1,4
        int ip=pomAll.maxloc_da_beg_to_end(s4_[k],0,4);	//ip=maxloc(s4(:,k))    ip=maxloc_da_beg_to_end(s8_[k],0,8);
        if (icos4a[k]==ip) is1++;          //if(icos4a(k-1).eq.(ip(1)-1)) is1=is1+1
        ip=pomAll.maxloc_da_beg_to_end(s4_[k+33],0,4);	//ip=maxloc(s4(:,k+33))
        if (icos4b[k]==ip) is2++;			//if(icos4b(k-1).eq.(ip(1)-1)) is2=is2+1
        ip=pomAll.maxloc_da_beg_to_end(s4_[k+66],0,4);	//ip=maxloc(s4(:,k+66))
        if (icos4c[k]==ip) is3++;			//if(icos4c(k-1).eq.(ip(1)-1)) is3=is3+1
        ip=pomAll.maxloc_da_beg_to_end(s4_[k+99],0,4);  //ip=maxloc(s4(:,k+99))
        if (icos4d[k]==ip) is4++;           //if(icos4d(k-1).eq.(ip(1)-1)) is4=is4+1
    }
    int nsync=is1+is2+is3+is4;

    if (nsync < 8)
    {
        badsync=true;
        return;
    }
    //if (smax < 0.7 || nsync < 8) continue;//cycle //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    //qDebug()<<"2nsync="<<nsync<<is1<<is2<<is3<<is4<<smax<<f0;
    for (int nseq = 1; nseq <= 3; ++nseq)
    {//do nseq=1,3            // !Try coherent sequences of 1, 2, and 4 symbols
        int nsym=1;
        if (nseq==1) nsym=1;
        if (nseq==2) nsym=2;
        if (nseq==3) nsym=4;
        int nt=pow(2,2*nsym);//nt=2**(2*nsym)
        for (int ks = 1; ks <= NN-nsym+1; ks+=nsym)//NN=103  103-4+1
        {//do ks=1,NN-nsym+1,nsym  //!87+16=103 symbols.
            //amax=-1.0
            for (int i = 0; i < nt; ++i)//graymap[4]
            {//do i=0,nt-1
                int i1=i/64;
                int i2=(i & 63)/16;
                int i3=(i & 15)/4;
                int i4=(i & 3);
                if (nsym==1) //s2[256]
                    s2[i]=cabs(cs_[ks-1][graymap[i4]]);//graymap(i4),ks)
                else if (nsym==2) //cs_(graymap(i3),ks)+cs(graymap(i4),ks+1)
                    s2[i]=cabs(cs_[ks-1][graymap[i3]]+cs_[ks+0][graymap[i4]]);
                else if (nsym==4)//cs_(graymap(i1),ks) + cs(graymap(i2),ks+1) + cs(graymap(i3),ks+2) + cs(graymap(i4),ks+3)
                    s2[i]=cabs(cs_[ks-1][graymap[i1]] + cs_[ks+0][graymap[i2]] + cs_[ks+1][graymap[i3]] + cs_[ks+2][graymap[i4]]);
                //else //no possyble
                //print*,"Error - nsym must be 1, 2, or 4."
            }
            int ipt=0+(ks-1)*2;//ipt=1+(ks-1)*2;
            int ibmax = 1;
            if (nsym==1) ibmax=1;
            if (nsym==2) ibmax=3;
            if (nsym==4) ibmax=7; //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
            for (int ib = 0; ib <= ibmax; ++ib)//bmeta[206] (2*NN)
            {//do ib=0,ibmax   //s2[256]  bool one_ft4_2[8][256];//(0:255,0:7)
                //bm=maxval(s2(0:nt-1),one(0:nt-1,ibmax-ib))-maxval(s2(0:nt-1),.not.one(0:nt-1,ibmax-ib))
                double max1v=0.0;
                for (int zz = 0; zz < nt; ++zz)
                {
                    if (one_ft4_2[ibmax-ib][zz]==true)
                    {
                        double tmax1v=s2[zz];
                        if (tmax1v>max1v)
                            max1v=tmax1v;
                    }
                }
                double max2v=0.0;
                for (int zz = 0; zz < nt; ++zz)
                {
                    if (one_ft4_2[ibmax-ib][zz]==false)
                    {
                        double tmax2v=s2[zz];
                        if (tmax2v>max2v)
                            max2v=tmax2v;
                    }
                }
                double bm=max1v-max2v;

                if (ipt+ib>(2*NN-1)) continue;//cycle
                /*if (nsym==1)
                    bmeta[ipt+ib]=bm;
                else if (nsym==2)
                    bmetb[ipt+ib]=bm;
                else if (nsym==4)
                    bmetc[ipt+ib]=bm;*/
                bitmetrics_[nseq-1][ipt+ib]=bm;//bitmetrics(ipt+ib,nseq)=bm
            }
        }
    }

    bitmetrics_[1][204]=bitmetrics_[0][204];//bmetb(205:206)=bmeta(205:206)
    bitmetrics_[1][205]=bitmetrics_[0][205];
    for (int zz = 200; zz < 204; ++zz)
        bitmetrics_[2][zz]=bitmetrics_[1][zz];//bmetc(201:204)=bmetb(201:204)
    bitmetrics_[2][204]=bitmetrics_[0][204];//bmetc(205:206)=bmeta(205:206)
    bitmetrics_[2][205]=bitmetrics_[0][205];

    pomFt.normalizebmet(bitmetrics_[0],2*NN);
    pomFt.normalizebmet(bitmetrics_[1],2*NN);
    pomFt.normalizebmet(bitmetrics_[2],2*NN);

}
/*void DecoderFt4::SetResetPrevT(QString ptime)
{
	s_time4_prev = ptime;
}
void DecoderFt4::SetNewP(bool f)
{
	f_new_p = f;
}*/
void DecoderFt4::ft4_decode(double *dd,double f0a,double f0b,double f0a1,double f0b1,double fqso,bool &have_dec)//,int /*npts no need*/)
{
    have_dec = false;
    outCount = 0;
    const int NDOWN=18;                 //!Downsample factor
    const int NSPS=576;
    const int NSS=NSPS/NDOWN;//=32
    const int ND=87;
    //const int NFFT1=2304;
    //const int NH1=NFFT1/2;//=1152
    const int NMAX=21*3456;//=72576
    const int NDMAX=NMAX/NDOWN;//=4032
    const int NS=16;//<-?????
    const int NN=NS+ND;    //=103
    const int maxcand=100;
    const int c_cb = NDMAX;//=4032
    const int c_cd = NN*NSS;//=3296
    const int c_cd2 = NDMAX;//=4032
    double fs=DEC_SAMPLE_RATE/(double)NDOWN;                //!Sample rate after downsampling
    //double dt=1.0/fs;                         //!Sample interval after downsample (s)
    //tt=NSPS*dt                      //!Duration of "itone" symbols (s)
    //txt=NZ*dt                       //!Transmission length (s) without ramp up/down
    //h=1.0
    double a[5];
    std::complex<double> ctwk[2*NSS];
    //std::complex<double> ctwk2_[33][2*NSS]; //ctwk2(2*NSS,-16:16) 2*32=64
    bool rvec[77]=
        {
            0,1,0,0,1,0,1,0,0,1,0,1,1,1,1,0,1,0,0,0,1,0,0,1,1,0,1,1,0,
            1,0,0,1,0,1,1,0,0,0,0,1,0,0,0,1,0,1,0,0,1,1,1,1,0,0,1,0,1,
            0,1,0,1,0,1,1,0,1,1,1,1,1,0,0,0,1,0,1
        };
    QString hiscall = s_HisCall4;
    QString mycall  = s_MyCall4;
    QString message;
    QString msgsent;
    bool c77[100];//77+10
    //bool message77[130];
    bool message91[140];
    bool cw[2*ND+20];//174
    //double savg[NH1+10];
    //double sbase[NH1+10];
    std::complex<double> cd2[NDMAX+100];
    std::complex<double> cb[NDMAX+100];
    std::complex<double> cd[4000];//(103*32-1)=3295   //!Complex waveform
    bool   hbits[220];//(2*NN)=206
    double llr [180];//(2*ND)=174
    double llra[180];//(2*ND)=174
    double llrb[180];//(2*ND)=174
    double llrc[180];//(2*ND)=174
    double llrd[180];//(2*ND)=174
    bool apmask[174];//(2*ND)=174
    int iaptype=0;
    bool f_only_one_color = true;
    double bitmetrics_[3][220];//real bitmetrics(2*NN,3)//2*NN = 206
    bool badsync = false;

    int ndecodes=0;
    QString decodes[maxcand+20];
    for (int i = 0; i < maxcand; ++i)
        decodes[i]=" ";

    //int ntol = G_DfTolerance;
    double nfqso=fqso;
    double nfa=f0a;//100.0;              // mybe from waterfull scale
    double nfb=f0b;//3000.0;
    double nfa1=f0a1;//100.0;              // mybe from waterfull scale
    double nfb1=f0b1;//3000.0;
    bool nagain = false;//s_mousebutton = mousebutton; //mousebutton Left=1, Right=3 fullfile=0 rtd=2
    if (s_mousebutton4==3)
        nagain = true;

    int nQSOProgress = s_nQSOProgress4;
    int cont_type = 0;
    int cont_id   = 0;
    if (!f_multi_answer_mod4)
    {
        cont_type = s_ty_cont_ft4_28;
        cont_id   = s_id_cont_ft4_28;
    }
    //double nftx = s_nftx;
    //bool lapon = s_lapon;
    bool lapcqonly = false;//lapcqonly<-no used fom me. if no TX more then 10min use only AP1
    if (!s_lapon4) lapcqonly = true;// only in mshv


    if (first_ft4d || (cont_id!=cont_id0_ft4_2))
    {
        for (int idf = 0; idf < 41; ++idf)   //  41    33
        {//do idf=-16,16
            a[0]=(double)(idf-20);           //-+20    +-16
            a[1]=0.0;
            a[2]=0.0;
            a[3]=0.0;
            a[4]=0.0;
            for (int i = 0; i < 2*NSS; ++i)
                ctwk[i]=1.0;
            pomFt.twkfreq1(ctwk,2*NSS,fs/2.0,a,ctwk2_ft4_[idf]);//  ctwk2(:,idf)
        }

        for (int i = 0; i < 29; ++i)
        {
            /*if 		(cont_id== 0) mcq_ft4[i]=2*fmod(mcq_ft[i]+rvec[i],2)-1;
            else if (cont_id== 2) mcq_ft4[i]=2*fmod(mcqtest_ft[i]+rvec[i],2)-1;
            else if (cont_id== 3) mcq_ft4[i]=2*fmod(mcqtest_ft[i]+rvec[i],2)-1;
            else if (cont_id== 4) mcq_ft4[i]=2*fmod(mcqfd_ft[i]+rvec[i],2)-1;
            else if (cont_id== 5) mcq_ft4[i]=2*fmod(mcqtest_ft[i]+rvec[i],2)-1;//mcq_ft4[i]=2*fmod(mcqru_ft[i]+rvec[i],2)-1;
            else if (cont_id== 6) mcq_ft4[i]=2*fmod(mcqww_ft[i]+rvec[i],2)-1;
            else if (cont_id== 7) mcq_ft4[i]=2*fmod(mcqww_ft[i]+rvec[i],2)-1;  //mcq_ft4[i]=2*fmod(mcqru_ft[i]+rvec[i],2)-1;
            else if (cont_id== 8) mcq_ft4[i]=2*fmod(mcqww_ft[i]+rvec[i],2)-1;  //mcq_ft4[i]=2*fmod(mcqru_ft[i]+rvec[i],2)-1;
            else if (cont_id== 9) mcq_ft4[i]=2*fmod(mcqru_ft[i]+rvec[i],2)-1;
            else if (cont_id==10) mcq_ft4[i]=2*fmod(mcqbu_ft[i]+rvec[i],2)-1;
            else if (cont_id==11) mcq_ft4[i]=2*fmod(mcqft_ft[i]+rvec[i],2)-1;
            else if (cont_id==12) mcq_ft4[i]=2*fmod(mcqpdc_ft[i]+rvec[i],2)-1;
            else if (cont_id==13) mcq_ft4[i]=2*fmod(mcqtest_ft[i]+rvec[i],2)-1;*/
            mcq_ft4[i]=2*fmod(mcq_ft[cont_id][i]+rvec[i],2)-1;
            if (i<19)
            {
                mrrr_ft4[i]=2*fmod(mrrr_ft[i]+rvec[i+58],2)-1;
                m73_ft4[i]=2*fmod(m73_ft[i]+rvec[i+58],2)-1;
                mrr73_ft4[i]=2*fmod(mrr73_ft[i]+rvec[i+58],2)-1;
            }
        } //qDebug()<<cont_id;

        mycall0_ft4="";
        hiscall0_ft4="";
        cont_id0_ft4_2 = cont_id;
        first_ft4d=false;
    }

    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    mycall=mycall.trimmed();
    hiscall=hiscall.trimmed();
    if (mycall!=mycall0_ft4 || hiscall!=hiscall0_ft4)
    {
        //QString sss = "";
        for (int i = 0; i < 2*ND; ++i)
            apbits_ft4[i]=0;
        apbits_ft4[0]=99;
        apbits_ft4[29]=99;
        for (int i = 0; i < 28; ++i)
        {
            apmy_ru_ft4[i]=0;
            aphis_fd_ft4[i]=0;
        }
        bool nohiscall;
        int i3;
        int n3;
        bool unpk77_success;
        if (mycall.count()<3) goto c10;//if(len(trim(mycall)) .lt. 3) go to 10

        nohiscall=false;
        hiscall0_ft4=hiscall;
        if (hiscall0_ft4.count()<3)//if(len(trim(hiscall0)).lt.3) then
        {
            hiscall0_ft4=mycall;  //! use mycall for dummy hiscall - mycall won't be hashed.
            nohiscall=true;
        }
        message=mycall+" "+hiscall0_ft4+" RR73";//message=trim(mycall)//' '//trim(hiscall0)//' RR73'
        i3=0;//i3=-1
        n3=0;//n3=-1
        unpk77_success=false;
        TGenFt4->pack77(message,i3,n3,c77);
        msgsent=TGenFt4->unpack77(c77,unpk77_success);
        if (i3!=1 || (message!=msgsent) || !unpk77_success) goto c10;
        //read(c77,'(77i1)') message77
        for (int i = 0; i < 28; ++i)
        {
            apmy_ru_ft4[i]=2*fmod(c77[i]+rvec[i+1],2)-1;
            aphis_fd_ft4[i]=2*fmod(c77[i+29]+rvec[i+28],2)-1;
        }
        for (int i = 0; i < 77; ++i)
            c77[i]=fmod(c77[i]+rvec[i],2);

        TGenFt4->encode174_91(c77,cw);
        for (int i = 0; i < 2*ND; ++i)
            apbits_ft4[i]=2*cw[i]-1;
        if (nohiscall) apbits_ft4[29]=99;
c10:
        //continue;
        mycall0_ft4=mycall;
        hiscall0_ft4=hiscall;
    }

    if (nagain)
    {
        //ntol = 50;// +/-10 hv +/-25
        nfa = nfqso-50;//50
        nfb = nfqso+50;//50
    }
    nfa=fmax(200,nfa);    	//hv nfa down
    nfa=fmin(5000-10,nfa);  //hv nfa up
    nfb=fmax(200+10,nfb);   //hv nfb down
    nfb=fmin(5000,nfb);     //hv nfb up

    nfa1=fmax(200,nfa1);      //hv nfa down
    nfa1=fmin(5000-10,nfa1);  //hv nfa up
    nfb1=fmax(200+10,nfb1);   //hv nfb down
    nfb1=fmin(5000,nfb1);     //hv nfb up

    //double nfqso_calc = nfqso;
    if (nfqso<nfa || nfqso>nfb)
    {
        nfqso = nfa + ((nfb-nfa)/2.0);
        //qDebug()<<"FT4 D="<<decid<<nfqso;
    }

    //! ndepth=3: 3 passes, bp+osd
    //! ndepth=2: 3 passes, bp only
    //! ndepth=1: 1 pass, no subtraction
    int ndepth = s_decoder_deep4;
    //int max_iterations=40;
    double syncmin=1.2;//1.2
    bool dosubtract=true;
    bool doosd=true;
    int nsp=3;//2

    if (ndepth==2)
        doosd=false;
    if (ndepth==1)
    {
        nsp=1;
        dosubtract=false;
        doosd=false;
    }
    //qDebug()<<"new==============================================";

    ///subtract error
    /*double dd[96000];//9s 12000*9=108000 //8s 12000*8=96000
    for (int i = 0; i < 96000; ++i)
        dd[i]=dd01[i];*/
    ///subtract error

    int nd1 = 0;
    int nd2 = 0;
    for (int isp = 1; isp <= nsp; ++isp)
    {//do isp = 1,nsp
        if (isp==2)
        {
            if (ndecodes==0) break;//exit
            nd1=ndecodes;
        }
        else if (isp==3)
        {
            nd2=ndecodes-nd1;
            if (nd2==0)  break;//exit
        }

        double candidate[2][115];//(3,100)
        for (int i = 0; i < 2; ++i)
        {
            for (int j = 0; j < 101; ++j)
                candidate[i][j]=0.0;
        }
        int ncand=0;
        getcandidates4(dd,nfa,nfb,nfa1,nfb1,syncmin,nfqso,maxcand,candidate,ncand);//,sbase

        /*for (int z= 0; z < ncand; z++)
        {
        QString sss = "";
            sss.append(QString("%1").arg((int)candidate[0][z]));
            sss.append(" ");
            double xsnr=10*log10(candidate[1][z])-14.0;
            sss.append(QString("%1").arg(xsnr,0,'f',1));
        qDebug()<<z<<sss;
        } */

        bool dobigfft=true;
        for (int icand = 0; icand < ncand; ++icand)//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        {//do icand=1,ncand
            double f0=candidate[0][icand];       //f0=candidate(1,icand)
            double snr=candidate[1][icand]-1.0;  //snr=candidate(3,icand)-1.0
            //if (f0>1200 && f0<1400) qDebug()<<icand<<f0<<snr;
            ft4_downsample(dd,dobigfft,f0,cd2);  //!Downsample to 32 Sam/Sym
            if (dobigfft) dobigfft=false;
            double sum2=0.0;
            for (int i = 0; i < c_cd2; ++i)
                sum2+=creal(cd2[i]*conj(cd2[i]));//???
            sum2=sum2/((double)NMAX/(double)NDOWN);//sum2=sum(cd2*conjg(cd2))/(real(NZZ)/real(NDOWN))

            if (sum2>0.0)//if(sum2.gt.0.0) cd2=cd2/sqrt(sum2)
            {
                for (int i = 0; i < c_cd2; ++i)
                    cd2[i]=cd2[i]/sqrt(sum2);
            }
            //! Sample rate is now 12000/18 = 666.67 samples/second

            //int idfbest = 0;
            //int ibest = 0;
            //double smax=-99.0;
            //int kkk = 0;
            //double sf0 = f0;
            for (int iseg = 1; iseg <= 3; ++iseg)//! DT search is done over 3 segments
            {//do iseg=1,3
                int idfbest = 0;
                int ibest = -1;
                double smax=-99.0;
                double smax1=-99.0;
                for (int isync = 1; isync <= 2; ++isync)
                {
                    int idfmin,idfmax,idfstp,ibmin,ibmax,ibstp;
                    ibmin = 108; 
                    ibmax = 565;
                    if (isync==1)
                    {
                        idfmin=-12;//12
                        idfmax=12; //12
                        idfstp=3;  //3
                        //ibmin=-344;
                        //ibmax=1012;
                        if (iseg==1)
                        {
                            ibmin=108;
                            ibmax=565;//560
                        }
                        else if (iseg==2)
                        {
                            smax1=smax;
                            ibmin=555;//560
                            ibmax=1012;
                        }
                        else if (iseg==3)
                        {
                            ibmin=-344;
                            ibmax=118;//108
                        }
                        ibstp=4;
                    }
                    else
                    {
                        idfmin=idfbest-4;//-4
                        idfmax=idfbest+4;//4
                        idfstp=1;
                        ibmin=ibest-5;//ibmin=fmax(0,ibest-5);
                        ibmax=ibest+5;//ibmax=fmin(ibest+5,NDMAX/NDOWN-1);
                        ibstp=1;
                    }
                    //qDebug()<<"m/m"<<isync<<idfmin<<idfmax<<idfbest;
                    ibest=-1;
                    smax=-99.0;
                    idfbest=0;
                    for (int idf = idfmin; idf <= idfmax; idf+=idfstp)
                    {//do idf=idfmin,idfmax,idfstp
                        for (int istart = ibmin; istart <= ibmax; istart+=ibstp)
                        {//do istart=ibmin,ibmax,ibstp
                            double sync=-99.0;
                            sync4d(cd2,istart,ctwk2_ft4_[idf+20],1,sync);//20  sync4d(cd2,istart,ctwk2(:,idf),1,sync)  //!Find sync power
                            if (sync>smax) //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
                            {
                                smax=sync;
                                ibest=istart;//+ibstp;
                                idfbest=idf;//+idfstp;
                            }
                        }
                    }
                    //if(f0>1905 && f0<1930)
                    //qDebug()<<"iseg"<<smax<<iseg<<f0<<f0+idfbest<<ibest;
                }
                //if(f0>1905 && f0<1930)
                //qDebug()<<"CORR======"<<f0+idfbest<<idfbest<<ibest;
                if (iseg==1) smax1=smax;
                if (smax<1.2) continue;
                if (iseg>1 && smax<smax1) continue;
                double f1=f0+(double)idfbest; //qDebug()<<idfbest<<f1;
                if ( f1<=10.0 || f1>=4990.0 ) continue;//cycle
                ft4_downsample(dd,dobigfft,f1,cb); //!Final downsample, corrected f1
                sum2=0.0;
                for (int i = 0; i < c_cb; ++i)//sum2=sum(abs(cb)**2)/(real(NSS)*NN)
                    sum2+=cabs(cb[i])*cabs(cb[i]);
                sum2 = sum2/(double)(NSS*NN);
                if (sum2>0.0)
                {
                    for (int i = 0; i < c_cb; ++i)
                        cb[i]=cb[i]/sqrt(sum2);
                }
                //????????????????????????????
                //qDebug()<<"kkk="<<ibest<<f1;
                //const int c_cb = NDMAX;//=4032
                //const int c_cd = NN*NSS;//=3296
                for (int i = 0; i < c_cd+5; ++i) //cd=0. //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
                    cd[i]=0.0;
                if (ibest>=0)
                {
                    int it=fmin(NDMAX-1,ibest+NN*NSS-1);
                    int np=it-ibest+1;
                    for (int i = 0; i < np; ++i)
                        cd[i]=cb[i+ibest];//cd(0:np-1)=cb(ibest:it)
                    //qDebug()<<"kkk="<<np<<ibest<<it;
                }
                else
                {
                    //cd(-ibest:ibest+NN*NSS-1)=cb(0:NN*NSS+2*ibest-1)
                    for (int i = 0; i < (NN*NSS+2*ibest); ++i)
                    {
                        if (i-ibest>=0)//for any case array out of bounds
                            cd[i-ibest]=cb[i];
                    }
                }
                //qDebug()<<fmin(NDMAX-1,ibest+NN*NSS-1)-ibest+1<<NN*NSS+2*ibest<<c_cd<<c_cb<<f1;

                get_ft4_bitmetrics(cd,bitmetrics_,badsync);
                //qDebug()<<badsync<<f1;
                if (badsync) continue;
                //hbits=0 //hbits[206]  //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
                //where(bmeta>=0) hbits=1;
                for (int x = 0; x < 2*NN; ++x)
                {
                    if (bitmetrics_[0][x]>=0.0) hbits[x]=1;
                    else hbits[x]=0;
                }

                bool ms1[8] = {0,0,0,1,1,0,1,1};//count(hbits(  1:  8)==(/0,0,0,1,1,0,1,1/))
                int ns1=count_eq_bits(hbits,0,  ms1,8);
                bool ms2[8] = {0,1,0,0,1,1,1,0};//count(hbits( 67: 74)==(/0,1,0,0,1,1,1,0/))
                int ns2=count_eq_bits(hbits,66, ms2,8);
                bool ms3[8] = {1,1,1,0,0,1,0,0};//count(hbits(133:140)==(/1,1,1,0,0,1,0,0/))
                int ns3=count_eq_bits(hbits,132,ms3,8);
                bool ms4[8] = {1,0,1,1,0,0,0,1};//count(hbits(199:206)==(/1,0,1,1,0,0,0,1/))
                int ns4=count_eq_bits(hbits,198,ms4,8);
                int nsync_qual=ns1+ns2+ns3+ns4;
                //qDebug()<<"nsync_qual="<<nsync_qual<<ns1<<ns2<<ns3<<ns4<<f1;
                if (nsync_qual<20) continue;//cycle

                double scalefac=2.83;  //llr[174]; =2*ND
                for (int x = 0; x < 58; ++x)
                {
                    llra[x]=bitmetrics_[0][x+8];
                    llra[x+58]=bitmetrics_[0][x+74];
                    llra[x+116]=bitmetrics_[0][x+140];
                    llrb[x]=bitmetrics_[1][x+8];
                    llrb[x+58]=bitmetrics_[1][x+74];
                    llrb[x+116]=bitmetrics_[1][x+140];
                    llrc[x]=bitmetrics_[2][x+8];
                    llrc[x+58]=bitmetrics_[2][x+74];
                    llrc[x+116]=bitmetrics_[2][x+140];
                }
                double maxval_llra_abs = 0.0;
                for (int x = 0; x < 2*ND; ++x)//llr[174]; =2*ND
                {
                    llra[x]=scalefac*llra[x];
                    llrb[x]=scalefac*llrb[x];
                    llrc[x]=scalefac*llrc[x];

                    double llra_abs = fabs(llra[x]);
                    if (llra_abs>maxval_llra_abs)
                        maxval_llra_abs=llra_abs;
                }
                double apmag = maxval_llra_abs*1.1;//apmag=maxval(abs(llra))*1.1

                /*int npasses;
                if (lapon)
                {
                //if (!lapcqonly)
                	npasses=3+nappasses_4[nQSOProgress];//ft4=2,2,2,2,2,3  ft8=2,2,2,4,4,3
                //else
                	//npasses=4;
                }
                else
                npasses=3;*/
                int npasses=3+nappasses_4[nQSOProgress];//npasses=3+nappasses(nQSOProgress)
                if (lapcqonly) npasses=4;//??? new if(lapcqonly) npasses=4
                if (ndepth==1) npasses=3;
                //hv=5 new  if(ncontest.ge.6) npasses=3 ! Don't support Fox and Hound
                if (cont_type>=5) npasses=3;

                int nharderror = -1;
                for (int ipass = 1; ipass <= npasses; ++ipass)
                {//do ipass=1,npasses
                    for (int z = 0; z < 2*ND; ++z)
                    {
                        if (ipass==1)
                            llr[z]=llra[z];//if(ipass.eq.2) llr=llr1
                        else if (ipass==2)
                            llr[z]=llrb[z];
                        else if (ipass==3)
                            llr[z]=llrc[z];//llr=llr0
                        //if (ipass==1)
                        //qDebug()<<"ft8b ipass======== Start"<<llrd[z];
                    }
                    if (ipass<=3)
                    {
                        for (int z = 0; z < 2*ND; ++z)
                            apmask[z]=0;
                        iaptype=0;
                    }
                    double napwid=80.0; //2.39 old double napwid=60.0;
                    if (ipass > 3)
                    {
                        for (int z = 0; z < 2*ND; ++z)
                            llrd[z]=llrc[z]; //2.39 old llrd[z]=llra[z];
                        //iaptype=naptypes(nQSOProgress,ipass-3)
                        iaptype=naptypes_4[nQSOProgress][ipass-(3+1)];// v1 4+1
                        if (lapcqonly) iaptype=1;
                        //! ncontest=0 : NONE			  CQ
                        //!          1 : NA_VHF			TEST
                        //!          2 : EU_VHF			TEST
                        //!          3 : FIELD DAY		  FD
                        //!          4 : RTTY             RU
                        //!          5 : WW_DIGI    MSHV= WW
                        //!          6 : FOX        MSHV= BU
                        //!          7 : HOUND		    NONE
                        //!
                        //! Conditions that cause us to bail out of AP decoding

                        // Activity Type                id	type	dec-id       dec-type	dec-cq
                        //"Standard"					0	0		0 = CQ		 0			0
                        //"EU RSQ And Serial Number"	1	NONE	1  NONE		 NONE		NONE
                        //"NA VHF Contest"				2	2		2  CQ TEST	 1			3 = CQ TEST
                        //"EU VHF Contest"				3 	3		3  CQ TEST	 2			3 = CQ TEST
                        //"ARRL Field Day"				4	4		4  CQ FD	 3			2 = CQ FD
                        //"ARRL Inter. Digital Contest"	5	2		5  CQ TEST   1 			3 = CQ TEST
                        //"WW Digi DX Contest"			6	2		6  CQ WW	 1			4 = CQ WW
                        //"FT4 DX Contest"				7	2		7  CQ WW	 1			4 = CQ WW
                        //"FT8 DX Contest"				8	2		8  CQ WW	 1			4 = CQ WW
                        //"FT Roundup Contest"			9	5		9  CQ RU	 4			1 = CQ RU
                        //"Bucuresti Digital Contest"	10 	5		10 CQ BU 	 4			5 = CQ BU
                        //"FT4 SPRINT Fast Training"	11 	5		11 CQ FT 	 4			6 = CQ FT
                        //"PRO DIGI Contest"			12  5		12 CQ PDC 	 4			7 = CQ PDC
                        //"CQ WW VHF Contest"			13	2		13 CQ TEST	 1			3 = CQ TEST

                        //double napwid=60.0; //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
                        //hv new =4
                        if (cont_type<=4 && iaptype>=3 && fabs(f1-nfqso)>napwid) continue;
                        //if(iaptype.ge.2 .and. apbits(1).gt.1) cycle  ! No, or nonstandard, mycall
                        //if(iaptype.ge.3 .and. apbits(30).gt.1) cycle ! No, or nonstandard, dxcall
                        if (iaptype>=2 && apbits_ft4[0]>1) continue;
                        if (iaptype>=3 && apbits_ft4[29]>1) continue;

                        if (iaptype==1) //then  ! CQ or CQ TEST or CQ FD or CQ RU or CQ SCC
                        {
                            for (int z = 0; z < 2*ND; ++z)//2*ND=174
                            {
                                apmask[z]=0;
                                if (z<29)
                                {
                                    apmask[z]=1;
                                    llrd[z]=apmag*(double)mcq_ft4[z];
                                }
                            } //qDebug()<<iaptype;
                        }
                        if (iaptype==2) //then ! MyCall,???,???
                        {
                            for (int z = 0; z < 2*ND; ++z)
                                apmask[z]=0;
                            if (cont_type==0 || cont_type==1)// || ncontest==5
                            {
                                for (int z = 0; z < 29; ++z)
                                {
                                    apmask[z]=1;//apmask(1:29)=1
                                    llrd[z]=apmag*(double)apbits_ft4[z];//llrd(1:29)=apmag*apsym(1:29)
                                }
                            }
                            else if (cont_type==2)
                            {
                                for (int z = 0; z < 28; ++z)
                                {
                                    apmask[z]=1;//apmask(1:28)=1
                                    llrd[z]=apmag*(double)apbits_ft4[z];//llrd(1:28)=apmag*apsym(1:28)
                                }
                            }
                            else if (cont_type==3)
                            {
                                for (int z = 0; z < 28; ++z)
                                {
                                    apmask[z]=1;  //apmask(1:28)=1
                                    llrd[z]=apmag*(double)apbits_ft4[z];//llrd(1:28)=apmag*apbits(1:28)
                                }
                            }
                            else if (cont_type==4)//RTTY UU HV-6=BU   || ncontest==6
                            {
                                for (int z = 1; z < 29; ++z)
                                {
                                    apmask[z]=1;//apmask(2:29)=1
                                    llrd[z]=apmag*(double)apmy_ru_ft4[z-1];//llrd(2:29)=apmag*apmy_ru(1:28)
                                }
                            }
                        }
                        if (iaptype==3) //then ! MyCall,DxCall,???
                        {
                            for (int z = 0; z < 2*ND; ++z)
                                apmask[z]=0;
                            if (cont_type==0 || cont_type==1 || cont_type==2)// || ncontest==5
                            {
                                for (int z = 0; z < 58; ++z)
                                {
                                    apmask[z]=1;// apmask(1:58)=1
                                    llrd[z]=apmag*(double)apbits_ft4[z];// llrd(1:58)=apmag*apbits(1:58)
                                }
                            }
                            else if (cont_type==3) //then ! Field Day
                            {
                                for (int z = 0; z < 56; ++z)//2.36 57 or 56 ???
                                {
                                    //2.36 if (z<56)//57 or 56 ???
                                    apmask[z]=1;//apmask(1:56)=1
                                    if (z<28)
                                        llrd[z]=apmag*(double)apbits_ft4[z];//llrd(1:28)=apmag*apbits(1:28)
                                    if (z<28)
                                        llrd[z+28]=apmag*(double)aphis_fd_ft4[z];//llrd(29:56)=apmag*aphis_fd(1:28)????
                                }
                            }
                            else if (cont_type==4)//then ! RTTY RU HV 6=BU // || ncontest==6
                            {
                                for (int z = 0; z < 57; ++z)
                                {
                                    if (z>0)
                                        apmask[z]=1;// apmask(2:57)=1
                                    if (z<28)
                                        llrd[z+1]=apmag*(double)apmy_ru_ft4[z];//llrd(2:29)=apmag*apmy_ru(1:28)
                                    if (z>28)
                                        llrd[z]=apmag*(double)apbits_ft4[z];// llrd(30:57)=apmag*apbits(30:57)
                                }
                            }
                        }
                        if (iaptype==4 || iaptype==5 || iaptype==6)
                        {
                            for (int z = 0; z < 2*ND; ++z)
                                apmask[z]=0;
                            //if(ncontest.le.5) then
                            if (cont_type<=4)//hv new=4
                            {
                                for (int z = 0; z < 77; ++z)
                                    apmask[z]=1; //apmask(1:77)=1   //! mycall, hiscall, RRR|73|RR73
                                if (iaptype==6)
                                {
                                    for (int z = 0; z < 77; ++z)
                                        llrd[z]=apmag*apbits_ft4[z];//llrd(1:77)=apmag*apbits(1:77)
                                }
                            }
                        }
                        for (int z = 0; z < 2*ND; ++z)
                            llr[z]=llrd[z];
                    }

                    for (int z = 0; z < 174; ++z)
                    {
                        cw[z]=0;
                        if (z<120)
                            message91[z]=0;
                    }
                    double dmin=0.0;

                    int ndeep=2;
                    int maxosd=2;
                    if (fabs(nfqso-f1)<=napwid)
                    {
                        ndeep=2;
                        maxosd=3;
                    }
                    if (!doosd) maxosd = -1;
                    //ndeep=3;
                    //Keff=91
                    pomFt.decode174_91(llr,maxosd,ndeep,apmask,message91,cw,nharderror,dmin);//Keff,ntype,
                    //for (int z = 0; z < 77; ++z)//message77=message91(1:77)
                    //message77[z]=message91[z];

                    //if(sum(message77).eq.0) cycle
                    int c_m77 = 0;
                    for (int z = 0; z < 77; z++)
                        c_m77 +=message91[z];
                    if (c_m77==0) continue;
                    /*int c_cw = 0;
                    for (int z = 0; z < 174; z++)
                    {
                        if (cw[z]==0)// 3*ND=174
                            c_cw++;
                    }
                    if (c_cw==174) continue;*/

                    if ( nharderror>=0 )
                    {
                        for (int z = 0; z < 77; z++)
                            message91[z]=fmod(message91[z]+rvec[z],2); //! remove rvec scrambling

                        bool unpk77_success=false;
                        QString message = TGenFt4->unpack77(message91,unpk77_success);
                        if (!unpk77_success) break;//2.43 exit

                        //if(message.isEmpty()) {qDebug()<<"EMPTY MSG"<<message; /*continue;*/}//2.21
                        if (unpk77_success && dosubtract)
                        {
                            //int i3=4*message77[74] + 2*message77[75] + message77[76];
                            int i4tone[120];
                            TGenFt4->make_c77_i4tone(message91,i4tone);

                            double dt=(double)ibest/666.67;//750.0;
                            subtractft4(dd,i4tone,f1,dt);
                            //qDebug()<<"subtractft4"<<message<<dt<<f1<<ipass<<i3;
                        }
                        //qDebug()<<"mmm"<<message;
                        bool ldupe=false;
                        for (int z = 0; z < ndecodes; z++)//maxcand
                        {//do i=1,ndecodes
                            if (decodes[z]==message)
                            {
                                ldupe=true;
                                break;
                            }
                        }
                        if (ldupe) break;// inportent for subtract

                        decodes[ndecodes]=message;
                        if (ndecodes < (maxcand-1)) ndecodes++;

                        double xsnr = 0.0;
                        if (snr>0.0) xsnr=10*log10(snr)-14.8;
                        else xsnr=-21.0;

                        int nsnr=(int)(fmax(-21.0,xsnr));
                        if (nsnr > 49) nsnr = 49; //2.31

                        float xdt=(float)ibest/666.67 - 0.5;

                        if (f_only_one_color)
                        {
                            f_only_one_color = false;
                            EmitBackColor();
                        }
                        /*if (s_time4_prev!=s_time4)
                        {
                        s_time4_prev=s_time4;
                        emit EmitBackColor();
                        }*/
                        /*if (f_new_p)
                        {
                           	f_new_p = false;
                           	emit EmitBackColor();
                        }*/

                        //double dmin = 0.0;
                        //double qual=1.0-((double)nharderror+dmin)/60.0;
                        //float qual=1.0-((float)nharderror)/60.0;
                        float qual=1.0-((float)nharderror+(float)dmin)/60.0;
                        if (qual<0.001) qual=0.0;//no show -0.0
                            
                        QString str_iaptype = "";
                        if (qual<0.17) str_iaptype = "? ";                            
                        if (iaptype!=0) str_iaptype.append("AP");                            
                        str_iaptype.append(QString("%1").arg(iaptype));

                        int df_hv = f1-s_nftx4;//2.12
                        QString sdtx = QString("%1").arg(xdt,0,'f',1);
                        if (sdtx=="-0.0") sdtx="0.0";//2.08 remove -0.0 exception

                        QStringList list;
                        list <<s_time4<<QString("%1").arg(nsnr)
                        <<sdtx<<QString("%1").arg((int)df_hv)
                        <<message<<str_iaptype
                        <<QString("%1").arg(qual,0,'f',1)
                        <<QString("%1").arg((int)f1);

                        EmitDecodedTextFt(list);//1.27 psk rep   fopen bool true    false no file open
                        have_dec = true;

                        /*//if (abs((int)s_nfqso65-(int)f1)<=10 || list.at(4).contains(s_MyCall))
                        //"TA2NC RR73; SP9HWY <LZ2HV> +00"
                        //"TU; LZ2HV SP9HWY R 589 NY"
                        //"TU; LZ2HV SP9HWY R 589 0001"
                        QString tstr = list.at(4);
                        tstr.remove("<");//v2 protokol 2.02
                        tstr.remove(">");//v2 protokol 2.02
                        QStringList tlist = tstr.split(" ");
                        bool fmyc = false;
                        for (int x = 0; x<tlist.count(); ++x)
                        {
                            if (tlist.at(x)==s_MyBaseCall4 || tlist.at(x)==s_MyCall4)//2.02
                            {
                                fmyc = true;
                                break;
                            }
                        }
                        if (abs((int)nfqso_calc-(int)f1)<=10 || fmyc)
                            emit EmitDecodetTextRxFreq(list,true);//1.60= true no emit other infos from decode list2 s_fopen*/

                        break;//exit
                    }
                }  //!Sequence estimation
                if (nharderror>=0) break;
            }      //!3 DT segments
        }          //!Candidate list
    }              //!Subtraction loop
}

