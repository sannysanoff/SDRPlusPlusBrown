/* The algorithms, source code, look-and-feel of WSJT-X and related
 * programs, and protocol specifications for the modes FSK441, FT8, JT4,
 * JT6M, JT9, JT65, JTMS, QRA64, ISCAT, MSK144, are Copyright © 2001-2017
 * by one or more of the following authors: Joseph Taylor, K1JT; Bill
 * Somerville, G4WJS; Steven Franke, K9AN; Nico Palermo, IV3NWV; Greg Beam,
 * KI7MT; Michael Black, W9MDB; Edson Pereira, PY2SDR; Philip Karn, KA9Q;
 * and other members of the WSJT Development Group.
 *
 * MSHV Decoder 
 * Rewritten into C++ and modified by Hrisimir Hristov, LZ2HV 2015-2017
 * May be used under the terms of the GNU General Public License (GPL)
 */

#include "decoderms.h"

#define _JTMS_DH_
#include "config_msg_all.h"
//#include <QtGui>

void DecoderMs::setupms()
{
    int nb[7];
    int nsps=8;
    //double twopi=8*atan(1.0);
    double dt=1.0/DEC_SAMPLE_RATE;                    // !Sample interval
    double f0=1155.46875;
    double f1=1844.53125;
    double dphi0=twopi*dt*f0;
    double dphi1=twopi*dt*f1;

    for (int i = 0; i<64; i++)//63 hv???
    {
        int k=0;
        int m=0;
        for (int n = 5; n>=0; n--)
        {                       //!Each character gets 6+1 bits
            nb[k]=1 & (i >> n);
            m=m+nb[k];
            k++;
        }

        nb[k]=m & 1;                        //!Insert parity bit
        k++;
        double phi=0.0;
        int j=0;
        for (int x = 0; x<7; x++)//2.12
        {
            double dphi;                     //!Generate the waveform
            if (nb[x]==0)
                dphi=dphi0;
            else
                dphi=dphi1;
            for (int ii = 0; ii<nsps; ii++)
            {
                //j=j+1;
                phi=phi+dphi;
                //cw(j,i)=cmplx(cos(phi),sin(phi));
                cw_jtms_[i][j]=cos(phi)+sin(phi)*complex_i;
                j++;
                //qDebug()<<j;
            }
            //qDebug()<<j;
        }
    }

    //cwb=cw(1:56,57);
    for (int z = 0; z<56; z++)
    {
        cwb_jtms[z]=cw_jtms_[57][z];// pod 57 ne priema
        // qDebug()<<creal(cwb[z]);
    }
    /* for (int z = 0; z<56; z++)
     {
         //std::complex<double> sum;
         for (int p = 0; p<56; p++)
         {
             cwb[z] = cwb[z] + cw[z][p];
         }
         //cwb[z]=sum;
     }*/
}

double DecoderMs::dot_product_da_da(double *a, double *b,int size,int offset_b)
{
    double sum = 0;
    int i;
    //If the vectors are INTEGER or REAL -> DOT_PRODUCT(VECTOR_A, VECTOR_B) = SUM(VECTOR_A*VECTOR_B)
    for (i = 0; i < size; i++)
    {
        sum += a[i] * b[i+offset_b];
    }
    return sum;
}

void DecoderMs::hipass(double*y,int y_begin,int npts,int nwidth)
{
    //!  Hipass filter for time-domain data.  Removes an RC-type running
    //!  mean (time constant nwidth) from array y(1:npts).
    if(nwidth<1) 
    	return;   
    
    double c1=1.0/nwidth;
    double c2=1.0-c1;
    double s=0.0;

    for (int i = 0; i<nwidth; i++)                   // !Get initial average
        s=s+y[i+y_begin];

    double ave=c1*s;

    for (int i = 0; i<npts; i++)
    {                         ///!Do the filtering
        double y0=y[i+y_begin];
        y[i+y_begin]=y0-ave;                     //!Remove the mean
        ave=c1*y0 + c2*ave;              //!Update the mean
        //qDebug()<<i+y_begin;//1623max
    }
}

double DecoderMs::msdf(std::complex<double> *cdat,int npts,int t2,int nfft1,double f0,int nfreeze,int mousedf,
                       int dftolerance)
{
    double snrsq2;
    const int NZ=32768;//32768;
    //double *sq = new double[NZ];
    double sq[NZ];

    //real ccf(-2600:2600)//5200                  //!Correct limits?
    double ccf_plus[6404];//7201
    double *ccf = &ccf_plus[3202];//ot -2600 to 2600//pri 2s max limits is +-3200 hv +2 rezerv

    double tmp[NZ];
    std::complex<double> *c = new std::complex<double>[NZ*2+10];
    //data nsps/8/
    int nsps = 8;
    double base;

    double df1=DEC_SAMPLE_RATE/nfft1;
    int nh=nfft1/2;
    double fac=1.0/(nfft1*nfft1);

    for (int i = 0; i<npts; i++)
        c[i]=fac*(cdat[i]*cdat[i]);
    //qDebug()<<npts<<nfft1;

    for (int i = npts; i<nfft1; i++)
    {//c(npts+1:nfft1)=0.
        //qDebug()<<i;
        c[i]=0.0+0.0*complex_i;
    }

    f2a->four2a_c2c(c,nfft1,-1,1);

    //! In the "doubled-frequencies" spectrum of squared cdat:
    double fa=2.0*(f0-400.0);
    double fb=2.0*(f0+400.0);
    int j0=(int)(2.0*f0/df1);
    int ja=(int)(fa/df1);
    int jb=(int)(fb/df1);
    int jd=(int)(nfft1/nsps);
    //qDebug()<<"0strlen(msg)"<<ja<<jb<<j0;

    for (int j = 0; j<nh+0; j++)
        //do j=1,nh+1
        //sq[j]=(creal(c[j])*creal(c[j])) + (cimag(c[j])*cimag(c[j]));
        sq[j]=pomAll.ps_hv(c[j]);

    //for (int j =-3201; j<3201; j++)
    //{
    //qDebug()<<j;
    //ccf[j]=0.0;
    //}
    //zero_double_beg_count(ccf,-3202,6404); ccf=0.
    pomAll.zero_double_beg_end(ccf,-3202,3202);//v 1.17

    //qDebug()<<"1strlen(msg)"<<ja-j0-0<<jb-j0-0<<nh+0;
    //qDebug()<<"sq="<<ja<<ja+jd;
    for (int j = ja; j<jb; j++)
        //ccf[j-j0-1]=sq[j]+sq[j+jd];
        ccf[j-j0-0]=sq[j]+sq[j+jd];

    //qDebug()<<"2strlen(msg)"<<ja-j0-0<<jb-ja+0;
    //qDebug()<<"tmp="<<0<<jb-ja+0;
    //call pctile(ccf(ja-j0-1),tmp,jb-ja+1,50,base)
    base = pctile(ccf,(ja-j0-0),tmp,jb-ja+0,50.0);//hv????
    if(base==0.0)
    	base=1.0;
    for (int j =-3201; j<3201; j++)
        ccf[j]=ccf[j]/base;

    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (nfreeze>0)
    {
        fa=2.0*(f0+mousedf-dftolerance);
        fb=2.0*(f0+mousedf+dftolerance);
    }
    ja=(int)(fa/df1);
    jb=(int)(fb/df1);

    double smax=0.0;
    int jpk=t2;                                   //!Silence compiler warning

    for (int j = ja; j<jb; j++)
    {
        //k=j-j0-1
        int k=j-j0-0;
        if (ccf[k]>smax)
        {
            smax=ccf[k];
            jpk=j;
        }
    }
    //fpk=(jpk-1)*df1
    double fpk=(jpk-0)*df1;
    jtms_dfx=0.5*fpk-f0;
    snrsq2=smax;

    //qDebug()<<snrsq2<<"msdf";
    //qDebug()<<dfx<<"1";
    delete [] c;
    return snrsq2;
}

int DecoderMs::syncms(std::complex<double> *cdat,int npts,std::complex<double> *cwb,double *r)
{
    double tmp[60000];
    int hist[56];
    // const int N = sizeof(hist) / sizeof(int);
    // vector <int> hist;
    //    int hmax[1];
    //std::complex<double> z;
    double rlim;


    //r=0.0;
    // qDebug()<<npts<<npts;
    int jz=npts-55;
    for (int j = 0; j<jz; j++)
    {
        std::complex<double> z=0.0+0.0*complex_i;
        double ss=0.0;
        for (int i = 0; i<56; i++)
        {
            //ss=ss + cabs(cdat[i+j-1]);         // !Total power
            //z=z + cdat[i+j-1]*conjg(cwb[i]);   //!Signal matching <space>
            ss=ss + cabs(cdat[i+j]);             // !Total power
            z=z + cdat[i+j]*conj(cwb[i]);        //!Signal matching <space>
        }
        if(ss==0.0)
        	ss=1.0;
        r[j]=cabs(z)/ss;                         //!Goodness-of-fit to <space>
    }

    double ncut=99.0*double(jz-10)/double(jz);
    rlim = pctile(r,0,tmp,jz,ncut);
    //qDebug()<<(int)((jz-10)*0.01*ncut);

    for (int j = 0; j<56; j++)
        hist[j]=0;

    for (int j = 0; j<jz; j++)
    {
        //int k=fmod(j-1,56)+1;
        //if (r[j]>rlim) hist[k]=hist[k]+1;
        int k=(int)fmod(j,56);
        if (r[j]>rlim) hist[k]=hist[k]+1;
    }
    //hmax=maxloc(hist);
    //hmax=max(hist);
    //i1=hmax(1)
    //int max;
    int pos = 0;
    int s_max = -300000;
    for (int x = 0; x<56; x++)
    {
        int max=hist[x];
        //qDebug()<<max;
        if (s_max < max)// i = kogato ima mnogo ravni dava poslednata
        {
            s_max=max;
            pos = x;
        }
    }
    //qDebug()<<"pos"<<pos;
    return pos;
}

int DecoderMs::lenms(double *r,int npts)
{
    int msglen =0;
    const int NMSGMAX = (37+9)*56; //v2.12    1624 29*56
    double *acf = new double[NMSGMAX];//1624 29*56
    //int np[9] ={5,7,9,11,13,17,19,23,29};      //!Permissible message lengths
    int np[13] ={5,7,9,11,13,17,19,23,29,31,33,35,37};// hv v1.01 {5,7,9,11,13,17,19,23,29} to {5,7,9,11,13,17,19,23,29,31,33,35,37,39}
    //!Why necessary?  (But don't remove!)
    msglen=0;                                  //!Use ACF to find msg length
    //qDebug()<<"glenms="<<npts;
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (npts>=8*56)
    {//if(npts.ge.8*56) then
        //r=r-sum(r(1:npts))/npts;
        double sum =0.0;
        for (int i = 0; i<npts; i++)
        {
            sum += r[i];
        }
        for (int i = 0; i<npts; i++)
        {
            r[i]=r[i]-sum/npts;
        }
        // r=r-sum/npts;

        double acfmax=0.0;
        //double acf0=dot_product(r(1:npts),r(1:npts))
        double acf0=dot_product_da_da(r,r,npts,0);
        if(acf0==0.0)
        	acf0=1.0;

        int kz=fmin((int)(0.75*npts),NMSGMAX);//// hv v1.01 <-1624 29*56 to 39*56
        //qDebug()<<"lenms="<<kz;
        for (int k = 7; k<kz; k++)
        {//do k=8,kz
            double fac=float(npts)/(npts-k);
            //acf[k]=fac*dot_product(r[1:npts],r[1+k:npts+k])/acf0;
            acf[k]=fac*dot_product_da_da(r,r,npts,k)/acf0;
        }
        //call hipass(acf(8),kz-7,50)
        hipass(acf,7,kz-7,50);//acf,7,kz-7,50 <- probvano 8 garmi hv  acf[0-1623]<-(kz-7)

        int kpk=1;                              //!Silence compiler warning
        for (int k = 7; k<kz; k++)
        {//do k=8,kz                           //!Find acfmax, kpk
            if (acf[k]>acfmax)
            {
                acfmax=acf[k];
                kpk=k;
            }
        }

        double sumsq=0.0;
        int n=0;
        for (int k = 7; k<kz; k++)
        {//do k=8,kz                          //!Find rms, skipping around kpk
            if (abs(k-kpk)>10)
            {
                sumsq=sumsq+(acf[k]*acf[k]);
                n++;
            }
        }
        if(n==0)
        	n=1;
        double rms=sqrt(sumsq/n);
        if(rms==0.0)
        	rms=1.0;
        for (int z = 0; z<NMSGMAX; z++) // hv v1.01 1624 to 2184
            acf[z]=acf[z]/rms;                        //!Normalize the acf

        double amax2=0.0;
        double acflim=3.5;
        for (int i = 0; i<13; i++)// hv v.1.01 9 to 13
        {//do i=1,9
            int k=56*np[i];                      //!Check only the permitted lengths
            if (k>kz) goto c10;
            if (acf[k]>acflim && acf[k]>amax2)
            {
                amax2=acf[k];                                  //!Save best value >3.5 sigma
                msglen=np[i];                                 //!Save message length
                //qDebug()<<"lenms="<<msglen;
            }
        }
//c10: //continue;
    }
c10:
	
    delete [] acf;//delete [] acf;
    return msglen;
}

void DecoderMs::decodems(std::complex<double> *cdat,int,std::complex<double> cww_[64][56],int i1,int nchar,double s2_[400][64],char*msg)
{
    //! DF snd sync have been established, now decode the message
    //complex cw(56,0:63)                  //!Complex waveforms for codewords
    //real s2(0:63,400)
    //character msg*400
    std::complex<double> z;
    int kpk=0;                                //!Silence compiler warning

    for (int j = 0; j<nchar; j++)
    { //!Find best match for each character
        //int ia=i1 + (j-1)*56;
        int ia=i1 + (j)*56;
        //qDebug()<<ia<<i1<<(j-1)*56;
        double smax=0.0;
        for (int k = 0; k<41; k++)
        {
            int kk=k;
            if (k==40) kk=57;//kk=57;
            z=0.0;

            for (int i = 0; i<56; i++)
            {
                //qDebug()<<i<<ia<<kk;
                z=z + cdat[ia+i]*conj(cww_[kk][i]);
            }

            double ss=cabs(z);
            s2_[j][k]=ss;//?????
            if (ss>smax)
            {
                smax=ss;
                kpk=kk;
            }
        }
        //msg[j]=cc[kpk+1];
        msg[j]=cc_JTMS_RX[kpk];//ko hv
        if (kpk==57) // kpk==57
            msg[j]=' ';
    }
}

void DecoderMs::foldms(double s2_[400][64],int msglen,int nchar,char *msg)
{
//! Fold the 2-d "goodness of fit" array s2 modulo message length,
//! then decode the folded message.
    double fs2_[39][64];//hv v1.01 29 to 39
    int nfs2[39];//hv v1.01 29 to 39

    for (int x = 0; x<64; x++)
    {
        for (int y = 0; y<39; y++) // hv v1.01 29 to 39
            fs2_[y][x]=0.0;
    }
    for (int z = 0; z<39; z++)// hv v1.01 29 to 39
        nfs2[z]=0;

    for (int j = 0; j<nchar; j++)
    {                         // !Fold s2 into fs2, modulo msglen
        //jj=mod(j-1,msglen)+1
        int jj=fmod(j,msglen);
        nfs2[jj]=nfs2[jj]+1;
        //do i=0,40
        for (int i = 0; i<41; i++)
            fs2_[jj][i]=fs2_[jj][i] + s2_[j][i];
    }

    for (int w = 0; w<400; w++)
        msg[w]=' ';

    int kpk=0;                                  //!Silence compiler warning

    for (int j = 0; j<msglen; j++)
    {
        double smax=0.0;
        //do k=0,40
        for (int k = 0; k<41; k++)
        {
            if (fs2_[j][k]>smax)
            {
                smax=fs2_[j][k];
                kpk=k;
            }
        }
        //if(kpk.eq.40) kpk=57
        if (kpk==40) kpk=57;
        //msg(j:j)=cc(kpk+1:kpk+1)
        msg[j]=cc_JTMS_RX[kpk];
        //if(kpk.eq.57) msg(j:j)=' '
        if (kpk==57) msg[j]=' ';
    }
    /*
      msg29=msg(1:msglen)
      call alignmsg('  ',2,msg29,msglen,idone)
      if(idone.eq.0) call alignmsg('CQ',  3,msg29,msglen,idone)
      if(idone.eq.0) call alignmsg('QRZ', 3,msg29,msglen,idone)
      if(idone.eq.0) call alignmsg(mycall,4,msg29,msglen,idone)
      if(idone.eq.0) call alignmsg(' ',   1,msg29,msglen,idone)
      msg29=adjustl(msg29)
    */
}

void DecoderMs::jtms(double *dat,int dat_c_begin,int npts,int DFTol,double t2,int mswidth,int peak,
                     int nrpt,bool pick,bool &f_only_one_color,int &disp_lines_dec_cou)
{
    QStringList list;

    int max_count_msg = 46;//v1.30 46 for 800pix
    //int part = max_count_msg;

    QString mssg;
    //std::complex<double> *idft = new std::complex<double>[N + 1];
    int NZ = 512*1024;
    double *s = new double[NZ+10];
    int nfft1;
    std::complex<double> *cdat = new std::complex<double>[NZ+10];
    //int t2 = 2;//ne sluzi tuk za ni6to hv?
    double f0 = 1155.46875; //!Nominal frequency for bit=0
    int nfreeze = 1; // hv??? > 0 stava ne6to
    int mousedf = 0; //otklonenie ot mouse hv ?
    //double dfx; //hv? 6te priema stoinost
    double snrsq2;//hv? 6te priema stoinost
    //std::complex<double> *cwb = new std::complex<double>[56];   //!Complex waveform for <space>
    //            complex cw(56,0:63)  dalgo 63 i natapkano s 56.0
    //std::complex<double> (*cw)[64] = new std::complex<double>[56][64];        //!Complex waveforms for all codewords

    //bool first = true;
    //char *mycall = new char[12];
    //char *hiscall = new char[12];
    //char mycall[13];
    //char hiscall[13];
    double r[60000];
    int i1;
    int msglen = 0;
    //real s2(0:63,400)
    double s2_[400][64];
    char msg[400];
    int nchar;
    int nchk;
    //QString fold = "no fold";

    for (int i = 0; i<400; i++)
        msg[i]=' ';

    pomAll.zero_double_beg_end(r,0,60000);

    //for (int i = 0; i<npts; i++)
    //qDebug()<<dat[i];
    /*if (hiscall==(char*)"dummy       ") first=true;//  !Silence compiler warning
    if (first)
        setupms(cw,cwb);        //!Calculate waveforms for codewords
    first=false;*/

    // ograni4ava da ne e po goliamo ot celia razmer ot 30sec
    /*qDebug()<<"1"<<npts;
    qDebug()<<"2"<<npts;*/

    double n=log(double(npts))/log(2.0) + 1.0;
    nfft1=pow(2,n);//  nfft1=2**n                            !FFT length

    analytic(dat,dat_c_begin,npts,nfft1,s,cdat); //!Convert to analytic signal
    //analytic_msk(dat,dat_c_begin,npts,nfft1,cdat);//qDebug()<<"2";

    snrsq2 = msdf(cdat,npts,t2,nfft1,f0,nfreeze,mousedf,DFTol); //!Get DF
    //qDebug()<<snrsq2<<"snrsq2";
    //dfx = 0.0;

    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    double sq2lim=7.0;
    if (pick) sq2lim=5.0;
    if (snrsq2<sq2lim) goto c900;          //!Reject non-JTMS signals

    //qDebug()<<"cb="<<creal(cdat[100]);
    tweak1(cdat,npts,-jtms_dfx,cdat);      //!Mix to standard frequency

    //! DF is known, now establish character sync.
    i1 = syncms(cdat,npts,cwb_jtms,r);       //!Get character sync
    //qDebug()<<"il="<<i1;
    msglen = lenms(r,npts);             //!Find message length
    //qDebug()<<"dolu msglen="<<msglen;


    //s2=0.0;
    for (int i = 0; i<400; i++)
    {
        for (int j = 0; j<64; j++)
            s2_[i][j]=0.0;
    }

    nchar=(npts-55-i1)/56;
    if (nchar>400) nchar=400;
    //qDebug()<<nchar<<npts<<i1;

    decodems(cdat,npts,cw_jtms_,i1,nchar,s2_,msg);   //!Decode the message
    //qDebug()<<"dolu msg="<<nchar;

    nchk = fmax(20,(int)(1.5*msglen));

    //qDebug()<<nchar<<nchk<<msglen;

    //if(msglen.eq.0 .or. nchar.lt.nchk .or. pick) then
    if (msglen==0 || nchar<nchk || pick)
    {
        mssg = CharToQString(msg,400);

        mssg = FormatLongMsg(mssg,max_count_msg);

        f0 = 1155.46875+jtms_dfx;

        if (f_only_one_color)
        {
            f_only_one_color = false;
            SetBackColor();
        }
        list <<s_time<<QString("%1").arg(t2,0,'f',1)<<QString("%1").arg(mswidth)<<
        QString("%1").arg((int)peak)<<QString("%1").arg(nrpt)<<QString("%1").arg((int)jtms_dfx)
        <<mssg<<QString("%1").arg((int)f0);
        
        
        EmitDecodetText(list,s_fopen,true);//1.27 psk rep   fopen bool true    false no file open
        
        if (s_mousebutton == 0 && disp_lines_dec_cou < MAX_DISP_DEC_COU) //mousebutton Left=1, Right=3 fullfile=0 rtd=2
        {
            disp_lines_dec_cou++;
            EmitDecLinesPosToDisplay(disp_lines_dec_cou,t2,t2,s_time);
        }
    }
    //if(msglen.gt.0 .and. nchar.ge.nchk) then
    if (msglen>0 && nchar>=nchk)
    {
        list.clear();
        foldms(s2_,msglen,nchar,msg);
        mssg = CharToQString(msg,msglen);
        //qDebug()<<"1="<<mssg;
        mssg = FormatFoldMsg(mssg);
        //mssg = "  SP9HWY  26R   LZ2HV    ";
        //qDebug()<<"2="<<mssg;
        bool f_align = false;
        mssg = AlignMsgSpecWord(mssg,"CQ",f_align);
        if (!f_align)
            mssg = AlignMsgSpecWord(mssg,"QRZ",f_align);
        if (!f_align)
            mssg = AlignMsgSpecWord(mssg,s_MyCall,f_align);
        //qDebug()<<"3="<<mssg;
        mssg = RemWSpacesInside(mssg);
        //qDebug()<<"4="<<mssg;

        mssg = RemBegEndWSpaces(mssg);
        //mssg.prepend("* ");//  removed v095 hv
        //folded_m = true;

        f0 = 1155.46875+jtms_dfx;

        if (f_only_one_color)
        {
            f_only_one_color = false;
            SetBackColor();
        }
        list <<s_time<<QString("%1").arg(t2,0,'f',1)<<QString("%1").arg(mswidth)<<
        QString("%1").arg((int)peak)<<QString("%1").arg(nrpt)<<QString("%1").arg((int)jtms_dfx)
        <<mssg<<"* "+QString("%1").arg((int)f0);
        
        
        
        EmitDecodetText(list,s_fopen,true);//1.27 psk rep   fopen bool true    false no file open
        
        if (s_mousebutton == 0 && disp_lines_dec_cou < MAX_DISP_DEC_COU) //mousebutton Left=1, Right=3 fullfile=0 rtd=2
        {
            disp_lines_dec_cou++;
            EmitDecLinesPosToDisplay(disp_lines_dec_cou,t2,t2,s_time);
        }
    }

    //QString str = "  lots\t of\nwhitespace\r\n ";
    //str = str.trimmed();
    // str == "lots\t of\nwhitespace"

c900:
    /*QString ss = AlignMsg(" SP9HWY 26R LZ2HV ");
    qDebug()<<ss;
    ss = RemWSpacesInside(ss);
    qDebug()<<ss;*/

    delete [] s;
    delete [] cdat;
    //return mssg;
}
