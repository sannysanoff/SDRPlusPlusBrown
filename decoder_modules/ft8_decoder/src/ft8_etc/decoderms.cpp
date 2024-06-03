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

#include "mshv_support.h"
#include "decoderms.h"


#include <numeric>
#include <functional>
using namespace std; // zaradi max(
static const double DEC_SAMPLE_RATE_11025 = 11025.0;
static const double DEC_SAMPLE_RATE_12000 = 12000.0;

std::function<void(const char *line)> decodeResultOutputFun;
//#include <QtGui>

#define MAXDUPMSGTHR 120 //2.63 from 100 to 120

DecoderMs::DecoderMs()//QObject *parent
//: QObject(parent)
{
    f2a = std::make_shared<F2a>();
	pomAll.initPomAll();//2.66 for pctile_shell in jt65 and pi4
    for (int i = 0; i <  MAXDUPMSGTHR; ++i)
    {
        dup_amsgs_thr[i] = "";
        dup_afs_thr[i]=0;
    }
    dup_camsgf_thr = 0;
    is_thrTime = false;

    s_thr_used = 1;
    HisGridLoc = "";
    s_nftx = 1200.0;
    s_nQSOProgress = 0;//default -> CQ LZ2HV KN23  for +AP
    s_lapon = false;   //default for +AP //
    //f_multi_answer_mod = false;
    //s_ncontest_ft8_2 = 0;
    /////////// FT4 ///////////////////////////////////////////////////////////
    DecFt4_0 = new DecoderFt4(0, f2a);
//    connect(DecFt4_0, SIGNAL(EmitDecodetTextFt(QStringList)), this, SLOT(SetDecodetTextFtQ65(QStringList)));
//    connect(DecFt4_0, SIGNAL(EmitBackColor()), this, SLOT(ThrSetBackColor()));
    DecFt4_1 = new DecoderFt4(1, f2a);
//    connect(DecFt4_1, SIGNAL(EmitDecodetTextFt(QStringList)), this, SLOT(SetDecodetTextFtQ65(QStringList)));
//    connect(DecFt4_1, SIGNAL(EmitBackColor()), this, SLOT(ThrSetBackColor()));
    DecFt4_2 = new DecoderFt4(2, f2a);
//    connect(DecFt4_2, SIGNAL(EmitDecodetTextFt(QStringList)), this, SLOT(SetDecodetTextFtQ65(QStringList)));
//    connect(DecFt4_2, SIGNAL(EmitBackColor()), this, SLOT(ThrSetBackColor()));
    DecFt4_3 = new DecoderFt4(3, f2a);
//    connect(DecFt4_3, SIGNAL(EmitDecodetTextFt(QStringList)), this, SLOT(SetDecodetTextFtQ65(QStringList)));
//    connect(DecFt4_3, SIGNAL(EmitBackColor()), this, SLOT(ThrSetBackColor()));
    DecFt4_4 = new DecoderFt4(4, f2a);
//    connect(DecFt4_4, SIGNAL(EmitDecodetTextFt(QStringList)), this, SLOT(SetDecodetTextFtQ65(QStringList)));
//    connect(DecFt4_4, SIGNAL(EmitBackColor()), this, SLOT(ThrSetBackColor()));
    DecFt4_5 = new DecoderFt4(5, f2a);
//    connect(DecFt4_5, SIGNAL(EmitDecodetTextFt(QStringList)), this, SLOT(SetDecodetTextFtQ65(QStringList)));
//    connect(DecFt4_5, SIGNAL(EmitBackColor()), this, SLOT(ThrSetBackColor()));
    /////////// FT4 END ///////////////////////////////////////////////////////
    /////////// FT8 ///////////////////////////////////////////////////////////
    //ft8 3 dec
    fromBufFt = false;
    id3decFt = 0;
    c_stat_ftb[0] = 0;
    c_stat_ftb[1] = 0;
    is_stat_ftb[0] = false;
    is_stat_ftb[1] = false;
    is_ftBuff = false;

    DecFt8_0 = new DecoderFt8(0, f2a);
    DecFt8_1 = new DecoderFt8(1, f2a);
    DecFt8_2 = new DecoderFt8(2, f2a);
    DecFt8_3 = new DecoderFt8(3, f2a);
    DecFt8_4 = new DecoderFt8(4, f2a);
    DecFt8_5 = new DecoderFt8(5, f2a);
    /////////// END FT8 ///////////////////////////////////////////////////////////

    allq65 = false;
    DecQ65 = new DecoderQ65();

    /////////// PI4 ///////////////////////////////////////////////////////////
    first_pi4 = true;
    first_xcorpi4 = true;
    first_extractpi4 = true;
    off_mettab_pi4 = 130;//130
    first_interleavepi4 = true;
    nutc0_pi4 = -1;
    first_avgpi4 = true;
    nfreq0_pi4 = -999999.0;
    clearave_pi4 = false;
    count_use_avgs_pi4 = 0;
    count_saved_avgs_pi4 = 0;
    ///////////END PI4 ///////////////////////////////////////////////////////////
    ///// JT65ABC /////////////////////////////////
    //App_Path = app_path_65;
    TGen65 = new Gen65();
    TGen65->setup65();
    s_aggres_lev_ftd = 5;//def
    s_aggres_lev_deeps = 95;//def
    first_symspec65 = true;
    thresh0_jt65 = 0.0;
    dfref_jt65 = 0.0;
    nsave_jt65 = 0;
    width_jt65 = 0.0;
    s1_ofs_jt65 = 256+20;
    first_avg65 = true;
    first_subtract65 = true;
    //first_subtract65();
    s_avg_jt65 = false; //1.60 to bool by default start //16=avg
    s_deep_search_jt65 = false;//1.49
    clearave_jt65 = false;

    s_nfqso_all = 1270.46;
    s_max65_cand_for_dec = 4;// default
    s_f00 = 200.0;
    s_f01 = 3200.0;

    count_saved_avgs_1jt65 = 0;
    count_saved_avgs_2jt65 = 0;
    count_use_avgs_1jt65 = 0;
    count_use_avgs_2jt65 = 0;
    s_bVHF_jt65 = false;//1.60= def fsk441
    mycall0_jt65ap = "none";
    hiscall0_jt65ap = "none";
    hisgrid0_jt65ap = "none";
    //hint65 sega ne rabotiat kato seve no za naptrd ???
    //first_hint65 = true;
    //end hint65 sega ne rabotiat kato seve no za naptrd ???

    //first_filbig = true;
    ///// END JT65ABC /////////////////////////////////

    ////dftool_msk144////////////////
    s_list_rpt_msk.clear();
    prev_pt_msk = 0.0;
    ping_width_msk = 0.0;
    prev_ping_t_msk = 0.0;
    last_rpt_snr_msk = 0.0;
    one_end_ping_msk = false;
    is_new_rpt_msk = false;
    ss_msk144ms = false;
    //s_period_time = 30;

    last_ntol_msk144 = -1;
    last_df_msk144 = -1;
    ////end dftool_msk144////////////////
    ////dftool_msk40////////////////
    last_ntol_msk40 = -1;
    last_df_msk40 = -1;
    ////end dftool_msk40////////////////

    //My_Grid_Loc = "GRID";
    s_fopen = false;
    TGenMsk = new GenMsk(true);//f_dec_gen = dec=true gen=false
    //nfft0_jtmsk = 0;//jtmsk analytic_msk
    //zero_double_beg_count(h,0,(1024*1024));//jtmsk analytic_msk h[1024*1024];
    //only1_syncmsk  = true;
    ////jtmsk short///////
    //first_short = true;
    //nrxfreq0 = -1;
    ////jtmsk short///////
    ////msk144///////
    first_dec_msk = true;
    first_sq = true;
    s_HisCall = "NOCALL";
    s_R1HisCall = "NOCALL";//for fr8 MA QSO Foxs
    s_R2HisCall = "NOCALL";//for fr8 MA QSO Foxs

    //s_ident_init_ldpc = -1;
    s_decoder_deep = 1;
    f_first_msk144 = false;

    for (int i = 0; i < HASH_CALLS_COUNT; i++)
    {
        hash_msk40_calls[i].hash = -101;
        hash_msk40_calls[i].calls="FALSE";
    }
    //qDebug()<<"HASH_CALLS_COUNT"<<HASH_CALLS_COUNT;

    //nfft0_msk144 = 0;//old analytic_msk

    ////// NEW analytic ////////////////////////////////////////////////////////////////
    s_msk144rxequal_s = false;  //1.31 default msk144/msk40
    s_msk144rxequal_d = false;   //1.31 default msk144/msk40
    DEC_SAMPLE_RATE = DEC_SAMPLE_RATE_11025;//HV important set to default mode fsk441 sample rate

    nfft0_msk144_2 = 0;//new analytic_msk144_2
    pomAll.zero_double_beg_end(dpclast_msk144_2,0,3);
    dpclast_msk144_2[0]=1.0;//to reset in begining, for any case HV 1.32

    pomAll.zero_double_comp_beg_end(s_corrd,0,524300);//decoderms.h max 524500 need for auto decode 30s=524288
    analytic_msk144_2_init_s_corrs_full();
    //zero_double_beg_end(spclast_msk144_2,0,3);
    //zero_double_comp_beg_end(s_corrs,0,524300);//decoderms.h max 524500 need for auto decode 30s=524288
    //zero_double_beg_end(saclast_msk144_2,0,5);
    //saclast_msk144_2[0]=1.0; //data saclast/1.0,0.0,0.0,0.0,0.0/
    ////// END NEW analytic /////////////////////////////////////////////////////

    s_f_rtd = false;
    s_end_rtd = true;
    first_rtd_msk = true;
    rtd_dupe_cou = 0;
    rtd_dupe_pos = 0.0;

    ////msk144///////
    ////msk40///////
    f_first_msk40 = false;
    ////msk40///////

    s_mod = 2;//HV important set to default mode fsk441

    s_MyCall = "NOT__EXIST";
    s_MyBaseCall = "NOT__EXIST";
    a1_ = 0.0;
    a2_ = 0.0;
    a3_ = 0.0;
    a4_ = 0.0;
    G_MinSigdB = 1;//HV important set to default mode fsk441
    G_DfTolerance = 400;//HV important set to default mode fsk441
    G_ShOpt = false;//for msk32 and my be jtmsk_short
    G_SwlOpt = false;//1.31 default
    s_nzap = false;//ZAP
    thred_busy = false;
    s_time = "0.0";
    s_static_dat_count = 0;// for thread decode
    s_mousebutton = 0;//mousebutton Left=1, Right=3 fullfile=0 rtd=2
    twopi=8.0*atan(1.0);
    pi=4.0*atan(1.0);

    jtms_dfx = 0.0;
    s_basevb =0.0;
    setupms();

    //setup fsk441
    //short itone[84];
    only1_s_mode = 0;
    for (int i = 0; i<84; i++)
    {
        itone_s_fsk[i]=0;
    }

    char c[1] = {' '};
    ndits_s = abc441(c,1,itone_s_fsk);
    //gen441(itone,ndits_s,cfrag_s);
    f_back_color = false;
    //qDebug()<<"CREATE";

}
DecoderMs::~DecoderMs()
{
    //qDebug()<<"DELETE";
    delete DecFt8_5;
    delete DecFt8_4;
    delete DecFt8_3;
    delete DecFt8_2;
    delete DecFt8_1;
    delete DecFt8_0;

    delete DecFt4_5;
    delete DecFt4_4;
    delete DecFt4_3;
    delete DecFt4_2;
    delete DecFt4_1;
    delete DecFt4_0;

    delete TGenMsk;
    delete TGen65;
    delete DecQ65;
}
void DecoderMs::SetMaxDrift(bool f)
{
    DecQ65->SetMaxDrift(f);
}
void DecoderMs::AutoClrAvgChanged(bool f)
{
    DecQ65->AutoClrAvgChanged(f);
}
void DecoderMs::SetClearAvgQ65()
{
    DecQ65->SetClearAvgQ65();
}
void DecoderMs::SetPerodTime(int i)
{
    DecQ65->SetPeriod(i);
}
void DecoderMs::Decode3intFt(bool f)//2.39 remm
{
    DecFt8_0->Decode3intFt(f);//2.39 remm
}
void DecoderMs::SetMAMCalls(QStringList ls)
{
    DecFt4_0->SetMAMCalls(ls);// static no need more
}
void DecoderMs::SetTxFreq(double f)
{
    s_nftx = f;
    DecFt8_0->SetStTxFreq(f);
    DecFt4_0->SetStTxFreq(f);
    DecQ65->SetTxFreq(f);
}
void DecoderMs::SetDecAftEMEDelay(bool f)
{
    DecQ65->SetDecAftEMEDelay(f);
}
void DecoderMs::SetQSOProgress(int i)
{
    if (i==5 || i==6)//1.69 and 1.70  //if (i==6)//1.70
        s_nQSOProgress = 0;   //MSHV +1  CQ LZ2HV 237 for +AP
    else
    {
        s_nQSOProgress = i+1; //MSHV +1  CQ LZ2HV 237 for +AP
    }
    DecFt8_0->SetStQSOProgress(s_nQSOProgress);//2.51 i=error
    DecFt4_0->SetStQSOProgress(s_nQSOProgress);//2.51 i=error
    DecQ65->SetStQSOProgress(s_nQSOProgress);//2.51 i=error
    //qDebug()<<"i="<<i<<"QSOProgress="<<s_nQSOProgress;

    /*
    1  call mycall kn23
    2  call mycall +00
    3  call mycall r+00
    4  call mycall rrr
    5  call mycall 73
    0  cq mycall kn23    //1.70=0 1.60=0
    0  cq qrg mycall +MSHV
      CALLING,      0
      REPLYING,     1
      REPORT,       2
      ROGER_REPORT, 3
      ROGERS,       4
      SIGNOFF       5
    */
}
void DecoderMs::SetMultiAnswerMod(bool f)
{
    DecFt8_0->SetStMultiAnswerMod(f);
    DecFt4_0->SetStMultiAnswerMod(f); //qDebug()<<"f_multi_answer_mod="<<f;
    DecQ65->SetStMultiAnswerMod(f);
}
void DecoderMs::SetThrLevel(int i)//0=1 1=th/2 2=all
{
    s_thr_used = i;
    if (s_thr_used<1) s_thr_used = 1;
    if (s_thr_used>6) s_thr_used = 6;
    //f2a.DestroyPlansAll(true);
    //qDebug()<<s_thr_used;
}
void DecoderMs::SetApDecode(bool f)
{
    s_lapon = f;//Ap decoding ?
    DecFt8_0->SetStApDecode(f);
    DecFt4_0->SetStApDecode(f);// only in mshv
    DecQ65->SetStApDecode(f);// only in mshv
    //qDebug()<<"ft8 s_lapon="<<s_lapon;
}
void DecoderMs::SetDecoderDeep(int d)
{
    s_decoder_deep = d;
    DecFt8_0->SetStDecoderDeep(d);
    DecFt4_0->SetStDecoderDeep(d);
    DecQ65->SetStDecoderDeep(d);  //qDebug()<<"s_decoder_deep="<<s_decoder_deep;
}
void DecoderMs::SetSingleDecQ65(bool f)
{
    DecQ65->SetSingleDecQ65(f);
}
QString DecoderMs::CharToQString(char* ch, int count)
{
    QString s;
    for (int j = 0; j<count; j++) s.append(ch[j]);
    return s;
}
QString DecoderMs::FormatFoldMsg(QString str)
{
    //str = "HWY   LZ2HV    SP9 ";//<-towa e problem na parvia na4in
    /*QString s;
       int beg = 0; 
     
    for (beg = 0; beg<str.count(); beg++)
       {
           if (str.at(beg)==' ')
               break;
       } 
       qDebug()<<beg<<str.count(); 
       if(beg!=str.count()) 
          s.append(str.mid(beg+1,(str.count()-(beg+1))));
       s.append(str.mid(0,beg));
       return s;*/

    QString s;
    int beg = 0;
    int end;

    for (beg = 0; beg<str.count(); beg++)
    {
        if (str.at(beg)==' ')
            break;
    }
    for (end = str.count()-1; end>=0; end--)
    {
        if (str.at(end)==' ')
            break;
    }
    //qDebug()<<beg<<end<<str.count();
    if (beg==0 || end==str.count()-1 || beg==str.count())
        s = str;
    else
    {
        s.append(str.mid((end+1),(str.count()-(end+1))));    //qDebug()<<"2="<<s<<end+1;
        s.append(str.mid(0,beg));                            //qDebug()<<"3="<<s<<beg;
        s.append(str.mid(beg,(end-beg+1)));                  //qDebug()<<"4="<<s<<beg<<end;
    }
    return s;
}
QString DecoderMs::AlignMsgSpecWord(QString msg,QString word, bool &f_align)
{
    /*QString s;
    int beg = 0;
    int end = 0;
    bool f_start_str = false;

    for (int i = 0; i<msg.count(); i++)
    {
        if (msg.at(i) != ' ' && !f_start_str)
        {
            f_start_str = true;
            beg = i;
        }

        if (f_start_str)
        {
            if (msg.at(i) == ' ' || i==msg.count()-1)
            {
                if (i==msg.count()-1 && msg.at(i) != ' ')
                    end = i+1;
                else
                    end = i;

                //qDebug()<<msg.mid(beg,end-beg);
                if (msg.mid(beg,end-beg)==mycall)
                {
                    s.append(msg.mid(beg,(msg.count()-beg)));
                    if (i==msg.count()-1)
                        s.append(" ");
                    s.append(msg.mid(0,beg));
                    return s;
                }
                else
                    f_start_str = false;
            }
        }
    }
    return msg;*/

    QString s;
    int beg = 0;
    //int end = 0;
    bool f_start_str = false;

    for (int i = 0; i<msg.count(); i++)
    {
        if (msg.at(i) != ' ' && !f_start_str)
        {
            f_start_str = true;
            beg = i;
        }

        if (f_start_str)
        {
            if (msg.at(i) == ' ' || i==msg.count()-1)
            {
                int end;
                if (i==msg.count()-1 && msg.at(i) != ' ')
                    end = i+1;
                else
                    end = i;

                //qDebug()<<msg.mid(beg,end-beg);
                if (msg.mid(beg,end-beg)==word)
                {
                    s.prepend(" ");
                    s.prepend(msg.mid(beg,(msg.count()-beg)));
                    f_align = true;
                    return s;
                }
                else
                {
                    f_start_str = false;
                    s.append(msg.mid(beg,(end-beg)));
                    s.append(" ");
                    //qDebug()<<s;
                }
            }
        }
    }
    return s;
}
QString DecoderMs::RemBegEndWSpaces(QString str)
{
    QString s;
    /*int msg_count = 0;//2.64 stop
    for (msg_count = str.count()-1; msg_count>=0; msg_count--)
    {
        if (str.at(msg_count)!=' ')
            break;
    }
    s = str.mid(0,msg_count+1);
    msg_count = 0;
    for (msg_count = 0; msg_count<s.count(); msg_count++)
    {
        if (s.at(msg_count)!=' ')
            break;
    }
    s = s.mid(msg_count,(s.count()-msg_count));*/
    s = str.trimmed();
    return s;
}
QString DecoderMs::RemWSpacesInside(QString s)
{
    for (int i = 0; i<s.count(); i++)
        s.replace("  "," ");
    return s;
}
QString DecoderMs::FormatLongMsg(QString msg,int max_count_msg)
{
    int part = max_count_msg;

    msg = RemWSpacesInside(msg);
    msg = RemBegEndWSpaces(msg);

c222:
    if (msg.count()>part)
    {
        int pouse = msg.lastIndexOf(" ",part);
        int pouse_n = msg.lastIndexOf("\n",part);

        if (pouse<=pouse_n)
            pouse=-1;

        //qDebug()<<"pouse="<<pouse<<"part="<<part<<msg_out[pouse-1];

        if (pouse==-1)// ako niama nikade pauza ina4e bezkraen cikal
        {
            pouse=part;
            msg.insert(pouse,"\n");
            part=(pouse+1) + max_count_msg;
            //qDebug()<<"NOPOUSE";
        }
        else
        {
            msg.replace(pouse,1,"\n");// ne oveli4awa count \n
            part=(pouse+0) + max_count_msg;
        }
        goto c222;
    }
    return msg;
}
void DecoderMs::SetWords(QStringList lst,int cont_cq,int cont_type)
{
    //from here for cqstr in ft4
    if (!lst.at(0).isEmpty())
    {
        s_MyCall = lst.at(0);//for jtms
        //no need static dec hash array TGenFt8->save_hash_call_my_his_r1_r2(s_MyCall,0);//0=my 1=his 2=r1 3=r2
        TGenMsk->save_hash_call_my_his_r1_r2(s_MyCall,0);
    }
    else s_MyCall = "NOT__EXIST";

    if (!lst.at(1).isEmpty()) s_MyBaseCall = lst.at(1);//for ft8 ap, jt65 ap
    else s_MyBaseCall = "NOT__EXIST";

    // 0 : NONE ncontest=0   CQ
    // 1 : NA_VHF 		     CQ TEST
    // 2 : EU_VHF 		     CQ TEST
    // 3 : FIELD DAY 	     CQ FD
    // 4 : RTTY 		   	 CQ RU
    // 5 : WW Digi DX 		 CQ WW
    // 6 : FOX   =    MSHV = CQ BU
    // 7 : HOUND =           NONE

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

    DecFt8_0->SetStWords(s_MyCall,s_MyBaseCall,cont_cq,cont_type);
    DecFt4_0->SetStWords(s_MyCall,s_MyBaseCall,cont_cq,cont_type);
    DecQ65->SetStWords(s_MyCall,s_MyBaseCall,cont_cq,cont_type);
    //qDebug()<<"CQ="<<cont_cq<<"type="<<cont_type;
}
void DecoderMs::SetRxFreqF0F1(double frq,double f00,double f01)
{
    s_nfqso_all = frq;
    s_f00 = f00;
    s_f01 = f01;
    //qDebug()<<"START="<<(int)s_f00<<"RX="<<(int)s_nfqso_all<<"STOP="<<(int)s_f01;
}
void DecoderMs::SetDfSdb(int sdb,int df)
{
    G_MinSigdB = sdb;
    G_DfTolerance = df; //qDebug()<<"DF="<<G_DfTolerance;
}
void DecoderMs::SetZap(bool f)
{
    s_nzap = f; //qDebug()<<s_nzap;
}
void DecoderMs::setMode(int ident)
{
    static int prev_mod = 2;
    s_mod = ident;
    if (s_mod == 14 || s_mod == 15 || s_mod == 16 || s_mod == 17) allq65 = true;
    else allq65 = false;
    /*if (s_mod == 0 || s_mod == 7 || s_mod == 8 || s_mod == 9 || s_mod == 10 || s_mod == 11 ||
            s_mod == 12 || s_mod == 13 || allq65)//jt65abc //zaradi zap msk144 pi4 ft8 ft4
        DEC_SAMPLE_RATE = DEC_SAMPLE_RATE_12000;
    else
        DEC_SAMPLE_RATE = DEC_SAMPLE_RATE_11025;*/
    if (s_mod>0 && s_mod<7) //2.65
        DEC_SAMPLE_RATE = DEC_SAMPLE_RATE_11025;
    else
        DEC_SAMPLE_RATE = DEC_SAMPLE_RATE_12000;

    is_new_rpt_msk = false;//reset s_list_rpt_msk QStringList
    f2a->DestroyPlansAll(true);//2.09  true inidiatly

    if (prev_mod == 14 || prev_mod == 15 || prev_mod == 16 || prev_mod == 17) DecQ65->SetClearAvgQ65all();
    prev_mod = ident;
}
void DecoderMs::analytic(double *d,int d_count_begin,int npts,int nfft,double *s,std::complex<double> *c)
{
    int nh=nfft/2;
    double fac=(double)2.0/nfft;
    //qDebug()<<d[2]<<d[3];

    for (int i = 0; i<npts; i++)
        c[i]=fac*d[i+d_count_begin];

    //for (int j = npts+1; j<nfft; j++)
    for (int j = npts; j<nfft; j++)
        c[j]=0.0;

    f2a->four2a_c2c(c,nfft,-1,1);               //!Forward c2c FFT

    for (int x = 0; x<nh; x++)
        //s[x]=real(c[x])**2 + aimag(c[x])**2
        s[x]=pomAll.ps_hv(c[x]);

    c[0]=0.5*c[0];
    //qDebug()<<nh;
    //for (int y = nh+2; y<nfft; y++)
    for (int y = nh+1; y<nfft; y++)
        c[y]=complex_zero;

    f2a->four2a_c2c(c,nfft,1,1);                //!Inverse c2c FFT

    //c[1] = 4.9+6*I;
    //c[2] = 6.7+6*I;
    //s[1]= 56.7;
    //qDebug()<<d[100]<<creal(c[100])<<cimag(c[100]);//<<creal(cdat[1])<<creal(cdat[2]);
}
void DecoderMs::move_da_to_da(double*x,int begin_x, double*y,int begin_y,int y_end)
{
    //for (int i = 0; i<n; i++)
    //    y[i]=x[i];
    for (int i = 0; i<y_end; i++)
        y[i+begin_y]=x[i+begin_x];
}
void DecoderMs::smooth(double *x,int nz)
{
    double x0=x[0];
    for (int i = 1; i<nz-1; i++)
    {
        double x1=x[i];
        x[i]=0.5*x[i] + 0.25*(x0+x[i+1]);
        x0=x1;
    }
    return;
}
int DecoderMs::ping(double *s,int nz,double dtbuf,int slim,double wmin,double pingdat_[100][3])
{
    int nping=0;
    double peak=0.0;
    bool inside = false;
    //!###    sdown=slim-1.0
    double snrlim=(double)pow(10.0,(0.1*(double)slim)) - 1.0;
    double sdown=10.0*log10(0.25*snrlim + 1.0);
    //qDebug()<<snrlim<<sdown;

    int i0=0; //!Shut up compiler warnings. -db
    double tstart=0.0; //!Shut up compiler warnings. -db
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    //qDebug()<<slim<<sdown<<wmin;
    //int two = 0;
    for (int i = 1; i<nz; i++)
    {  //do i=2,nz

        if (s[i]>=(double)slim && !inside)
        {
            //i0=i
            //if(two>1)
            //{
            i0=i;//HV
            tstart=(double)i0*dtbuf;
            inside=true;
            peak=0.0;
            //qDebug()<<"ping============================"<<tstart;
            //two = 0;
            //}
            //two++;
        }
        //else
        //two=0;
        if (inside && s[i]>peak)
            peak=s[i];

        //qDebug()<<nz<<i<<inside;
        if (inside && (s[i]<sdown || i==nz-1))//vartia do nz-1
        {
            if (i>i0)
            {
                //qDebug()<<dtbuf<<dtbuf*(double)(i-i0);
                if (dtbuf*(double)(i-i0)>=wmin)
                {
                    pingdat_[nping][0]=tstart;
                    pingdat_[nping][1]=dtbuf*(double)(i-i0);//+0.02
                    pingdat_[nping][2]=peak;
                    //qDebug()<<pingdat_[nping][0]<<pingdat_[nping][1]<<pingdat_[nping][2];

                    if (nping<99)// garmi <=99 e 100 a pingdat[3][100] max 99
                        nping++;
                }
                inside=false;
                peak=0.0;
            }
        }
    }
    return nping;
}
void DecoderMs::xfft(std::complex<double> *c,double *d,int nfft)
{
    //! Real-to-complex FFT.
    //four2a((std::complex<double>*)x,nfft,1,-1,0);
    f2a->four2a_d2c(c,d,nfft,-1,0);
}
void DecoderMs::zero_int_beg_end(int*d,int begin,int end)
{
    for (int i = begin; i<end; i++)
    {
        //d[i]=0.0;
        d[i]=0;
        //qDebug()<<i;
    }
}
void DecoderMs::ssort(double *x,double *y,int n,int kflag)
{
    double r, t, tt, tty, ty;
    int i, ij, j, k, kk, l, m, nn;
    int il[21];
    int iu[21];
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    nn = n;
    if (nn < 1)
    {
        //print*,'ssort: The number of sort elements is not positive.'
        //print*,'ssort: n = ',nn,'   kflag = ',kflag
        return;
    }

    kk = abs(kflag);//fabs ????
    if (kk!=1 && kk!=2)
    {
        //print *,'The sort control parameter, k, is not 2, 1, -1, or -2.'
        return;
    }

//! Alter array x to get decreasing order if needed
    if (kflag <= -1)
    {
        for (i = 0; i<nn; i++)//2.12
            x[i] = -x[i];
    }

    if (kk >= 2) goto c100;

//! Sort x only
    m = 1;
    i = 0;
    j = nn;
    r = 0.375e0;

c20:
    if (i == j) goto c60;
    if (r <= 0.5898437e0)
        r = r+3.90625e-2;
    else
        r = r-0.21875e0;

c30:
    k = i;

//! Select a central element of the array and save it in location t
    ij = i + int((j-i)*r);
    t = x[ij];

//! If first element of array is greater than t, interchange with t
//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (x[i] > t)
    {
        x[ij] = x[i];
        x[i] = t;
        t = x[ij];
    }
    l = j;

//! If last element of array is less than than t, interchange with t
    if (x[j] < t)
    {
        x[ij] = x[j];
        x[j] = t;
        t = x[ij];

//! If first element of array is greater than t, interchange with t
        if (x[i] > t)
        {
            x[ij] = x[i];
            x[i] = t;
            t = x[ij];
        }
    }

//! Find an element in the second half of the array which is smaller than t
c40:
    l = l-1;
    if (x[l] > t) goto c40;

//! Find an element in the first half of the array which is greater than t
c50:
    k = k+1;
    if (x[k] < t) goto c50;

//! Interchange these elements
//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (k <= l)
    {
        tt = x[l];
        x[l] = x[k];
        x[k] = tt;
        goto c40;
    }

//! Save upper and lower subscripts of the array yet to be sorted
    if (l-i > j-k)
    {
        il[m] = i;
        iu[m] = l;
        i = k;
        m = m+1;
    }
    else
    {
        il[m] = k;
        iu[m] = j;
        j = l;
        m = m+1;
    }
    goto c70;

//! Begin again on another portion of the unsorted array
c60:
    m = m-1;
    if (m == 0) goto c190;
    i = il[m];
    j = iu[m];

c70:
    if (j-i >= 1) goto c30;
    if (i == 1) goto c20;
    i = i-1;

c80:
    i = i+1;
    if (i == j) goto c60;
    t = x[i+1];
    if (x[i] <= t) goto c80;
    k = i;

c90:
    x[k+1] = x[k];
    k = k-1;
    if (t < x[k]) goto c90;
    x[k+1] = t;
    goto c80;

//! Sort x and carry y along
c100:
    m = 1;
    i = 0;
    j = nn;
    r = 0.375e0;

c110:
    if (i == j) goto c150;
    if (r <= 0.5898437e0)
        r = r+3.90625e-2;
    else
        r = r-0.21875e0;

c120:
    k = i;
//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
//! Select a central element of the array and save it in location t
    ij = i + int((j-i)*r);
    t = x[ij];
    ty = y[ij];
//qDebug()<<"kkk"<<ij;
//! If first element of array is greater than t, interchange with t
    if (x[i] > t)
    {
        x[ij] = x[i];
        x[i] = t;
        t = x[ij];
        y[ij] = y[i];
        y[i] = ty;
        ty = y[ij];
    }
    l = j;
//! If last element of array is less than t, interchange with t
    if (x[j] < t)
    {
        x[ij] = x[j];
        x[j] = t;
        t = x[ij];
        y[ij] = y[j];
        y[j] = ty;
        ty = y[ij];
//! If first element of array is greater than t, interchange with t

        if (x[i] > t)
        {
            x[ij] = x[i];
            x[i] = t;
            t = x[ij];
            y[ij] = y[i];
            y[i] = ty;
            ty = y[ij];
        }
    }
//! Find an element in the second half of the array which is smaller than t
c130:
    l = l-1;
    if (x[l] > t) goto c130;
//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
//! Find an element in the first half of the array which is greater than t

c140:
    k = k+1;
    if (x[k] < t) goto c140;

//! Interchange these elements

    if (k <= l)
    {
        tt = x[l];
        x[l] = x[k];
        x[k] = tt;
        tty = y[l];
        y[l] = y[k];
        y[k] = tty;
        goto c130;
    }

//! Save upper and lower subscripts of the array yet to be sorted

    if (l-i > j-k)
    {
        il[m] = i;
        iu[m] = l;
        i = k;
        m = m+1;
    }
    else
    {
        il[m] = k;
        iu[m] = j;
        j = l;
        m = m+1;
    }
    goto c160;

//! Begin again on another portion of the unsorted array
//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
c150:
    m = m-1;
    if (m == 0) goto c190;
    i = il[m];
    j = iu[m];

c160:
    if (j-i >= 1) goto c120;
    if (i == 1) goto c110;
    i = i-1;

c170:
    i = i+1;
    if (i == j) goto c150;
    t = x[i+1];
    ty = y[i+1];
    if (x[i] <= t) goto c170;
    k = i;

c180:
    x[k+1] = x[k];
    y[k+1] = y[k];
    k = k-1;
    if (t < x[k]) goto c180;
    x[k+1] = t;
    y[k+1] = ty;
    goto c170;

//! Clean up
c190:
    if (kflag <= -1)
    {
        for (i = 0; i<nn; i++)
            x[i] = -x[i];
    }
}
void DecoderMs::sort(int n,double *arr)
{
    //double *tmp = new double[n];
    double *tmp = new double[n];
    ssort(arr,tmp,n,1);
    delete [] tmp;//delete [] tmp;
}
double DecoderMs::pctile(double *x,int begin_x,double *tmp,int nmax,double npct)
{
    for (int i = 0; i<nmax; i++)
    {//do i=1,nmax
        tmp[i]=x[i + begin_x];
    }

    sort(nmax,tmp);// garmi???
    //for (int i = 0; i<nmax; i++)
    //qDebug()<<i<<tmp[i];

    int j=(int)((double)nmax*0.01*npct);
    //if (j<1)
    //	j=1;
    if (j<0)
        j=0;
    if (j>nmax-1) j=nmax-1; //v1.35 if(j.gt.npts) j=npts
    //qDebug()<<"SSSSSSSSSSSSSSS"<<j<<tmp[j];
    return tmp[j];
}

void DecoderMs::tweak1(std::complex<double> *ca,int jz,double f0,std::complex<double> *cb)
{
    //double twopi = 0.0;
    std::complex<double> w,wstep;
    //std::complex<double> w4;
    //qDebug()<<"DEC_SAMPLE_RATE="<<DEC_SAMPLE_RATE;
    //if (twopi==0.0) twopi=8.0*atan(1.0);
    w= mk_complex(1.0, 1.0);
    double dphi=twopi*f0/DEC_SAMPLE_RATE;//11025.0;//DEC_SAMPLE_RATE;
    //wstep=cmplx(cos(dphi),sin(dphi));
    wstep= mk_complex(cos(dphi), sin(dphi));
    //w=w*wstep;

    for (int i = 0; i<jz; i++)
    {
        w=w*wstep;
        //w4=w;
        cb[i]=w*ca[i];
    }
}

void DecoderMs::indexx(int n,double *arr,int *indx)
{
    const int NMAX=3000;
    double brr[NMAX];
    double indx1[NMAX];
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (n>NMAX)
    {
        //print*,'n=',n,' too big in indexx.'
        return;
    }

    for (int i = 0; i<n; i++)
    {
        brr[i]=arr[i];
        indx1[i]=i;
    }
    //qDebug()<<n;
    ssort(brr,indx1,n,2);

    for (int i = 0; i<n; i++)
    {
        indx[i] = (int)indx1[i];
        //qDebug()<<indx[i];
    }
}
void DecoderMs::SetNexColor(bool f)
{
    f_back_color = f;
}
void DecoderMs::SetBackColor()
{
    if (f_back_color)
        f_back_color = false;
    else
        f_back_color = true;
    EmitBackColor(f_back_color);
}
void DecoderMs::wsjt1_mtdecode(double*raw_in,int count_in,bool pick)
{
    int disp_lines_dec_cou = 0;
    bool f_only_one_color = true;
    int nstep = 221;//20ms hv 221
    int istep = 221;//20ms hv
    int nz=count_in/nstep;
    double sigdb[3100];
    double base1;
    double work[3100];
    int nping = 0;

    int MinWidth=40;  //1.34 40 !Minimum width of pings, ms 40
    int slim = G_MinSigdB;     //it t74=0db t75=-20bd MinWidth !Minimum ping strength, dB

    if (slim<-2)//1.77 -2 no possible -3,-4...
        slim = -2;

    int DFTol = G_DfTolerance; //+-200hz da go tarsi i v fsk441 i jtms???
    int nf1=-G_DfTolerance;
    int nf2=G_DfTolerance;
    double wmin=0.001*(double)MinWidth*(19.95/20.0);//qDebug()<<wmin;
    //double wmin=0.001*(double)slim*(19.95/20.0);
    //qDebug()<<wmin;//=0,0399
    double pingdat_[100][3];
    double dt=1.0/DEC_SAMPLE_RATE;

    double ps[128]={0.0};
    //double *ps = new double[128];


    double mswidth = 0.0;//width ms in table
    int nrpt;// report
    double peak;// db report

    //itol=5                                       #Default tol=400 Hz
    //      0  1  2  3   4   5   6
    //ntol=(10,25,50,100,200,400,600)              #List of available tolerances

    //bool pick = true;// no pink
    int indx[3100];

    int msglen = 0;//??
    double bauderr = 0.0;
    noffset_fsk441_dfx =0;
    //f0 = 0.0;

    //! Find signal power at suitable intervals to search for pings.
    double dtbuf=(double)istep/DEC_SAMPLE_RATE;
    ///qDebug()<<dtbuf<<dt;
    for (int n = 0; n<nz; n++)
    {
        double s=0.0;
        int ib=(n+1)*istep;
        int ia=ib-istep+0;
        for (int i = ia; i<ib; i++)
        {
            //qDebug()<< i<<ia<<ib;
            s+=raw_in[i]*raw_in[i];
        }

        sigdb[n]=((double)s/(double)istep);
        //sigdb[n]*=0.01;
        //qDebug()<<n;//<<ib<<count_in;
    }
    ///////////////////////////////////////////////////////////////
    if (!pick)// tuk ima da se raboti HV
    {
        //! Remove initial transient from sigdb
        indexx(nz,sigdb,indx);
        //indexx_msk(sigdb,nz,indx);

        int imax=0;
        for (int i = 0; i<50; i++)
        {
            if (indx[i]>50) goto c10;
            imax=fmax(imax,indx[i]);
        }
c10:
        for (int i = 0; i<50; i++)
        {
            //if(indx(nz+1-i).gt.50) go to 20
            //imax=max(imax,indx(nz+1-i))
            if (indx[nz-1-i]>50) goto c20;
            imax=fmax(imax,indx[nz-1-i]);
        }
c20:
        imax+=6;            //!Safety margin  imax=imax+6            !Safety margin
        base1=sigdb[indx[nz/2]]; //qDebug()<< sigdb[indx[0]]*10.0 << sigdb[indx[nz/2]]*10.0<<sigdb[indx[nz-1]]*10.0;
        for (int i = 0; i<imax; i++)
            sigdb[i]=base1;
        //qDebug()<< imax << base1;
        //base1 *= 5.0;
    }
    //////////////////////////////////////////////////////////////
    smooth(sigdb,nz);
    base1 = pctile(sigdb,0,work,nz,50);

    if (!pick && base1<10000.0)//1.77
        base1 *= 1.6;//1.77 1.3to1.7 ok
    //qDebug()<<"TTTTTTTTTTTTTTTT"<< base1;

    if (base1==0.0)// no devide by zero
        base1=1.0;
    //QString sss = "";
    for (int i = 0; i<nz; i++)
    {
        sigdb[i]=pomAll.db(sigdb[i]/base1) - 1.0;//-1.0
        //sss.append(QString("%1").arg(sigdb[i],0,'f',1));
        //sss.append(",");
    }
    //qDebug()<<"222 mi="<<sss;
    nping = ping(sigdb,nz,dtbuf,slim,wmin,pingdat_);
    //qDebug()<<nping;
    //nping = 1;
    //! If this is a "mouse pick" and no ping was found, force a pseudo-ping
    //! at center of data.
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    //nping=0;
    if (pick && nping==0)// predizvikva pone edno 4etene ako e pod shuma slim i nping==0
    {
        pingdat_[nping][0]=(0.5*(double)count_in)*dt - 0.08;//1.34 = -0.1->dae w sredata  0.08   problem triabva da se oprawi
        //pingdat_[nping][0]=((double)count_in/2.0)*dt;// problem triabva da se oprawi

        if (s_mod==3)//fsk315
            pingdat_[nping][1]=0.34; //hv v1.04 0.16 to 0.34 tested fsk315
        else
            //fsk441 and jtms
            pingdat_[nping][1]=0.22; //hv v1.01 0.16 to 0.22 tested fsk441 and jtms

        pingdat_[nping][2]=1.0;
        if (nping<99)// max 99 v c++
            nping++;
    }
    //int iping = 0;
    //for (int i = 0; i<nping; i++)
    //qDebug()<<pingdat[0][i]<<pingdat[1][i]<<pingdat[2][i];
    //qDebug()<<nping;

    for (int iping = 0; iping<nping; iping++)
    {
        //! Find starting place and length of data to be analyzed:
        QStringList list;
        QString msg;
        int dfx_all = 0;
        double f0 = 0.0;
        double tstart=pingdat_[iping][0];
        double width=pingdat_[iping][1]; //+0.05
        //qDebug()<<"WidthP="<<tstart<<width;
        peak=pingdat_[iping][2];
        mswidth=10*(int)(100.0*width);

        //jj=(tstart-0.02)/dt ->hv 0.98
        int jj=(int)((double)(tstart-0.02)/dt);//0.02 here and void-chk441() start -20ms
        //qDebug()<<"jj="<<jj;
        //if(jj.lt.1) jj=1
        if (jj<0) jj=0;

        //jjz=nint((width+0.02)/dt)+1 // +0.02 -> hv ne decodira short pings v0.98
        int jjz;
        if (s_mod==3)//fsk315
            jjz=(int)(((double)(width+0.18)/dt)+1.0); //fsk315 v1.04 +0.36 or 0.18 tested  //
        else
            //fsk441 and jtms
            jjz=(int)(((double)(width+0.05)/dt)+1.0);//0.05 fsk441 here and void-chk441() HV v0.98 +0.06 from +0.02 for short pings 0.05->v1.43

        //jjz=min(jjz,jz+1-jj)
        //qDebug()<<jjz;
        //count_in = 330760;
        jjz=fmin(jjz,count_in+1-jj);
        //qDebug()<<"beg="<<jj<<"end"<<jjz<<"jj+jjz="<<jj+jjz<<"all="<<count_in;

        //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        //! Assemble a signal report:
        double nwidth=0.0;
        /*if (width>=0.04) nwidth=1;  //k1jt !These might depend on NSPD
        if (width>=0.12) nwidth=2;
        if (width>1.00) nwidth=3;*/

        if (width>=0.0) nwidth=2;     //lz2hv duration Hanbook 7.00
        if (width>=0.5) nwidth=3;
        if (width>=1.0) nwidth=4;
        if (width>=5.0) nwidth=5;

        int nstrength=6;
        /*if (peak>=11.0) nstrength=7; //k1jt
        if (peak>=17.0) nstrength=8;
        if (peak>=23.0) nstrength=9;*/

        if (peak>=5.0) nstrength=7;    //lz2hv S-unit Hanbook 7.00
        if (peak>=11.0) nstrength=8;   //be6e 10
        if (peak>=15.0) nstrength=9;

        nrpt=10*nwidth + nstrength;
        //int istart = s_time_sec;
        double t2=tstart + dt*(s_in_istart);//-1 ne pri men start at 0


        //qDebug()<<"T2="<<t2<<iping;
        //int jj = 0;
        //int jjz = count_in;
        if (s_mod == 2 || s_mod == 3)//fsk441 fsk315
        {   //qDebug()<<"Count="<<count_in<<"Start="<<jj<<"Width="<<jjz;
            //! Compute average spectrum of this ping.

            f0 = spec441(raw_in,jj,jjz,ps);
            //longx(dat(jj),jjz,ps,DFTolerance,noffset,msg,msglen,bauderr)???? dat(jj)
            //qDebug()<<"WidthP="<<jj<<jjz<<f0;
            /*QString sss = "";///gen_osd174_[174][87];
            for (int z= 0; z < 128; z++)//decoded=87   cw-174 
            {
                sss.append(QString("%1").arg((int)ps[z]));
                sss.append(",");
            }
            qDebug()<<"222 mi="<<sss;*/
            msg = longx(raw_in,jj,jjz,ps,DFTol,msglen,bauderr,s_mod);
            //msg->setfont
            dfx_all = (int)noffset_fsk441_dfx;
            //dfx_all = 401;
            //s_time_sec =t2;

            int nfreeze = 0;
            int mousedf = 0;
            int nok = 0;
            double dfx_real_hv = 0.0;

            //double width1 = 0.120;
            nok = chk441(raw_in,count_in,tstart,width,nfreeze,mousedf,DFTol,pick,s_mod,dfx_real_hv);
            //qDebug()<<nok;
            //qDebug()<<"nping="<<nping<<iping<<"msglen="<<msglen<<"nok="<<nok<<tstart<<width;
            if (msglen==0 || nok==0)//!Reject non-FSK441 signals
                goto c100;

            //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
            //if(.not.pick .and. ((noffset.lt.nf1 .or. noffset.gt.nf2))) goto 100
            //qDebug()<<dfx_all<<nf1<<dfx_all<<nf2;
            if (!pick && ((dfx_all<nf1 || dfx_all>nf2)))
                goto c100;

            dfx_all = (int)dfx_real_hv;//HV v1.09
        }


        if (s_mod == 1)//jtms
        {
            int jjzz=fmin(jjz,2*DEC_SAMPLE_RATE); // !Max data size 2 s
            //qDebug()<<"dat_c_beginJTMS="<<jj<<jjz;
            jtms(raw_in,jj,jjzz,DFTol,t2,mswidth,int(peak),nrpt,pick,f_only_one_color,disp_lines_dec_cou);
            goto c100;
        }

        if (f_only_one_color)
        {
            f_only_one_color = false;
            SetBackColor();
        }
        list <<s_time<<QString("%1").arg(t2,0,'f',1)<<QString("%1").arg(mswidth)<<
        QString("%1").arg((int)peak)<<QString("%1").arg(nrpt)<<QString("%1").arg(dfx_all)
        <<msg<<QString("%1").arg((int)f0);
        EmitDecodetText(list,s_fopen,true);//1.27 psk rep   fopen bool true    false no file open

        if (s_mousebutton == 0 && disp_lines_dec_cou < MAX_DISP_DEC_COU) //mousebutton Left=1, Right=3 fullfile=0 rtd=2
        {
            disp_lines_dec_cou++;
            EmitDecLinesPosToDisplay(disp_lines_dec_cou,t2,t2,s_time);
        }

c100:
        continue;
    }
}
void DecoderMs::set_double_to_d(double a,double*y,int n)
{
    for (int i = 0; i<n; i++)
        y[i]=a;
}

void DecoderMs::spec2d(double *data,int jz,double &sigma)
{
//! Computes 2d spectrogram for FSK441 single-tone search and waterfall
//! display.
    const int NFFT=256;
    const int NR=NFFT+2;
    const int NH=NFFT/2;
    int NQ=NFFT/4;
    double w1[7];
    double w2[7];
    double ps2[128];
    //complex c(0:NH)
    double x[NR];
    std::complex<double> c[NR];//2.09 ->[NH];
    double s[3100];
    int indx[3100];
    //double *s = new double[3100];
    //int *indx = new int[3100];
    //common/fcom/s(3100),indx(3100)
    //equivalence (x,c)
    //save
    double psavg0[128];
    double psavg[128];
    int nstep = 221;
    int nz=jz/nstep - 0;
    //int nchan=64;
    double s2_[3100][64];
    double df=(double)DEC_SAMPLE_RATE/(double)NFFT;
    //double base = 0.0;
    double afac;
    double smaxx;

    pomAll.zero_double_beg_end(psavg0,0,128);

    //! Compute the 2d spectrogram s2(nchan,nz).  Note that in s2 the frequency
    //! bins are shifted down 5 bins from their natural positions.
    int bins=5;

    set_double_to_d(0.0,psavg,NH);
    for (int n = 0; n<nz; n++)
    {//do n=1,nz
        //j=1 + (n-1)*nstep
        int j=(n)*nstep;//qDebug()<<j<<NFFT;
        //move(data(j),x,NFFT)
        move_da_to_da(data,j,x,0,NFFT);
        xfft(c,x,NFFT);

        double sum=0.0;
        for (int i = 0; i<NQ; i++)
        {//do i=1,NQ
            //s2[i][n]=real(c(5+i))**2 + aimag(c(5+i))**2
            s2_[n][i]=pomAll.ps_hv(c[bins+i]);
            sum=sum+s2_[n][i];
            //qDebug()<<i<<n;
        }
        s[n]=sum/(double)NQ;

        //! Accumulate average spectrum for the whole file.
        for (int i = 0; i<NH; i++)
            //do i=1,nh
            //psavg0(i) = psavg0(i)+ real(c(i))**2 + aimag(c(i))**2
            psavg0[i] = psavg0[i] + pomAll.ps_hv(c[i]);

    }

    //! Normalize and save a copy of psavg0 for plotting.  Roll off the
    //! spectrum at 300 and 3000 Hz.
    for (int i = 0; i<NH; i++)
    {//do i=1,nh
        psavg0[i]=3.e-5*psavg0[i]/(double)nz;// v 1.11
        double f=df*(double)(i+0);
        double fac=1.0;
        //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        if (f<300.0) fac=f/300.0;
        if (f>3000.0) fac=fmax(0.00333,(3300.0-f)/300.0);
        //psavg0(i)=(fac**2)*psavg0(i)
        psavg0[i]=(fac*fac)*psavg0[i];
    }

    //! Compute an average spectrum from the weakest 25% of time slices.
    //qDebug()<<nz;
    indexx(nz,s,indx);
    //indexx_msk(s,nz-1,indx);

    //for(int i = 0; i < nz; i++)
    //qDebug()<<"index="<<indx[i];

    //call zero(ps2,NQ)
    pomAll.zero_double_beg_end(ps2,0,NQ);// NQ e end
    for (int j = 0; j<nz/4; j++)
    {//do j=1,nz/4
        int k=indx[j];
        for (int i = 0; i<NQ; i++)
            //do i=1,NQ
            //ps2(i+5)=ps2(i+5)+s2(i,k)
            ps2[i+bins]=ps2[i+bins]+s2_[k][i];
    }
    //ps2(1)=ps2(5)
    //ps2(2)=ps2(5)
    //ps2(3)=ps2(5)
    //ps2(4)=ps2(5)
    ps2[0]=ps2[4];
    ps2[1]=ps2[4];
    ps2[2]=ps2[4];
    ps2[3]=ps2[4];

    double sum=0.0;
    for (int i = bins; i<59; i++)//
        //do i=6,59
        sum=sum+ps2[i];

    if (sum==0.0)
    {
        sigma=-999.0;
        goto c999;
    }

    //! Compute a smoothed spectrum without local peaks, and find its max.
    smaxx=0.0;
    for (int i = 3; i<NQ; i++)
    {//do i=4,NQ
        sum=0.0;
        for (int k = 0; k<7; k++)
        {//do k=1,7
            //w1(k)=ps2(i+k-4)
            w1[k]=ps2[i+k-3];
            sum=sum+w1[k];
        }
        double ave=sum/7.0;
        //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        //if(i.ge.14 .and. i.le.58) then
        if (i>=13 && i<=58)
        {
            //base = pctile(w1,0,w2,7-0,50);
            pctile(w1,0,w2,7,50);
            //ave=0.25*(w2(1)+w2(2)+w2(3)+w2(4))
            ave=0.25*(w2[0]+w2[1]+w2[2]+w2[3]);
        }
        //qDebug()<<ave;
        psavg[i]=ave;
        smaxx=fmax(psavg[i],smaxx);
    }

    //! Save scale factors for flattening spectra of pings.
    a1_=1.0;
    a2_=psavg[int(2*441/df)]/psavg[int(3*441/df)];//for fsk315 may be need change
    a3_=psavg[int(2*441/df)]/psavg[int(4*441/df)];//for fsk315 may be need change
    a4_=psavg[int(2*441/df)]/psavg[int(5*441/df)];//for fsk315 may be need change
    afac=4.0/(a1_+a2_+a3_+a4_);
    a1_=afac*a1_;
    a2_=afac*a2_;
    a3_=afac*a3_;
    a4_=afac*a4_;
    //a1_=0.413093;  //0.413093 0.73435 1.23157 1.62098
    //a2_=0.73435;
    //a3_=1.23157;
    //a4_=1.62098;
    //qDebug()<<"spec2d"<<a1_<<a2_<<a3_<<a4_;
    //! Normalize 2D spectrum by the average based on weakest 25% of time
    //! slices, smoothed, and with local peaks removed.
    for (int i = 0; i<NQ; i++)//nq=64
    {//do i=1,NQ
        for (int j = 0; j<nz; j++)
            //do j=1,nz
            //s2(i,j)=s2(i,j)/max(psavg(i+5),0.01*smaxx)
            s2_[j][i]=s2_[j][i]/fmax(psavg[i+bins],0.01*smaxx);
        //enddo
    }

    //! Find average of active spectral region, over the whole file.
    sum=0.0;
    for (int i = 8; i<52; i++)
    {//do i=9,52
        for (int j = 0; j<nz; j++)
            //do j=1,nz
            sum=sum+s2_[j][i];
        //enddo
    }
    sigma=sum/(double)(44*nz);

c999:
    return;
}
void DecoderMs::bzap(double*dat,int jz,int nadd,int mode,double*fzap)//,double*fzap
{
    //Find and remove birdies
    int NMAX=1024*1024;
    //int NMAXH=NMAX;

    double *x = new double[NMAX+10];
    std::complex<double> *c = new std::complex<double>[NMAX+10];
    //equivalence (x,c)

    double xn=log(double(jz))/log(2.0);
    int n=(int)xn;
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if ((xn-n)>0.0)
        n++;

    int nfft=pow(2,n);

    if (nadd==0)// no devide by zero
        nadd=1;

    int nh=nfft/nadd;
    int nq=nh/2;
    for (int i = 0; i<jz; i++)
    {//do i=1,jz
        x[i]=dat[i];
    }
    if (nfft>jz)
        //zero(x(jz+1),nfft-jz)
        pomAll.zero_double_beg_end(x,jz,nfft);//HV pri men e taka v1.17

    xfft(c,x,nfft);

    //! This is a kludge:
    double df=DEC_SAMPLE_RATE/(double)(nadd*nfft);

    if (mode==7 || mode==8 || mode==9) //JT65abc ->mode  100
        df=DEC_SAMPLE_RATE/(double)(2*nadd*nfft);

    double tol=10.0;
    int itol=int(2.0/df);
    for (int izap = 0; izap<200; izap++)
    {//do izap=1,200
        //qDebug()<<fzap[izap];
        if (fzap[izap]==0.0) goto c10;

        int ia=(fzap[izap]-tol)/df;
        int ib=(fzap[izap]+tol)/df;
        double smax=0.0;
        int ipk=0; //!Shut up compiler warnings. -db
        //qDebug()<<ia<<ib;
        for (int i = ia+0; i<=ib+0; i++)
        {//do i=ia+1,ib+1
            //s=real(c(i))**2 + aimag(c(i))**2
            //qDebug()<<"GGGGG"<<izap<<i;
            double s=pomAll.ps_hv(c[i]);
            if (s>smax)
            {
                smax=s;
                ipk=i;
            }

        }
        //fzap(izap)=df*(ipk-1)
        fzap[izap]=df*(ipk-0);
        for (int i = ipk-itol; i<=ipk+itol; i++)
        {//do i=ipk-itol,ipk+itol
            c[i]=complex_zero;
        }
    }
c10:

    int ia=70.0/df;
    for (int i = 0; i<ia; i++)
        //do i=1,ia
        c[i]=complex_zero;

    ia=2700.0/df;
    for (int i = ia; i<nq+0; i++)
        //do i=ia,nq+1
        c[i]=complex_zero;

    for (int i = 1; i<nq; i++)
        //do i=2,nq
        //c(nh+2-i)=conjg(c(i))
        c[nh+1-i]=conj(c[i]);

    f2a->four2a_d2c(c,x,nh,1,-1);
    double fac=(double)1.0/nfft;
    for (int i = 0; i<jz/nadd; i++)
    {
        //do i=1,jz/nadd
        dat[i]=fac*x[i];
        //qDebug()<<dat[i];
    }

    delete [] x;
    delete [] c;
}
void DecoderMs::ps(double *dat,int dat_begin,int nfft,double*s)
{
    const int NMAX=16384+2;
    const int NHMAX=(NMAX/2)-1;
    //real dat(nfft)
    //real s(NHMAX)
    double x[NMAX];
    //complex c(0:NHMAX)
    std::complex<double> c[NHMAX];
    //equivalence (x,c)

    int nh=nfft/2;
    for (int i = 0; i<nfft; i++)
    {//do i=1,nfft
        x[i]=dat[i+dat_begin]/128.0;       //!### Why 128 ??
    }

    xfft(c,x,nfft);
    double fac=(double)1.0/nfft;
    for (int i = 0; i<nh; i++)
    {//do i=1,nh
        //s(i)=fac*(real(c(i))**2 + aimag(c(i))**2)
        s[i]=fac*pomAll.ps_hv(c[i]);
        //s[i]=fac*(creal(c[i])*creal(c[i]) + cimag(c[i])*cimag(c[i]));
    }

}
void DecoderMs::add_da_da2_da(double *a,double b_[324][558],int b_beg,int b_row,double *c,int n)
{
    /*subroutine add(a,b,c,n)
     real a(n),b(n),c(n)
     do i=1,n
        c(i)=a(i)+b(i)
     enddo
     return
      end subroutine add*/
    //call add(psa,s2(1,j),psa,nbins)
    //add_dd_to_d_s2(psa,s2,0,j,psa,nbins);
    for (int i = 0; i<n; i++)
        c[i]=a[i]+b_[b_row][i+b_beg];
}
void DecoderMs::flatten(double s2_[324][558],int nbins,int jz,double *psa,double *ref,double *birdie,double *variance)
{

    double ref2[750];                  //!Work array
    double power[750];//hv 750 e be6e sbyrkano i garmi
    double facmax;
    int kpk;
    int nsum;
    //jz=75;
//zero_double_beg_count(power,0,750);
//! Find power in each time block, then get median
    for (int j = 0; j<jz; j++)
    {//do j=1,jz
        double s=0.0;
        for (int i = 0; i<nbins; i++)
        {//do i=1,nbins
            s=s+s2_[j][i];
        }
        power[j]=s; //qDebug()<<s;
    }
    //pctile(power,ref2,jz,50,xmedian)
    double xmedian = pctile(power,0,ref2,jz,50);
    //xmedian = xmedian +2.0;
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (jz<5) goto c900;

//! Get variance in each freq channel, using only those spectra with
//! power below the median.

    for (int i = 0; i<nbins; i++)
    {//do i=1,nbins
        double s=0.0;
        nsum=0;
        //qDebug()<<"flatten111"<<nbins<<jz;
        for (int j = 0; j<jz; j++)
        {//do j=1,jz
            if (power[j]<=xmedian)
            {
                s=s+s2_[j][i];
                //nsum=nsum+1;
                nsum++;
            }
        }

        if (nsum==0)// no devide by zero
            nsum = 1;
        s=(double)s/nsum;
        if (s==0.0)// no devide by zero
            s = 1.0;
        double sq=0.0;
        for (int j = 0; j<jz; j++)
        {//do j=1,jz
            if (power[j]<=xmedian)
                //sq=sq + (s2(i,j)/s-1.0)**2
                sq=sq + (((s2_[j][i]/s)-1.0)*((s2_[j][i]/s)-1.0));
        }
        variance[i]=(double)sq/nsum;
    }

    //! Get grand average, and average of spectra with power below median.
    //zero(psa,nbins)
    pomAll.zero_double_beg_end(psa,0,nbins);
    //call zero(ref,nbins)
    pomAll.zero_double_beg_end(ref,0,nbins);
    nsum=0;
    for (int j = 0; j<jz; j++)
    {//do j=1,jz
        //call add(psa,s2(1,j),psa,nbins)  s2[557][323];
        //(double *a,double b[558][324],int b_row,double *c,int n)
        add_da_da2_da(psa,s2_,0,j,psa,nbins);
        if (power[j]<=xmedian)
        {
            //call add(ref,s2(1,j),ref,nbins)
            add_da_da2_da(ref,s2_,0,j,ref,nbins);//HV 0.99 be6e gre6no psa vmesto ref-> add_dd_to_d_s2(psa,s2,0,j,ref,nbins);
            //nsum=nsum+1;
            nsum++;
        }
    }
    if (nsum==0)// no devide by zero
        nsum = 1;
    for (int i = 0; i<nbins; i++)
    {//do i=1,nbins                          //!Normalize the averages
        psa[i]=(double)psa[i]/jz;
        ref[i]=(double)ref[i]/nsum;
        birdie[i]=ref[i];                   //!Copy ref into birdie
    }

    kpk=0; //!shut up compiler warnings -db
    //! Compute smoothed reference spectrum with narrow lines (birdies) removed
    for (int i = 3; i<nbins-3; i++)
    {//do i=4,nbins-3
        double rmax=-1.e10;
        for (int k = i-3; k<i+3; k++)
        {//do k=i-3,i+3                  //!Get highest point within +/- 3 bins
            //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
            if (ref[k]>rmax)
            {
                rmax=ref[k];
                kpk=k;
            }
        }
        double sum=0.0;
        nsum=0;
        for (int k = i-3; k<i+3; k++)
        {//do k=i-3,i+3
            if (abs(k-kpk)>1)
            {
                sum=sum+ref[k];
                //nsum=nsum+1;
                nsum++;
            }
        }
        if (nsum==0)// no devide by zero
            nsum = 1;
        ref2[i]=(double)sum/nsum;
    }
    //call move(ref2(4),ref(4),nbins-6)     //!Copy smoothed ref back into ref
    move_da_to_da(ref2,3,ref,3,nbins-6);

    //call pctile(ref(4),ref2,nbins-6,50,xmedian)  //!Get median in-band level
    xmedian = pctile(ref,3,ref2,nbins-6,50);

    //! Fix ends of reference spectrum
    for (int i = 0; i<3; i++)
    {//do i=1,3
        //ref(i)=ref(4)
        //ref(nbins+1-i)=ref(nbins-3)
        ref[i]=ref[3];
        ref[nbins+0-i]=ref[nbins-3];// ?????
    }
    if (xmedian==0.0)// no devide by zero
        xmedian = 1.0;
    facmax=30.0/xmedian;
    for (int i = 0; i<nbins; i++)
    {//do i=1,nbins                          //!Flatten the 2d spectrum
        double devid=ref[i];
        if (devid==0.0)
            devid=1.0;
        double fac=xmedian/devid;
        fac=fmin(fac,facmax);
        for (int j = 0; j<jz; j++)
        {//do j=1,jz
            s2_[j][i]=fac*s2_[j][i];
            //qDebug()<<s2[i][j];
        }
        psa[i]=pomAll.db(psa[i]) + 25.0;
        ref[i]=pomAll.db(ref[i]) + 25.0;
        birdie[i]=pomAll.db(birdie[i]) + 25.0;
    }

c900: //continue;
    return;
}
void DecoderMs::move_da_to_da2(double *x,double y_[324][558],int b,int k,int n)
{
    //real x(n),y(n)
    //move(s2db(0,j),s2db(0,k),44)
    //move(psa,s2(1,j),iz);
    for (int i = 0; i<n; i++)
        y_[k][i+b]=x[i];

}
void DecoderMs::avesp2(double *dat,int jza,int nadd,int mode,bool NFreeze,int MouseDF,int DFTolerance,double *fzap)
{
    //real dat(jza)
    //integer DFTolerance
    double psa[1025];                 //!Ave ps, flattened and rolled off
    double ref[558];                  //!Ref spectrum, lines excised
    double birdie[558];               //!Birdie spectrum (ave-ref)
    double variance[558];
    double s2_[324][558];
    //double fzap(200)

    int iz=557;                             // !Compute the 2d spectrum
    double df=DEC_SAMPLE_RATE/2048.0;
    int nfft=nadd*1024;
    int jz=jza/nfft;
    for (int j = 0; j<jz; j++)
    {//do j=1,jz
        //k=(j-1)*nfft + 1;
        int k=(j-0)*nfft + 1;
        ps(dat,k,nfft,psa);
        //move(psa,s2(1,j),iz);
        move_da_to_da2(psa,s2_,0,j,iz);
    }
    //qDebug()<<"JZ="<<jz;
    //! Flatten s2 and get psa, ref, and birdie
    //jz = 75;//HV pri jz > 90 problem

    flatten(s2_,557,jz,psa,ref,birdie,variance);
    //flatten(s2,557,80,psa,ref,birdie,variance);

    //call zero(fzap,200)
    pomAll.zero_double_beg_end(fzap,0,200);

    int ia=300/df;
    int ib=2700/df;
    int n=0;
    double fmouse=0.0;
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (mode==7 || mode==8 || mode==9) fmouse=1270.46+MouseDF; //JT65abc mode pri men
    if (mode==6) fmouse=1076.66+MouseDF; // JT6M mode pri men 5 v104

    for (int i = ia; i<=ib; i++)//hv hv hvv
    {//do i=ia,ib

        if (birdie[i]-ref[i]>3.0)
        {
            //qDebug()<<"b r "<<birdie[i]<<ref[i];
            double f=i*df;
            //qDebug()<<variance[i];
            //Don't zap unless Freeze is OFF or birdie is outside the "Tol" range.
            //if(NFreeze.eq.0 .or. abs(f-fmouse).gt.float(DFTolerance))
            if (NFreeze==0 || fabs(f-fmouse)>double(DFTolerance))
            {
                //if(n.lt.200 .and. variance(i-1).lt.2.5 .and. variance(i).lt.2.5.and.variance(i+1).lt.2.5)
                if (n<200 && variance[i-1]<2.5 && variance[i]<2.5 && variance[i+1]<2.5)
                {
                    //n=n+1;
                    fzap[n]=f;
                    //qDebug()<<fzap[n];
                    n++;
                }
            }

        }
    }
}
void DecoderMs::CalcZapDat()
{
    double sum=0.0;
    for (int i = 0; i<s_zap_count; i++)
        //do i=1,jz
        sum=sum+s_zap_in[i];

    int nave=int(sum/(double)s_zap_count);
    for (int i = 0; i<s_zap_count; i++)
    {//do i=1,jz
        //raw[i]=raw[i]-nave;
        s_zap_in[i]=s_zap_in[i]-nave;
    }//enddo

    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (s_zap_count>14*DEC_SAMPLE_RATE)//kogato e celia region ili >14s
        dtrim(s_zap_in,s_zap_count);

    sum=0.0;
    for (int i = 0; i<s_zap_count; i++)//!Convert raw data from i*2 to real, remove DC
    {
        s_zap_dat[i]=0.1*(double)s_zap_in[i];
        sum+=s_zap_dat[i];
    }
    if (s_zap_count==0)// no devide by zero
        s_zap_count=1;
    double ave=sum/(double)s_zap_count;

    for (int j = 0; j<s_zap_count; j++)
        s_zap_dat[j]=(double)s_zap_dat[j]-ave;
}
//#include <QTime>
static bool have_dec0_ = false;
void DecoderMs::StrtDecode() {
    if (s_f_rtd || s_mod == 11 || s_mod == 13 || allq65) // ft8 ft4 q65
        usleep(20000);                                   // 1.30 important time>10ms 20000us=20ms slab PC pri auto decode da zabavi malko pri postoqnno decodirane hv
    else
        usleep(100000); // 1.30 important time>10ms 100000us=100ms slab PC pri auto decode da zabavi malko pri postoqnno decodirane hv

    double sq = 0.0;
    // double degrade = 1.0;
    int nforce = 1; // force_decode
    bool pick;

    //debugPrintf("StrtDecode ok");

    if ((s_mod != 0 && s_mod != 7 && s_mod != 8 && s_mod != 9 && s_mod != 10 && s_mod != 11 && s_mod != 12 &&
         s_mod != 13 && !allq65) &&
        s_nzap) // 1.52 msk144 JT65abc ft8 pi4 ft4  no zap
    {           // if(mode.ne.2 .and. nzap.ne.0) then // nzap e check box
        // qDebug()<<"2ZAP Start";
        double fzap[200]; // pri men ne se izpolzva jt65-jt4
        pomAll.zero_double_beg_end(fzap, 0, 200);

        bool nfrz = false; // e chesk box Freeze
        int MouseDF = 0;
        // if(mode==1) nfrz=0;//neznam
        // c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.

        if (s_mousebutton == 0 && s_static_dat_count > 100000) // mousebutton Left=1, Right=3 fullfile=0 rtd=2
        {
            avesp2(static_dat0, s_static_dat_count, 2, s_mod, nfrz, MouseDF, G_DfTolerance, fzap);
        }
        else if (s_zap_count > 100000) {
            // qDebug()<<"ZAP Start";
            CalcZapDat();
            // qDebug()<<"ZAP Calc"<<s_zap_count;
            avesp2(s_zap_dat, s_zap_count, 2, s_mod, nfrz, MouseDF, G_DfTolerance, fzap);
        }
        int nadd = 1;
        // qDebug()<<"ZAP BZAP"<<s_static_dat_count;
        bzap(static_dat0, s_static_dat_count, nadd, s_mod, fzap); // fzap
    }

    sq = 0.0;
    for (int j = 0; j < s_static_dat_count; j++) { //! Compute power level for whole array
        sq = sq + static_dat0[j] * static_dat0[j];
    }
    double avesq = (double)sq / s_static_dat_count;
    s_basevb = pomAll.db(avesq) - 44.0; //! Base power level
    if (avesq == 0.0)
        goto c900;

    pick = false;
    if (s_mousebutton != 0) // mousebutton Left=1, Right=3 fullfile=0 rtd=2
        pick = true;        //! This is a mouse-picked decoding
    if (!pick && nforce == 0 && (s_basevb < -15.0 || s_basevb > 20.0))
        goto c900;

    if (s_mod == 0 || s_mod == 12) // msk144 msk144ms
    {
        bool f_mskms = false;
        if (s_mod == 12) // msk144ms
            f_mskms = true;

        if (s_f_rtd)
            msk_144_40_rtd(static_dat0, s_static_dat_count, s_in_istart, f_mskms);
        else
            msk_144_40_decode(static_dat0, s_static_dat_count, s_in_istart, f_mskms);
    }
    else if (s_mod == 11) // one thread ft8
    {
        DecFt8_0->ft8_decode(static_dat0, s_static_dat_count, s_f00, s_f01, s_nfqso_all, have_dec0_, id3decFt, s_f00, s_f01);
        ResetDupThr();
        goto c990;
    }
    else if (s_mod == 13) // ft4
    {
        DecFt4_0->ft4_decode(static_dat0, s_f00, s_f01, s_f00, s_f01, s_nfqso_all, have_dec0_);
        ResetDupThr();
        goto c990;
    }
    else if (allq65) // q65
    {
        DecQ65->q65_decode(static_dat0, s_f00, s_f01, s_nfqso_all, s_mod, have_dec0_);
        if (is_thrTime) {
            if (have_dec0_) {
                //                float ftmp =(float)thrTime->elapsed()*0.001;
                //                emit EmitTimeElapsed(ftmp);
            }
            //            delete thrTime;
            is_thrTime = false;
        }
        id3decFt = 0;
        // ResetDupThr();
        // goto c990;
    }
    else if (s_mod == 1 || s_mod == 2 || s_mod == 3) // jtms fsk144 fsk315
    {
        if (s_mod == 2 || s_mod == 3) // fsk441 fsk315
        {
            double sigma = 0.0;
            spec2d(static_dat0, s_static_dat_count, sigma);
            // c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
            // if(sigma<0.0) basevb=-99.0;
            // qDebug()<<"sigma1"<<sigma;
            if (sigma < 0.0)
                goto c900;
            // qDebug()<<"sigma2"<<sigma;
        }
        wsjt1_mtdecode(static_dat0, s_static_dat_count, pick); // 30sec static dufer for decode
    }
    else if (s_mod == 4) // iskat-a
    {
        int mode4 = 1;
        abort();
//        wsjt1_iscat(static_dat0, s_static_dat_count, mode4, pick);
    }
    else if (s_mod == 5) // iskat-b
    {
        int mode4 = 2;
        abort();
//        wsjt1_iscat(static_dat0, s_static_dat_count, mode4, pick);
    }
    else if (s_mod == 6) { // jt6m
        abort();
        //wsjt1_jt6m(static_dat0, s_static_dat_count, s_basevb);
    }
    else if (s_mod == 7) // jt65a
    {
        // int n2pass = 1;
        // int mode65 = 1;
        abort();
//        jt65_decode(static_dat0, s_static_dat_count, 1);
    }
    else if (s_mod == 8) // jt65b
        abort();
//        jt65_decode(static_dat0, s_static_dat_count, 2);
    else if (s_mod == 9) // jt65c
        abort();
//        jt65_decode(static_dat0, s_static_dat_count, 4);
    else if (s_mod == 10) {                           // pi4
        abort();
//        pi4_decode(static_dat0, s_static_dat_count); // for test
    }


c900:
    f2a->DestroyPlansAll(false);
    EmitDecodeInProgresPskRep(false);
    //qDebug()<<"1-DestroyPlansAll";
c990:  //2.40
    if (s_end_rtd)
    {
        if (ss_msk144ms && s_f_rtd)
            EndRtdPeriod();
        EmitDecode(false,0);
    }
    else
        EmitDecode(false,2);

    if (is_ftBuff)
    {
        usleep(25000);
        thred_busy = false;
        SETftBuff();
    }
    else
        thred_busy = false;
    //qDebug()<<"One Thread False thred_busy";
}
void *DecoderMs::ThreadDecode(void *argom)
{
    DecoderMs* pt = (DecoderMs*)argom;
    pt->StrtDecode();
    pthread_detach(pt->th); // inportant delete thread memory
#ifndef __wasm__
    pthread_exit(NULL);
#endif
    return NULL;
}
/*void DecoderMs::SetDecodetTextFt(QStringList list)
{
    bool ldupe = false;
    QString message = list.at(4);

    QString sf1 = list.at(7);
    int f1 = sf1.toInt();
    int f1tool = 6;
    if (s_mod == 13) f1tool = 10;

    for (int id = 0; id < dup_camsgf_thr; ++id)
    {
        if (message==dup_amsgs_thr[id] && abs(dup_afs_thr[id]-f1)<f1tool)
        {
            //qDebug()<<"FT DEC Dupe="<<message<<abs(dup_afs_thr[id]-f1)<<list.at(1);
            ldupe=true;
            break;
        }
    }
    if (ldupe) return;

    dup_amsgs_thr[dup_camsgf_thr]=message;
    dup_afs_thr[dup_camsgf_thr]=f1;
    if (dup_camsgf_thr < (MAXDUPMSGTHR-1)) dup_camsgf_thr++;

    QString tstr = message;//list.at(4);
    tstr.remove("<");//v2 protokol 2.02
    tstr.remove(">");//v2 protokol 2.02
    QStringList tlist = tstr.split(" ");
    bool fmyc = false;
    for (int x = 0; x<tlist.count(); ++x)
    {
        if (tlist.at(x)==s_MyBaseCall || tlist.at(x)==s_MyCall)//2.02
        {
            fmyc = true;
            break;
        }
    }
    bool forme = false;
    if (abs((int)s_nfqso_all-(int)f1)<=10 || fmyc)
    {
        emit EmitDecodetTextRxFreq(list,true,true);//1.60= true no emit other infos from decode list2 s_fopen
        forme = true;
    }

    emit EmitDecodetText(list,s_fopen,forme);//2.43
    //qDebug()<<"AlFreq"<<list;
}
void DecoderMs::SetDecodetTextQ65(QStringList list)//,bool to2list
{
    QString message = list.at(4);
    QString sf1 = list.at(7);
    int f1 = sf1.toInt();

    QString tstr = message;
    tstr.remove("<");
    tstr.remove(">");
    QStringList tlist = tstr.split(" ");
    bool fmyc = false;
    //if (to2list)
    //{
    for (int x = 0; x<tlist.count(); ++x)
    {
        if (tlist.at(x)==s_MyBaseCall || tlist.at(x)==s_MyCall)//2.02
        {
            fmyc = true;
            break;
        }
    }
    if (abs((int)s_nfqso_all-(int)f1)<=10 || fmyc)
    {
        emit EmitDecodetTextRxFreq(list,true,true);//1.60= true no emit other infos from decode list2 s_fopen
    }
    //}
    emit EmitDecodetText(list,s_fopen,true);//2.43
}*/
void DecoderMs::SetDecodetTextFtQ65(QStringList list)//2.66
{
    QString message = list.at(4);
    QString sf1 = list.at(7);
    int f1 = sf1.toInt();

    if (s_mod == 11 || s_mod == 13)//ft8/4
    {
    	bool ldupe = false;
        int f1tool = 6;
        if (s_mod == 13) f1tool = 10;
        for (int id = 0; id < dup_camsgf_thr; ++id)
        {
            if (message==dup_amsgs_thr[id] && abs(dup_afs_thr[id]-f1)<f1tool)
            {
                ldupe = true;
                break;
            }
        }
        if (ldupe) return;
        dup_amsgs_thr[dup_camsgf_thr]=message;
        dup_afs_thr[dup_camsgf_thr]=f1;
        if (dup_camsgf_thr < (MAXDUPMSGTHR-1)) dup_camsgf_thr++;
    }

    QString tstr = message;
    tstr.remove("<");
    tstr.remove(">");
    QStringList tlist = tstr.split(" ");
    bool fmyc = false;
    for (int x = 0; x<tlist.count(); ++x)
    {
        if (tlist.at(x)==s_MyBaseCall || tlist.at(x)==s_MyCall)//2.02
        {
            fmyc = true;
            break;
        }
    }
    bool forme = false;
    if (abs((int)s_nfqso_all-(int)f1)<=10 || fmyc)
    {
        EmitDecodedTextRxFreq(list,true,true);//1.60= true no emit other infos from decode list2 s_fopen
        forme = true;
    }
    if (allq65) forme = true;

    EmitDecodetText(list,s_fopen,forme);
}
void DecoderMs::CreateStartTimerthr()
{
//    thrTime = new QElapsedTimer();
    is_thrTime = true;
//    thrTime->start(); //qDebug()<<"CreateTimer++++++";
}
static bool have_dec1_ = false;
static bool have_dec2_ = false;
static bool have_dec3_ = false;
static bool have_dec4_ = false;
static bool have_dec5_ = false;
static bool have_decALL3_ = false;
static bool thr_only_one_color = true;
void DecoderMs::ResetDupThr()
{
    //static bool have_decALL3_ = false; <- no use
    if (have_dec0_ || have_dec1_ || have_dec2_ || have_dec3_  || have_dec4_ || have_dec5_) have_decALL3_ = true;
    //qDebug()<<"1-DestroyPlansAll"<<have_decALL3_<<id3decFt;
    if (id3decFt>2)
    {
        //qDebug()<<have_dec0_;
        if (is_thrTime)
        {
            if (have_decALL3_)
            {
//                float ftmp =(float)thrTime->elapsed()*0.001;
//                emit EmitTimeElapsed(ftmp);
            }
//            delete thrTime;
            is_thrTime = false;
            //qDebug()<<"DeleteTimer------";
        }

        id3decFt = 0;
        dup_camsgf_thr = 0;

        thr_only_one_color = true;//2.56
        DecFt8_0->SetNewP(true);
        DecFt8_1->SetNewP(true);
        DecFt8_2->SetNewP(true);
        DecFt8_3->SetNewP(true);
        DecFt8_4->SetNewP(true);
        DecFt8_5->SetNewP(true);

        f2a->DestroyPlansAll(false);
        EmitDecodeInProgresPskRep(false);
        have_decALL3_ = false;
        //qDebug()<<"END-DestroyPlansAll";
    }
}//2.56 stop  static bool thr_only_one_color = true;
void DecoderMs::ThrSetBackColor()
{
    if (thr_only_one_color)
    {
        thr_only_one_color = false;
        SetBackColor();
    }
}
void DecoderMs::SetBackColorQ65()
{
    SetBackColor();
}
static bool end_dec0_ = true;
static bool end_dec1_ = true;
static bool end_dec2_ = true;
static bool end_dec3_ = true;
static bool end_dec4_ = true;
static bool end_dec5_ = true;
bool DecoderMs::IsWorking() {
    if (!end_dec0_ || !end_dec1_ || !end_dec2_ || !end_dec3_ || !end_dec4_ || !end_dec5_ || thred_busy) return true;
    return false;
}

void DecoderMs::TryEndThr()
{
    if (!end_dec0_ || !end_dec1_ || !end_dec2_ || !end_dec3_ || !end_dec4_ || !end_dec5_) return;

    ResetDupThr();
    EmitDecode(false,0);

    if (is_ftBuff)
    {
        usleep(25000);
        thred_busy = false;
        SETftBuff();
    }
    else
        thred_busy = false;
    //qDebug()<<"Multy Thread False thred_busy";
}
static double _f00_ = 200;
static double _f01_ = 700;
void DecoderMs::StrtDec0()
{
    usleep(17000); //qDebug()<<"0";
    if      (s_mod == 11) DecFt8_0->ft8_decode(static_dat0,s_static_dat_count,_f00_,_f01_,s_nfqso_all,have_dec0_,id3decFt,s_f00,s_f01);
    else if (s_mod == 13) DecFt4_0->ft4_decode(static_dat0,_f00_,_f01_,s_f00,s_f01,s_nfqso_all,have_dec0_);
    end_dec0_ = true;
    TryEndThr();
}
static double _f02_ = 1200;
//#define CORFT8 60.0
//#define CORFT4 100.0
void DecoderMs::StrtDec1()
{
    usleep(18000); //qDebug()<<"1";
    if      (s_mod == 11) DecFt8_1->ft8_decode(static_dat1,s_static_dat_count,(_f01_-50.0),_f02_,s_nfqso_all,have_dec1_,id3decFt,s_f00,s_f01);
    else if (s_mod == 13) DecFt4_1->ft4_decode(static_dat1,(_f01_-100.0),_f02_,s_f00,s_f01,s_nfqso_all,have_dec1_);
    end_dec1_ = true;
    TryEndThr();
}
static double _f03_ = 1700;
void DecoderMs::StrtDec2()
{
    usleep(19000); //qDebug()<<"2";
    if      (s_mod == 11) DecFt8_2->ft8_decode(static_dat2,s_static_dat_count,(_f02_-50.0),_f03_,s_nfqso_all,have_dec2_,id3decFt,s_f00,s_f01);
    else if (s_mod == 13) DecFt4_2->ft4_decode(static_dat2,(_f02_-100.0),_f03_,s_f00,s_f01,s_nfqso_all,have_dec2_);
    end_dec2_ = true;
    TryEndThr();
}
static double _f04_ = 2200;
void DecoderMs::StrtDec3()
{
    usleep(20000); //qDebug()<<"3";
    if      (s_mod == 11) DecFt8_3->ft8_decode(static_dat3,s_static_dat_count,(_f03_-50.0),_f04_,s_nfqso_all,have_dec3_,id3decFt,s_f00,s_f01);
    else if (s_mod == 13) DecFt4_3->ft4_decode(static_dat3,(_f03_-100.0),_f04_,s_f00,s_f01,s_nfqso_all,have_dec3_);
    end_dec3_ = true;
    TryEndThr();
}
static double _f05_ = 2700;
void DecoderMs::StrtDec4()
{
    usleep(21000); //qDebug()<<"4";
    if      (s_mod == 11) DecFt8_4->ft8_decode(static_dat4,s_static_dat_count,(_f04_-50.0),_f05_,s_nfqso_all,have_dec4_,id3decFt,s_f00,s_f01);
    else if (s_mod == 13) DecFt4_4->ft4_decode(static_dat4,(_f04_-100.0),_f05_,s_f00,s_f01,s_nfqso_all,have_dec3_);
    end_dec4_ = true;
    TryEndThr();
}
static double _f06_ = 3200;
void DecoderMs::StrtDec5()
{
    usleep(22000); //qDebug()<<"5";
    if      (s_mod == 11) DecFt8_5->ft8_decode(static_dat5,s_static_dat_count,(_f05_-50.0),_f06_,s_nfqso_all,have_dec4_,id3decFt,s_f00,s_f01);
    else if (s_mod == 13) DecFt4_5->ft4_decode(static_dat5,(_f05_-100.0),_f06_,s_f00,s_f01,s_nfqso_all,have_dec3_);
    end_dec5_ = true;
    TryEndThr();
}
void *DecoderMs::ThrDec0(void *argom)
{
    DecoderMs* pt = (DecoderMs*)argom;
    pt->StrtDec0();
    pthread_detach(pt->th0);
#ifndef __wasm__
    pthread_exit(NULL);
#endif
    return NULL;
}
void *DecoderMs::ThrDec1(void *argom)
{
    DecoderMs* pt = (DecoderMs*)argom;
    pt->StrtDec1();
    pthread_detach(pt->th1);
#ifndef __wasm__
    pthread_exit(NULL);
#endif
    return NULL;
}
void *DecoderMs::ThrDec2(void *argom)
{
    DecoderMs* pt = (DecoderMs*)argom;
    pt->StrtDec2();
    pthread_detach(pt->th2);
#ifndef __wasm__
    pthread_exit(NULL);
#endif
    return NULL;
}
void *DecoderMs::ThrDec3(void *argom)
{
    DecoderMs* pt = (DecoderMs*)argom;
    pt->StrtDec3();
    pthread_detach(pt->th3);
#ifndef __wasm__
    pthread_exit(NULL);
#endif
    return NULL;
}
void *DecoderMs::ThrDec4(void *argom)
{
    DecoderMs* pt = (DecoderMs*)argom;
    pt->StrtDec4();
    pthread_detach(pt->th4);
#ifndef __wasm__
    pthread_exit(NULL);
#endif
    return NULL;
}
void *DecoderMs::ThrDec5(void *argom)
{
    DecoderMs* pt = (DecoderMs*)argom;
    pt->StrtDec5();
    pthread_detach(pt->th5);
#ifndef __wasm__
    pthread_exit(NULL);
#endif
    return NULL;
}

void DecoderMs::dtrim(short*dat,int count)
{
    //! Remove any transient data at start of record.
    //!  real dat(jz),dat2(jz)
    double ssq[1000];

    double sumsq=0.0;
    int nz=count/1000.0;
    int k=0;
    int ix=0;

    for (int i = 0; i<1000; i++)
    {//do i=1,1000
        double sq=0.0;
        for (int n = 0; n<nz; n++)
        {//do n=1,nz
            //k=k+1
            short x=dat[k];
            sq=sq + x*x;
            k++;
        }
        ssq[i]=sq;
        sumsq=sumsq+sq;
    }

    double avesq=sumsq/1000.0;

    int ichk=DEC_SAMPLE_RATE/nz;

    for (ix = ichk; ix>=0; ix--)
    {//do i=ichk,1,-1
        //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        //qDebug()<<ix;
        if (ssq[ix]<(avesq/3.0) || ssq[ix]>(3.0*avesq))
            goto c10;
    }
    ix=0;

c10: //continue;
    int ia=(ix+1)*nz;
    //qDebug()<<ia<<nz;

    //  if(i.eq.1) ia=1
    if (ix==0)
        ia=0;

    //if(ia.gt.1)
    if (ia>0)
    {
        //dat(1:ia)=0               //!Zero the bad data
        for (int j = 0; j<ia; j++)
            dat[j]=0;
    }
}
void DecoderMs::SetZapData(short*dat, int count)
{
    if (thred_busy || count<=0)// vazno pazi ot crash -> count_q<=0
        return;

    s_zap_count = fmin(60*DEC_SAMPLE_RATE,count);//1.36
    //qDebug()<<s_zap_count;

    for (int i = 0; i<s_zap_count; i++)
        s_zap_in[i] = dat[i];
    //s_zap_dat[STATIC_DAT_COUNT];
}

void DecoderMs::SETftBuff()
{
    if (thred_busy) return;
    if (is_stat_ftb[0])
    {
        //qDebug()<<"strt 5";
        fromBufFt = true;
        SetDecode(stat_1ftb,c_stat_ftb[0],s_timeftb[0],0,s_mousebftb[0],false,true,s_fopenftb[0]);
        is_stat_ftb[0] = false;
    }
    else if (is_stat_ftb[1])
    {
        //qDebug()<<"strt 6";
        fromBufFt = true;
        SetDecode(stat_2ftb,c_stat_ftb[1],s_timeftb[1],0,s_mousebftb[1],false,true,s_fopenftb[1]);
        is_stat_ftb[1] = false;
    }
    if (!is_stat_ftb[0] && !is_stat_ftb[1])
    {
        is_ftBuff = false;
        //qDebug()<<"STOP TIMM";
    }
}

void DecoderMs::SetDecode(short *raw,int count_q,QString time, int t_istart,int mousebutton,bool f_rtd,bool end_rtd,bool ffopen)//1.27 psk rep   fopen bool true    false no file open
{
    //qDebug()<<count_q;
    if (count_q<512) return; //2.69=512 min samples 2.51 200, old=100
    if (s_mod == 11)
    {
        bool t_busy = thred_busy;
        if (is_ftBuff) t_busy = true; //click to display no in -> mousebutton>3
        if (mousebutton>3 && !fromBufFt && t_busy && (!is_stat_ftb[0] || !is_stat_ftb[1]))
        {
            if (count_q<5*DEC_SAMPLE_RATE) return;//tf8 minimum
            if (count_q > 186000) count_q = 186000;

            if (!is_stat_ftb[0] && mousebutton==5)
            {
                is_stat_ftb[0]=true;
                c_stat_ftb[0] = count_q;
                s_mousebftb[0] = mousebutton;
                s_fopenftb[0] = ffopen;
                s_timeftb[0] = time;
                for (int i = 0; i<count_q; ++i)
                    stat_1ftb[i]=raw[i];
                //qDebug()<<"1 SAVE----"<<(mousebutton-3);
            }
            else if (!is_stat_ftb[1] && mousebutton==6)
            {
                is_stat_ftb[1]=true;
                c_stat_ftb[1] = count_q;
                s_mousebftb[1] = mousebutton;
                s_fopenftb[1] = ffopen;
                s_timeftb[1] = time;
                for (int i = 0; i<count_q; ++i)
                    stat_2ftb[i]=raw[i];
                if (!is_thrTime) CreateStartTimerthr();
                //qDebug()<<"2 SAVE----"<<(mousebutton-3);
            }
            if (!is_ftBuff) is_ftBuff = true;

            //qDebug()<<"NOTACT----------------------------------";
            return;
        }
    }
    //if (fromBufFt) qDebug()<<"BUF DECODE --->"<<mousebutton-3<<thred_busy;
    //else qDebug()<<"DECODE"<<mousebutton-3<<thred_busy;
    if (fromBufFt) fromBufFt = false;

    if (thred_busy) return;
    int static_dat_max = 0;
    //////////// Minimum Count For Decode from mode in 1.46 realy this is from second display data ////////////
    if (s_mod == 7 || s_mod == 8 || s_mod == 9 || s_mod == 10)//10sec min pi4 full 24s  jt65abc full 50s (RRR RO 73 on 5s)
    {
        if (count_q<10*DEC_SAMPLE_RATE) return;
        s_static_dat_count = fmin(60*DEC_SAMPLE_RATE,count_q);//max 60sec jt65abc pi4
    }
    else if (s_mod == 11) //4 esc min ft8 full 15s
    {
        if (count_q<5*DEC_SAMPLE_RATE)
        {
            //qDebug()<<"Except Three-stage Decoding FT8="<<count_q<<(mousebutton-3);
            if (mousebutton == 6)
            {
                id3decFt = 3;
                ResetDupThr(); //single thread end=ResetDupThr();
                //TryEndThr(); // multy trhead end=TryEndThr();
            }
            return;
        }
        static_dat_max = 16*DEC_SAMPLE_RATE;//=192000
        s_static_dat_count = fmin(static_dat_max,count_q);//max 15sec ft8
    }
    else if (s_mod == 13) //ft4 2.35=8s
    {
        if (count_q<4*DEC_SAMPLE_RATE)
            return;
        static_dat_max = 8*DEC_SAMPLE_RATE;
        s_static_dat_count = fmin(static_dat_max,count_q);
    }
    else if (allq65) //q65
    {
        if (count_q<8*DEC_SAMPLE_RATE) return;
        static_dat_max = 120*DEC_SAMPLE_RATE;
        s_static_dat_count = fmin(static_dat_max,count_q);
    }
    else
        s_static_dat_count = fmin(30*DEC_SAMPLE_RATE,count_q);//max 30sec static dufer for decode thread
    //////////// END Minimum Count For Decode from mode in 1.46 realy this is from second display data ////////
    thred_busy = true;//critical -> hv need to be after all return

    //2.39 mousebutton Left=1, Right=3 fullfile=0 rtd=2 ft8=4,5,6
    //2.39 for thread, normal and mouse click decode ft8/4
    if (s_mod == 11 && mousebutton > 3)
    {
        id3decFt = (mousebutton-3);
        if (mousebutton==6 && !is_thrTime) CreateStartTimerthr();//hv inportent
    }
    else
    {
        id3decFt = 100;
        if ((s_mod == 11 || s_mod == 13 || allq65) && !is_thrTime)//|| allq65
            CreateStartTimerthr();
    }
    //qDebug()<<"DECODE -------"<<id3decFt<<count_q;
    //qDebug()<<"Decode C="<<s_static_dat_count<<(double)s_static_dat_count/12000.0;

    s_fopen = ffopen;
    EmitDecodeInProgresPskRep(true);

    if (f_rtd) EmitDecode(true,2);//dec_state no=0 dec=1 rtddec=2
    else 	   EmitDecode(true,1);//dec_state no=0 dec=1 rtddec=2

    s_in_istart = (double)t_istart;
    s_time = time;
    s_mousebutton = mousebutton; //mousebutton Left=1, Right=3 fullfile=0 rtd=2 ft8=4,5,6
    DecFt8_0->SetStDecode(time,mousebutton,s_fopen);//2.66 for ap7 s_fopen8
    DecFt4_0->SetStDecode(time,mousebutton);
    DecQ65->SetStDecode(time,mousebutton);

    s_f_rtd = f_rtd;
    s_end_rtd = end_rtd;

    double sum=0.0;
    for (int i = 0; i<s_static_dat_count; ++i)
        sum=sum+raw[i];

    int nave=int((double)sum/(double)s_static_dat_count);
    for (int i = 0; i<s_static_dat_count; ++i)
    {
        raw_in_s[i]=raw[i]-nave;
    }
    //qDebug()<<s_static_dat_count;

    if (s_mod != 0 && s_mod != 12 && s_mod != 11 && s_mod != 13 && !allq65)//old modes only kogato e celia region ili >14s
    {
        //qDebug()<<"YES Dtrim=============="<<count_q;
        if (s_static_dat_count>14*DEC_SAMPLE_RATE) dtrim(raw_in_s,s_static_dat_count);
    }

    sum=0.0;
    for (int i = 0; i<s_static_dat_count; ++i)//!Convert raw data from i*2 to real, remove DC
    {
        static_dat0[i]=(double)0.1*(double)raw_in_s[i];
        sum=sum+static_dat0[i];
    }

    double ave=(double)sum/(double)s_static_dat_count;
    for (int j = 0; j<s_static_dat_count; ++j)
    {
        static_dat0[j]=(static_dat0[j]-ave);
    }

    if (s_static_dat_count<static_dat_max)
    {
        for (int j = s_static_dat_count; j<static_dat_max; ++j) static_dat0[j]=0.0;
        //qDebug()<<"flush0="<<static_dat_max - s_static_dat_count<<static_dat_max;
    }
    /*if (s_mod == 11)
    {
    	for (int j = 0; j<156000; ++j) static_dat0[j]=static_dat0[j+24000];//id2(1:156000)=id2(24001:180000)  ! Drop the first 2 seconds of data
    	for (int j = 156000; j<180000; ++j) static_dat0[j]=0.0;//id2(156001:180000)=0
    	//for (int j = 167000; j<180000; ++j) static_dat0[j]=0.0;    
    }*/

    bool nagain = false;//mousebutton Left=1, Right=3 fullfile=0 rtd=2
    if (s_mousebutton==3) nagain = true;

    double thrsum = 0;
    int nthr = s_thr_used;
    if ((s_mod != 11 && s_mod != 13) || nagain) nthr = 1;
    else if (nthr>1)
    {
        double limit = 300.0;//ft8 300
        if (s_mod == 13) limit = 400.0;//ft4 500
        double band_all = s_f01 - s_f00;

        thrsum = band_all/(double)nthr;
        if (nthr==6 && thrsum<limit) nthr--;
        thrsum = band_all/(double)nthr;
        if (nthr==5 && thrsum<limit) nthr--;
        thrsum = band_all/(double)nthr;
        if (nthr==4 && thrsum<limit) nthr--;
        thrsum = band_all/(double)nthr;
        if (nthr==3 && thrsum<limit) nthr--;
        thrsum = band_all/(double)nthr;
        if (nthr==2 && thrsum<limit) nthr--;
    }
    //if (nthr==1) qDebug()<<"Used Thread= 1"<<"-> THD BW= All";
    //else qDebug()<<"Used Thread="<<nthr<<"-> THD BW="<<thrsum;

    /*const int po = sched_getscheduler(0);
    qDebug()<<sched_get_priority_max(po);
    qDebug()<<sched_get_priority_min(po);*/
    //int poly;
    //pthread_attr_t thread_attr;
    //struct sched_param param;
    //pthread_getschedparam(th,&poly,&param);
    //qDebug()<<param.sched_priority<<poly;
    /*pthread_attr_init(&thread_attr);  // Initialise the attributes
    pthread_attr_setschedpolicy(&thread_attr, SCHED_RR);  // Set attributes to FIFO RR policy    
    param.sched_priority = 0;
    pthread_attr_setschedparam(&thread_attr, &param); // Set attributes to priority 30*/
    /*pthread_create(&thread1, &thread_attr, KBD_thr, NULL);     // low priority: keyboard monitor
    param.sched_priority = 50;
    pthread_attr_setschedparam(&thread_attr, &param); // Set attributes to priority 50 (policy is already RR)
    pthread_create(&thread2, &thread_attr, UART_thr, NULL);    // medium  priority: UART
    pthread_attr_destroy(&thread_attr); // We've done with the attributes*/
    if (nthr==1)
    {
        have_dec0_ = false;
        //2.56 stop thr_only_one_color = true;
        this->StrtDecode();
        // pthread_create(&th,NULL,DecoderMs::ThreadDecode,(void*)this);
        //pthread_attr_destroy(&thread_attr);
    }
    else
    {
        //in= 2,3,4,5,6
        _f00_ = s_f00;
        //2.56 stop thr_only_one_color = true;
        double tcorf = 0.0;
        double corf = 25.0;
        int c_arrrs = 186000;//ft8  15,5s
        if (s_mod==13)
        {
            corf = 50.0; //140
            c_arrrs = 96000;//ft4 8s
        }

        for (int i = 0; i < c_arrrs; ++i)
        {
            //min=2
            static_dat1[i]=static_dat0[i];
            if (nthr>2) static_dat2[i]=static_dat0[i];
            if (nthr>3) static_dat3[i]=static_dat0[i];
            if (nthr>4) static_dat4[i]=static_dat0[i];
            if (nthr>5) static_dat5[i]=static_dat0[i];
        }
        //qDebug()<<"RXF---->"<<s_nfqso_all;
        _f01_ = _f00_ + thrsum;
        if (fabs(s_nfqso_all - _f01_)<11.0) tcorf = corf;
        _f01_ -= tcorf;
        have_dec0_ = false;
        end_dec0_ = false;
        //qDebug()<<"D1="<<_f00_<<_f01_<<_f01_-_f00_;
        _f02_ = _f01_ + thrsum + tcorf;
        if (fabs(s_nfqso_all - _f02_)<11.0) tcorf = corf;
        else tcorf = 0.0;
        _f02_ -= tcorf;
        have_dec1_ = false;
        end_dec1_ = false;
        //qDebug()<<"D2="<<_f01_<<_f02_<<_f02_-_f01_;
        if (nthr>2)
        {
            _f03_ = _f02_ + thrsum + tcorf;
            if (fabs(s_nfqso_all - _f03_)<11.0) tcorf = corf;
            else tcorf = 0.0;
            _f03_ -= tcorf;
            have_dec2_ = false;
            end_dec2_ = false;
            //qDebug()<<"D3="<<_f02_<<_f03_<<_f03_-_f02_;
        }
        if (nthr>3)
        {
            _f04_ = _f03_ + thrsum + tcorf;
            if (fabs(s_nfqso_all - _f04_)<11.0) tcorf = corf;
            else tcorf = 0.0;
            _f04_ -= tcorf;
            have_dec3_ = false;
            end_dec3_ = false;
            //qDebug()<<"D4="<<_f03_<<_f04_<<_f04_-_f03_;
        }
        if (nthr>4)
        {
            _f05_ = _f04_ + thrsum + tcorf;
            if (fabs(s_nfqso_all - _f05_)<11.0) tcorf = corf;
            else tcorf = 0.0;
            _f05_ -= tcorf;
            have_dec4_ = false;
            end_dec4_ = false;
            //qDebug()<<"D5="<<_f04_<<_f05_<<_f05_-_f04_;
        }
        if (nthr>5)
        {
            _f06_ = _f05_ + thrsum + tcorf;
            have_dec5_ = false;
            end_dec5_ = false;
            //qDebug()<<"D6="<<_f05_<<_f06_<<_f06_-_f05_;
        }
        //2.41 important to be here for slow speed PCs
        // debugPrintf("Pthread create....");
        abort();
        /*
        pthread_create(&th0,NULL,DecoderMs::ThrDec0,(void*)this);
        pthread_create(&th1,NULL,DecoderMs::ThrDec1,(void*)this);
        if (nthr>2) pthread_create(&th2,NULL,DecoderMs::ThrDec2,(void*)this);
        if (nthr>3) pthread_create(&th3,NULL,DecoderMs::ThrDec3,(void*)this);
        if (nthr>4) pthread_create(&th4,NULL,DecoderMs::ThrDec4,(void*)this);
        if (nthr>5) pthread_create(&th5,NULL,DecoderMs::ThrDec5,(void*)this);
        */
        // debugPrintf("Pthread crete end");
    }
}

void DecoderMs::SetResultsCallback(std::function<void(const char *)> fun) {
    DecFt8_0->SetResultsCallback(fun);
    DecFt8_1->SetResultsCallback(fun);
    DecFt8_2->SetResultsCallback(fun);
    DecFt8_3->SetResultsCallback(fun);
    DecFt8_4->SetResultsCallback(fun);
    DecFt8_5->SetResultsCallback(fun);
    DecFt4_0->SetResultsCallback(fun);
    DecFt4_1->SetResultsCallback(fun);
    DecFt4_2->SetResultsCallback(fun);
    DecFt4_3->SetResultsCallback(fun);
    DecFt4_4->SetResultsCallback(fun);
    DecFt4_5->SetResultsCallback(fun);
}
