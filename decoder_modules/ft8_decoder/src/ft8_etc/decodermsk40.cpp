/* The algorithms, source code, look-and-feel of WSJT-X and related
 * programs, and protocol specifications for the modes FSK441, FT8, JT4,
 * JT6M, JT9, JT65, JTMS, QRA64, ISCAT, MSK144, are Copyright © 2001-2017
 * by one or more of the following authors: Joseph Taylor, K1JT; Bill
 * Somerville, G4WJS; Steven Franke, K9AN; Nico Palermo, IV3NWV; Greg Beam,
 * KI7MT; Michael Black, W9MDB; Edson Pereira, PY2SDR; Philip Karn, KA9Q;
 * and other members of the WSJT Development Group.
 *
 * MSHV MSK40 Decoder
 * Rewritten into C++ and modified by Hrisimir Hristov, LZ2HV 2015-2017
 * May be used under the terms of the GNU General Public License (GPL)
 */

#include "decoderms.h"
#include "config_rpt_msk40.h"
//#include <QtGui>
/*
double DecoderMs::maxval_da_beg_to_end(double*a,int a_beg,int a_end)
{
    double max = a[a_beg];
    for (int i = a_beg; i < a_end; i++)
    {
        if (a[i]>max)
        {
            max = a[i];
        }
    }
    return max;
}
*/
void DecoderMs::first_msk40()
{
    if (f_first_msk40)
        return;

    double cbi[42],cbq[42];
    //! define half-sine pulse and raised-cosine edge window
    fs_msk40=DEC_SAMPLE_RATE;
    dt_msk40=1.0/fs_msk40;
    //df_msk40=(double)fs_msk40/(double)NFFT; //qDebug()<<"df_144="<<df_144;


    for (int i = 0; i<12; i++)
    {//do i=1,12
        double angle=(i)*pi/12.0;//angle=(i-1)*pi/12.0
        pp_msk40[i]=sin(angle);
        rcw_msk40[i]=(1.0-cos(angle))/2.0;//rcw(i)=(1-cos(angle))/2
    }

    for (int i = 0; i<8; i++)
        s_msk40_2s8r[i]=2*s8r[i]-1;

    mplay_da_da_i(cbq,0,6,pp_msk40,6,s_msk40_2s8r[0]);
    mplay_da_da_i(cbq,6,18,pp_msk40,0,s_msk40_2s8r[2]);
    mplay_da_da_i(cbq,18,30,pp_msk40,0,s_msk40_2s8r[4]);
    mplay_da_da_i(cbq,30,42,pp_msk40,0,s_msk40_2s8r[6]);
    mplay_da_da_i(cbi,0,12,pp_msk40,0,s_msk40_2s8r[1]);
    mplay_da_da_i(cbi,12,24,pp_msk40,0,s_msk40_2s8r[3]);
    mplay_da_da_i(cbi,24,36,pp_msk40,0,s_msk40_2s8r[5]);
    mplay_da_da_i(cbi,36,42,pp_msk40,0,s_msk40_2s8r[7]);

    for (int i = 0; i<42; i++)
        cbr_msk40[i]=cbi[i] + cbq[i]*complex_i;   //cb=cmplx(cbi,cbq)

    f_first_msk40=true;
}

void DecoderMs::dftool_msk40(int ntol, double nrxfreq,double df)
{
    ///for all msk40///
    if (last_ntol_msk40==ntol && last_df_msk40==df)
        return; //qDebug()<<"dftool_msk40="<<ntol<<df;

    last_ntol_msk40=ntol;
    last_df_msk40=df;

    double nfhi=2.0*(nrxfreq+500.0);
    double nflo=2.0*(nrxfreq-500.0);
    ihlo_msk40=(nfhi-(double)2.0*ntol)/df;
    ihhi_msk40=(nfhi+(double)2.0*ntol)/df;
    illo_msk40=(nflo-(double)2.0*ntol)/df;
    ilhi_msk40=(nflo+(double)2.0*ntol)/df;
    i2000_msk40=nflo/df;
    i4000_msk40=nfhi/df;
}
void DecoderMs::SetShOpt(bool f)
{
    G_ShOpt = f;
    //qDebug()<<"G_ShOpt"<<G_ShOpt;
}
void DecoderMs::SetSwlOpt(bool f)
{
    G_SwlOpt = f;
    //qDebug()<<"G_SwlOpt="<<G_SwlOpt;
}
/*int DecoderMs::minloc_da_beg_to_end(double*a,int a_beg,int a_end)
{
    double min = a[a_beg];
    int loc = a_beg;
    for (int i = a_beg; i < a_end; i++)
    {
        if (a[i]<min)
        {
            loc = i;
            min = a[i];
        }
    }//qDebug()<<"Rmax-="<<min;   
    return loc;
}*/
/*double DecoderMs::minval_da_beg_to_end(double*a,int a_beg,int a_end)
{
    double min = a[a_beg];
    for (int i = a_beg; i < a_end; i++)
    {
        if (a[i]<min)
        {
            min = a[i];
        }
    }
    return min;
}*/
void DecoderMs::detectmsk40(std::complex<double> *cbig,int n,double s_istart)
{
    //parameter (NSPM=240, NPTS=3*NSPM, MAXSTEPS=7500, NFFT=3*NSPM, MAXCAND=15)
    char ident = '#';////text->'$' codet->'*' short_rpt->'#';
    //double fest = 0.0;

    QString msgreceived;
    bool f_only_one_color = true;

    const int MAXCAND=15;//15; //MAXCAND=15
    const int NSPM=240;    //NSPM=192
    const int NPTS=3*NSPM; //  parameter (NSPM=864, NPTS=3*NSPM)
    const int NFFT=720;    //3*NSPM;//NFFT=3*NSPM=720 //da e 4islo 1.46

    //double cbi[42],cbq[42];
    std::complex<double> cdat[NPTS];                    //!Analytic signal
    std::complex<double> cdat2[NPTS];
    std::complex<double> c[NSPM+10];

    //int nfft_144 = 6000;
    std::complex<double> ctmp[NFFT+10];

    const int MAXSTEPS=7500;    //MAXSTEPS=7500
    double detmet_plus[MAXSTEPS+20];
    double *detmet = &detmet_plus[10];         //real detmet(-2:nstep+3)
    for (int i = -8; i<MAXSTEPS+8; i++)
        detmet[i]=0.0;

    double detmet2_plus[MAXSTEPS+12];
    double *detmet2 = &detmet2_plus[6];
    for (int i = -5; i<MAXSTEPS+5; i++)
        detmet2[i]=0.0;

    double detfer[MAXSTEPS];
    int indices[MAXSTEPS];
    //double locate[MAXSTEPS];//hv
    //real hannwindow(NPTS)

    //double ferr = 0.0;
    double tonespec[NFFT];
    double times[MAXCAND];
    double ferrs[MAXCAND];
    double snrs[MAXCAND];
    //std::complex<double> ccr[NPTS];
    std::complex<double> ccr1[NPTS];
    std::complex<double> ccr2[NPTS];
    double ddr[NPTS];
    int ipeaks[10];
    std::complex<double> bb[6];
    std::complex<double> cfac,cca,ccb;
    //double phase0;
    double softbits[40];
    //int hardbits[40];
    //double lratio[32];
    //double llr[32];
    //char decoded[16];
    //char cw[32];

    //QString allmessages[20];
    int ntol=G_DfTolerance;
    double nrxfreq = 1500.0;

    //int ncorrected = 0;
    //double eyeopening = 0.0;

    first_msk40();
    double df = (double)fs_msk40/(double)NFFT;
    dftool_msk40(ntol,nrxfreq,df);

    int nmessages= 0 ;
    int istp_real = 0;
    int nstepsize=60;  //! 4ms steps
    int nstep=(n-NPTS)/nstepsize; //nstep=(n-NPTS)/nstepsize
    for (int i = 0; i<MAXSTEPS; i++)
        detfer[i]=-999.99;

    for (int istp = 0; istp<nstep; ++istp)//1.68 ++istp ima efect 100ms
    {//do istp=1,nstep
        int ns=0+nstepsize*(istp-0);//ns=1+256*(istp-1)
        int ne=ns+NPTS-0;    //ne=ns+NPTS-1    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
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
        mplay_dca_dca_da(ctmp,0,12,ctmp,0,rcw_msk40,0,1);//ctmp(1:12)=ctmp(1:12)*rcw
        mplay_dca_dca_da(ctmp,NPTS-12,NPTS,ctmp,NPTS-12,rcw_msk40,11,-1);// ctmp(NPTS-11:NPTS)=ctmp(NPTS-11:NPTS)*rcw(12:1:-1)
        f2a->four2a_c2c(ctmp,NFFT,-1,1);//call four2a(ctmp,nfft,1,-1,1)
        mplay_da_absdca_absdca(tonespec,NFFT,ctmp,ctmp);//tonespec=abs(ctmp)**2

        int ihpk = pomAll.maxloc_da_beg_to_end(tonespec,ihlo_msk40,ihhi_msk40);//! high tone search window
        double deltah=-creal( (ctmp[ihpk-1]-ctmp[ihpk+1]) / (2.0*ctmp[ihpk]-ctmp[ihpk-1]-ctmp[ihpk+1]) );//deltah=-real( (ctmp(ihpk-1)-ctmp(ihpk+1)) / (2*ctmp(ihpk)-ctmp(ihpk-1)-ctmp(ihpk+1)) )
        double ah=tonespec[ihpk]; //ah=tonespec(ihpk)

        double ahavp = 0.0;//ahavp=(sum(tonespec,ismask)-ah)/count(ismask)
        for (int i = ihlo_msk40; i<ihhi_msk40; i++)
            ahavp+=tonespec[i];
        ahavp = (ahavp-ah)/(double)(ihhi_msk40-ihlo_msk40);
        double trath=ah/(ahavp+0.01);//trath=ah/(ahavp+0.01)


        int ilpk = pomAll.maxloc_da_end_to_beg(tonespec,illo_msk40,ilhi_msk40);//! window for low tone
        double deltal=-creal( (ctmp[ilpk-1]-ctmp[ilpk+1]) / (2.0*ctmp[ilpk]-ctmp[ilpk-1]-ctmp[ilpk+1]) );//deltal=-real( (ctmp(ilpk-1)-ctmp(ilpk+1)) / (2*ctmp(ilpk)-ctmp(ilpk-1)-ctmp(ilpk+1)) )
        double al=tonespec[ilpk];

        double alavp = 0.0;//alavp=(sum(tonespec,ismask)-al)/count(ismask)
        for (int i = illo_msk40; i<ilhi_msk40; i++)
            alavp+=tonespec[i];
        alavp = (alavp-al)/(double)(ilhi_msk40-illo_msk40);
        double tratl=al/(alavp+0.01);//tratl=al/(alavp+0.01)

        //double fdiff=(ihpk+deltah-ilpk-deltal)*df_144;//fdiff=(ihpk+deltah-ilpk-deltal)*df

        double ferrh=((double)ihpk+deltah-(double)i4000_msk40)*df/2.0;//ferrh=(ihpk+deltah-i4000)*df/2.0
        double ferrl=((double)ilpk+deltal-(double)i2000_msk40)*df/2.0;//ferrl=(ilpk+deltal-i2000)*df/2.0
        //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        //if( abs(fdiff-2000) <= 25.0 )  //!    if( abs(fdiff-2000) .le. 25.0 ) then
        //{
        double ferr;
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

        if ( detmet[il] < 4.1 ) break; ///*tested 4.1<-hv 4.2*/ if( (detmet(il) .lt. 4.2) ) exit
        if ( fabs(detfer[il]) <= (double)(ntol) ) //if( abs(detfer(il)) .le. 100.0 ) then
        {
            //inportant no(il-1)<-out of array HV tested->(il-0) times(ndet)=((il-1)*256+NPTS/2)*dt
            times[ndet]=(double)((il-0)*nstepsize+NPTS/2.0)*dt_msk40;//times(ndet)=((il-1)*216+NSPM/2)*dt
            ferrs[ndet]=detfer[il];
            //snrs[ndet]=10.0*log10(detmet[il])/2.0-5.0; //snrs(ndet)=10.0*log10(detmet(il))/2-5.0 !/2 because detmet is a 4th order moment
            snrs[ndet]=12.0*log10(detmet[il]-1.0)/2.0-8.0;    //snrs(ndet)=12.0*log10(detmet(il))/2-9.0
            ndet++;
        }

        if (ss_msk144ms)
            duration[ndet]=MskPingDuration(detmet_dur,istp_real,il,4.1,nstepsize,(NPTS/2),dt_msk40);

        //for (int i = il-3; i<il+3+1; i++)// -3 +4 lost good locations
        //for (int i = il-3; i<il+4; i++)
        //detmet[i]=0.0;  //detmet(il-3:il+3)=0.0

        //for (int i = il-3; i<il+3+1; i++)
        //detmet[i]=0.0;//detmet(max(1,il-3):min(nstep,il+3))=0.0
        //for (int i = il-1; i<il+1+1; i++)
        //detmet[i]=0.0;

        detmet[il]=0.0;
        //for (int i = il-3; i<il+4; i++)
        //locate[i]=0.0;  //detmet(il-3:il+3)=0.0
    }
    delete [] detmet_dur;
    //qDebug()<<"ndet40="<<ndet;

    if ( ndet < 3 )//for Tropo/ES if( ndet .lt. 3 ) then
    {
        //qDebug()<<"ndet="<<ndet;
        for (int ip = 0; ip<MAXCAND-ndet; ip++)
        {//do ip=1,MAXCAND-ndet //! Find candidates
            int il = pomAll.maxloc_da_beg_to_end(detmet2,0,istp_real);//iloc=maxloc(detmet2(1:nstep))

            if ( detmet2[il] < 20.0 ) break; //if( (detmet2(il) .lt. 20.0) ) exit
            if ( fabs(detfer[il]) <= (double)(ntol) )//if( abs(detfer(il)) .le. ntol ) then
            {
                //ndet=ndet+1
                times[ndet]=(double)((il-0)*nstepsize+NSPM/2)*dt_msk40;//times(ndet)=((il-1)*216+NSPM/2)*dt
                ferrs[ndet]=detfer[il];//ferrs(ndet)=detfer(il)
                snrs[ndet]=12.0*log10(detmet2[il])/2.0-9.0; //snrs(ndet)=12.0*log10(detmet2(il))/2-9.0
                ndet++;
            }

            if (ss_msk144ms)
                duration[ndet]=MskPingDuration(detmet_dur2,istp_real,il,20.0,nstepsize,(NSPM/2),dt_msk40);

            //detmet2(max(1,il-1):min(nstep,il+1))=0.0
            for (int i = il-1; i<il+1+1; i++)
                detmet2[i]=0.0;
            //detmet2[il]=0.0;
        }
    }
    delete [] detmet_dur2;

    if (ndet>0)
        pomAll.indexx_msk(times,ndet-1,indices);//HV sprt pings in one scan by time

    //qDebug()<<"---------ndet istp nstep="<<ndet<<istp<<nstep;
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE

    for (int iip = 0; iip < ndet; iip++)
    {//do ip=1,ndet
        int ip = indices[iip];//indices[MAXSTEPS]->1700
        int imid=(int)((double)times[ip]*fs_msk40);
        //int a_beg = imid-NPTS/2;
        //if( a_beg < 0 ) a_beg=0;
        //int a_end = imid+NPTS/2;
        //if( a_end > n ) a_end=n;
        if ( imid < NPTS/2 ) imid=NPTS/2;//if( imid .lt. NPTS/2 ) imid=NPTS/2
        if ( imid > n-NPTS/2 ) imid=n-NPTS/2;//if( imid .gt. n-NPTS/2 ) imid=n-NPTS/2

        double t0=times[ip] + dt_msk40*(s_istart);
        //qDebug()<<"times="<<t0+ dt_msk_144*(s_istart);
        int c_dd = 0;
        for (int i = imid-NPTS/2; i<imid+NPTS/2; i++)//cdat=cbig(imid-NPTS/2+1:imid+NPTS/2)
        {
            cdat[c_dd]=cbig[i];
            c_dd++;
        }

        double ferr=ferrs[ip];
        //nsnr=int(snrs[ip]);
        //double nsnr=2.0*int(snrs[ip]/2.0);
        double xsnr=snrs[ip];//xsnr=snrs(ip)
        double nsnr=(int)(snrs[ip]);// nsnr=nint(snrs(ip))
        if ( nsnr < -5.0 ) nsnr=-5.0;
        if ( nsnr > 25.0 ) nsnr=25.0;
        double p_duration = 0.0;
        if (ss_msk144ms)
            p_duration = duration[ip];

        //! remove coarse freq error - should now be within a few Hz
        tweak1(cdat,NPTS,-(nrxfreq+ferr),cdat);//call tweak1(cdat,NPTS,-(1500+ferr),cdat)
        //qDebug()<<"ferr="<<ferr;

        //! attempt frame synchronization
        //! correlate with sync word waveforms
        //zero_double_comp_beg_end(ccr,0,NPTS);//cc=0
        pomAll.zero_double_comp_beg_end(ccr1,0,NPTS);    //cc1=0
        pomAll.zero_double_comp_beg_end(ccr2,0,NPTS);    //cc2=0

        for (int i = 0; i<NPTS-(40*6+(41+0)); i++)
        {//do i=1,NPTS-(40*6+41)
            ccr1[i]=pomAll.sum_dca_mplay_conj_dca(cdat,i,i+42,cbr_msk40);//cc1[i]=sum(cdat(i:i+41)*conjg(cb))
            ccr2[i]=pomAll.sum_dca_mplay_conj_dca(cdat,i+40*6,i+40*6+42,cbr_msk40); //ccr2(i)=sum(cdat(i+40*6:i+40*6+41)*conjg(cbr))
            //qDebug()<<"IIIIIIIIIIIIII="<<i; i=2214
        }
        //sum_dca_dca_dca(ccr,NPTS,ccr1,ccr2);//cc=cc1+cc2
        mplay_da_absdca_absdca(ddr,NPTS,ccr1,ccr2);//dd=abs(cc1)*abs(cc2)
        //crmax=maxval(abs(ccr))

        //! Find 6 largest peaks
        for (int ipk = 0; ipk<6; ipk++)
        {//do ipk=1,6
            //int ic1 = maxloc_absdca_beg_to_end(ccr,0,NPTS);//iloc=maxloc(abs(cc))
            //iloc=maxloc(dd)
            int ic1 = pomAll.maxloc_da_beg_to_end(ddr,0,NPTS);//ic2=iloc(1)
            //ipeaks[ipk]=ic2;
            ipeaks[ipk]=ic1;//ipeaks(ipk)=ic1
            pomAll.zero_double_beg_end(ddr,(int)fmax(0,ic1-7),(int)fmin(NPTS-40*6-(41+1),ic1+7+0));
            //ddr(max(1,ic1-7):min(NPTS-40*6-41,ic1+7))=0.0
            //zero_double_comp_beg_end(ccr,(int)fmax(0,ic1-7),(int)fmin(NPTS-32*6-(41+1),ic1+7+0)); //ccr(max(1,ic1-7):min(NPTS-32*6-41,ic1+7))=0.0
        }

        //int ndither= 0;//-99;
        for (int ipk = 0; ipk<4; ipk++)
        {//do ipk=1,4

            //! we want ic to be the index of the first sample of the frame
            int ic0=ipeaks[ipk];//ic0=ipeaks(ipk)

            //! fine adjustment of sync index
            //! bb lag used to place the sampling index at the center of the eye
            for (int i = 0; i<6; i++)
            {//do i=1,6   //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
                int cd_b = ic0+i;//hv tested ok -0 cd_b = ic0+i-0; e po doble HV
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
            /*if ( ic0+(11+1)+NSPM < NPTS )
                qDebug()<<"V1=";
            else
                qDebug()<<"V2=";*/

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
            {//do id=1,3     ! slicer dither.
                int is = 0;
                //if ( id == 0 ) is=0;
                if ( id == 1 ) is=-1;
                if ( id == 2 ) is=1;

                /*if ( id == 3 ) is=-2;
                if ( id == 4 ) is=2;
                if ( id == 5 ) is=-3;
                if ( id == 6 ) is=3;*/

                //! Adjust frame index to place peak of bb at desired lag
                int ic=ic0+ibb+is;//ic=ic0+ibb+is
                //qDebug()<<"ic0 ibb is="<<ic0<<ibb<<is<<ic;
                if ( ic < 0 ) ic=ic+NSPM; //if( ic .lt. 1 ) ic=ic+NSPM

                //! Estimate fine frequency error.
                //! Should a larger separation be used when frames are averaged?
                double ferr2=0.0;
                cca=pomAll.sum_dca_mplay_conj_dca(cdat,ic,ic+42,cbr_msk40);//cca=sum(cdat(ic:ic+41)*conjg(cb))
                if ( ic+40*6+42 < NPTS ) //if( ic+40*6+41 .le. NPTS ) then
                {
                    ccb=pomAll.sum_dca_mplay_conj_dca(cdat,ic+40*6,ic+40*6+42,cbr_msk40);//ccb=sum(cdat(ic+32*6:ic+32*6+41)*conjg(cb))
                    cfac=ccb*conj(cca);//cfac=ccb*conjg(cca)
                    ferr2=atan2(cimag(cfac),creal(cfac))/(twopi*40*6*dt_msk40);//ferr2=atan2(imag(cfac),real(cfac))/(twopi*32*6*dt)
                    //qDebug()<<"V1=";
                }
                else
                {
                    ccb=pomAll.sum_dca_mplay_conj_dca(cdat,ic-40*6,ic-40*6+42,cbr_msk40);//ccb=sum(cdat(ic-32*6:ic-32*6+41)*conjg(cb))
                    cfac=ccb*conj(cca);//cfac=cca*conjg(ccb)
                    ferr2=atan2(cimag(cfac),creal(cfac))/(twopi*40*6*dt_msk40);//ferr2=atan2(imag(cfac),real(cfac))/(twopi*32*6*dt)
                    //qDebug()<<"V2=";
                }

                //! Final estimate of the carrier frequency - returned to the calling program
                //qDebug()<<"ferr ferr2="<<ferr<<ferr2;
                //fest=(int)(nrxfreq+ferr+ferr2);//fest=1500+ferr+ferr2

                for (int idf = 0; idf < 3; idf++)
                {//do idf=0,2    ! frequency jitter
                    double deltaf = 0.0;
                    if ( idf == 0 )
                        deltaf=0.0;
                    else if ( fmod(idf,2) == 0 )
                        deltaf=2.5*(double)idf;// deltaf=2.5*idf
                    else
                        deltaf=-2.5*((double)idf+1.0);//deltaf=-2.5*(idf+1)
                    //qDebug()<<"deltaf="<<deltaf;

                    //! Remove fine frequency error
                    tweak1(cdat,NPTS,-(ferr2+deltaf),cdat2);// call tweak1(cdat,NPTS,-ferr2,cdat2)
                    //qDebug()<<"-ferr2="<<-ferr2;

                    //fest=(int)(nrxfreq+ferr+(ferr2+deltaf));

                    //! place the beginning of frame at index NSPM+1
                    pomAll.cshift1(cdat2,NPTS,ic-(NSPM+0));//cdat2=cshift(cdat2,ic-(NSPM+1))
                    //qDebug()<<"cshift="<<ic-(NSPM+1);

                    for (int iav = 0; iav<4; iav++)//! Frame averaging patterns
                    {//do iav=1,4 ! try each of 7 averaging patterns, hope that one works

                        if ( iav == 0 )
                        {
                            //c=cdat2(NSPM+1:2*NSPM)
                            copy_dca_or_sum_max3dca(c,NSPM,cdat2,NSPM);
                            //qDebug()<<"INAV0";
                        }
                        else if ( iav == 1 )
                        {
                            //c=cdat2(1:NSPM)+cdat2(NSPM+1:2*NSPM)
                            copy_dca_or_sum_max3dca(c,NSPM,cdat2,0,cdat2,NSPM);
                            //qDebug()<<"INAV5";
                        }
                        else if ( iav == 2 )
                        {
                            //c=cdat2(NSPM+1:2*NSPM)+cdat2(2*NSPM+1:3*NSPM)
                            copy_dca_or_sum_max3dca(c,NSPM,cdat2,NSPM,cdat2,2*NSPM);
                            //qDebug()<<"INAV6";
                        }
                        else if ( iav == 3 )
                        {
                            //c=cdat2(1:NSPM)+cdat2(NSPM+1:2*NSPM)+cdat2(2*NSPM+1:3*NSPM)
                            copy_dca_or_sum_max3dca(c,NSPM,cdat2,0,cdat2,NSPM,cdat2,2*NSPM);
                            //qDebug()<<"INAV7";
                        }

                        int nsuccess = 0;
                        msk40decodeframe(c,softbits,xsnr,msgreceived,nsuccess,ident,true);//hv
                        if (nsuccess > 0)
                        {
                            double fest=(int)(nrxfreq+ferr+ferr2+deltaf);
                            int df_hv = fest-nrxfreq;

                            if (f_only_one_color)
                            {
                                f_only_one_color = false;
                                SetBackColor();
                            }

                            int ncorrected = 0;
                            double eyeopening = 0.0;

                            msk144signalquality(c,nsnr,fest,t0,softbits,msgreceived,s_HisCall,ncorrected,
                                                eyeopening,s_trained_msk144,s_pcoeffs_msk144,false);// false no calc pcoeffs

                            // not used but for any case
                            //if (msk144_ft8_cont_msg)//1.69
                            //msgreceived = TGenMsk->fix_contest_msg(My_Grid_Loc,msgreceived);//for " R " in msg 1.31


                            msgreceived = RemBegEndWSpaces(msgreceived);//for msk40

                            QStringList list;
                            list <<s_time<<QString("%1").arg(t0,0,'f',1)<<""<<
                            QString("%1").arg((int)nsnr)<<""<<QString("%1").arg((int)df_hv)
                            <<msgreceived<<QString("%1").arg(iav+1)  //navg +1 za tuk  no ne 0 hv 1.31
                            <<QString("%1").arg(ncorrected)<<QString("%1").arg(eyeopening,0,'f',1)
                            <<QString(ident)+" "+QString("%1").arg((int)fest);

                            //qDebug()<<"Msk40msgreceived="<<msgreceived<<"durat=="<<p_duration;
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

                            return;
                        }
                    }        //! frame averaging loop
                }            //! frequency dithering loop
            }                //! slicer dither loop
        }                    //! time-sync correlation-peak loop
    }                        //! candidate loop
}

bool DecoderMs::check_hash_msk40(int hash_in,int rpt,QString &msg)
{
    bool res= false;
    //for (int i = 0; i < HASH_CALLS_COUNT; i++)
    //qDebug()<<"hashs"<<i<<hash_msk40_calls[i].calls<<hash_msk40_calls[i].hash;

    for (int i = 0; i < 6; i++) //6 standart
    {
        if (hash_in == hash_msk40_calls[i].hash)
        {
            res = true;
            msg = "<"+hash_msk40_calls[i].calls+"> "+rpt_msk40[rpt];
            break;
        }
    }
    return res;
}
bool DecoderMs::check_hash_msk40_swl(int hash_in,int rpt,QString &msg)
{
    bool res= false;
    //for (int i = 0; i < HASH_CALLS_COUNT; i++)
    //qDebug()<<"hashs"<<i<<hash_msk40_calls[i].calls<<hash_msk40_calls[i].hash;

    for (int i = 6; i < HASH_CALLS_COUNT; i++)// swl
    {
        if (hash_in == hash_msk40_calls[i].hash)
        {
            res = true;
            msg = "<"+hash_msk40_calls[i].calls+"> "+rpt_msk40[rpt];
            break;
        }
    }
    return res;
}
//2.00 HV need for QString DecoderMs::FindBaseFullCallRemAllSlash
/*bool DecoderMs::isValidCallsign(QString callsign)
{
    bool valid = true;
    QRegExp rx("(\\d+)");

    if (callsign.count()< 3) // A callsign must be longer than two characters
        valid = false;
    else
    {
        if ((callsign.at(0) == 'Q') || (callsign.at(0) == '0')) // Do not accept callsigns begining with Q or 0
            valid = false;
        else
        {
            if (callsign.contains("//")) // Do not accept callsigns with //
                valid = false;
            else
                if ((callsign.at(0) == '/') || (callsign.at(callsign.count() - 1) == '/')) // Do not accept callsigns with / in the beginning or at the end
                    valid = false;

                else
                    //if (DigitPresent(callsign) == false)
                    if (!callsign.contains(rx))
                        valid = false;
                    else
                    {
                        foreach (QChar Char , callsign)
                        {
                            if (((Char >= 'A') && (Char <= 'Z')) ||
                                    ((Char >= 'a') && (Char <= 'z')) ||
                                    ((Char >= '0') && (Char <= '9')) ||
                                    (Char == '/'))
                            {}
                            else
                                valid = false;
                        }
                    }
        }
    }
    return valid;
}
//2.00
QString DecoderMs::FindBaseFullCallRemAllSlash(QString str)// nay be need flag only 6
{
    QString res = str;//2.00 HV need tests ???
    if (str.contains("/"))
    {
        QStringList list_str = str.split("/");

        if (list_str.count() >= 3) //more then 2 shlashes SP9/LZ2HV/QRP
            res = list_str.at(1);
        else
        {// only 2 count
            bool call_0 = false;
            bool call_1 = false;

            if (isValidCallsign(list_str.at(0)))//if (!is_pfx(list_str.at(0)) && isValidCallsign(list_str.at(0)))
                call_0 = true;
            if (isValidCallsign(list_str.at(1)))//if (!is_pfx(list_str.at(1)) && isValidCallsign(list_str.at(1)))
                call_1 = true;

            if (call_0 && call_1)    //bin=11 int=3
                res = list_str.at(1);//res = list_str.at(1).mid(0,6);
            else if (call_0 && !call_1) //bin=10 int=2
                res = list_str.at(0);//res = list_str.at(0).mid(0,6);
            else if (!call_0 && call_1) //bin=01 int=1
                res = list_str.at(1);//res = list_str.at(1).mid(0,6);
            else
                res = list_str.at(0);//res = list_str.at(0).mid(0,6);   //bin=00 int=0
        }
    }
    return res;
}*/
void DecoderMs::hash_msk40_all_calls(int id,QString calls)
{
    QStringList list = calls.split(" ");// lista vinagi ima count 2 1.31
    if (list.count()<2) return;//2.00
    //qDebug()<<"list="<<list.count();

    if (list.at(0).isEmpty() || list.at(1).isEmpty()) //1.31 ako edin lipswa false inportant for static from 0 to 6
    {
        hash_msk40_calls[id].hash = -101;//ihash = TGenMsk->hash(hashmsg,22,ihash);//hash_msk40_rpt[i].hash_calls[j].hash=(ihash & 4095);
        hash_msk40_calls[id].calls="FALSE"; //"NO CALLS"
    }
    else
    {
        //QString c1 = FindBaseFullCallRemAllSlash(list.at(0));
        //QString c2 = FindBaseFullCallRemAllSlash(list.at(1));
        //c1 = c1+" "+c2;
        hash_msk40_calls[id].hash = TGenMsk->hash_msk40(calls);//ihash = TGenMsk->hash(hashmsg,22,ihash);//hash_msk40_rpt[i].hash_calls[j].hash=(ihash & 4095);
        hash_msk40_calls[id].calls=calls;
        //qDebug()<<"calssssssssssssl="<<hash_msk40_calls[id].calls;
    }
    //qDebug()<<"calssssssssssssl="<<id<<calls<<hash_msk40_calls[id].hash;
}
void DecoderMs::hash_msk40_swl()
{
    int c = 6; //first 6 reserved
    for (int i = 0; i<RECENT_CALLS_COU; i++)
    {
        for (int j = i+1; j<RECENT_CALLS_COU; j++)
        {
            if (!recent_calls[i].isEmpty() && !recent_calls[j].isEmpty())
            {
                hash_msk40_all_calls(c,recent_calls[i]+" "+recent_calls[j]);
                c++;
                hash_msk40_all_calls(c,recent_calls[j]+" "+recent_calls[i]);
                c++;
            }
        }
    }
    /*qDebug()<<"CCCC=================================================="<<c;
    for (int i = 0; i<c; i++)
         qDebug()<<"all="<<hash_msk40_calls[i].calls<<hash_msk40_calls[i].hash<<i;*/
}
void DecoderMs::update_recent_calls(QString call)
{
    //qDebug()<<"update_recent_calls="<<call;
    for (int i = 0; i<RECENT_CALLS_COU; i++)
    {
        //qDebug()<<"CCCC="<<recent_calls[i]<<call<<RECENT_CALLS_COU;
        if (recent_calls[i]==call)
            return;
    }

    for (int i = RECENT_CALLS_COU-2; i>=0; i--)
        recent_calls[i+1]=recent_calls[i];
    recent_calls[0]=call;

    hash_msk40_swl();

    /*QString s;
    for (int i = 0; i<RECENT_CALLS_COU; i++)
    s.append(recent_calls[i]+",");
    qDebug()<<"Calls="<<s;*/
}
bool DecoderMs::update_recent_shmsgs(QString msg)
{
    //bool res = false;
    for (int i = 0; i<RECENT_SHMSGS_COU; i++)
    {
        //qDebug()<<"CCCC="<<recent_calls[i]<<call<<RECENT_CALLS_COU;
        if (recent_shmsgs[i]==msg)
        {
            //res = true;
            return true;
        }
    }

    for (int i = RECENT_SHMSGS_COU-2; i>=0; i--)
        recent_shmsgs[i+1]=recent_shmsgs[i];
    recent_shmsgs[0]=msg;

    /*QString s;
    for (int i = 0; i<RECENT_SHMSGS_COU; i++)
    s.append(recent_shmsgs[i]+",");
    qDebug()<<"Shmsgs="<<s;*/

    return false;
}
/* //msk40 za po natatak ako ima oplakwane 1.31
void DecoderMs::ResetCalsHashFileOpen()
{
	hash_msk40_calls[4].hash = -101;
    hash_msk40_calls[4].calls="FALSE";
    hash_msk40_calls[5].hash = -101;
    hash_msk40_calls[5].calls="FALSE";
	//hash_msk40_all_calls(4," ");
    //hash_msk40_all_calls(5," ");
}
*/
void DecoderMs::SetCalsHashFileOpen(QString str)
{
    if (str.isEmpty())
        return;

    str.replace("_SLASH_","/");

    QStringList lst = str.split("_");   //qDebug()<<lst.count();

    if (lst.count()<2)
        return;

    hash_msk40_all_calls(4,lst.at(0)+" "+lst.at(1));//obraten hash_msk40_all_rpt
    hash_msk40_all_calls(5,lst.at(1)+" "+lst.at(0));//prav    hash_msk40_all_rpt

    //recent_calls[4]=lst.at(0);
    //recent_calls[5]=lst.at(1);

}
void DecoderMs::SetCalsHash(QStringList l)
{
    if (l.count()<5) return;

    hash_msk40_all_calls(0,l.at(0)+" "+l.at(1));//prav    hash_msk40_all_rpt
    hash_msk40_all_calls(1,l.at(1)+" "+l.at(0));//obraten hash_msk40_all_rpt
    hash_msk40_all_calls(2,l.at(2)+" "+l.at(3));//prav    hash_msk40_all_rpt
    hash_msk40_all_calls(3,l.at(3)+" "+l.at(2));//obraten hash_msk40_all_rpt

    //recent_calls[0]=l.at(0);
    //recent_calls[1]=l.at(1);
    //recent_calls[2]=l.at(2);
    //recent_calls[3]=l.at(3);

    //for (int i = 0; i<36; i++)
    //qDebug()<<"all="<<hash_msk40_calls[i].calls<<hash_msk40_calls[i].hash<<i;
    
    //HisGridLoc = l.at(4);//2.53 for jt65 q65 
    if (HisGridLoc != l.at(4))
    {
    	HisGridLoc = l.at(4);//2.53 for jt65 q65
    	DecQ65->SetStHisCallGrid(s_HisCall,HisGridLoc);
   	}    	
    if (s_HisCall != l.at(1))
    { 
        s_HisCall  = l.at(1);
        //no need static dec hash array TGenFt8->save_hash_call_my_his_r1_r2(s_HisCall,1);//0=my 1=his 2=r1 3=r2
        TGenMsk->save_hash_call_my_his_r1_r2(s_HisCall,1);
        DecFt8_0->SetStHisCall(s_HisCall);
        DecFt4_0->SetStHisCall(s_HisCall);
        DecQ65->SetStHisCallGrid(s_HisCall,HisGridLoc);
    }     	   
    if (s_R1HisCall != l.at(2))
    {
        s_R1HisCall = l.at(2);//for fr8 MA QSO Foxs
        //no need static dec hash array TGenFt8->save_hash_call_my_his_r1_r2(s_R1HisCall,2);//0=my 1=his 2=r1 3=r2
        TGenMsk->save_hash_call_my_his_r1_r2(s_R1HisCall,2);
    }
    if (s_R2HisCall != l.at(3))
    {
        s_R2HisCall = l.at(3);//for fr8 MA QSO Foxs
        //no need static dec hash array TGenFt8->save_hash_call_my_his_r1_r2(s_R2HisCall,3);//0=my 1=his 2=r1 3=r2
        TGenMsk->save_hash_call_my_his_r1_r2(s_R2HisCall,3);
    }
}
void DecoderMs::msk40decodeframe_p(std::complex<double> *c,double *softbits,double xsnr, QString &msgreceived, int &nsuccess,char &ident,double phase0)//hv
{
    int NSPM = 240;
    std::complex<double> cfac;
    //double softbits[40];
    int hardbits[40];
    double llr[32];
    char decoded[16];
    //char cw[32];
    ident = '#';

    //! Remove phase error - want constellation such that sample points lie on re,im axes
    cfac=(cos(phase0)+sin(phase0)*complex_i);//cfac=cmplx(cos(phase0),sin(phase0))
    for (int i = 0; i<NSPM; i++)
        c[i]=c[i]*conj(cfac);//c=c*conjg(cfac)

    //if( nmatchedfilter .eq. 0 ) then
    //! sample to get softsamples
    //do i=1,72
    //softbits(2*i-1)=imag(c(1+(i-1)*12))
    //softbits(2*i)=real(c(7+(i-1)*12))
    //enddo
    //else
    //! matched filter -
    //! how much mismatch does the RX/TX/analytic filter cause?, how rig (pair) dependent is this loss?
    //softbits(1)=sum(imag(c(1:6))*pp(7:12))+sum(imag(c(864-5:864))*pp(1:6))
    softbits[0] = 0.0;
    softbits[1] = 0.0;
    //zero_double_beg_end(softbits,0,144);
    //softbits(1)=sum(imag(c(1:6))*pp(7:12))+sum(imag(c(NSPM-5:NSPM))*pp(1:6))
    for (int i = 0; i<6; i++)
    {
        softbits[0] += cimag(c[i])*pp_msk40[i+6];
        softbits[0] += cimag(c[i+(NSPM-5-1)])*pp_msk40[i];
    }
    //softbits(2)=sum(real(c(1:12))*pp)
    for (int i = 0; i<12; i++)
        softbits[1] += creal(c[i])*pp_msk40[i];

    zero_int_beg_end(hardbits,0,40);//v1.33 0 here important hardbits=0

    for (int i = 1; i<20; i++)
    {//do i=2,20
        //softbits(2*i-1)=sum(imag(c(1+(i-1)*12-6:1+(i-1)*12+5))*pp)
        double sum01 = 0.0;
        for (int j = 0; j<12; j++)
            sum01 += cimag(c[((i)*12-6)+j])*pp_msk40[j];
        softbits[2*i-0]=sum01;

        //softbits(2*i)=sum(real(c(7+(i-1)*12-6:7+(i-1)*12+5))*pp)
        sum01 = 0.0;
        for (int j = 0; j<12; j++)
            sum01 += creal(c[(6+(i)*12-6)+j])*pp_msk40[j];
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

    //zero_int_beg_end(hardbits,0,40);// 0 here important hardbits=0
    /*
    for (int i = 0; i<40; i++)
    {//do i=1,40
        if ( softbits[i] >= 0.0 )// if( softbits(i) .ge. 0.0 ) then
            hardbits[i]=1;
    }
    */
    //nbadsync1=(8-sum( (2*hardbits(1:8)-1)*s8r ) )/2
    int nbadsync1 = 0;
    for (int i = 0; i<8; i++)
        nbadsync1+=(2*hardbits[i]-1)*s_msk40_2s8r[i];
    nbadsync1 = (8-nbadsync1)/2;

    //nbadsync2=(8-sum( (2*hardbits(1+56:8+56)-1)*s8 ) )/2
    //int nbadsync2 = 0;
    //for (int i = 0; i<8; i++)
    //nbadsync2+=((2*hardbits[i+57-1]-1)*s_2s8r[i]);
    //nbadsync2 = (8-nbadsync2)/2;

    int nbadsync=nbadsync1;
    //qDebug()<<"NEEEEnbadsync="<<nbadsync;
    if ( nbadsync > 3 ) return;//if( nbadsync .gt. 3 ) cycle
    //qDebug()<<"nbadsync="<<nbadsync;

    //! normalize the softsymbols before submitting to decoder
    double sav = 0.0;
    double s2av = 0.0;
    for (int i = 0; i<40; i++)
    {
        sav+=softbits[i];//sav=sum(softbits)/40
        s2av+=softbits[i]*softbits[i];//s2av=sum(softbits*softbits)/40
    }
    sav = sav/40.0;
    s2av = s2av/40.0;

    double ssig=sqrt(s2av-sav*sav);//ssig=sqrt(s2av-sav*sav)
    if (ssig==0.0)//no devide by zero
        ssig=1.0;
    for (int i = 0; i<40; i++)//softbits=softbits/ssig
        softbits[i]=softbits[i]/ssig;

    double sigma=0.75;//sigma=0.75
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE
    //if (xsnr<1.5) sigma=1.1 - 0.0875*(xsnr+4.0);  //if(xsnr.lt.1.5) sigma=1.1 - 0.0875*(xsnr+4.0)

    //if (xsnr<0.0) sigma=0.0 - 0.0875*xsnr; //if(xsnr.lt.0.0) sigma=0.75-0.0875*xsnr
    if (xsnr<0.0) sigma=0.75-0.11*xsnr;      //if(xsnr.lt.0.0) sigma=0.75-0.11*xsnr

    //copy_double_ar_ainc(lratio,0,1,softbits,8,9+47);//lratio(1:48)=softbits(9:9+47)
    //copy_double_ar_ainc(lratio,48,1,softbits,64,65+80-1);//lratio(49:128)=softbits(65:65+80-1)
    //for (int i = 0; i<128; i++)//lratio=exp(2.0*lratio/(sigma*sigma))
    //lratio[i]=exp(2.0*lratio[i]/(sigma*sigma));

    //lratio(1:32)=exp(2.0*softbits(9:40)/(sigma*sigma))
    for (int i = 0; i<32; i++)//lratio=exp(2.0*lratio/(sigma*sigma))
    {
        //lratio[i]=exp(2.0*softbits[i+8]/(sigma*sigma));//! Use this for Radford Neal's routines
        llr[i]=2.0*softbits[i+8]/(sigma*sigma);//llr(1:32)=2.0*softbits(9:40)/(sigma*sigma)  ! Use log likelihood for bpdecode40
    }

    //copy_double_ar_ainc(unscrambledsoftbits,0,2,lratio,0,64);//unscrambledsoftbits(1:127:2)=lratio(1:64)
    //copy_double_ar_ainc(unscrambledsoftbits,1,2,lratio,64,128);//unscrambledsoftbits(2:128:2)=lratio(65:128)

    int max_iterations=5;//max_iterations=5
    //int max_dither=1; // max_dither=50  sega e 1
    int niterations= -1;
    //int ndither= 0;//-99;

    //call ldpc_decode(unscrambledsoftbits, decoded, max_iterations, niterations, max_dither, ndither)
    //TGenMsk->ldpc_decode(unscrambledsoftbits, decoded, max_iterations, niterations, max_dither, ndither);
    //TGenMsk->ldpc_decode(lratio, decoded, max_iterations, niterations, max_dither, ndither);
    TGenMsk->bpdecode40(llr,max_iterations,decoded,niterations);

    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if ( niterations >= 0 )
    {
        char cw[32];
        //TGenMsk->ldpc_encode(decoded,cw);
        TGenMsk->encode_msk40(decoded,cw);
        int nhammd=0;
        double cord=0.0;

        for (int i = 0; i<32; i++)
        {//do i=1,32
            if ( cw[i] != hardbits[i+8] )
            {
                nhammd++;                   //nhammd=nhammd+1
                cord+=fabs(softbits[i+8]);  // imah gre6ka
            }
        }

        int imsg=0;
        for (int i = 0; i<16; i++)
        {//do i=1,16
            //imsg=ishft(imsg,1)+iand(1,decoded(17-i))
            imsg=(imsg << 1)+(1 & decoded[15-i]);
        }
        int nrxrpt=(imsg & 15);
        int nrxhash=(imsg-nrxrpt)/16;
        //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        //if ( nhammd <= 5 && cord < 1.7 /*&& nrxhash == nhashes[nrxrpt]*/ )
        //if(nhammd.le.4 .and. cord .lt. 0.65 .and. nrxhash.eq.ihash .and. nrxrpt.ge.7) then
        if (nhammd<=4 && cord < 0.65 && nrxrpt>=7 /*&& nrxhash.eq.ihash*/)
        {
            //qDebug()<<nhammd<<cord<<xsnr;
            if (check_hash_msk40(nrxhash ,nrxrpt, msgreceived))
            {
                nsuccess = 1;
                return;
            }
            //else if (G_SwlOpt && nhammd<=2 && cord<0.40 && xsnr>-3.0)
            //elseif(bswl .and. nhammd.le.4 .and. cord.lt.0.65 .and. nrxrpt.ge.7 ) then
            else if (G_SwlOpt /*&& nrxrpt>=7 && nhammd<=4 && cord<0.65*/)
            {
                //qDebug()<<nhammd<<cord<<xsnr;
                if (check_hash_msk40_swl(nrxhash ,nrxrpt, msgreceived))
                {
                    nsuccess = 2;
                    return;
                }
                //if(nsuccess==0 && nhammd<=2 && cord<0.40 && xsnr>0.0 )
                if (nsuccess==0)
                {
                    //qDebug()<<"TT="<<nhammd<<cord<<xsnr<<msgreceived;
                    nsuccess = 3;
                    //"<",nrxhash,">",rpt(nrxrpt)
                    msgreceived = "<UNKNOWN CALLS "+QString("%1").arg(nrxhash)+"> "+rpt_msk40[nrxrpt];
                }
            }
        }
    }
}
void DecoderMs::msk40decodeframe(std::complex<double> *c,double *softbits,double xsnr,QString &msgreceived,int &nsuccess,char &ident,bool f_phase)
{
    std::complex<double> cca;
    double phase0;
    nsuccess=0;

    //! Estimate final frequency error and carrier phase.
    cca=pomAll.sum_dca_mplay_conj_dca(c,0,42,cbr_msk40);//cca=sum(c(1:1+41)*conjg(cbr))
    phase0=atan2(cimag(cca),creal(cca));//phase0=atan2(imag(cca),real(cca)) phase0=atan2(imag(cca+ccb),real(cca+ccb))

    if (f_phase)
    {
        for (int ipha = 0; ipha<3; ipha++)
        {//do ipha=1,3
            if ( ipha==1 ) phase0=phase0-30.0*pi/180.0;//if( ipha.eq.2 ) phase0=phase0-30*pi/180.0
            if ( ipha==2 ) phase0=phase0+30.0*pi/180.0;//if( ipha.eq.3 ) phase0=phase0+30*pi/180.0

            msk40decodeframe_p(c,softbits,xsnr,msgreceived,nsuccess,ident,phase0);

            if ( nsuccess > 0 ) //then  ! CRCs match, so print it
                return;
        }
    }
    else
        msk40decodeframe_p(c,softbits,xsnr,msgreceived,nsuccess,ident,phase0);
}
void DecoderMs::msk40_freq_search(std::complex<double> *cdat,double fc,int if1,int if2,double delf,int nframes,
                                  int *navmask,double &xmax,double &bestf,std::complex<double> *cs,double *xccs)//std::complex<double> *cdat2,
{
    const int NSPM=240;
    const int c_cdat2 = NSPM*8;//NSPM*nframes nframes=max=8
    int navg;
    std::complex<double> c[NSPM];
    std::complex<double> ct2[2*NSPM];
    std::complex<double> cc[NSPM];
    double xcc[NSPM];

    std::complex<double> cdat2[c_cdat2];
    //zero_double_comp_beg_end(cdat2,0,c_cdat2);

    navg = sum_ia(navmask,0,3);//navg=sum(navmask)
    int n=nframes*NSPM; //n=nframes*NSPM
    //qDebug()<<n<<cdat.;
    double fac=1.0/(24.0*sqrt((double)navg));//fac=1.0/(24.0*sqrt(float(navg))) fac=1.0/(48.0*sqrt(float(navg)))

    for (int ifr = if1; ifr < if2; ++ifr)
    {//do ifr=if1,if2            //!Find freq that maximizes sync
        double ferr=(double)ifr*delf; //ferr=ifr*delf
        tweak1(cdat,n,-(fc+ferr),cdat2);//call tweak1(cdat,n,-(fc+ferr),cdat2)
        pomAll.zero_double_comp_beg_end(c,0,NSPM);//c=0

        for (int i = 0; i < nframes; ++i)
        {//do i=1,nframes
            int ib=(i)*NSPM+0;//ib=(i-1)*NSPM+1
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
        //#pragma omp parallel for
        for (int ish = 0; ish < NSPM; ++ish)
        {//do ish=0,NSPM-1
            //cc(ish)=dot_product(ct2(1+ish:42+ish)+ct2(336+ish:377+ish),cb(1:42))
            //cc[ish]=dot_product_dca_sum_dca_dca(ct2,ish,ct2,336-1+ish,cb_msk144,42);
            //cc(ish)=dot_product(ct2(1+ish:42+ish),cb(1:42))
            cc[ish]=dot_product_dca_dca(ct2,ish,cbr_msk40,0,42);
            xcc[ish]=cabs(cc[ish]);         //xcc=abs(cc)

            double xb1 = xcc[ish]*fac;//2.28
            if (xb1>xb) xb=xb1;
        }

        //for (int i = 0; i < NSPM; i++)
        //    xcc[i]=cabs(cc[i]);         //xcc=abs(cc)

        //double xb=maxval_da_beg_to_end(xcc,0,NSPM)*fac;   //xb=maxval(xcc)*fac
        if (xb>xmax) //then//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        {
            xmax=xb;
            bestf=ferr;
            for (int i = 0; i < NSPM; ++i)
            {
                cs[i]=c[i];                //cs=c
                xccs[i]=xcc[i];            //xccs=xcc
            }
        }
    }
}
void DecoderMs::msk40sync(std::complex<double> *cdat,int nframes,int ntol,double delf,int *navmask,int npeaks,double fc,double &fest,int *npklocs,int &nsuccess,std::complex<double> *c)
{
    const int NSPM=240;
    //double xcc[NSPM];                //real xcc(0:NSPM-1)

    /*std::complex<double> cs_[8][NSPM];       //complex cs(NSPM,8) //complex cs(NSPM)
    const int c_cdat2_ = NSPM*3;//NSPM*nframes nframes=max=3
    std::complex<double> cdat2_[8][c_cdat2_]; // complex cdat2(NSPM*nframes,8)
    double xccs_[8][NSPM];           //real xccs(0:NSPM-1,8)
    double xm[8];
    double bf[8];*/
    //std::complex<double> cs_[NSPM];
    double xccs_[NSPM];
    double xm = 0.0;
    double bf = 0.0;
    //zero_double_beg_end(xm,0,8);//double xm=0.0;
    //zero_double_beg_end(bf,0,8);//double bf=0.0;

    first_msk40();//problem ste e   data s8r/0,1,0,0,1,1,1,0/ static const char s8r[8]= {1,0,1,1,0,0,0,1};

    int nfreqs=2*(int)(ntol/delf) + 1;

    int nthreads=1;
    //!$ nthreads=min(8,int(OMP_GET_MAX_THREADS(),4))
    int nstep=nfreqs/nthreads;

    //!$OMP PARALLEL NUM_THREADS(nthreads) PRIVATE(id,if1,if2)
    int id=1;
    //!$ id=OMP_GET_THREAD_NUM() + 1            !Thread id = 1,2,...
    int if1=-(int)(ntol/delf) + (id-1)*nstep;
    int if2=if1+nstep-1;
    if (id==nthreads) if2=(int)(ntol/delf);
    //call msk40_freq_search(cdat,fc,if1,if2,delf,nframes,navmask,cb,    &
    //cdat2(1,id),xm(id),bf(id),cs(1,id),xccs(1,id))
    //msk40_freq_search(cdat,fc,if1,if2,delf,nframes,navmask,xm,bf,cs_,xccs_);//,cdat2_[id-1]
    msk40_freq_search(cdat,fc,if1,if2,delf,nframes,navmask,xm,bf,c,xccs_);

    //double xmax=xm[0];// xmax=xm(1)
    //fest=fc+bf[0]; //fest=fc+bf(1)
    //double xmax=xm;
    fest=fc+bf;

    /*for (int i = 0; i < NSPM; i++)
    {
        //c[i]=cs_[0][i];        //c=cs(1:NSPM,1)
        //xcc[i]=xccs_[0][i];    //xcc=xccs(0:NSPM-1,1)
        c[i]=cs_[i];        //c=cs(1:NSPM,1)
        xcc[i]=xccs_[i];    //xcc=xccs(0:NSPM-1,1)
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
void DecoderMs::msk40spd(std::complex<double> *cbig,int n,int &nsuccess,QString &msgreceived,double fc,double &fret,
                         double &tret,char &ident,int &navg,std::complex<double> *ct,double *softbits)
{
    const int NSPM=240;
    const int MAXSTEPS=150;
    const int NFFT=240;//NSPM;//da e 4islo 1.46
    const int MAXCAND=7;// org->5 HV tested->7
    int NPATTERNS=6;
    double ferr = 0.0;
    double tonespec[NFFT];
    std::complex<double> ctmp[NFFT+10];
    std::complex<double> cdat[3*NSPM];                    //!Analytic signal
    int navmask[3];

    //NPATTERNS=6; zaradi dwoen array na staro gcc
    int navpatterns_[6][3] =
        { //bbb
            {0,1,0},
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
    double snrs[MAXCAND];

    first_msk40();
    double df = (double)fs_msk40/(double)NFFT;
    dftool_msk40(ntol,fc,df);

    //! fill the detmet, detferr arrays
    int istp_real = 0;
    int nstepsize = 60;
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
        mplay_dca_dca_da(ctmp,0,12,ctmp,0,rcw_msk40,0,1);//ctmp(1:12)=ctmp(1:12)*rcw
        mplay_dca_dca_da(ctmp,NSPM-12,NSPM,ctmp,NSPM-12,rcw_msk40,11,-1);//ctmp(NSPM-11:NSPM)=ctmp(NSPM-11:NSPM)*rcw(12:1:-1)
        f2a->four2a_c2c(ctmp,NFFT,-1,1);//call four2a(ctmp,nfft,1,-1,1)
        mplay_da_absdca_absdca(tonespec,NFFT,ctmp,ctmp);//tonespec=abs(ctmp)**2

        int ihpk = pomAll.maxloc_da_beg_to_end(tonespec,ihlo_msk40,ihhi_msk40);//! high tone search window
        double deltah=-creal( (ctmp[ihpk-1]-ctmp[ihpk+1]) / (2.0*ctmp[ihpk]-ctmp[ihpk-1]-ctmp[ihpk+1]) );//deltah=-real( (ctmp(ihpk-1)-ctmp(ihpk+1)) / (2*ctmp(ihpk)-ctmp(ihpk-1)-ctmp(ihpk+1)) )
        double ah=tonespec[ihpk]; //ah=tonespec(ihpk)

        double ahavp = 0.0;//ahavp=(sum(tonespec,ismask)-ah)/count(ismask)
        for (int i = ihlo_msk40; i<ihhi_msk40; i++)
            ahavp+=tonespec[i];
        ahavp = (ahavp-ah)/(double)(ihhi_msk40-ihlo_msk40);
        double trath=ah/(ahavp+0.01);//trath=ah/(ahavp+0.01)

        int ilpk = pomAll.maxloc_da_end_to_beg(tonespec,illo_msk40,ilhi_msk40);//! window for low tone
        double deltal=-creal( (ctmp[ilpk-1]-ctmp[ilpk+1]) / (2.0*ctmp[ilpk]-ctmp[ilpk-1]-ctmp[ilpk+1]) );//deltal=-real( (ctmp(ilpk-1)-ctmp(ilpk+1)) / (2*ctmp(ilpk)-ctmp(ilpk-1)-ctmp(ilpk+1)) )
        double al=tonespec[ilpk];

        double alavp = 0.0;//alavp=(sum(tonespec,ismask)-al)/count(ismask)
        for (int i = illo_msk40; i<ilhi_msk40; i++)
            alavp+=tonespec[i];
        alavp = (alavp-al)/(double)(ilhi_msk40-illo_msk40);
        double tratl=al/(alavp+0.01);//tratl=al/(alavp+0.01)

        //double fdiff=(ihpk+deltah-ilpk-deltal)*df_144;//fdiff=(ihpk+deltah-ilpk-deltal)*df
        double ferrh=((double)ihpk+deltah-(double)i4000_msk40)*df/2.0;//ferrh=(ihpk+deltah-i4000)*df/2.0
        double ferrl=((double)ilpk+deltal-(double)i2000_msk40)*df/2.0;//ferrl=(ilpk+deltal-i2000)*df/2.0
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
        //qDebug()<<"detmet="<<detmet[istp]<<istp<<istp_real;
    } //! end of detection-metric and frequency error estimation loop

    if (istp_real>0)//v1.29
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

        if ( detmet[il] < 3.4 ) break; //tested 3.4<-hv 3.5*/ //if( (detmet(il) .lt. 3.5) ) exit
        if ( fabs(detfer[il]) <= (double)(ntol) ) //if( abs(detfer(il)) .le. 100.0 ) then
        {
            //inportant no(il-1)<-out of array HV tested->(il-0) times(ndet)=((il-1)*256+NPTS/2)*dt
            //times[ndet]=(double)((il-0)*nstepsize+NSPM/2)*dt_msk40;//times(ndet)=((il-1)*216+NSPM/2)*dt
            nstart[ndet]=1+(il-0)*nstepsize+1;                        //nstart(ndet)=1+(il-1)*216+1
            ferrs[ndet]=detfer[il];
            snrs[ndet]=12.0*log10(detmet[il])/2.0-9.0;    //snrs(ndet)=12.0*log10(detmet(il))/2-9.0
            ndet++;
        }
        //for (int i = il-3; i<il+3+1; i++)// -3 +4 lost good locations
        //for (int i = il-3; i<il+4; i++)
        //detmet[i]=0.0;  //detmet(il-3:il+3)=0.0 !    detmet(max(1,il-1):min(nstep,il+1))=0.0
        detmet[il]=0.0;
        //for (int i = il-3; i<il+4; i++)
        //locate[i]=0.0;  //detmet(il-3:il+3)=0.0
    }
    //qDebug()<<"ndet="<<ndet<<xmed;
    if ( ndet < 3 )//for Tropo/ES  if( ndet .lt. 3 ) then
    {
        for (int ip = 0; ip<MAXCAND-ndet; ip++)
        {//do ip=1,MAXCAND-ndet //! Find candidates
            int il = pomAll.maxloc_da_beg_to_end(detmet2,0,istp_real);//iloc=maxloc(detmet2(1:nstep))

            if ( detmet2[il] < 12.0 ) break; //if( (detmet2(il) .lt. 12.0) ) exit
            if ( fabs(detfer[il]) <= (double)(ntol) )//if( abs(detfer(il)) .le. ntol ) then
            {
                //times[ndet]=(double)((il-0)*nstepsize+NSPM/2)*dt_msk40;//times(ndet)=((il-1)*216+NSPM/2)*dt
                nstart[ndet]=1+(il-0)*nstepsize+1;                        //nstart(ndet)=1+(il-1)*216+1
                ferrs[ndet]=detfer[il];//ferrs(ndet)=detfer(il)
                snrs[ndet]=12.0*log10(detmet2[il])/2.0-9.0; //snrs(ndet)=12.0*log10(detmet2(il))/2-9.0
                ndet++;
            }
            //!detmet2(max(1,il-1):min(nstep,il+1))=0.0
            detmet2[il]=0.0;
        }
    }
    //qDebug()<<"ndet="<<ndet<<xmed;
    //if (ndet>0)
    //indexx_msk(times,ndet-1,indices);//HV sprt pings in one scan by time

    nsuccess=0;
    msgreceived="";
    int npeaks=2;
    int ntol0=29;
    double deltaf=7.2;

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
        double xsnr=snrs[icand]; //qDebug()<<"xsnr"<<xsnr<<icand;
        for (int iav = 0; iav<NPATTERNS; iav++)
        {//do iav=1,NPATTERNS

            for (int z = 0; z<3; z++)
                navmask[z]=navpatterns_[iav][z];// navmask=navpatterns(1:3,iav)

            int nsyncsuccess = 0;
            double fest = 0.0;
            msk40sync(cdat,3,ntol0,deltaf,navmask,npeaks,fo,fest,npkloc,nsyncsuccess,c);
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
                    cshift2(ct,c,NSPM,ic0-0);// opasno r_lz2hv

                    int ndecodesuccess = 0;
                    //call msk40decodeframe(ct,mycall,hiscall,xsnr,msgreceived,ndecodesuccess)
                    msk40decodeframe(ct,softbits,xsnr,msgreceived,ndecodesuccess,ident,false);
                    //qDebug()<<"msk40decodeframe ndecodesuccess="<<ndecodesuccess;
                    if ( ndecodesuccess > 0 )
                    {
                        tret=(nstart[icand]+NSPM/2)/fs_msk40;//tret=(nstart[icand]+NSPM/2)/fs
                        fret=fest;
                        //!write(*,*) icand, iav, ipk, is, tret, fret, msgreceived
                        navg = sum_ia(navmask,0,3);//navg=sum(navmask)//za msk40
                        nsuccess=ndecodesuccess;
                        //qDebug()<<"msgreceived=========="<<msgreceived<<"==="<<ic0<<ipk;
                        return;
                    }
                }
            }
        }
    }       //! candidate loop
}
void DecoderMs::msk144signalquality(std::complex<double> *cframe,double snr,double freq,double t0,
                                    double *softbits,QString msg,QString dxcall,int &nbiterrors,
                                    double &eyeopening,bool &trained,double *pcoeffs,bool f_calc_pcoeffs)
{
    bool msg_has_dxcall = false;
    int hardbits[144];//144
    int i4tone[234];// 144 vazno -> 234
    int msgbits[144];
    std::complex<double> cross[864];

    bool is_training_frame = false;
    //double dphi0;
    //double dphi1;
    //double phi;
    //double dphi;
    double waveform[864];//waveform(0:863)
    //double d[1024];    //real d(1024)
    double a[5];
    //std::complex<double> canalytic[1024+10];
    std::complex<double> cmodel[1024+10];
    double phase[864];
    //double x[145];
    //double y[145];
    //double sigmay[145];
    //double chisqr;
    //double pp[145];

    //qDebug()<<"DXCall="<<dxcall_sq;
    int NSYM = 144;
    int NSPM=864;
    if (msg.at(0).toLatin1()=='<')
    {
        int next_ = msg.indexOf(">");//2.00
        if (msg.midRef(0,next_+1).contains(" "))//2.00
        {
            NSYM = 40;
            NSPM = 240;
        }
    }

    if (first_sq)
    {
        navg_sq=0;
        pomAll.zero_double_comp_beg_end(cross,0,NSPM);
        pomAll.zero_double_comp_beg_end(cross_avg_sq,0,NSPM);
        wt_avg_sq=0.0;
        tlast_sq=0.0;
        trained_dxcall_sq=" ";
        training_dxcall_sq=" ";
        currently_training_sq=false;
        trained=false;
        pomAll.zero_double_beg_end(pcoeffs, 0 ,3);  // hv ?????
        ///qDebug()<<"Zerooooooooooooo1=";
        first_sq=false;
    }
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    //if ( (trained && (dxcall != trained_dxcall_sq)) || (currently_training_sq && (dxcall != training_dxcall_sq)) || (navg_sq > 10 ))
    if ( (currently_training_sq && (dxcall != training_dxcall_sq)) || (navg_sq > 10 )) //then !reset and retrain
    {//!reset and retrain
        navg_sq=0;
        pomAll.zero_double_comp_beg_end(cross,0,NSPM);
        pomAll.zero_double_comp_beg_end(cross_avg_sq,0,NSPM);
        //qDebug()<<"reset wt_avg_sq ==========================================";
        wt_avg_sq=0.0;
        tlast_sq=0.0;
        trained=false;
        currently_training_sq=false;
        trained_dxcall_sq=" ";
        training_dxcall_sq=" ";
        pomAll.zero_double_beg_end(pcoeffs, 0 ,3);  // hv ?????
        //qDebug()<<"Zerooooooooooooo2=";
        //write(*,*) 'reset to untrained state '
    }

    int indx_dxcall=msg.indexOf(dxcall); //indx_dxcall=index(msg,trim(dxcall))
    if (indx_dxcall >= 4) msg_has_dxcall = true; //msg_has_dxcall = indx_dxcall .ge. 4

    //if( btrain && msg_has_dxcall && (!currently_training_sq) ) then
    //if ( msg_has_dxcall && (!trained) && !currently_training_sq )
    if (/*btrain &&*/ msg_has_dxcall && !currently_training_sq )
    {
        currently_training_sq=true;
        training_dxcall_sq=dxcall;
        trained_dxcall_sq=" ";
        pomAll.zero_double_beg_end(pcoeffs, 0 ,3);  // hv ?????
        //write(*,*) 'start training on call ',training_dxcall
        //qDebug()<<"Zerooooooooooooo3=";
    }

    if ( msg_has_dxcall && currently_training_sq )
    {
        trained=false; //! just to be sure
        trained_dxcall_sq=" ";
        training_dxcall_sq=dxcall;
        pomAll.zero_double_beg_end(pcoeffs, 0 ,3);  // hv ?????
        //qDebug()<<"Zerooooooooooooo4=";
    }

    //! use decoded message to figure out how many bit errors in the frame
    for (int i = 0; i < NSYM; i++)
    {//do i=1, 144
        if (NSYM==40)// neznam za6to hv
        {
            hardbits[i]=1;
            if (softbits[i] > 0.0 ) hardbits[i]=0;// ae->ge //if(softbits(i) .gt. 0 ) hardbits(i)=1
        }
        else
        {
            hardbits[i]=0;//0
            if (softbits[i] > 0.0 ) hardbits[i]=1;//1 ae->ge //if(softbits(i) .gt. 0 ) hardbits(i)=1
        }
    }

    //! generate tones from decoded message
    //QString mygrid="KN23";// fiktiwno
    //int ichk=0;
    //bool bcontest=false;
    //double GEN_SAMPLE_RATE = DEC_SAMPLE_RATE;//48000.0;// fiktiwno
    //double koef_srate = 1.0;//4.0;// fiktiwno
    //call genmsk144(msg,mygrid,ichk,bcontest,msgsent,i4tone,itype)
    //double samfacout = 1.0; // fiktiwno
    short iwave[20];// fiktiwno
    char c_msg[55];
    for (int i = 0; i < 55; i++)
    {
        if (i<msg.count())
            c_msg[i]=msg[i].toLatin1();
        else
            c_msg[i]=' ';
    }
    //qDebug()<<"0"<<ss_msk144ms;
    TGenMsk->genmsk(c_msg,1.0,i4tone,false,iwave,DEC_SAMPLE_RATE,1.0,0,s_mod,ss_msk144ms);//last is rpt_db_msk  mygrid,bcontest,

    //! reconstruct message bits from tones
    //QString sss;
    //QString sss1;

    msgbits[0]=0;
    for (int i = 0; i < NSYM; i++)
        msgbits[i]=0;
    //int c = 1;
    for (int i = 0; i < NSYM-1; i++)//144 may ?????
    {//do i=1,143
        if ( i4tone[i] == 0 )
        {
            if ( fmod(i+1,2) == 1 )//if( mod(i,2) .eq. 1 ) then
                msgbits[i+1]=msgbits[i];
            else
                msgbits[i+1]=fmod(msgbits[i]+1,2);
        }
        else
        {
            if ( fmod(i+1,2) == 1 )//if( mod(i,2) .eq. 1 ) then
                msgbits[i+1]=fmod(msgbits[i]+1,2);
            else
                msgbits[i+1]=msgbits[i];
        }
    }

    /* stop 1.33 loop optimisation
    nbiterrors=0;
    for (int i = 0; i < NSYM; i++)
    {//do i=1,144
        if (hardbits[i] != msgbits[i]) nbiterrors++;//nbiterrors=nbiterrors+1;
        //sss.append(QString("%1").arg(hardbits[i]));
        //sss1.append(QString("%1").arg(msgbits[i]));
    }
    */

    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    double eyetop=1.0;
    double eyebot=-1.0;
    nbiterrors=0;
    for (int i = 0; i < NSYM; i++)
    {//do i=1,144
        int k = 1;
        if (NSYM==40)// msk40
            k = 0;
        if ( msgbits[i] == k )
        {
            if ( softbits[i] < eyetop ) eyetop=softbits[i];
        }
        else
        {
            if ( softbits[i] > eyebot ) eyebot=softbits[i];
        }
        if (hardbits[i] != msgbits[i]) nbiterrors++;// 1.33 loop optimisation
    }
    eyeopening=eyetop-eyebot;

    if (!f_calc_pcoeffs)
        return;

    if ((snr>5.0 && (nbiterrors<7)) && (fabs(t0-tlast_sq) > 0.072) && msg_has_dxcall)
        is_training_frame = true;

    //qDebug()<<"RRRR="<<snr<<nbiterrors<<fabs(t0-tlast_sq);
    if ( currently_training_sq && is_training_frame )
        //if (snr>5.0 && nbiterrors<7 && fabs(t0-tlast_sq) > 0.072)//za mahane hv
    {
        //twopi=8.0*atan(1.0) //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        //int nsym=144;
        //if ( i4tone[41-1] < 0 ) nsym=40; //if( i4tone(41) .lt. 0 ) nsym=40
        double dphi0=twopi*(freq-500.0)/DEC_SAMPLE_RATE;
        double dphi1=twopi*(freq+500.0)/DEC_SAMPLE_RATE;
        double phi=-twopi/8.0;
        int indx=0;
        for (int i = 0; i < NSYM; i++)
        {//do i=1,nsym
            double dphi;
            if ( i4tone[i] == 0 )
                dphi=dphi0;
            else
                dphi=dphi1;

            for (int j = 0; j < 6; j++)
            {//do j=1,6
                waveform[indx]=cos(phi);
                indx++;//indx=indx+1
                phi=fmod(phi+dphi,twopi);
            }
        }
        //! convert the passband waveform to complex baseband
        int npts=864; //problem msk40
        int nfft=1024;// my be 720 for msk40
        double d[1024];
        pomAll.zero_double_beg_end(d,0,1024);
        for (int xa = 0; xa < NSPM; xa++)
            d[xa]=waveform[xa]/10.0;   //hv 1.31 /10.0 osobennost na generatora za men /10.0   //d(1:864)=waveform(0:863)
        //zero_double_beg_end(a,0,5);

        //qDebug()<<"d[x]"<<d[10];
        //for (int x = 0; x < NSPM; x++)
        //cframe[x]=cframe[x]/10.0;

        //analytic_msk144_2(d,0,npts,nfft,canalytic,a,false,false); //! don't equalize the model
        std::complex<double> canalytic[1024+10];
        analytic_msk144_2(d,0,npts,nfft,canalytic,pcoeffs,false,false);
        tweak1(canalytic,nfft,-freq,cmodel);//call tweak1(canalytic,nfft,-freq,cmodel)
        f2a->four2a_c2c(cframe,NSPM,-1,1);//call four2a(cframe(1:864),864,1,-1,1)
        f2a->four2a_c2c(cmodel,NSPM,-1,1);//call four2a(cmodel(1:864),864,1,-1,1)

        //qDebug()<<-freq;
        //! Cross spectra from different messages can be averaged
        //! as long as all messages originate from dxcall.
        for (int i = 0; i < NSPM; i++)
            cross[i]=cmodel[i]*conj(cframe[i])/1000.0;//cross_sq=cmodel(1:864)*conjg(cframe)/1000.0

        pomAll.cshift1(cross,NSPM,NSPM/2);//cross=cshift(cross,864/2)

        for (int i = 0; i < NSPM; i++)//nfft=fmin(pow(2,n),(1024.0*1024.0));//    nfft=min(2**n,1024*1024)
            cross_avg_sq[i]+=pow(10.0,(snr/20.0))*cross[i]; //cross_avg=cross_avg+10**(snr/20.0)*cross

        wt_avg_sq+=pow(10.0,(snr/20.0));//wt_avg_sq=wt_avg_sq+10**(snr/20.0)
        //qDebug()<<"wt_avg_sq"<<wt_avg_sq<<snr;
        navg_sq++;//navg_sq=navg_sq+1
        tlast_sq=t0;
        for (int i = 0; i < NSPM; i++)
            phase[i]=atan2(cimag(cross_avg_sq[i]),creal(cross_avg_sq[i]));//phase=atan2(imag(cross_avg),real(cross_avg))

        double df=DEC_SAMPLE_RATE/(double)NSPM;
        double nm=145;
        double x[145];
        for (int i = 0; i < 145; i++)
        {//do i=1,145
            x[i]=(i-72)*df/1000.0;//hv x(i)=(i-73)*df/1000.0   posledno 4islo
        }

        int offset = (NSPM/2-nm/2);//359,5 ? hihi 432 72,5
        double y[145];
        double sigmay[145];
        for (int i = 0; i < 145; i++)
        {
            y[i]=phase[i+offset];//y=phase((864/2-nm/2):(864/2+nm/2))  //phase[864];
            sigmay[i]=wt_avg_sq/cabs(cross_avg_sq[i+offset]);//sigmay=wt_avg/abs(cross_avg((864/2-nm/2):(864/2+nm/2)))

            //if(sigmay[i]>0.1)
            //sigmay[i] = 3.9;
            //qDebug()<<"sigmay[i]"<<sigmay[i]<<wt_avg_sq<<cabs(cross_avg_sq[i+offset])<<i;
        }
        int mode=1;
        npts=145;
        int nterms=5;//!!! max=10
        double chisqr;
        pomAll.polyfit(x,y,sigmay,npts,nterms,mode,a,chisqr);

        double pp[145];
        for (int i = 0; i < 145; i++)
        {
            pp[i]=a[0]+x[i]*(a[1]+x[i]*(a[2]+x[i]*(a[3]+x[i]*a[4])));  //pp=a(1)+x*(a(2)+x*(a(3)+x*(a(4)+x*a(5)))) //double pp[145]; a[5]; x(145);
        }

        double rmsdiff = 0.0;

        for (int i = 0; i < 145; i++)
        {
            rmsdiff+=(pp[i]-phase[i+offset])*(pp[i]-phase[i+offset]); //*(pp[i]-phase[i+offset])  rmsdiff=sum( (pp-phase((864/2-nm/2):(864/2+nm/2)))**2 )/145.0
            //rmsdiff+=(-4)*(-4);
            //qDebug()<<" rmsdiff="<< rmsdiff<<pp[i]<<phase[i+offset];
        }
        rmsdiff = rmsdiff/145.0;
        //write(*,*) 'training ',navg,sqrt(chisqr),rmsdiff
        //if( (sqrt(chisqr).lt.1.5) .and. (rmsdiff.lt.1.1) .and. (navg.ge.5) )//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        //qDebug()<<"SSS================="<<sqrt(chisqr)<<rmsdiff<<navg_sq;
        //if ( (sqrt(chisqr)<1.5) && (rmsdiff<1.1) && (navg_sq>=2) ) // old
        if ( (sqrt(chisqr)<1.8) && (rmsdiff<0.5) && (navg_sq>=2) )// new
        {
            //qDebug()<<"==============================";
            int dd = 0;
            for (int i = 2; i < 5; i++)
            {
                pcoeffs[dd]=a[i];//pcoeffs=a(3:5)
                dd++;
            }
            trained_dxcall_sq=dxcall;
            training_dxcall_sq=" ";
            currently_training_sq=false;
            trained=true;
            //qDebug()<<"s_pcoeffs_msk144[dd]="<<s_pcoeffs_msk144[0]<<s_pcoeffs_msk144[1]<<s_pcoeffs_msk144[2];
        }
    }
}



