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

#include "decoderq65.h"
#include "gen_q65.h"
// #include "../HvMsPlayer/libsound/gen_pulse_gfsk.h"
//#include "ft_all_ap_def.h"
//#include <unistd.h>

#define NSTEP 8
#define NMAX (120*12000)          // !Max TRperiod is 300 s
#define PLOG_MIN -242.0           //!List decoding threshold

static const int isync[86] =
    {
        1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85
    }
    ;//22

//#define s3_offsetc 84
#define ccf_offsetr 70   //and ccf1_
#define ccf_offsetc 7000 //and ccf1_

//#include <QtGui>

DecoderQ65::DecoderQ65()
{
	pomAll.initPomAll();//2.66 for pctile_shell
	f_multi_answer_modq65 = false;
    LL0=iz0=jz0=0;
    ncw = 0;
    mode_q65 = 1;
    s_ntrperiod = 30;//default
    s_ndepth = 1;
    TGenQ65 = new GenQ65(true);
    npasses = 0;
    ncontest0 = -1;//first
    f_clravg_after_decode = false;
    s_nQSOprogress = 0;
    f_averaging = false;
    f_single_decode = false;
    lclearave = false;
    nftx = 1200.0;
    f_emedelay = false;
    s_cont_id = 0;
    s_cont_type = 0;
    s_maxiters = 0;
    s_lapon = false;
    nhist = 0; //twopi=8.0*atan(1.0); //pi=4.0*atan(1.0);
    drift = 0.0;
    f_max_drift = false;
    f0nd = 1200.0;
    xdtnd = 0.01;
    max_drift = 0;
}
DecoderQ65::~DecoderQ65() {
    delete TGenQ65;
}
void DecoderQ65::SetStMultiAnswerMod(bool f)//2.65
{
    f_multi_answer_modq65 = f;
}	
void DecoderQ65::SetMaxDrift(bool f)
{
    f_max_drift = f;
}
void DecoderQ65::SetPeriod(int i)
{
    s_ntrperiod = i; //qDebug()<<i;
}
void DecoderQ65::SetStDecoderDeep(int i)
{
    s_ndepth = i;
}
void DecoderQ65::SetStWords(QString s1,QString,int cq3,int ty4)
{
    s_mycall = s1;//May call or my base call ?
    s_cont_id = cq3;
    s_cont_type = ty4; //qDebug()<<id3<<ty4;
}
void DecoderQ65::SetStHisCallGrid(QString c,QString g)
{
    s_hiscall = c;
    s_hisgrid = g; //qDebug()<<"s_cg"<<s_hiscall<<s_hisgrid;
}
void DecoderQ65::SetStDecode(QString time,int mousebutton)
{
    s_time = time;
    s_mousebutton = mousebutton;//mousebutton Left=1, Right=3 fullfile=0 rtd=2
}
void DecoderQ65::SetStQSOProgress(int i)
{
    s_nQSOprogress = i; //qDebug()<<"s_nQSOprogress"<<i;
}
void DecoderQ65::AutoClrAvgChanged(bool f)
{
    f_clravg_after_decode = f; //qDebug()<<"Auto clar avg"<<f;
}
void DecoderQ65::AvgDecodeChanged(bool f)
{
    f_averaging = f; //qDebug()<<"f_averaging"<<f;
}
void DecoderQ65::SetSingleDecQ65(bool f)
{
    f_single_decode = f; //qDebug()<<"f_single_decode"<<f;
}
void DecoderQ65::SetClearAvgQ65()
{
    lclearave = true;  //qDebug()<<"lclearave"<<lclearave;
    EmitAvgSavesQ65(0,0);
}
void DecoderQ65::SetTxFreq(double fr)
{
    nftx = fr;
}
void DecoderQ65::SetDecAftEMEDelay(bool f)
{
    f_emedelay = f; //qDebug()<<"f_emedelay"<<f_emedelay;
}
void DecoderQ65::SetStApDecode(bool f)
{
    s_lapon = f; //qDebug()<<"s_lapon"<<s_lapon;
}
void DecoderQ65::q65_snr(int *dat4,double dtdec,double f0dec,int mode_q65,double &snr2)//int nused,
{
    //! Estimate SNR of a decoded transmission by aligning the spectra of
    //! all 85 symbols.
    int codeword2[70];//63
    int itone[100];//(85)
    double spec[7000];//iz0 7000

    q65S.q65_enc(dat4,codeword2);
    int iii=0;
    int kk=0;
    for (int j = 0; j < 85 ; ++j)
    {//do j=1,85
        if (j==isync[iii]-1)//2.57 error -1
        {
            iii++;
            itone[j]=0;
        }
        else
        {
            itone[j]=codeword2[kk] + 1;
            kk++;
        }
    }

    for (int i = 0; i < 6700 ; ++i) spec[i]=0.0;
    int lagpk=(int)(dtdec/dtstep);
    for (int k = 0; k < 85 ; ++k)//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {//do k=1,85
        int j=j0 + NSTEP*k + lagpk;//j=j0 + NSTEP*(k-1) + 1 + lagpk
        if (j>=0 && j<jz0)
        {
            for (int i = 0; i < iz0 ; ++i)
            {//do i=1,iz0
                int ii=i+mode_q65*itone[k];
                if (ii>=0 && ii<iz0) spec[i]+=s1raw_[j][ii];//(ii,j)
            }
        }
    }

    int i00=(int)(f0dec/df);
    int nsum=fmax(10*mode_q65,(int)(50.0/df));
    int ia=fmax(0,(i00 - 2*nsum));//2.55 ia=max(1,i0-2*nsum)  //old int ia=i00 - 2*nsum;
    int ib=fmin(iz0,(i00+2*nsum));//2.55 ib=min(iz0,i0+2*nsum) //old int ib=i00 + 2*nsum;
    int ibb = ib-nsum;
    double sum1 = 0.0;  //qDebug()<<ia<<ib<<nsum<<ibb;
    double sum2 = 0.0;
    for (int i = 0; i < nsum ; ++i)
    {
        sum1+=spec[i+ia];//sum1=sum(spec(ia:ia+nsum-1))
        sum2+=spec[ibb+i];//sum2=sum(spec(ib-nsum+1:ib))
    }
    double avg=(sum1+sum2)/(2.0*(double)nsum);
    if (avg==0.0) avg=0.000001;
    for (int i = 0; i < iz0 ; ++i) spec[i]/=avg;                          //!Baseline level is now 1.0
    //double smax = pomAll.maxval_da_beg_to_end(spec,ia,ib);//smax=maxval(spec(ia:ib))
    double sig_area = 0.0;
    for (int i = ia+nsum; i < ib-nsum ; ++i) sig_area+=(spec[i]-1.0);  //sig_area=sum(spec(ia+nsum:ib-nsum)-1.0)
    //w_equiv=sig_area/(smax-1.0)
    snr2=pomAll.db(fmax(1.0,sig_area)) - pomAll.db(2500.0/df);//snr2=db(max(1.0,sig_area)) - db(2500.0/df)
    //2.66 stop w260rc1
    /*if (nused==2) snr2=snr2 - 2.0;
    if (nused==3) snr2=snr2 - 2.9;
    if (nused>=4) snr2=snr2 - 3.5;*/
}
void DecoderQ65::SetClearAvgQ65all()
{
    //qDebug()<<"Clear ALL";
    for (int i = 0; i < 2; ++i)
    {
        for (int j = 0; j < 798 ; ++j)
        {
            for (int z = 0; z < 6998 ; ++z) s1a_[i][j][z]=0.0;//s1a=0.
        }
    }
    navg[0]=0;
    navg[1]=0;
    EmitAvgSavesQ65(0,0);
}
void DecoderQ65::q65_clravg()
{
    //! Clear the averaging array to start a new average.
    //s1a_[2][800][7000] = 0.0;   //if(allocated(s1a)) s1a(:,:,iseq)=0.
    for (int j = 0; j < 798 ; ++j)
    {
        for (int z = 0; z < 6998 ; ++z) s1a_[iseq][j][z]=0.0;//s1a=0.
    }
    navg[iseq]=0;
    EmitAvgSavesQ65(navg[0],navg[1]);
}
bool DecoderQ65::isgrid4_rr73(QString s)
{
    bool res = false;
    /*isgrid(g1)=g1(1:1).ge.'A' .and. g1(1:1).le.'R' .and. g1(2:2).ge.'A' .and. &
        g1(2:2).le.'R' .and. g1(3:3).ge.'0' .and. g1(3:3).le.'9' .and.       &
        g1(4:4).ge.'0' .and. g1(4:4).le.'9' .and. g1(1:4).ne.'RR73'*/
    if (s.count()>3)
    {
        int c1 = (int)s.at(0).toLatin1();
        int c2 = (int)s.at(1).toLatin1();
        int c3 = (int)s.at(2).toLatin1();
        int c4 = (int)s.at(3).toLatin1();
        if (c1>=(int)'A' && c1<=(int)'R' && c2>=(int)'A' && c2<=(int)'R' &&
                c3>=(int)'0' && c3<=(int)'9' && c4>=(int)'0' && c4<=(int)'9' && s.mid(0,4)!="RR73")
            res = true;
    }
    return res;
}
void DecoderQ65::q65_hist(int if0,QString msg0,QString &dxcall,QString &dxgrid)//2.55 f=rtue=write f=false=find ,bool f
{
    //! Save the MAXHIST most receent decodes, and their f0 values; or, if
    //! dxcall is present, look up the most recent dxcall and dxgrid at the
    //! specified f0.
    const int MAXHIST = 60;//wsjt-x=100
    QString g1;

    //if(!dxcall.isEmpty()) goto c100;  //if(present(dxcall)) goto c100; //!This is a lookup request
    //if(!f) goto c100; //2.55 f=rtue=write f=false=find
    if (!msg0.isEmpty())
    {
        if (nhist>MAXHIST-1)
        {
            for (int i = 0; i < MAXHIST-1; ++i) //!List is full, must make room
            {
                nf0[i]=nf0[i+1];
                msg[i]=msg[i+1];
            }
            nhist = MAXHIST-1;
        }                                      //!Insert msg0 at end of list
        nf0[nhist]=if0;
        msg[nhist]=msg0;
        nhist++;
        //for (int i = 0; i < nhist ; ++i) qDebug()<<QString("%1").arg(nf0[i])<<msg[i];
        //qDebug()<<"-------------------------";
        return;
    }
    //c100:
    //hv dxcall="";                        //!This is a lookup request
    //hv dxgrid=""; //! Look for a decode close to if0, starting with most recent ones
    for (int i = nhist; i >= 0; --i)//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {//do i=nhist,1,-1
        if (fabs(nf0[i]-if0)>10) continue; //if(abs(nf0(i)-if0).gt.10) cycle
        int i1 = msg[i].indexOf(" ",0); //i1=index(msg(i),' ')
        if (i1>=3 && i1<=12) //LZ2HV SP9HWY KN23   if(i1>=4 .and. i1<=13) then
        {
            int i2=msg[i].indexOf(" ",i1+1); //i2=index(msg(i)(i1+1:),' ') + i1
            dxcall=msg[i].mid(i1+1,i2-(i1+1));  //dxcall=msg(i)(i1+1:i2-1)
            g1=msg[i].mid(i2+1,4); //g1=msg(i)(i2+1:i2+4)
            if (isgrid4_rr73(g1)) dxgrid=g1; //if(isgrid(g1)) dxgrid=g1(1:4)
            else dxgrid=""; //hv
            break;
        }
    } //qDebug()<<"RESULT="<<if0<<dxcall<<dxgrid;
}
void DecoderQ65::q65_set_list(QString mycall,QString hiscall,QString hisgrid)//2.61
{
    ncw = 0;
    if (hiscall.isEmpty()) return;
    ncw = MAX_NCW;

    bool my_std  = pomAll.isStandardCall(mycall.trimmed());
    bool his_std = pomAll.isStandardCall(hiscall.trimmed());

    //QString msg0=mycall+" "+hiscall+" ";
    for (int i = 0; i < ncw; ++i)
    {//do i=1,ncw
        //QString msg = msg0;
        QString msg = mycall+" "+hiscall;
        if (!my_std)
        {
            //if (i==0) msg = "<"+mycall+"> "+hiscall;
            //if (i>=5) msg = "<"+mycall+"> "+hiscall+" ";
            if (i==0 || i>=5) msg = "<"+mycall+"> "+hiscall;
            if (i>=1 && i<=3) msg = mycall+" <"+hiscall+">";
        }
        else if (!his_std)
        {
            //if(i==0) msg = "<"+mycall+"> "+hiscall;
            //if(i>=1 && i<=3) msg = "<"+mycall+"> "+hiscall+" ";
            if (i<=3 || i==5) msg = "<"+mycall+"> "+hiscall;//wsjt252
            //if (i==5) msg = "TNX 73 GL";
            if (i>=6) msg = mycall+" <"+hiscall+">";
        }
        //else if (i==0) msg = msg0.trimmed();

        if (i==1) msg.append(" RRR");
        if (i==2) msg.append(" RR73");
        if (i==3) msg.append(" 73");
        if (i==4)
        {
            if (his_std)  msg="CQ "+hiscall+" "+hisgrid.mid(0,4);//KN23SF
            if (!his_std) msg="CQ "+hiscall;//KN23SF
        }
        if (i==5 && his_std) msg.append(" "+hisgrid.mid(0,4));

        if (i>=6 && i<206)
        {
            int isnr = -50 + (i-6)/2; //isnr = -50 + (i-7)/2 ss.append(QString("%1").arg(abs(s.toInt()),2,10,QChar('0')));
            if (((i+1) & 1)==1)
            {
                if (isnr>-1) msg.append(" +"+QString("%1").arg(abs(isnr),2,10,QChar('0')));
                else msg.append(" -"+QString("%1").arg(abs(isnr),2,10,QChar('0')));
            }
            else
            {
                if (isnr>-1) msg.append(" R+"+QString("%1").arg(abs(isnr),2,10,QChar('0')));
                else msg.append(" R-"+QString("%1").arg(abs(isnr),2,10,QChar('0')));
            }

        }
        /*if (i<=9 || i>=200)
        {
        	qDebug()<<i<<msg;
        	if (i==205) qDebug()<<"------------------------------";
        }*/
        int itone[90];
        TGenQ65->genq65itone(msg,itone,false);
        int x = 0;
        int j = 0;
        for (int k = 0; k < 85; ++k)
        {//do k=1,85
            if (k==isync[x]-1)
            {
                x++;
                continue;
            }
            codewords_[i][j]=itone[k];//false make this -> codewords(j,i)=itone(k) - 1
            j++;
        } //qDebug()<<j<<x;
        codewords_[i][j]=0;
        //QString sss;
        //for (int x = 0; x < 63; ++x) sss.append(QString("%1").arg((int)codewords_[i][x]));
        //qDebug()<<"msg"<<sss;
    }
    int c_c = 0;
    for (int i = 0; i < ncw; ++i)
    {
        for (int j = 0; j < 63; ++j)//po kolko da sa
        {
            codewords_1da[c_c]=codewords_[i][j];
            c_c++;
        }
    }
}
/*void DecoderQ65::q65_set_list(QString mycall,QString hiscall,QString hisgrid)
{
    ncw = 0;
    if (hiscall.isEmpty()) return;
    ncw = MAX_NCW;

    QString msg0=mycall+" "+hiscall+" ";
    for (int i = 0; i < ncw; ++i)
    {//do i=1,ncw
        QString msg = msg0;
        if (i==0) msg = msg0.trimmed();
        if (i==1) msg.append("RRR");
        if (i==2) msg.append("RR73");
        if (i==3) msg.append("73");
        if (i==4) msg="CQ "+hiscall+" "+hisgrid.mid(0,4);//KN23SF
        if (i==5) msg.append(hisgrid.mid(0,4));
        if (i>=6 && i<206)
        {
            int isnr = -50 + (i-6)/2; //isnr = -50 + (i-7)/2 ss.append(QString("%1").arg(abs(s.toInt()),2,10,QChar('0')));
            if (((i+1) & 1)==1)
            {

                if (isnr>-1) msg.append("+"+QString("%1").arg(abs(isnr),2,10,QChar('0')));
                else msg.append("-"+QString("%1").arg(abs(isnr),2,10,QChar('0')));
            }
            else
            {
                if (isnr>-1) msg.append("R+"+QString("%1").arg(abs(isnr),2,10,QChar('0')));
                else msg.append("R-"+QString("%1").arg(abs(isnr),2,10,QChar('0')));
            }

        } //qDebug()<<i<<msg;
        int itone[90];
        TGenQ65->genq65itone(msg,itone,false);
        int x = 0;
        int j = 0;
        for (int k = 0; k < 85; ++k)
        {//do k=1,85
            if (k==isync[x]-1)
            {
                x++;
                continue;
            }
            codewords_[i][j]=itone[k];//false make this -> codewords(j,i)=itone(k) - 1
            j++;
        } //qDebug()<<j<<x;
        codewords_[i][j]=0;
        //QString sss;
        //for (int x = 0; x < 63; ++x) sss.append(QString("%1").arg((int)codewords_[i][x]));
        //qDebug()<<"msg"<<sss;
    }
    int c_c = 0;
    for (int i = 0; i < ncw; ++i)
    {
        for (int j = 0; j < 63; ++j)//po kolko da sa
        {
            codewords_1da[c_c]=codewords_[i][j];
            c_c++;
        }
    }
}*/
void DecoderQ65::smo121(double *x,int beg,int nz)
{
    double x0=x[beg];
    for (int i =1; i<nz-1; i++)
    {//do i=2,nz-1
        double x1=x[i+beg];
        x[i+beg]=0.5*x[i+beg] + 0.25*(x0+x[i+1+beg]);// x(i)=0.5*x(i) + 0.25*(x0+x(i+1))
        x0=x1;
    }
}
void DecoderQ65::q65_symspec(double *iwave,int iz,int jz,double s1_[800][7000])
{
    //! Compute symbol spectra with NSTEP time-steps per symbol.
    std::complex<double> c0[17000];//c0(0:nsps-1)  120 nsps=16000;
    //std::complex<double> *c0= new std::complex<double>[17000];
    double dc0[17000];

    int nfft=nsps;
    double fac=(1/32767.0)*0.01;//hv double correction ? =0.01

    for (int j = 0; j < jz ; ++j)
    {//do j=1,jz                              !Compute symbol spectra at step size
        int i1=(j-0)*istep;
        int i2=i1+nsps-0;
        int k=0;//k=-1
        for (int i = i1; i < i2; ++i)//for (int i = i1; i < i2; i+=2)
        {//do i=i1,i2,2          //!Load iwave data into complex array c0, for r2c FFT
            dc0[k]=fac*iwave[i]; //c0(k)=fac*cmplx(xx,yy)
            /*double xx = iwave[i];
            double yy = iwave[i+1];
            c0[k]=fac*(xx+yy*I);*/
            k++;
        }
        for (int z = 0; z < 16100 ; ++z) c0[z]=0.0+0.0*complex_i;//c0(k+1:)=0.
        f2a->four2a_d2c(c0,dc0,nfft,-1,0); //call four2a(c0,nfft,1,-1,0) / !r2c FFT  f2a.four2a_d2c(cx_ft8,x,NFFT1,-1,0,decid);//call four2a(cx,NFFT1,1,-1,0)
        for (int i = 0; i < iz ; ++i)
        {//do i=1,iz
            s1_[j][i]=(creal(c0[i])*creal(c0[i]) + cimag(c0[i])*cimag(c0[i]));//s1(i,j)=real(c0(i))**2 + aimag(c0(i))**2
        } //qDebug()<<"nsmo"<<j<<iz<<s1_[100][100]<<s1_[200][300];
        //! For large Doppler spreads, should we smooth the spectra here? //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        //qDebug()<<nsmo;
        if (nsmo<=1) nsmo=0;
        for (int i = 1; i <= nsmo; ++i)
        {//do i=1,nsmo
            smo121(s1_[j],0,iz);//call smo121(s1(1:iz,j),iz)
        }
    }
    if (lnewdat)
    {
        /*for (int j = 0; j < jz ; ++j)
        {
            for (int z = 0; z < iz ; ++z) s1a_[iseq][j][z]+=s1_[j][z]; //s1a(:,:,iseq)=s1a(:,:,iseq) + s1
        }
        navg[iseq]+=1; //navg(iseq)=navg(iseq) + 1*/

        navg[iseq]+=1; //navg(iseq)=navg(iseq) + 1
        double ntc=fmin(navg[iseq],4);               //!Averaging time constant in sequences
        double u=1.0/ntc;
        for (int j = 0; j < jz ; ++j) //s1a(:,:,iseq)=u*s1 + (1.0-u)*s1a(:,:,iseq)
        {
            for (int z = 0; z < iz ; ++z) s1a_[iseq][j][z] = u*s1_[j][z] + (1.0-u)*s1a_[iseq][j][z];
        }
        EmitAvgSavesQ65(navg[0],navg[1]);
    }
}
void DecoderQ65::q65_ccf_85(double s1_[800][7000],int iz,int jz,double nfqso,int ia,int ia2,
                            int &ipk,int &jpk,double &f0,double &xdt/*,int &imsg_best*/,double &better/*,double *ccf1*/)
{
    //! Attempt synchronization using all 85 symbols, in advance of an
    //! attempt at q3 decoding.  Return ccf1 for the "red sync curve".
    //double ccf_[300][14000];//ccf(:,:) allocate(ccf(-ia2:ia2,-53:214)) -53:214=267  allocate(ccf1(-ia2:ia2)) 5000Hz= 6333*2
    double (*ccf_)[14000] = new double[310][14000];
    int itone[90];//integer itone(85)
    double best[MAX_NCW+10];//2.59 MAX_NCW 206

    ipk=0;
    jpk=0;
    double ccf_best=0.0;
    int imsg_best=-1;
    for (int imsg = 0; imsg < ncw; ++imsg)
    {//do imsg=1,ncw
        int i=0;
        int k=0;
        for (int j = 0; j < 85; ++j)
        {//do j=1,85
            if (j==isync[i]-1)
            {
                itone[j]=0;
                i++;
            }
            else
            {
                itone[j]=codewords_[imsg][k] + 1;//codewords(k,imsg) + 1
                k++;
            }
        }

        //! Compute 2D ccf using all 85 symbols in the list message
        for (int z = 0; z < lag2+ccf_offsetr; ++z)//299
        {
            for (int j = ccf_offsetc-ia2; j < ia2+ccf_offsetc; ++j) ccf_[z][j]=0.0;//ccf=0.  //13999
        }
        int iia=(int)(200.0/df);
        for (int lag = lag1; lag <= lag2; ++lag)
        {//do lag=lag1,lag2
            for (int x = 0; x < 85; ++x)
            {//do k=1,85
                int j=j0 + NSTEP*x +1 + lag; //j=j0 + NSTEP*(k-1) + 1 + lag
                if (j>0 && j<jz)//if(j.ge.1 .and. j.le.jz) then //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
                {
                    for (int y = -ia2; y < ia2; ++y)
                    {//do i=-ia2,ia2
                        int ii=i0+mode_q65*itone[x]+y;//ii=i0+mode_q65*itone(k)+i
                        //if(ii.ge.iia .and. ii.le.iz) ccf(i,lag)=ccf(i,lag) + s1(ii,j)
                        if (ii>=iia && ii<iz)
                        {
                            ccf_[lag+ccf_offsetr][y+ccf_offsetc]+=s1_[j][ii];
                        }
                    }
                }
            }
        }

        double ccfmax0=0.0;//2.59
        for (int j = 0; j < lag2+ccf_offsetr; ++j)//299
        {
            double ccfmax = pomAll.maxval_da_beg_to_end(ccf_[j],(ccf_offsetc-ia),(ia+ccf_offsetc));//ccfmax=maxval(ccf(-ia:ia,:))
            if (ccfmax>ccf_best)
            {
                ccf_best=ccfmax;
                ipk = pomAll.maxloc_da_beg_to_end(ccf_[j],(ccf_offsetc-ia),(ia+ccf_offsetc))-ccf_offsetc;//(ccf(-ia:ia,:))
                jpk = j-ccf_offsetr;//-15    -53-1;
                f0 = nfqso+ipk*df;
                xdt=jpk*dtstep;
                imsg_best=imsg;
                //for (int z = 0; z < 13999; ++z) ccf1[z]=ccf_[j][z];//no used hv -> ccf1=ccf(:,jpk)
                //qDebug()<<"ccf_best"<<ccf_best<<nfqso + ipk*df;
            }
            if (ccfmax>ccfmax0) ccfmax0=ccfmax;
        }

        /*double ccfmax0=0.0;//2.59
        int sj = 0;
        for (int j = 0; j < lag2+ccf_offsetr; ++j)//299
        {
            double ccfmax = pomAll.maxval_da_beg_to_end(ccf_[j],(ccf_offsetc-ia),(ia+ccf_offsetc));//ccfmax=maxval(ccf(-ia:ia,:))
            if (ccfmax>ccfmax0) 
            {
            	ccfmax0=ccfmax;
            	sj=j;
           	}
        }
        if (ccfmax0>ccf_best)
        {
            ccf_best=ccfmax0;
            ipk = pomAll.maxloc_da_beg_to_end(ccf_[sj],(ccf_offsetc-ia),(ia+ccf_offsetc))-ccf_offsetc;//(ccf(-ia:ia,:))
            jpk = sj-ccf_offsetr; //-15    -53-1;
            f0 = nfqso+ipk*df;
            xdt=jpk*dtstep;
            imsg_best=imsg;
            //qDebug()<<"ccf_best"<<ccf_best<<nfqso + ipk*df;
        }*/

        best[imsg]=ccfmax0;//2.59

        /*ccfmax=maxval(ccf(-ia:ia,:))
        if(ccfmax.gt.ccf_best) then
        ccf_best=ccfmax
        ijpk=maxloc(ccf(-ia:ia,:))
        ipk=ijpk(1)-ia-1
        jpk=ijpk(2)-53-1
        f0=nfqso + ipk*df
        xdt=jpk*dtstep
        imsg_best=imsg
        ccf1=ccf(:,jpk)
        endif*/
    } //! imsg

    better=0.0; //2.59 c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (imsg_best>-1) //if(imsg_best.gt.0) then
    {
        best[imsg_best]=0.0;
        double tbest = pomAll.maxval_da_beg_to_end(best,0,MAX_NCW);
        if (tbest == 0.0) tbest = 0.001;
        better=ccf_best/tbest;//better=ccf_best/maxval(best)
        //qDebug()<<"1="<<ccf_best<<tbest<<better;
    }

    //deallocate(ccf)
    //qDebug()<<"2="<<imsg_best<<ccf_best<<ipk<<jpk<<"f0="<<f0<<"xdt="<<xdt<<better;
    delete [] ccf_;
}
int DecoderQ65::imaxval_da_beg_to_end(int*a,int a_beg,int a_end)
{
    int max = a[a_beg];
    for (int i = a_beg; i < a_end; i++)
    {
        if (a[i]>max)
        {
            max = a[i];
        }
    }
    return max;
}
int DecoderQ65::fmaxloc_da_beg_to_end(float*a,int a_beg,int a_end)
{
    float max = a[a_beg];
    int loc = a_beg;
    for (int i = a_beg; i < a_end; i++)
    {
        if (a[i]>max)
        {
            loc = i;
            max = a[i];
        }
    }
    return loc;
}
void DecoderQ65::q65_bzap(float *s3f,int LL)
{
    const int NBZAP=15;
    int hist[700];
    for (int i = 0; i < 690 ; ++i) hist[i]=0;

    for (int j = 0; j < 63 ; ++j)
    {
        int beg = j*LL;
        int ipk1 = fmaxloc_da_beg_to_end(s3f,beg,beg+LL); //int ipk2 = ipk1;
        ipk1 -= beg;// - 64;// - 65;//-65;//??
        hist[ipk1]++; //qDebug()<<j<<ipk1;		//=hist[ipk1]+1;
    }

    /*bool test=false;
    QString sss;
    if (imaxval_da_beg_to_end(hist,0,LL)>NBZAP) test=true;
    if (test)
    {
        for (int x = 0; x < LL; ++x)
        {
            sss.append(QString("%1").arg((int)hist[x]));
            if (hist[x]>9)sss.append(",");
        }
        qDebug()<<"msg"<<sss;

        int kk = 0;
        for (int j = 0; j < 5; ++j)
        {
            sss.clear();
            for (int i = 0; i < LL; ++i)
            {
            	int ss = (int)s3f[kk]; if (ss>1) ss=1;
                sss.append(QString("%1").arg(ss));
                kk++;
            }
            qDebug()<<"in "<<sss;
        }
    }*/

    if (imaxval_da_beg_to_end(hist,0,LL)>NBZAP)
    {
        for (int i = 0; i < LL; ++i)
        {
            if (hist[i]>NBZAP)
            {
                for (int j = 0; j < 63; ++j)
                {
                    s3f[j*LL+i]=1.0;
                }
            }
        }
    }

    /*if (test)
    {
        int kk = 0;
        for (int j = 0; j < 5; ++j)
        {
            sss.clear();
            for (int i = 0; i < LL; ++i)
            {
            	int ss = (int)s3f[kk]; if (ss>1) ss=1;
                sss.append(QString("%1").arg(ss));
                kk++;
            }  qDebug()<<"out"<<sss;
        }
    }*/
}
void DecoderQ65::q65_s1_to_s3(double s1_[800][7000],int iz,int jz,int ipk,int jpk,int LL,float *s3_1fa)
{
    //! Copy synchronized symbol energies from s1 (or s1a) into s3.
    int i1 = i0+ipk+mode_q65-64;
    int i2 = i1 + LL; //int LL=64*(2+mode_q65);
    int i3 = i2 - i1; //A=192 .... D=640
    if (i1>0 && i2<iz)
    {
        int j=(j0+jpk-8); //j=j0+jpk-7
        int n=0; //int nnn =0;
        for (int k = 0; k < 85 ; ++k)
        {//do k=1,85
            j=j+8;
            if (sync[k]>0.0) continue;
            if (j>0 && j<jz)
            {
                //int nn = 0;
                //QString sss;
                //s3(-64:LL-65,n)=s1(i1:i2,j)
                for (int i = 0; i < i3; ++i)
                {
                    s3_1fa[n]=(float)s1_[j][i+i1];
                    n++;
                    //sss.append(QString("%1").arg((int)s3_1fa[n]));
                    //nn++;
                }
                //nnn++; //qDebug()<<nn<<sss;
            }
        } //qDebug()<<nnn<<n;
    }
    q65_bzap(s3_1fa,LL);                   //!Zap birdies
}
void DecoderQ65::SetArrayBits(int in,int in_bits,bool *ar,int &co)
{
    int izz = in_bits-1;
    for (int i = 0; i < in_bits; ++i)
    {
        ar[co]=(1 & (in >> -(i-izz)));
        co++;
    }
}
void DecoderQ65::q65_dec1(float *s3_1fa,int nsubmode,float b90ts,float &esnodb,int &irc,
                          int *dat4,QString &decoded)
{
    //! Attmpt a full-AP list decode.
    bool c77[100];
    float s3prob[4132] = {0.0};//row= 63 col= 64=4032
    bool unpk77_success = false;

    float plog = PLOG_MIN;
    int nFadingModel=1;
    decoded="";   //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    q65S.q65_intrinsics_ff(s3_1fa,nsubmode,b90ts,nFadingModel,s3prob);
    /*for (int i = 0; i < 63; ++i)
    {
    	 QString sss;
    	 for (int j = 0; j < 64; ++j) sss.append(QString("%1").arg((int)s3prob[i+j]));
    	 qDebug()<<"2="<<sss; 
    }*/
    q65S.q65_dec_fullaplist(s3_1fa,s3prob,codewords_1da,ncw,esnodb,dat4,plog,irc);
    //qDebug()<<"<<"<<s3prob[1000]<<s3prob[1001]<<s3prob[1002]<<"==="<<irc<<plog;

    int sumd4 = 0;
    for (int i = 0; i < 13; ++i) sumd4+=dat4[i];
    if (sumd4<=0) irc=-2;
    if (irc>=0 && plog>PLOG_MIN)//-242
    {
        int co_t = 0;
        for (int i = 0; i < 13; ++i)
        {
            int bits = 6;
            int in = dat4[i];
            if (i==12)
            {
                in/=2;
                bits = 5;
            }
            SetArrayBits(in,bits,c77,co_t);
        }
        decoded = TGenQ65->unpack77(c77,unpk77_success);
        //qDebug()<<"==="<<sumd4<<irc<<plog<<"d="<<decoded<<unpk77_success;
    }
    else irc=-1;
}
void DecoderQ65::q65_dec_q3(double s1_[800][7000],int iz,int jz,float *s3_1fa,int LL,
                            int ipk,int jpk,double &snr2,int *dat4,int &idec,QString &decoded)
{
    //! Copy synchronized symbol energies from s1 into s3, then attempt a q3 decode.
    //double s3_[70][750];
    int irc=-2;

    q65_s1_to_s3(s1_,iz,jz,ipk,jpk,LL,s3_1fa);

    int nsubmode=0;
    if	    (mode_q65==2) nsubmode=1;
    else if (mode_q65==4) nsubmode=2;
    else if (mode_q65==8) nsubmode=3;
    //if(mode_q65.eq.16) nsubmode=4
    //if(mode_q65.eq.32) nsubmode=5
    double baud=12000.0/(double)nsps;
    //qDebug()<<ibwa<<ibwa;
    for (int ibw = ibwa; ibw <= ibwb; ++ibw)// min =ibwb
    {//do ibw=ibwa,ibwb
        double b90=pow(1.72,ibw);//1.72**ibw
        float b90ts=b90/baud;
        irc=-2;
        float esnodb=0.0;
        q65_dec1(s3_1fa,nsubmode,b90ts,esnodb,irc,dat4,decoded); //qDebug()<<"ibw="<<ibw;
        if (irc>=0) //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.  if(irc.ge.0) then
        {
            snr2=esnodb - pomAll.db(2500.0/baud) + 3.0;     //!Empirical adjustment
            idec=3;
            break;//exit
        }
    }//qDebug()<<irc<<snr2<<idec<<decoded;
}
/*void DecoderQ65::q65_ccf_22(double s1_[800][7000],int iz,int jz,double nfqso,int,int &ipk,int &jpk,
                            double &f0,double &xdt) //,double *ccf2
{
    double xdt2[7000];
    double ccf3[7000];//hv correction
    int indx[7000];
    double ccfbest=0.0;
    int ibest=0;
    int lagpk=0;
    int lagbest=0;

    double mdec_df = 25;
    double snfa = nfqso-mdec_df;
    double snfb = nfqso+mdec_df;
    if (f_single_decode)
    {
        snfa = nfa;
        snfb = nfb;
    }
    //int sif1 = snfa/df;//<- in dots
    //int sif2 = snfb/df;//<- in dots
    //qDebug()<<"limit FREQ="<<snfa<<snfb<<df;

    for (int i = 0; i < iz; ++i)
    {//do i=1,iz
        double ccfmax=0.0;
        for (int lag = lag1; lag <= lag2; ++lag)
        {//do lag=lag1,lag2
            double ccft=0.0;
            for (int k = 0; k < 85; ++k)
            {//do k=1,85
                int n=NSTEP*k; //n=NSTEP*(k-1) + 1 //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
                int j=n+lag+j0; //hv+1  if(j>=1 .and. j<=jz) then
                if (j>0 && j<jz) ccft += sync[k]*s1_[j][i]; //0.01 ccft=ccft + sync(k)*s1(i,j)
            }
            if (ccft>ccfmax)  //if(ccft.gt.ccfmax) then
            {
                ccfmax=ccft;
                lagpk=lag; //if (ccfmax>300) qDebug()<<i*df<<"====="<<ccfmax;
            }
        }
        ccf3[i]=ccfmax;
        //ccf2[i]=ccfmax;
        xdt2[i]=(double)lagpk*dtstep; //xdt2(i)=lagpk*dtstep (i*df>=nfa && i*df<=nfb)  fabs(i*df-nfqso)<=ftol_single
        double f=(double)i*df;   //if (f>=snfa && f<=snfb) qDebug()<<f;
        //if (f>=1000 && f<=1600) qDebug()<<"="<<f<<ccfmax<<ccfbest;
        if (ccfmax>ccfbest && (f>=snfa && f<=snfb)) //if(ccfmax.gt.ccfbest .and. abs(i*df-nfqso)<=ftol) then
            //if (ccfmax>ccfbest && (i>=sif1-1 && i<=sif2+1))
        {
            ccfbest=ccfmax;
            ibest=i;
            lagbest=lagpk; //qDebug()<<"-------------------"<<i*df<<ccfbest;
        }
    }

    //! Parameters for the top candidate:
    ipk=ibest-i0; //ipk=ibest - i0
    jpk=lagbest;
    f0=nfqso+(double)ipk*df; //ibest*df;// precision error -> nfqso+ipk*df;
    xdt=(double)jpk*dtstep;
    //qDebug()<<"SINGLE="<<xdt<<f0<<ibest;

    //! Save parameters for best candidates
    int i1=(int)(fmax(nfa,100.0)/df);
    int i2=(int)(fmin(nfb,4900.0)/df);
    int jzz=i2-i1;   //750,15
    if (jzz<25) jzz = 25; // as mimimum

    //int ccf1a = 200.0/df;
    //int ccf1b = 3500.0/df; //2600.0
    //int jzz1 = ccf1b - ccf1a;

    double t_s[7000];
    for (int z = 0; z < jzz; ++z) t_s[z] = ccf3[z+i1];
    double base = pomAll.pctile_shell(t_s,jzz,40);//pctile(ccf2(i1:i2),jzz,40,base)
    //for (int z = 0; z < jzz1; ++z) t_s[z] = ccf2[z+ccf1a];
    //double base1 = pomAll.pctile_shell(t_s,jzz1,40);
    if (base ==0.0) base =0.000001;
    //if (base1==0.0) base1=0.000001;
    for (int z = 0; z < 6800; ++z)
    {
        ccf3[z]/=base;
        //ccf2[z]/=base1;
        //ccf2[z]=ccf3[z];
    }
    double limit = 0.0;
    for (int z = 0; z < jzz; ++z)
    {
        t_s[z] = ccf3[z+i1];
        indx[z] = 0;
        limit += t_s[z];
    }
    limit /= (double)jzz/2.5;// test 3 ok-> / 2.4
    //limit+=0.5;
    //limit protection ->if (jzz>0) pomAll.indexx_msk(t_s,jzz-1,indx);  //call indexx(ccf2(i1:i2),jzz,indx)
    pomAll.indexx_msk(t_s,jzz-1,indx);

    //qDebug()<<"bASE=================="<<base<<"JZZ="<<jzz<<limit;
    ncand=0;
    int maxcand=20;
    for (int j = 0; j < maxcand; ++j)//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {//do j=1,20
        int i=indx[jzz-j-1]+i1; //i=indx(jzz-j+1)+i1-1 //tested -1 ->1.69 n=ia + indx(iz+1-i) - 1
        //qDebug()<<"ccf2="<<i<<(int)(i*df)<<"CCCCFFF="<<ccf3[i]<<xdt2[i];
        if (ccf3[i]<limit) break;//if(ccf2(i).lt.3.3) exit// !Candidate limit
        double f=(double)i*df;  //qDebug()<<"ccf2="<<ccf2[i]<<nfqso<<ftol;
        //double f=(double)nfqso+(i-i0)*df; //nfqso+ipk*df
        //if(f>=(nfqso-ftol) .and. f<=(nfqso+ftol)) cycle

        if (f>=snfa && f<=snfb) continue;//6.666 Hz error
        //if (i>=sif1 && i<=sif2) continue;

        int i3=fmax(0, i-mode_q65);//2.55 i3=max(1, i-mode_q65) old int i3=fmax(0,i-67*mode_q65); //max(1,i-67*mode_q65)
        int i4=fmin(iz,i+mode_q65);//2.55 i4=min(iz,i+mode_q65) old  int i4=fmin(iz,i+3*mode_q65); //min(iz,i+3*mode_q65)
        double biggest=pomAll.maxval_da_beg_to_end(ccf3,i3,i4); //biggest=maxval(ccf2(i3:i4))
        if (ccf3[i]!=biggest) continue; //if(ccf2(i).ne.biggest) cycle
        //candidates_[0][ncand]=ccf3[i];
        //qDebug()<<"candidats="<<f<<ccf3[i];
        candidates_[0][ncand]=xdt2[i];
        candidates_[1][ncand]=f;
        ncand++;
        if (ncand>maxcand-1) break; // no needed
    }
    //for (int i = 0; i < ncand; ++i)
    //{
        //qDebug()<<"candidats="<<candidates_[0][i]<<candidates_[1][i];
    //}
}*/
void DecoderQ65::q65_ccf_22(double s1_[800][7000],int iz,int jz,double nfqso,int iavg,int &ipk,int &jpk,
                            double &f0,double &xdt)//,double *ccf2
{
    double xdt2[7000];
    double ccf3[7000];
    double s1avg[7000];
    int indx[7000];
    //double ddrift[7000];

    double mdec_df = 50;
    double snfa = nfqso-mdec_df;
    double snfb = nfqso+mdec_df;
    if (f_single_decode)
    {
        snfa = nfa;
        snfb = nfb;
    }
    //int sif1 = snfa/df;//<- in dots
    //int sif2 = snfb/df;//<- in dots
    max_drift = 0;
    if (f_max_drift ) max_drift = 100.0/df;
    if (max_drift>60) max_drift = 60;
    //qDebug()<<"Max_Drift="<<max_drift<<"In Hz="<<df*(double)max_drift;

    if (iavg!=0) max_drift = 0; //if(nqd!=1 || iavg!=0) max_drift=0;

    int ia=(int)(fmax(nfa,100.0)/df);//ia=max(nint(100/df),nint((nfqso-ntol)/df))
    int ib=(int)(fmin(nfb,4900.0)/df);//ib=min(nint(4900/df),nint((nfqso+ntol)/df))

    for (int i = ia; i < ib; ++i)
    {
        double sum = 0.0;
        for (int j = 0; j < jz; ++j)
        {
            sum += s1_[j][i];
        }
        s1avg[i]=sum;
    }

    double ccfbest=0.0;
    int ibest=0;
    int lagbest=0;
    int idrift_best=0;
    for (int i = ia; i < ib; ++i)
    {
        double ccfmax_s=0.0;
        double ccfmax_m=0.0;
        int lagpk_s=0;
        int lagpk_m=0;
        int idrift_max_s=0;
        for (int lag = lag1; lag <= lag2; ++lag)
        {
            for (int idrift = -max_drift; idrift <= max_drift; ++idrift)
            {
                double ccft=0.0;
                for (int kk = 0; kk < 22; ++kk)
                {
                    int k=isync[kk]-1; //k=isync(kk)
                    double zz = (idrift*(k-43));
                    int ii = i + (int)(zz/85.0);  //ii=i + nint(idrift*(k-43)/85.0)
                    if (ii<0 || ii>=iz) continue; //if(ii.lt.1 .or. ii.gt.iz) cycle
                    int n=NSTEP*k;  //n=NSTEP*(k-1) + 1
                    int j=n+lag+j0; //j=n+lag+j0
                    if (j>-1 && j<jz) ccft += s1_[j][ii]; //if(j.ge.1 .and. j.le.jz) ccft=ccft + s1(ii,j)
                }
                ccft -= ((22.0/(double)jz)*s1avg[i]);     //ccft=ccft - (22.0/jz)*s1avg(i)
                if (ccft>ccfmax_s)
                {
                    ccfmax_s=ccft;
                    lagpk_s=lag;
                    idrift_max_s=idrift;
                }
                //2.57 hv for multi dec no drift
                if (ccft>ccfmax_m && idrift == 0)
                {
                    ccfmax_m=ccft;
                    lagpk_m=lag;
                }
            }
        }

        ccf3[i]=ccfmax_m; //ccf2[i]=ccfmax;
        xdt2[i]=(double)lagpk_m*dtstep; //xdt2(i)=lagpk*dtstep (i*df>=nfa && i*df<=nfb)  fabs(i*df-nfqso)<=ftol_single
        //ddrift[i]=df*(double)idrift_max_s;

        double f=(double)i*df;   //if (f>=snfa && f<=snfb) qDebug()<<f;
        //if (f>=1200 && f<=1550) qDebug()<<"="<<(int)f<<ccfmax_s<<ccfbest<<idrift_max_s<<lagpk_s;
        if (ccfmax_s>ccfbest && (f>=snfa && f<=snfb)) //if(ccfmax.gt.ccfbest .and. abs(i*df-nfqso)<=ftol) then
        {
            ccfbest=ccfmax_s;
            ibest=i;
            lagbest=lagpk_s;
            idrift_best=idrift_max_s;
        }
    }

    int corrp = pomAll.maxloc_da_beg_to_end(ccf3,snfa/df,snfb/df);
    f0nd = f0=nfqso+(double)(corrp-i0)*df;
    xdtnd = xdt2[corrp];

    //! Parameters for the top candidate:
    ipk=ibest-i0;
    jpk=lagbest;
    f0=nfqso+(double)ipk*df; //ibest*df;// precision error -> nfqso+ipk*df;
    xdt=(double)jpk*dtstep; //qDebug()<<"SINGLE="<<xdt<<f0<<ibest;
    drift=df*(double)idrift_best;
    //qDebug()<<"MaxD="<<(double)max_drift*df<<"MinD="<<1.0*df<<"   f0="<<f0<<"Drift Hz="<<drift;

    for (int i =  0; i < ia; ++i) ccf3[i]=0.0;
    for (int i = ib; i < iz; ++i) ccf3[i]=0.0;

    //! Save parameters for best candidates
    int jzz=ib-ia;   //750,15
    if (jzz<25) jzz = 25; // as mimimum

    //int ccf1a = 200.0/df;
    //int ccf1b = 3500.0/df; //2600.0
    //int jzz1 = ccf1b - ccf1a;

    double t_s[7000];
    for (int z = 0; z < jzz; ++z) t_s[z] = ccf3[z+ia];
    double base = pomAll.pctile_shell(t_s,jzz,40);//pctile(ccf2(i1:i2),jzz,40,base)
    //for (int z = 0; z < jzz1; ++z) t_s[z] = ccf2[z+ccf1a];
    //double base1 = pomAll.pctile_shell(t_s,jzz1,40);
    if (base ==0.0) base =0.000001;
    //if (base1==0.0) base1=0.000001;
    for (int z = 0; z < 6800; ++z)
    {
        ccf3[z]/=base;
        //ccf2[z]/=base1;
        //ccf2[z]=ccf3[z];
    }
    double limit = 0.0;
    for (int z = 0; z < jzz; ++z)
    {
        t_s[z] = ccf3[z+ia];
        indx[z] = 0;
        limit += t_s[z];
    }
    limit /= (double)jzz/2.5;// test 3 ok-> / 2.4
    pomAll.indexx_msk(t_s,jzz-1,indx);

    //qDebug()<<"bASE=================="<<base<<"JZZ="<<jzz<<limit;
    ncand=0;
    int maxcand=20;
    for (int j = 0; j < maxcand; ++j)//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {//do j=1,20
        int k=jzz-j-1; //k=jzz-j+1
        if (k<0 || k>=iz) continue;//if(k.lt.1 .or. k.gt.iz) cycle
        int i=indx[k]+ia; //i=indx(jzz-j+1)+i1-1 //tested -1 ->1.69 n=ia + indx(iz+1-i) - 1
        //qDebug()<<"ccf2="<<i<<(int)(i*df)<<"CCCCFFF="<<ccf3[i]<<xdt2[i];
        if (ccf3[i]<limit) break;//if(ccf2(i).lt.3.3) exit// !Candidate limit
        double f=(double)i*df;  //qDebug()<<"ccf2="<<ccf2[i]<<nfqso<<ftol;
        //double f=(double)nfqso+(i-i0)*df; //nfqso+ipk*df
        //if(f>=(nfqso-ftol) .and. f<=(nfqso+ftol)) cycle

        if (f>=snfa && f<=snfb) continue;//6.666 Hz error stopped in w260rc1

        int i3=fmax(0, i-mode_q65);//2.55 i3=max(1, i-mode_q65) old int i3=fmax(0,i-67*mode_q65); //max(1,i-67*mode_q65)
        int i4=fmin(iz,i+mode_q65);//2.55 i4=min(iz,i+mode_q65) old  int i4=fmin(iz,i+3*mode_q65); //min(iz,i+3*mode_q65)
        double biggest=pomAll.maxval_da_beg_to_end(ccf3,i3,i4); //biggest=maxval(ccf2(i3:i4))
        if (ccf3[i]!=biggest) continue; //if(ccf2(i).ne.biggest) cycle
        //candidates_[0][ncand]=ccf3[i];
        //qDebug()<<"candidats="<<f<<ccf3[i];
        //qDebug()<<"candidats="<<(int)(i*df)<<xdt2[i]<<ddrift[i]<<ccf3[i]<<limit;
        candidates_[0][ncand]=xdt2[i];
        candidates_[1][ncand]=f;
        ncand++;
        if (ncand>maxcand-1) break; // no needed
    }
    /*for (int i = 0; i < ncand; ++i)
    {
    	qDebug()<<"candidats="<<candidates_[0][i]<<candidates_[1][i];
    }*/
}
/*void DecoderQ65::q65_sync_curve(double *ccf1,int ia,int ib,double &rms1)
{
    //! Condition the red or orange sync curve for plotting.
    //real ccf1(ia:ib)
    int ic=(ib-ia)/8;
    int nsum=2*(ic); //2*(ic+1);

    double sum1 = 0.0;
    double sum2 = 0.0;
    for (int i = 0; i < ic; ++i)
    {
        sum1+=ccf1[i+ia];
        sum2+=ccf1[(ib-ic)+i];
    }

    double base1 = (sum1+sum2)/(double)nsum; //base1=(sum(ccf1(ia:ia+ic)) + sum(ccf1(ib-ic:ib)))/nsum
    for (int i = ia; i < ib; ++i) ccf1[i]-=base1;  //ccf1=ccf1-base1

    double dp1= 0.0;  //If the vectors are INTEGER or REAL -> DOT_PRODUCT(VECTOR_A, VECTOR_B) = SUM(VECTOR_A*VECTOR_B)
    double dp2= 0.0;
    for (int i = 0; i < ic; ++i)//sq=dot_product(ccf1(ia:ia+ic),ccf1(ia:ia+ic)) + dot_product(ccf1(ib-ic:ib),ccf1(ib-ic:ib))
    {
        dp1 += ccf1[i+ia] * ccf1[i+ia];
        dp2 += ccf1[(ib-ic)+i] * ccf1[(ib-ic)+i];
    }
    double sq = dp1+dp2; //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    //qDebug()<<"q65_sync_curve"<<sq<<sqrt(sq);
    rms1=0.0;
    if (nsum>0) rms1=sqrt(sq/(double)nsum);
    if (rms1>0.0)
    {
        for (int i = ia; i < ib; ++i) ccf1[i]=ccf1[i]/rms1;  //if(rms1.gt.0.0) ccf1=ccf1/rms1 old ccf1=2.0*ccf1/rms1
    }
    //double smax1 = pomAll.maxval_da_beg_to_end(ccf1,ia,ib);//smax1=maxval(ccf1)
    //if (smax1>10.0)
    //{
        //for (int i = ia; i < ib; ++i) ccf1[i]=10.0*ccf1[i]/smax1;   //ccf1=10.0*ccf1/smax1
    //}
}*/
/*
subroutine q65_write_red(iz,xdt,ccf2_avg,ccf2)

! Write data for the red and orange sync curves to LU 17.

  real ccf2_avg(iz)
  real ccf2(iz)

  call q65_sync_curve(ccf2_avg,1,iz,rms1)
  call q65_sync_curve(ccf2,1,iz,rms2)

  rewind 17
  write(17,1000) xdt,minval(ccf2_avg),maxval(ccf2_avg)
  do i=max(1,nint(nfa/df)),min(iz,int(nfb/df))
     freq=i*df
     y1=ccf2_avg(i)
     if(y1.gt.10.0) y1=10.0 + 2.0*log10(y1/10.0)
     y2=ccf2(i)
     if(y2.gt.10.0) y2=10.0 + 2.0*log10(y2/10.0)
     write(17,1000) freq,y1,y2
1000 format(3f10.3)
  enddo
  flush(17)

  return
end subroutine q65_write_red
*/
#include "ft_all_ap_def.h"
static const int naptypes_q65[6][4]=
    {//gjhghghj
        {1,2,0,0},{2,3,0,0},{2,3,0,0},{3,4,5,6},{3,4,5,6},{3,1,2,0}
    };
void DecoderQ65::q65_ap(int nQSOprogresst,int ipass,int cont_id,int cont_type,bool lapcqonly,
                        int &iaptypet,int *apsym0t,bool *apmaskt,bool *apsymbolst)
{
    /*! nQSOprogress
    !   0  CALLING
    !   1  REPLYING
    !   2  REPORT
    !   3  ROGER_REPORT
    !   4  ROGERS
    !   5  SIGNOFF*/
    if (cont_id!=ncontest0)
    {
        /*! iaptype
        !------------------------
        !   1        CQ     ???    ???           (29+4=33 ap bits)
        !   2        MyCall ???    ???           (29+4=33 ap bits)
        !   3        MyCall DxCall ???           (58+4=62 ap bits)
        !   4        MyCall DxCall RRR           (78 ap bits)
        !   5        MyCall DxCall 73            (78 ap bits)
        !   6        MyCall DxCall RR73          (78 ap bits)*/
        for (int i = 0; i < 29; ++i)
        {
            /*if 		(cont_id== 0) mcq_q65[i]=mcq_ft[i];
            else if (cont_id== 2) mcq_q65[i]=mcqtest_ft[i];
            else if (cont_id== 3) mcq_q65[i]=mcqtest_ft[i];
            else if (cont_id== 4) mcq_q65[i]=mcqfd_ft[i];
            else if (cont_id== 5) mcq_q65[i]=mcqtest_ft[i];//mcq_q65[i]=mcqru_ft[i];
            else if (cont_id== 6) mcq_q65[i]=mcqww_ft[i];
            else if (cont_id== 7) mcq_q65[i]=mcqww_ft[i];  //mcq_q65[i]=mcqru_ft[i];
            else if (cont_id== 8) mcq_q65[i]=mcqww_ft[i];  //mcq_q65[i]=mcqru_ft[i];
            else if (cont_id== 9) mcq_q65[i]=mcqru_ft[i];
            else if (cont_id==10) mcq_q65[i]=mcqbu_ft[i];
            else if (cont_id==11) mcq_q65[i]=mcqft_ft[i];
            else if (cont_id==12) mcq_q65[i]=mcqpdc_ft[i];
            else if (cont_id==13) mcq_q65[i]=mcqtest_ft[i];*/
            mcq_q65[i]=mcq_ft[cont_id][i];
        }
        ncontest0=cont_id;
    }

    for (int i = 0; i < 78; ++i) apsymbolst[i]=0;
    iaptypet=naptypes_q65[nQSOprogresst][ipass-1]; //hv-1 iaptype=naptypes(nQSOProgress,ipass)
    //qDebug()<<"iaptypet="<<iaptypet;
    if (lapcqonly) iaptypet=1;
    /*! ncontest=0 : NONE
    !          1 : NA_VHF
    !          2 : EU_VHF
    !          3 : FIELD DAY
    !          4 : RTTY
    !          5 : WW_DIGI
    !          6 : FOX
    !          7 : HOUND*/

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

    /*! Conditions that cause us to bail out of AP decoding
    !  if(ncontest.le.5 .and. iaptype.ge.3 .and. (abs(f1-nfqso).gt.napwid .and. abs(f1-nftx).gt.napwid) ) goto 900
    !  if(ncontest.eq.6) goto 900                      !No AP for Foxes
    !  if(ncontest.eq.7.and.f1.gt.950.0) goto 900      !Hounds use AP only below 950 Hz*/
    //if(ncontest.ge.6) goto 900
    if (iaptypet>=2 && apsym0t[0]>1) return;//if(iaptype.ge.2 .and. apsym0(1).gt.1) goto 900  !No, or nonstandard, mycall
    //if(ncontest.eq.7 .and. iaptype.ge.2 .and. aph10(1).gt.1) goto 900
    if (iaptypet>=3 && apsym0t[29]>1) return;//if(iaptype.ge.3 .and. apsym0(30).gt.1) goto 900 !No, or nonstandard, dxcall

    if (iaptypet==1) //! CQ or CQ RU or CQ TEST or CQ FD
    {
        for (int z = 0; z < 78; ++z)
        {
            apmaskt[z]=0;
            if (z<29)
            {
                apmaskt[z]=1;
                apsymbolst[z]=mcq_q65[z];
            }
        }
        apmaskt[74]=1;
        apmaskt[75]=1;
        apmaskt[76]=1;
        apmaskt[77]=1;
        apsymbolst[74]=0;
        apsymbolst[75]=0;
        apsymbolst[76]=1;
        apsymbolst[77]=0;
    }
    if (iaptypet==2) //then //! MyCall,???,???
    {
        for (int z = 0; z < 78; ++z)
            apmaskt[z]=0;
        if (cont_type==0 || cont_type==1)//hv new || cont_type==5
        {
            for (int z = 0; z < 29; ++z)
            {
                apmaskt[z]=1;
                apsymbolst[z]=apsym0t[z];
            }
            apmaskt[74]=1;
            apmaskt[75]=1;
            apmaskt[76]=1;
            apmaskt[77]=1;
            apsymbolst[74]=0;
            apsymbolst[75]=0;
            apsymbolst[76]=1;
            apsymbolst[77]=0;
        }
        else if (cont_type==2) //then
        {
            for (int z = 0; z < 28; ++z)
            {
                apmaskt[z]=1;
                apsymbolst[z]=apsym0t[z];
            }
            apmaskt[71]=1;
            apmaskt[72]=1;
            apmaskt[73]=1;
            apsymbolst[71]=0;
            apsymbolst[72]=1;//+1=??
            apsymbolst[73]=0;
            apmaskt[74]=1;
            apmaskt[75]=1;
            apmaskt[76]=1;
            apmaskt[77]=1;
            apsymbolst[74]=0;
            apsymbolst[75]=0;
            apsymbolst[76]=0;
            apsymbolst[77]=0;
        }
        else if (cont_type==3)
        {
            for (int z = 0; z < 28; ++z)
            {
                apmaskt[z]=1;
                apsymbolst[z]=apsym0t[z];
            }
            apmaskt[74]=1;
            apmaskt[75]=1;
            apmaskt[76]=1;
            apmaskt[77]=1;
            apsymbolst[74]=0;
            apsymbolst[75]=0;
            apsymbolst[76]=0;
            apsymbolst[77]=0;
        }
        else if (cont_type==4)// || ncontest==6  RTTY RU  HV new
        {
            for (int z = 1; z < 29; ++z)
            {
                apmaskt[z]=1;
                apsymbolst[z]=apsym0t[z];
            }
            apmaskt[74]=1;
            apmaskt[75]=1;
            apmaskt[76]=1;
            apmaskt[77]=1;
            apsymbolst[74]=0;
            apsymbolst[75]=0;
            apsymbolst[76]=1;
            apsymbolst[77]=0;
        }
        /*else if(ncontest.eq.7) then ! ??? RR73; MyCall <Fox Call hash10> ???
           apmask(29:56)=1  
           apsymbols(29:56)=apsym0(1:28)
           apmask(57:66)=1
           apsymbols(57:66)=aph10(1:10)
           apmask(72:78)=1
           apsymbols(72:74)=(/0,0,1/)
           apsymbols(75:78)=0
        endif*/
    }
    if (iaptypet==3) // ! MyCall,DxCall,???
    {
        for (int z = 0; z < 78; ++z)
            apmaskt[z]=0;//apmask=0
        //if(ncontest.eq.0.or.ncontest.eq.1.or.ncontest.eq.2.or.ncontest.eq.5.or.ncontest.eq.7) then
        if (cont_type==0 || cont_type==1 || cont_type==2)//||ncontest==7   || ncontest==5
        {
            for (int z = 0; z < 58; ++z)
            {
                apmaskt[z]=1;
                apsymbolst[z]=apsym0t[z];
            }
            apmaskt[74]=1;
            apmaskt[75]=1;
            apmaskt[76]=1;
            apmaskt[77]=1;
            apsymbolst[74]=0;
            apsymbolst[75]=0;
            apsymbolst[76]=1;
            apsymbolst[77]=0;
        }
        else if (cont_type==3) //then ! Field Day
        {
            for (int z = 0; z < 57; ++z)
            {
                if (z<56)
                    apmaskt[z]=1;
                if (z<28)
                    apsymbolst[z]=apsym0t[z];
                if (z>28)
                    apsymbolst[z-1]=apsym0t[z];
            }
            apmaskt[71]=1;
            apmaskt[72]=1;
            apmaskt[73]=1;
            apmaskt[74]=1;
            apmaskt[75]=1;
            apmaskt[76]=1;
            apmaskt[77]=1;
            apsymbolst[74]=0;
            apsymbolst[75]=0;
            apsymbolst[76]=0;
            apsymbolst[77]=0;
        }
        else if (cont_type==4)//RTTY RU  HV new   || ncontest==6
        {
            for (int z = 0; z < 57; ++z)
            {
                if (z>0)
                    apmaskt[z]=1;
                if (z<28)
                    apsymbolst[z+1]=apsym0t[z];
                if (z>28)
                    apsymbolst[z]=apsym0t[z];
            }
            apmaskt[74]=1;
            apmaskt[75]=1;
            apmaskt[76]=1;
            apmaskt[77]=1;
            apsymbolst[74]=0;
            apsymbolst[75]=0;
            apsymbolst[76]=1;
            apsymbolst[77]=0;
        }
    }
    //if(iaptype==5 && ncontest==7) continue;//cycle !Hound
    if (iaptypet==4 || iaptypet==5 || iaptypet==6)
    {
        for (int z = 0; z < 78; ++z)
            apmaskt[z]=0;//apmask=0
        //if(ncontest.le.5 || (ncontest.eq.7.and.iaptype.eq.6)) then
        if (cont_type<=4)//HV new=4
        {
            for (int z = 0; z < 78; ++z)
            {
                apmaskt[z]=1;//apmask(1:77)=1   //! mycall, hiscall, RRR|73|RR73
                if (z<58)
                    apsymbolst[z]=apsym0t[z];
            }
            //apmask(72:74)=0
            apmaskt[71]=0;
            apmaskt[72]=0;
            apmaskt[73]=0;
            for (int z = 0; z < 19; ++z)
            {
                if (iaptypet==4)
                    apsymbolst[z+58]=mrrr_ft[z];//apsymbols(59:77)=mrrr
                if (iaptypet==5)
                    apsymbolst[z+58]=m73_ft[z];//apsymbols(59:77)=m73
                if (iaptypet==6)
                    apsymbolst[z+58]=mrr73_ft[z];//apsymbols(59:77)=mrr73
            }
        }
        //HOUND = MSHV no
        /*else if(ncontest.eq.7.and.iaptype.eq.4) then ! Hound listens for MyCall RR73;...
        apmask(1:28)=1
        apsymbols(1:28)=apsym0(1:28)
        apmask(57:66)=1
        apsymbols(57:66)=aph10(1:10)
        apmask(72:78)=1
        apsymbols(72:78)=(/0,0,1,0,0,0,0/)
        endif*/
    }
}
int DecoderQ65::BinToInt32(bool*a,int b_a,int bits_sz)
{
    int k = 0;
    for (int i = b_a; i < bits_sz; ++i)
    {
        k <<= 1;
        k |= a[i];
    }
    return k;
}
void DecoderQ65::q65_dec2(float *s3_1fa,int nsubmode,float b90ts,float &esnodb,int &irc,int *dat4)
{
    //! Attempt a q0, q1, or q2 decode using spcified AP information.
    bool c77[100];
    float s3prob[4132] = {0.0};//row= 63 col= 64=4032
    bool unpk77_success = false;

    int nFadingModel=1;
    decoded="";
    q65S.q65_intrinsics_ff(s3_1fa,nsubmode,b90ts,nFadingModel,s3prob);
    q65S.q65_dec(s3_1fa,s3prob,apmask,apsymbols,s_maxiters,esnodb,dat4,irc);

    int sumd4 = 0;
    for (int i = 0; i < 13; ++i) sumd4+=dat4[i]; //if (sumd4!=0) qDebug()<<sumd4;//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (sumd4<=0) irc=-2;
    if (irc>=0)
    {
        int co_t = 0;
        for (int i = 0; i < 13; ++i)
        {
            int bits = 6;
            int in = dat4[i];
            if (i==12)
            {
                in/=2;
                bits = 5;
            }
            SetArrayBits(in,bits,c77,co_t);
        }
        decoded = TGenQ65->unpack77(c77,unpk77_success);
        //qDebug()<<"==="<<sumd4<<irc<<plog<<"d="<<decoded<<unpk77_success;
    }
}
void DecoderQ65::q65_dec_q012(float *s3_1fa,double &snr2,int *dat4,int &idec, int nQSOprogress,
                              int cont_id,int cont_type)
{
    //! Do separate passes attempting q0, q1, q2 decodes.
    int irc=-2;
    int nsubmode=0;
    if	    (mode_q65==2) nsubmode=1;
    else if (mode_q65==4) nsubmode=2;
    else if (mode_q65==8) nsubmode=3;
    //if(mode_q65.eq.16) nsubmode=4
    //if(mode_q65.eq.32) nsubmode=5

    double baud=12000.0/(double)nsps;

    bool lapcqonly=false;
    int iaptype = 0;
    bool exitt = false;
    for (int ipass = 0; ipass <= npasses ; ++ipass)//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {//do ipass=0,npasses                  //!Loop over AP passes
        for (int i = 0; i < 13 ; ++i)
        {
            apmask[i]=0;  //13                       //!Try first with no AP information
            apsymbols[i]=0; //13
        }
        if (ipass>=1)//if(ipass.ge.1) then
        {
            //! Subsequent passes use AP information appropiate for nQSOprogress
            q65_ap(nQSOprogress,ipass,cont_id,cont_type,lapcqonly,iaptype,apsym0,apmask1,apsymbols1);
            /*write(c78,1050) apmask1
            1050    format(78i1)
            read(c78,1060) apmask
            1060    format(13b6.6)
            write(c78,1050) apsymbols1
            read(c78,1060) apsymbols  */
            int z = 0;
            for (int i = 0; i < 13; ++i)
            {
                apmask[i]    = BinToInt32(apmask1,z,z+6);
                apsymbols[i] = BinToInt32(apsymbols1,z,z+6);
                z += 6;
            }
        }
        for (int ibw = ibwa; ibw <= ibwb; ++ibw)
        {//do ibw=ibwa,ibwb
            double b90=pow(1.72,ibw);//b90=1.72**ibw
            float b90ts=b90/baud;//b90ts=b90/baud
            irc=-2;
            float esnodb=0.0;
            q65_dec2(s3_1fa,nsubmode,b90ts,esnodb,irc,dat4);
            if (irc>=0)//if(irc.ge.0) then  //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
            {
                snr2=esnodb - pomAll.db(2500.0/baud) + 3.0;//snr2=esnodb - db(2500.0/baud) + 3.0     !Empirical adjustment
                idec=iaptype;
                exitt = true;
                break;
            }
        }
        if (exitt) break;
    }
}
void DecoderQ65::q65_dec0(int iavg,double *iwave,double nfqso,
                          bool &lclearave,bool emedelay,double &xdt,double &f0,double &snr1,
                          int *dat4,double &snr2,int &idec,int nQSOp,int cont_id,int cont_type,int stageno)
{
    /* Top-level routine in q65 module
    !   - Compute symbol spectra
    !   - Attempt sync and q3 decode using all 85 symbols
    !   - If that fails, try sync with 22 symbols and standard q[0124] decode

    ! Input:  iavg                   0 for single-period decode, 1 for average
    !         iwave(0:nmax-1)        Raw data
    !         ntrperiod              T/R sequence length (s)
    !         nfqso                  Target frequency (Hz)
    !         ntol                   Search range around nfqso (Hz)
    !         ndepth                 Requested decoding depth
    !         lclearave              Flag to clear the accumulating array
    !         emedelay               Extra delay for EME signals
    ! Output: xdt                    Time offset from nominal (s)
    !         f0                     Frequency of sync tone
    !         snr1                   Relative SNR of sync signal
    !         width                  Estimated Doppler spread
    !         dat4(13)               Decoded message as 13 six-bit integers
    !         snr2                   Estimated SNR of decoded signal
    !         idec                   Flag for decing results
    !            -1  No decode
    !             0  No AP
    !             1  "CQ        ?    ?"
    !             2  "Mycall    ?    ?"
    !             3  "MyCall HisCall ?"*/

    idec = -1;//2.57 inportent
    int LL=64*(2+mode_q65);  //LL=640 mode for q65D
    int nfft=nsps;
    df=12000.0/(double)nfft;    //!Freq resolution = baud
    istep=nsps/NSTEP;
    int iz=(int)(5000.0/df);                      //!Uppermost frequency bin, at 5000 Hz
    double txt=85.0*(double)nsps/12000.0;
    int jz=(int)((txt+1.0)*12000.0/(double)istep);       //!Number of symbol/NSTEP bins
    if (nsps>=6912) jz=(int)((txt+2.0)*12000.0/(double)istep);  //!For TR 60 s and higher

    int ia=(int)(nfa/df);//ORG->int ia=(int)(ftol/df); ia=ntol/df
    int ia2=(int)(nfb/df);
    double xxmax = fmax(10*mode_q65,(int)(100.0/df)); //ORG->ia2=max(ia,10*mode_q65,nint(100.0/df))
    ia2=fmax(ia,xxmax); //ORG->int ia2=fmax(ia,xxmax);

    nsmo=int(0.7*(double)(mode_q65*mode_q65));
    if (nsmo<1) nsmo=1;
    //double s1_[800][7000];//max iz=120s=6666 jz=733	300s=17280 allocate(s1(iz,jz))
    double (*s1_)[7000] = new double[800][7000];
    //double s3_[70][750];//s3(-64:LL-65,63))  LL=640 mode for q65D
    float s3_1fa[41320];//63*640=40320
    //float *s3_1fa = new float[41320];//attention = 63*640=40320 q65d from q65_subs
    //qDebug()<<ia2<<ia2*2<<jz;

    /*int w3t;
    int w3f;
    int mm;*/
    double (*s1w_)[7000] = new double[800][7000];
    //integer stageno

    //int s3_offset = 84;// = 63+20=83 all 83+LL=83+640=723
    //double ccf1[14000]; //no used hv    allocate(ccf1(-ia2:ia2)) 5000Hz= 6333*2
    //int ccf1_offset = 7000;
    //double ccf2[7000];//allocate(ccf2(iz)) iz = 6666
    double base;
    double t_s[700];//?? max=640  s1_[][7000] last column
    double s1max = 0.0;
    int ipk = 0;
    int jpk = 0;
    //int imsg_best = 0;// not used
    double f0a = 0.0;
    double xdta = 0.0;
    //double rms2 = 0.0;
    double smax = 0.0;
    //int ibeg = nfa/df;
    //int iend = 3000.0/df;
    snr1=0.0;

    //s1a(iz,jz,0:1) = iz =6666 jz=733
    //qDebug()<<ia<<ia2<<iz<<jz;
    static bool first = true;
    if (first) //!Generate the sync vector
    {
        for (int i = 0; i<85; ++i) sync[i]=-22.0/63.0;//!Sync tone OFF   //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        for (int k = 0; k<22; ++k) sync[isync[k]-1]=1.0;//do k=1,22 !Sync tone ON
        first = false;
    }

    //if(LL.ne.LL0 .or. iz.ne.iz0 .or. jz.ne.jz0 .or. lclearave) then
    if (LL!=LL0 || iz!=iz0 || jz!=jz0 || lclearave)
    {
        for (int i = 0; i < 2 ; ++i)//if(allocated(s1a)) deallocate(s1a) //allocate(s1a(iz,jz,0:1))
        {
            for (int j = 0; j < jz ; ++j)
            {
                for (int z = 0; z < iz ; ++z) s1a_[i][j][z]=0.0;//s1a=0.
            }
        }
        navg[0]=0;
        navg[1]=0;
        LL0=LL;
        iz0=iz;
        jz0=jz;
        lclearave=false;
    }

    dtstep = (double)nsps/((double)NSTEP*12000.0);  //!Step size in seconds //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    lag1 = (int)(-1.0/dtstep);
    lag2 = (int)(1.0/dtstep + 0.9999);
    if (nsps>=3600 && emedelay) lag2 = (int)(5.5/dtstep + 0.9999);//50rc2  !Include EME
    j0=(int)(0.5/dtstep);
    if (nsps>=7200) j0=(int)(1.0/dtstep); //!Nominal start-signal index if(nsps.ge.7200) j0=1.0/dtstep


    for (int i = 0; i < 40350 ; ++i) s3_1fa[i]=0.0;//attention = 63*640=40320 q65d from q65_subs
    if (iavg==0) q65_symspec(iwave,iz,jz,s1_);//q65_symspec(iwave,ntrperiod*12000,iz,jz,s1) //! Compute symbol spectra with NSTEP time bins per symbol
    else
    {
        for (int j = 0; j < jz ; ++j)
        {
            for (int z = 0; z < iz ; ++z) s1_[j][z]=s1a_[iseq][j][z];//s1=s1a(:,:,iseq)
        }
    }

    //qDebug()<<"Bounds Restrict="<<(64*df)<<(5000 - (LL-64)*df);
    //int i01 = 100; qDebug()<<i01-64<<i01-65+LL<<(i01-64+LL)-(i01-64);
    //int iall = (i0-64+LL); //qDebug()<<iall-(i0-64);
    i0=(int)(nfqso/df);   //!Target QSO frequency
    /*ii1=fmax(0,i0-64);//ii1=max(1,i0-64)
    ii2=i0-64+LL;       //ii2=i0-65+LL   
    if (i0-64<0 || i0-64+LL>iz-1)  //if(i0-64.lt.1 .or. i0-65+LL.gt.iz) !Frequency out of range
    {
        //qDebug()<<"RESTRICT";
        goto c900;
    }*/
    if (i0-64<0) i0=64;
    if (i0-64+LL>iz-1) i0=iz+64-LL;
    //qDebug()<<i0<<iz<<iz+64-LL<<(LL-1)+(iz+64-LL)-64;

    for (int j = 0; j < jz ; ++j) //pctile(s1(i0-64:i0-65+LL,1:jz),LL*jz,40,base)
    {
        for (int z = 0; z < LL; ++z)
        {
            t_s[z] = s1_[j][z+i0-64];
            //if (j==0) qDebug()<<LL<<z;
        }
        //call pctile(s1(i0-64:i0-65+LL,1:jz),LL*jz,45,base)
        //call pctile(s1(ii1:ii2,1:jz),ii2-ii1+1*jz,45,base)
        base = pomAll.pctile_shell(t_s,LL,45);
        if (base==0.0) base=0.000001;
        for (int z = 0; z < iz ; ++z)
        {
            s1_[j][z]/=base;//s1=s1/base
            s1raw_[j][z]=s1_[j][z];
        }
    } //qDebug()<<"LL="<<LL;

    for (int j = 0; j < jz ; ++j)
    {
        //! Apply fast AGC to the symbol spectra
        s1max=20.0;//20.0    !Empirical choice
        //smax = pomAll.maxval_da_beg_to_end(s1_[j],i0-64,iall);//smax=maxval(s1(i0-64:i0-65+LL,j))
        smax = pomAll.maxval_da_beg_to_end(s1_[j],0,iz); //smax=maxval(s1(ii1:ii2,j))
        //qDebug()<<j<<smax;
        //if(smax>s1max) s1(i0-64:i0-65+LL,j)=s1(i0-64:i0-65+LL,j)*s1max/smax
        if (smax>s1max)
        {
            //for (int z = i0-64; z < iall; ++z) s1_[j][z]=s1_[j][z]*s1max/smax;
            //s1(ii1:ii2,j)=s1(ii1:ii2,j)*s1max/smax
            for (int z = 0; z < iz; ++z) s1_[j][z]*=(s1max/smax);
        }
    }

    for (int i = 0; i < 14 ; ++i) dat4[i]=0;
    //old 	    if(ncw.gt.0 .and. iavg.lt.2) then
    //i'ts same if(ncw.gt.0 .and. iavg.le.1) then
    //qDebug()<<ncw<<iavg;
    if (ncw>0 && iavg<=1) //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {
        //! Try list decoding via "Deep Likelihood".
        //! Try to synchronize using all 85 symbols
        double better = 0.0;
        q65_ccf_85(s1_,iz,jz,nfqso,ia,ia2,ipk,jpk,f0,xdt/*,imsg_best*/,better/*,ccf1*/);//q65_ccf_85(s1,iz,jz,nfqso,ia,ia2,ipk,jpk,f0,xdt,imsg_best,ccf1)
        if (better>=1.10 || mode_q65>=8) q65_dec_q3(s1_,iz,jz,s3_1fa,LL,ipk,jpk,snr2,dat4,idec,decoded);//2.59
    }

    /*if(iavg==0) then
     call q65_ccf_22(s1,iz,jz,nfqso,ipk,jpk,f0a,xdta,ccf2)
    endif*/

    //! Get 2d CCF and ccf2 using sync symbols only
    /*if(iavg>=1) then
       call q65_ccf_22(s1,iz,jz,nfqso,ipk,jpk,f0a,xdta,ccf2_avg)
    endif*/

    //! Get 2d CCF and ccf2 using sync symbols only
    q65_ccf_22(s1_,iz,jz,nfqso,iavg,ipk,jpk,f0a,xdta/*,ccf2*/);// maybe out of bandwidth df
    if (idec<0) //if(idec.lt.0) then
    {
        f0=f0a;
        xdt=xdta;
    }

    //hv not used for the moment
    //int ccf1a = 200.0/df;
    //int ccf1b = 2600.0/df;
    //! Estimate rms on ccf2 baseline
    /*rms2 = 0.0;  //qDebug()<<200.0/df<<4500.0/df<<20<<iz-20;
    q65_sync_curve(ccf2,200.0/df,4800.0/df,rms2); //call q65_sync_curve(ccf2,1,iz,rms2)
    smax = pomAll.maxval_da_beg_to_end(ccf2,0,iz);//maxval(ccf2);
    snr1=0.0;  //qDebug()<<smax<<rms2;
    if (rms2>0.0) snr1=smax/rms2;*/

    if (idec<=0)//q65_s1_to_s3(s1_,iz,jz,ipk,jpk,LL,s3_1fa);
    {
        //! The q3 decode attempt failed. Copy synchronized symbol energies from s1
        //! into s3 and prepare to try a more general decode.
        q65_s1_to_s3(s1_,iz,jz,ipk,jpk,LL,s3_1fa);
    }

    /*smax = pomAll.maxval_da_beg_to_end(ccf1,0,13999);//maxval(ccf1)
    //! Estimate frequenct spread
    i1=-9999
    i2=-9999
    do i=-ia,ia
       if(i1.eq.-9999 .and. ccf1(i).ge.0.5*smax) i1=i
       if(i2.eq.-9999 .and. ccf1(-i).ge.0.5*smax) i2=-i
    enddo
    width=df*(i2-i1)

    if(ncw.eq.0) ccf1=0.
    call q65_write_red(iz,ia2,xdt,ccf1,ccf2)*/

    //if (iavg==2) q65_dec_q012(s3_1fa,snr2,dat4,idec,nQSOp,cont_id,cont_type);//old 2.56
    //if(iavg.eq.0 .or. iavg.eq.2) then //2.57 start ApS in first start ???
    //if(idec.lt.0 .and. (iavg.eq.0 .or. iavg.eq.2)) 50rc2 //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (idec<0 && (iavg==0 || iavg==2)) q65_dec_q012(s3_1fa,snr2,dat4,idec,nQSOp,cont_id,cont_type);

    //int max_drift=60;
    if (idec<0 && max_drift!=0 && stageno==5) //2.61 if(idec.lt.0 .and. max_drift.eq.50 .and. stageno.eq.5)
    {
        for (int j = 0; j < jz ; ++j)//s1w=s1
        {
            for (int z = 0; z < iz ; ++z) s1w_[j][z] = s1_[j][z];
        }
        for (int w3t = 0; w3t < jz ; ++w3t)
        {//do w3t=1,jz
            for (int w3f = 0; w3f < iz ; ++w3f)
            {//do w3f=1,iz
                int mm = w3f + int((drift*(double)w3t)/((double)jz*df));//mm=w3f + nint(drift*w3t/(jz*df))
                if (mm>=0 && mm<iz) s1w_[w3t][w3f] = s1_[w3t][mm]; //s1w(w3f,w3t)=s1(mm,w3t)
            }
        }
        if (ncw>0 && iavg<=1) //! Try list decoding via "Deep Likelihood".
        {
            double better = 0.0; //! Try to synchronize using all 85 symbols
            q65_ccf_85(s1w_,iz,jz,nfqso,ia,ia2,ipk,jpk,f0,xdt/*,imsg_best*/,better/*,ccf1*/);
            //!         nsubmode  is Tone-spacing indicator, 0-4 for A-E: a 0; b 1; c 2; d 3; e 4.
            //!         and mode_q65=2**nsubmode
            if (better>=1.10)//  if(better.ge.1.10)
            {
                //!     if(better.ge.1.04 .or. mode_q65.ge.8) then
                //!     if(better.ge.1.10 .or. mode_q65.ge.8) then  ORIGINAL
                q65_dec_q3(s1w_,iz,jz,s3_1fa,LL,ipk,jpk,snr2,dat4,idec,decoded);
            }
        }
        if (idec==3) idec = 5;
    }

    delete [] s1w_;
    delete [] s1_;
    //delete [] s3_1fa;
}
void DecoderQ65::q65apset(QString mycall12,QString hiscall12,int *apsym2)
{
    int i3=0;
    int n3=0;
    bool c77[77+5];
    for (int i = 0; i < 77; ++i)
        c77[i]=0;

    if (mycall12.isEmpty())
    {
        for (int i = 0; i < 58; ++i) //	apsym2[58+5]
            apsym2[i]=0;
        apsym2[0]=99;
        apsym2[29]=99;
        return;
    }
    bool nohiscall=false;
    //hiscall=hiscall12;
    if (hiscall12.isEmpty())//if(len(trim(hiscall)).eq.0) then
    {
        hiscall12="LZ2ABC";//"K9ABC";
        nohiscall=true;
    }
    /* if needed for HOUND   MSHV no HOUND Activity
    call save_hash_call(hc13,n10,n12,n22)
    write(c10,'(b10.10)') iand(n10,Z'3FF') 
    read(c10,'(10i1.1)',err=1) aph10   <-- int n10 = BinToInt32(aph10,0,10);
    aph10=2*aph10-1*/

    //! Encode a dummy standard message: i3=1, 28 1 28 1 1 15
    //!
    QString msgs2;
    msgs2.append(mycall12.trimmed());
    msgs2.append(" ");
    msgs2.append(hiscall12.trimmed());
    msgs2.append(" ");
    msgs2.append("RRR");
    TGenQ65->pack77(msgs2,i3,n3,c77);  //call pack77(msg,i3,n3,c77)

    // ??? no need this
    /*bool unpk77_success;
    QString msgchk = TGenFt8->unpack77(c77,unpk77_success); //call unpack77(c77,1,msgchk,unpk77_success)
    if(i3!=1 || (msgs2!=msgchk) || !unpk77_success) return;*/

    if (i3!=1)
    {
        for (int i = 0; i < 58; ++i) //	apsym2[58+5]
            apsym2[i]=0;
        apsym2[0]=99;
        apsym2[29]=99;
        return;
    }

    //read(c77,'(58i1)',err=1) apsym(1:58)
    for (int i = 0; i < 58; ++i)
        apsym2[i] = c77[i];
    if (nohiscall) apsym2[29]=99;
    return;
    //c1:
    //??????????????????????
    for (int i = 0; i < 58; ++i) //	apsym2[58+5]
        apsym2[i]=0;
    apsym2[0]=99;
    apsym2[29]=99;
    return;
}
void DecoderQ65::ana64(double *iwave,int npts,std::complex<double> *c0)
{
    /*integer*2 iwave(npts)                      !Raw data at 12000 Hz
    complex c0(0:npts-1)                       !Complex data at 6000 Hz*/
    int nfft1=npts;// 1440000
    int nfft2=nfft1/2;//=720000   16000+8000=24000
    //double df1=12000.0/nfft1;
    double fac=(2.0/(32767.0*(double)nfft1))*0.01;// hv correction duble
    for (int i=0; i<npts ; ++i) c0[i]=fac*iwave[i];//c0(0:npts-1)=fac*iwave(1:npts)
    f2a->four2a_c2c(c0,nfft1,-1,1);//four2a(c0,nfft1,1,-1,1) //!Forward c2c FFT
    for (int i=nfft2/2; i<nfft2; ++i) c0[i]=0.0+0.0*complex_i;//c0(nfft2/2+1:nfft2-1)=0.
    c0[0]=0.5*c0[0];// //c0(0)=0.5*c0(0)
    f2a->four2a_c2c(c0,nfft2,1,1); //call four2a(c0,nfft2,1,1,1)              //!Inverse c2c FFT; c0 is analytic sig
}
void DecoderQ65::twkfreq(std::complex<double> *c3,std::complex<double> *c4,int npts,double fsample,double *a)
{
    double twopi=6.283185307;
    //! Mix the complex signal
    std::complex<double> w=1.0+1.0*complex_i;
    //std::complex<double> wstep=1.0+1.0*I;
    int x0=0.5*(npts);//x0=0.5*(npts+1)
    double s=2.0/(double)npts;//s=2.0/npts
    for (int i =0; i<npts; ++i)
    {//do i=1,npts
        double x=s*(i-x0);//x=s*(i-x0)
        double p2=1.5*x*x - 0.5;                            //p2=1.5*x*x - 0.5
        //double p3=2.5*pow(x,3.0) - 1.5*x;                     //p3=2.5*(x**3) - 1.5*x
        //double p4=4.375*pow(x,4.0) - 3.75*pow(x,2.0) + 0.375;    //p4=4.375*(x**4) - 3.75*(x**2) + 0.375
        double dphi=(a[0] + x*a[1] + p2*a[2]) * (twopi/fsample);
        std::complex<double> wstep=cos(dphi)+sin(dphi)*complex_i;//wstep=cmplx(cos(dphi),sin(dphi))
        w=w*wstep;
        c4[i]=w*c3[i];
    }
}
void DecoderQ65::spec64(std::complex<double> *c0,int nsps,int jpk,float *s3f,int LL,int NN)//int npts,
{
    //const int MAXFFT = 20736;    //8000=120s
    std::complex<double> cs[21736];//(0:MAXFFT-1)=20736
    //std::complex<double> *cs = new std::complex<double>[21736];
    double pom1[70][700];//63/640
    //double (*pom1)[700] = new double[70][700];//63/640    //double (*ccf_)[14000] = new double[300][14000];
    double xbase0[700];// max LL = 640
    double xbase[700]; // max LL = 640
    double pom[70];//63/640
    double s3d[41320];//LN =40320 to made D  s3_1fa

    int nfft=nsps;//=721000
    int j=0;
    int n=0;
    //qDebug()<<"nfft--------"<<nfft<<npts;

    for (int k = 0; k < 84; ++k)//85=???
    {//do k=1,84
        if (k==isync[n]-1)
        {
            n++;
            continue;
        }
        int ja=k*nsps+jpk;//ja=(k-1)*nsps + jpk
        //if (ja+nsps>300000) qDebug()<<ja+nsps;
        if (ja<0) ja=0;//2.58
        //jb=ja+nsps-1
        //if(ja.lt.0) ja=0
        //if(jb.gt.npts-1) jb=npts-1
        //nz=jb-ja
        for (int x = 0; x < nfft; ++x) cs[x]=c0[x+ja];//cs(0:nfft-1)=c0(ja:jb) cs(0:nz)=c0(ja:jb)
        //for (int x = nfft; x < 20736; ++x) cs[x]=0.0;//if(nz.lt.nfft-1) cs(nz+1:)=0.
        f2a->four2a_c2c(cs,nsps,-1,1);//four2a(cs,nsps,1,-1,1)             //!c2c FFT to frequency
        for (int ii = 0; ii < LL; ++ii)//192 - 640 //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        {//do ii=1,LL    i1=i0+ipk-64 + mode_q65
            int i=ii-64+mode_q65;//hv=64<-tested=?? old -65;    i=ii-65+mode_q65   //-65=?? !mode_q65 = 1 2 4 8 16 for Q65 A B C D E
            if (i<0)
            {
                i=i+nsps; //-1
            }
            //else i=i-1;
            //if(k==1)qDebug()<<i<<LL;
            //s3[j]=(creal(cs[i])*creal(cs[i]) + cimag(cs[i])*cimag(cs[i]));//s3(ii,j)=real(cs(i))**2 + aimag(cs(i))**2
            pom1[j][ii]=(creal(cs[i])*creal(cs[i]) + cimag(cs[i])*cimag(cs[i]));
        }
        j++;
    }
    for (int i = 0; i < LL; ++i)
    {//do i=1,LL
        for (int x = 0; x < NN; ++x) pom[x] = pom1[x][i];
        xbase0[i] = pomAll.pctile_shell(pom,NN,45);//pctile(s3(i,1:NN),NN,45,xbase0(i)) //!Get baseline for passband shape
    }
    int nh=25;
    double sum0 = 0.0;
    double sum1 = 0.0;
    for (int i = 0; i < nh; ++i)
    {
        sum0+=xbase0[i];
        sum1+=xbase0[i+LL-nh];
    }
    sum0/=((double)nh-1.0);
    sum1/=((double)nh-1.0);
    if (sum0==0.0) sum0=0.000001;
    if (sum1==0.0) sum1=0.000001;
    for (int i = 0; i < nh; ++i)
    {
        xbase[i]=xbase0[i]/sum0;   //xbase(1:nh-1)=sum(xbase0(1:nh-1))/(nh-1.0)
        xbase[i+LL-nh]=xbase0[i+LL-nh]/sum1; //xbase(1:nh-1)=sum(xbase0(1:nh-1))/(nh-1.0)
    }
    double sum3 = 0.0;
    for (int i = 0; i <LL; ++i) sum3+=xbase0[i];
    sum3/=(2.0*(double)nh+1.0);
    if (sum3==0.0) sum3=0.000001;
    for (int i = nh; i < LL-nh; ++i)
    {//do i=nh,LL-nh
        xbase[i]=xbase0[i]/sum3;//xbase(i)=sum(xbase0(i-nh+1:i+nh))/(2*nh+1)  !Smoothed passband shape
    }

    //do i=1,LL
    //s3(i,1:NN)=s3(i,1:NN)/(xbase(i)+0.001) !Apply frequency equalization
    //enddo

    j=0;
    for (int i = 0; i < NN; ++i)
    {//do i=1,LL
        for (int x = 0; x < LL; ++x)
        {
            double del = (xbase[x]+0.001);
            if (del==0.0) del=0.000001;
            s3d[j] = pom1[i][x]/del;
            j++;
        }
    }

    double base = pomAll.pctile_shell(s3d,LL*NN,40);//pctile(s3,LL*NN,40,base)
    if (base==0.0) base=0.000001;
    for (int i = 0; i < NN*LL; ++i) s3f[i]=(float)(s3d[i]/base);
}
void DecoderQ65::q65_loops(std::complex<double> *c00,int npts2,int nsps2,int nsubmode,int ndepth,int jpk0,
                           double xdt0,double f0,int iaptype,double &xdt1,double &f1,double &snr2,
                           int *dat4,int &idec,bool sing_dec)
{
    idec=-1;
    //ircbest=9999
    //std::complex<double> c0[721000];//allocate(c0(0:npts2-1)) 1440000/2=720000;
    std::complex<double> *c0 = new std::complex<double>[721000];
    int irc=-99;
    double s3lim=20.0;
    double baud=6000.0/(double)nsps2;
    double ndf = 0.0;
    double ndt = 0.0;
    float esnodb = 0.0;
    double a[5];//3
    const int NN=63;
    //const int LN=2176*63;//
    float s3f[41320];//LN =40320 to made D

    //ibwa = 4;
    //ibwb = 8;

    int idfmax=3;//2.57 old=3;
    int idtmax=3;//2.57 old=3;
    double ibw0=(double)(ibwa+ibwb)/2.0;
    int maxdist=4; //2.57 old=5
    if (ndepth==2)//(ndepth & 3)>=2
    {
        idfmax=4; //2.57 old=4
        idtmax=4; //2.57 old=4
        maxdist=6;//2.57 old=10
    }
    if (ndepth==3)
    {
        idfmax=5;
        idtmax=5;
        maxdist=15;
    }
    //qDebug()<<ibwa<<ibwb<<ibw0;

    int LL=64*(mode_q65+2);//A=127
    //napmin=99
    xdt1=xdt0;
    f1=f0;
    /*int idfbest=0;
    int idtbest=0;
    int ndistbest=0;*/

    //qDebug()<<"q65_loops"<<npts2;
    bool exitt = false;
    for (int idf = 1; idf <= idfmax; ++idf)//tested from 0 to <   //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {//do idf=1,idfmax
        ndf=(double)idf/2.0;
        if (fmod(idf,2.0)==0) ndf=-ndf;
        for (int x = 0; x < 3; ++x) a[x]=0.0;
        a[0]=-(f0+0.5*baud*ndf); //if (f0>840 && f0<860) qDebug()<<a[0]<<f0; //+0.5  hv=0.53*
        if (sing_dec) a[1]=-(0.5*drift);//2.57
        twkfreq(c00,c0,npts2,6000.0,a);
        for (int idt = 1; idt <= idtmax; ++idt)//tested from 0 to <
        {//do idt=1,idtmax
            ndt=(double)idt/2.0;
            if (fmod(idt,2.0)==0) ndt=-ndt;
            int jpk=(int)((double)jpk0 + (double)nsps2*ndt/16.0);              //!tsym/16
            //qDebug()<<jpk;
            if (jpk<0) jpk=0; // jpk=max(0,jpk) if((int)f0==1533) qDebug()<<ndt<<jpk;
            if (jpk>29000) jpk=29000;//2.58 jpk=min(29000,jpk)
            spec64(c0,nsps2,jpk,s3f,LL,NN);//npts2
            //double base = pomAll.pctile_shell(s3,LL*NN,40);//pctile(s3,LL*NN,40,base)
            //if (base<0.0001) base=0.0001;
            //for (int i = 0; i < LL*NN; ++i) s3[i]=s3[i]/base;
            for (int i = 0; i < LL*NN; ++i)
            {
                if (s3f[i]>s3lim)
                {
                    s3f[i]=s3lim; //where(s3(1:LL*NN)>s3lim) s3(1:LL*NN)=s3lim
                    //qDebug()<<i<<"s3lim";
                }
            }
            q65_bzap(s3f,LL);                   //!Zap birdies
            for (int ibw = ibwa; ibw <= ibwb; ++ibw)
            {//do ibw=ibwa,ibwb
                int ndist=(ndf*ndf + ndt*ndt + ((double)((double)ibw-ibw0)*((double)ibw-ibw0)));
                if (ndist>maxdist) continue;
                double b90=pow(1.72,ibw);
                if (b90>345.0) continue;
                float b90ts = b90/baud;
                q65_dec2(s3f,nsubmode,b90ts,esnodb,irc,dat4);
                //! irc > 0 ==> number of iterations required to decode
                //!  -1 = invalid params
                //!  -2 = decode failed
                //!  -3 = CRC mismatch
                if (irc>=0)
                {
                    //qDebug()<<decoded;
                    /*idfbest=idf;
                    idtbest=idt;
                    ndistbest=ndist;
                    nrc=irc;*/
                    exitt = true;
                    break;
                }
            }  //! ibw (b90 loop)
            if (exitt) break;
        }  //! idt (DT loop)
        if (exitt) break;
    }
    //c100:
    if (irc>=0)//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {
        idec=iaptype;
        snr2=esnodb -  pomAll.db(2500.0/baud);
        xdt1=xdt0 +  (double)nsps2*ndt/(16.0*6000.0);
        f1=f0 + 0.5*baud*ndf;
    }
    delete [] c0;
}
void DecoderQ65::PrintMsg(QString tmm,double snrr,double dtt,int dff,QString mss,int frqq,QString idecc,
                          QString inf,bool &bgc, bool &havdd)//bool to2list,
{
    if (bgc)
    {
        bgc = false;
        EmitBackColor();
    }
    QString sdtxx = QString("%1").arg(dtt,0,'f',1);
    if (sdtxx=="-0.0") sdtxx="0.0";
    int nsnrr=(int)(fmax(-35.0,snrr));
    if (nsnrr > 49) nsnrr = 49;
    QStringList list;
    list <<tmm<<QString("%1").arg(nsnrr)
    <<sdtxx<<QString("%1").arg(dff)
    <<mss<<idecc<<inf
    <<QString("%1").arg(frqq);
    EmitDecodetText(list);//,to2list
    havdd = true;
}
void DecoderQ65::q65_decode(double *iwave,double nfa0,double nfb0,double fqso,int modid,bool &have_dec)
{
    bool f_only_one_color = true;
    static QString sl_mycall  = "";
    static QString sl_hiscall = "";
    static QString sl_hisgrid = "";
    int dgen1[13];//13
    int codewords1[63];
    QString mycall = s_mycall.trimmed();
    QString hiscall = s_hiscall.trimmed();
    QString hisgrid = s_hisgrid.trimmed();
    int ntrperiod = s_ntrperiod;
    int ndepth = s_ndepth;
    double nfqso = fqso;
    bool emedelay = f_emedelay;
    double f0 = 0.0;
    double snr1 = 0.0;
    //double width = 0.0;
    int dat4[20];//13
    double snr2 = 0.0;
    int idec = -1;
    double xdt = 0.0;
    double dtdec = 0.0;
    double f0dec = 0.0;
    double xdt1 = 0.0;
    double f1 = 0.0;
    int nused = 0;

    int jpk0 = 0;
    int npts=ntrperiod*12000;//max 120*12000=1440000;
    //int nfft1=ntrperiod*12000;//max 120*12000=1440000;
    //std::complex<double> c00[1441000];//=1440000   allocate (c00(0:nfft1-1))
    std::complex<double> *c00 = new std::complex<double>[1441000];

    int nQSOprogress = s_nQSOprogress;//for the moment
    int iaptype = 0;//for the moment
    
    int cont_id = 0;
    int cont_type = 0;
    if (!f_multi_answer_modq65)//2.65
    {
    	cont_id = s_cont_id;
    	cont_type = s_cont_type;    	
   	}
    
    bool real_dec = false;
    QString hv_dec = "?";
    QString sidec = "?";
    //bool f_averaging = false;
    //bool lclearave = false; //>> clar avg
    //bool f_clravg_after_decode = true;
    //bool single_decode =
    QString prev_sing_dec = "---";
    bool lapcqonly = false;//for the moment
    if (!s_lapon) lapcqonly = true;// only in mshv

    bool lagain = false;
    if (s_mousebutton==3)
        lagain = true;

    //if(lagain) ndepth=3; //ndepth=ior(ndepth,3)       //wsjt252 !Use 'Deep' for manual Q65 decodes
    	
    //! Determine the T/R sequence: iseq=0 (even first), or iseq=1 (odd second)
    /*n=nutc
    if(ntrperiod>=60 .and. nutc<=2359) n=100*n  // old if(ntrperiod.ge.60) n=100*n   
    write(cutc,'(i6.6)') n
    read(cutc,'(3i2)') ih,im,is
    int ih =  tp.mid(0,2).toInt();
    int im =  tp.mid(2,2).toInt();//get min 120023
    int is =  tp.mid(4,2).toInt();//get seconds 120023
    int nsec=3600*ih + 60*im + is;
    iseq=fmod(nsec/ntrperiod,2);*/
    int time_ss =  s_time.midRef(4,2).toInt();//get seconds 120023
    int time_mm =  s_time.midRef(2,2).toInt();//get min 120023
    int time_p = (time_mm*60)+time_ss;
    time_p = time_p % (ntrperiod*2);
    if (time_p<ntrperiod) iseq = 0;
    else iseq = 1;

    lnewdat = true;//hv for correction  if in diferent time
    //qDebug()<<iseq<<s_time;

    if 		(modid==14) mode_q65 = 1;
    else if (modid==15) mode_q65 = 2;
    else if (modid==16) mode_q65 = 4;
    else if (modid==17) mode_q65 = 8;

    int nsubmode=0;
    if	    (mode_q65==2) nsubmode=1;
    else if (mode_q65==4) nsubmode=2;
    else if (mode_q65==8) nsubmode=3;
    //if(mode_q65.eq.16) nsubmode=4
    //if(mode_q65.eq.32) nsubmode=5

    //!w3sz added
    /*int stageno;*/
    int stageno = 0;

    nfa=nfa0;
    nfb=nfb0;
    nfa=fmax(100,nfa);    	//hv nfa down
    nfa=fmin(5000-10,nfa);  //hv nfa up
    nfb=fmax(100+10,nfb);   //hv nfb down
    nfb=fmin(5000,nfb);     //hv nfb up
    /*if (nfqso<nfa || nfqso>nfb)
    {
        nfqso = nfa + ((nfb-nfa)/2.0);
    }*/
    //double ntol = ftol;
    //ftol = (nfb-nfa)/2.0;
    //qDebug()<<nfqso<<"ftol"<<ftol<<(nfqso-ftol)<<(nfqso+ftol);
    if (lagain) q65_hist(nfqso,"",hiscall,hisgrid);//2.55 rtue=write false=find

    if (lclearave) q65_clravg();
    nsps = 1800;
    if      (ntrperiod== 30) nsps=3600;
    else if (ntrperiod== 60) nsps=7200;
    else if (ntrperiod==120) nsps=16000;
    //else if (ntrperiod==300) nsps=41472;

    int baud=12000.0/(double)nsps;
    //nFadingModel=1
    s_maxiters=33;//2.57 33=wsjt-x250rc1 myold=80
    ibwa=fmax(1,int(1.8*log(baud*mode_q65)) + 1);
    ibwb=fmin(10,ibwa+2);//2.57 ibwb=min(10,ibwa+2) myold=+4 //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (ndepth==2)//((ndepth & 3)>=2)
    {
        ibwa=fmax(1,int(1.8*log(baud*mode_q65)) + 1);//ibwa=fmax(1,int(1.8*log(baud*mode_q65)) + 2);
        ibwb=fmin(10,ibwa+5);
        s_maxiters=67;//2.57 67=wsjt-x250rc1  myold=90
    }
    else if (ndepth==3) //((ndepth & 3)==3)
    {
        ibwa=fmax(1,ibwa-1);
        ibwb=fmin(10,ibwb+2);//2.61 hv=ibwb+2    ibwb=min(10,ibwb+1)
        s_maxiters=100;
    }
    //qDebug()<<ibwa<<ibwb;
    static bool init_q65_enc = true;
    if (init_q65_enc)
    {
        for (int i = 0; i < 63; ++i)
        {
            if (i<13) dgen1[i]=0;
            codewords1[i]=0;
        }
        q65S.q65_enc(dgen1,codewords1);
        init_q65_enc = false;
    }

    if (mycall.isEmpty()) mycall = "XX2XX";
    //if (hiscall.isEmpty()) hiscall= mycall;   //stop for ft8apset
    //if (hisgrid.isEmpty()) hisgrid= "FN20MM"; //stop for ft8apset
    bool mhc = false;
    bool lcc = false;
    if (mycall!=sl_mycall || hiscall!=sl_hiscall)
    {
        mhc = true;
        sl_mycall =mycall;
        sl_hiscall=hiscall;
    }
    if (hisgrid!=sl_hisgrid)
    {
        lcc = true;
        sl_hisgrid=hisgrid;
    }
    //ncw = 0; //stop=2.61 no Ap after 1 decode,  add=2.57->ncw = 0;
    if (mhc || lcc /*|| lagain*/) //2.57 myold=no_lagain wsjt-x250rc1 if(nqd.eq.1 .or. lagain) then
    {
        q65_set_list(mycall,hiscall,hisgrid);
        /*QString sss;
        for (int i = 0; i < 63; ++i) sss.append(QString("%1").arg((int)codewords_[4][i]));
        qDebug()<<"1="<<sss;
        qDebug()<<"Call or Loc Cahnged"<<mycall<<hiscall<<hisgrid;*/
        //qDebug()<<"set_list ncw="<<ncw;
    }

    QString drft = "";
    nused=1;
    int iavg=0;  //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    
     
    /*! W3SZ patch: Initialize AP params here, rather than afer the call to ana64().
    call ft8apset(mycall,hiscall,ncontest,apsym0,aph10) ! Generate ap symbols
    where(apsym0.eq.-1) apsym0=0
    npasses=2
    if(nQSOprogress.eq.5) npasses=3*/
	q65apset(mycall,hiscall,apsym0);// ft8apset(mycall,hiscall,ncontest,apsym0,aph10) //! Generate ap symbols
    for (int x = 0; x < 59; ++x)//no possible ???
    {
        if (apsym0[x]==-1) apsym0[x]=0; //where(apsym0.eq.-1) apsym0=0
    }
    npasses=2;
    if (nQSOprogress==5) npasses=3;//msg=73    
    

    //! Call top-level routine in q65 module: establish sync and try for a q3 decode.
    q65_dec0(iavg,iwave,nfqso,lclearave,emedelay,xdt,f0,snr1,dat4,snr2,idec,nQSOprogress,cont_id,cont_type,stageno);
    if (idec>=0)
    {
        dtdec=xdt;                        //!We have a list-decode result at nfqso
        f0dec=f0;
        hv_dec = "StdS";
        if (idec>=1) hv_dec = "ApS"+QString("%1").arg(idec);
        goto c100;
    }

    //! Prepare for a single-period decode with iaptype = 0, 1, 2, or 4
    jpk0=(int)((xdt+1.0)*6000.0);                      //!Index of nominal start of signal
    if (ntrperiod<=30) jpk0=(int)((xdt+0.5)*6000.0);//  !For shortest sequences
    if (jpk0<0) jpk0=0;
    ana64(iwave,npts,c00);          //!Convert to complex c00() at 6000 Sa/s
    
/*    
	q65apset(mycall,hiscall,apsym0);// ft8apset(mycall,hiscall,ncontest,apsym0,aph10) //! Generate ap symbols
    for (int x = 0; x < 59; ++x)//no possible ???
    {
        if (apsym0[x]==-1) apsym0[x]=0; //where(apsym0.eq.-1) apsym0=0
    }
    npasses=2;
    if (nQSOprogress==5) npasses=3;//msg=73
*/    	
    	
    if (lapcqonly) npasses=1;
    iaptype=0;
    for (int ipass = 0; ipass <= npasses; ++ipass)
    {//do ipass=0,npasses                  !Loop over AP passes
        for (int i = 0; i < 13 ; ++i)
        {
            apmask[i]=0;  //13                       //!Try first with no AP information
            apsymbols[i]=0; //13   //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        }
        if (ipass>=1)
        {
            //! Subsequent passes use AP information appropiate for nQSOprogress
            q65_ap(nQSOprogress,ipass,cont_id,cont_type,lapcqonly,iaptype,apsym0,apmask1,apsymbols1);
            int z = 0;
            for (int i = 0; i < 13; ++i)
            {
                apmask[i]    = BinToInt32(apmask1,z,z+6);
                apsymbols[i] = BinToInt32(apsymbols1,z,z+6);
                z += 6;
            }
        } //qDebug()<<ipass<<nQSOprogress<<f0;
        q65_loops(c00,npts/2,nsps/2,nsubmode,ndepth,jpk0,xdt,f0,iaptype,xdt1,f1,snr2,dat4,idec,true);
        if (idec>=0)
        {
            if (f_max_drift && (drift<-14.0 || drift>14.0)) drft = "D";
            dtdec=xdt1;
            f0dec=f1;
            hv_dec = "StdS";
            if (ipass>=1) hv_dec = "ApS"+QString("%1").arg(iaptype);
            goto c100;       //!Successful decode, we're done
        }
        if (f_max_drift)// if no Successful decode try with no drift
        {
            int jpk0nd=(int)((xdtnd+1.0)*6000.0);               //!Index of nominal start of signal
            if (ntrperiod<=30) jpk0nd=(int)((xdtnd+0.5)*6000.0);//!For shortest sequences
            if (jpk0nd<0) jpk0nd=0;
            q65_loops(c00,npts/2,nsps/2,nsubmode,ndepth,jpk0nd,xdtnd,f0nd,iaptype,xdt1,f1,snr2,dat4,idec,false);
            if (idec>=0)
            {
                dtdec=xdt1;
                f0dec=f1;
                hv_dec = "StdS";
                if (ipass>=1) hv_dec = "ApS"+QString("%1").arg(iaptype);
                goto c100;       //!Successful decode, we're done
            }
        }
    }
    //(ndepth & 16)==0 average  Include_averaging->setChecked(m_ndepth&16);
    //Include_correlation->setChecked(m_ndepth&32);
    //m_ndepth&64  Enable_AP_DXcall
    //(ndepth & 128)==0  clar avg
    if (!f_averaging || navg[iseq]<2) goto c100; //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    //! There was no single-transmission decode. Try for an average 'q3n' decode.
    //! Call top-level routine in q65 module: establish sync and try for a q3
    //! decode, this time using the cumulative 's1a' symbol spectra.

c50://2.55 ???
    iavg=1; //nutc, no from this period
    q65_dec0(iavg,iwave,nfqso,lclearave,emedelay,xdt,f0,snr1,dat4,snr2,idec,nQSOprogress,cont_id,cont_type,stageno);
    if (idec>=0)
    {
        dtdec=xdt;               //!We have a list-decode result from averaged data
        f0dec=f0;
        nused=navg[iseq];  //qDebug()<<"AVg111="<<decoded;
        hv_dec = "Avg";
        goto c100;
    }
    //! There was no 'q3n' decode.  Try for a 'q[0124]n' decode.
    //! Call top-level routine in q65 module: establish sync and try for a q[012]n
    //! decode, this time using the cumulative 's1a' symbol spectra.
    iavg=2;
    q65_dec0(iavg,iwave,nfqso,lclearave,emedelay,xdt,f0,snr1,dat4,snr2,idec,nQSOprogress,cont_id,cont_type,stageno);
    if (idec>=0)
    {
        dtdec=xdt;                         
        f0dec=f0;
        nused=navg[iseq]; //qDebug()<<"AVg222="<<decoded;
        hv_dec = "Avg";
    }

c100:       
    if (idec<0 && max_drift!=0)//wsjt252 if(idec.lt.0 .and. max_drift.eq.50) old //if (idec<0) 
    {
    	stageno = 5;//2.61 Call top-level routine in q65 module: establish sync and try for a  q3 or q0 decode.        
        q65_dec0(iavg,iwave,nfqso,lclearave,emedelay,xdt,f0,snr1,dat4,snr2,idec,nQSOprogress,cont_id,cont_type,stageno);
        if (idec>=0)
        {
            dtdec=xdt;             
            f0dec=f0;
        }
    }

    if (idec>=0)
    {
        q65_snr(dat4,dtdec,f0dec,mode_q65,snr2);//nused,
        prev_sing_dec = decoded;
        int df_hv = (f0dec-nftx);
        if (idec >= 0) sidec = "Q"+QString("%1").arg(idec);
        if (nused>= 2) sidec.append(QString("%1").arg(nused));
        sidec.append(drft);
        PrintMsg(s_time,snr2,dtdec,df_hv,decoded,(int)f0dec,sidec,hv_dec,f_only_one_color,have_dec);
        real_dec = true;
        QString cc = "";
        QString gg = "";
        q65_hist((int)f0dec,decoded,cc,gg);//2.55 rtue=write false=find
        //qDebug()<<"SINGLE="<<decoded;
    }
    /*else
    {
        snr1 = snr1 - 35;
        int df_hv = (f0-nftx);
        if (idec >= 0) sidec = "Q"+QString("%1").arg(idec);
        if (nused>= 2) sidec.append(QString("%1").arg(nused));
        //NO SINGLE DECODE
        PrintMsg(s_time,snr1,xdt,df_hv,"---------",(int)f0,sidec,hv_dec,false,f_only_one_color,have_dec);
        real_dec = false;
    }*/

    /*if(idec>=0) then
    ! idec Meaning
    ! ------------------------------------------------------
    ! -1:  No decode
    !  0:  Decode without AP information
    !  1:  Decode with AP for "CQ        ?   ?"
    !  2:  Decode with AP for "MyCall    ?   ?"
    !  3:  Decode with AP for "MyCall DxCall ?"*/

    //! Report snr1, even if no decode.
    //nsnr=db(snr1) - 35.0
    //if(nsnr<-35) nsnr=-35;
    //idec=-1;
    //navg0=1000*navg(0) + navg(1)
    if (f_single_decode || lagain) goto c900;

    for (int icand = 0; icand < ncand; ++icand)//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {//do icand=1,ncand
        //! Prepare for single-period candidate decodes with iaptype = 0, 1, 2, or 4
        //qDebug()<<candidates_[0][icand];//(icand,1)
        xdt= candidates_[0][icand];//(icand,2)
        f0 = candidates_[1][icand];//(icand,3)
        //qDebug()<<icand<<xdt<<f0;
        jpk0=(int)((xdt+1.0)*6000.0);                  //!Index of nominal start of signal
        if (ntrperiod<=30) jpk0=(int)((xdt+0.5)*6000.0); //!For shortest sequences
        if (jpk0<0) jpk0=0; // qDebug()<<"jjjjjjjjjjjjjjjjjj"<<jpk0;
        ana64(iwave,npts,c00);       //!Convert to complex c00() at 6000 Sa/s
        q65apset(mycall,hiscall,apsym0); //! Generate ap symbols
        for (int x = 0; x < 59; ++x)//no possible ???
        {
            if (apsym0[x]==-1) apsym0[x]=0; //where(apsym0.eq.-1) apsym0=0
        }
        npasses=2;
        if (nQSOprogress==5) npasses=3;
        if (lapcqonly) npasses=1;
        iaptype=0;
        for (int ipass = 0; ipass <= npasses; ++ipass)
        {//do ipass=0,npasses                  //!Loop over AP passes
            for (int i = 0; i < 13 ; ++i)
            {
                apmask[i]=0;  //13                       //!Try first with no AP information
                apsymbols[i]=0; //13   //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
            }
            if (ipass>=1)
            {
                //! Subsequent passes use AP information appropiate for nQSOprogress
                q65_ap(nQSOprogress,ipass,cont_id,cont_type,lapcqonly,iaptype,apsym0,apmask1,apsymbols1);
                int z = 0;
                for (int i = 0; i < 13; ++i)
                {
                    apmask[i]    = BinToInt32(apmask1,z,z+6);
                    apsymbols[i] = BinToInt32(apsymbols1,z,z+6);
                    z += 6;
                }
            }
            q65_loops(c00,npts/2,nsps/2,nsubmode,ndepth,jpk0,xdt,f0,iaptype,xdt1,f1,snr2,dat4,idec,false);
            if (idec>=0 && prev_sing_dec!=decoded)
            {
                dtdec=xdt1; //qDebug()<<"yyyyyyyyyyyyyy";
                f0dec=f1;
                q65_snr(dat4,dtdec,f0dec,mode_q65,snr2);//nused,
                int df_hv = (f0dec-nftx);
                hv_dec = "StdM";
                if (ipass>=1) hv_dec = "ApM"+QString("%1").arg(iaptype);
                if (idec >= 0) sidec = "Q"+QString("%1").arg(idec);
                if (nused>= 2) sidec.append(QString("%1").arg(nused));
                PrintMsg(s_time,snr2,dtdec,df_hv,decoded,(int)f0dec,sidec,hv_dec,f_only_one_color,have_dec);//!Successful decode, were done
                real_dec = true;
                QString cc = "";
                QString gg = "";
                q65_hist((int)f0dec,decoded,cc,gg);//2.55 rtue=write false=find
                break; // next candidate
            }
        }
    }
    //old if(iavg.eq.0 .and.navg(iseq).ge.2) go to 50
    //if(iavg.eq.0 .and.navg(iseq).ge.2 .and. iand(ndepth,16).ne.0) go to 50
    // old if(iavg==0 && navg[iseq]>=2) goto c50;
    if (iavg==0 && navg[iseq]>=2 && f_averaging) goto c50;//2.56  && !real_dec
c900:
    if (real_dec && f_clravg_after_decode && !lagain/* && (nfqso>=nfa && nfqso<=nfb)*/) q65_clravg(); //!AutoClrAvg after decode)
    //! if(iand(ndepth,128).ne.0) call q65_clravg    !AutoClrAvg after decode
    //if(iand(ndepth,128)!=0 && !lagain && int(abs(f0dec-nfqso))<=ntol ) call q65_clravg    //!AutoClrAvg

    delete [] c00;
}

