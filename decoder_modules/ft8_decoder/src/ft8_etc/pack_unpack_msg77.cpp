/* The algorithms, source code, look-and-feel of WSJT-X and related
 * programs, and protocol specifications for the modes FSK441, FT8, JT4,
 * JT6M, JT9, JT65, JTMS, QRA64, ISCAT, MSK144, are Copyright © 2001-2017
 * by one or more of the following authors: Joseph Taylor, K1JT; Bill
 * Somerville, G4WJS; Steven Franke, K9AN; Nico Palermo, IV3NWV; Greg Beam,
 * KI7MT; Michael Black, W9MDB; Edson Pereira, PY2SDR; Philip Karn, KA9Q;
 * and other members of the WSJT Development Group.
 *
 * MSHV PackUnpackMessage77bit
 * Rewritten into C++ and modified by Hrisimir Hristov, LZ2HV 2015-2018
 * May be used under the terms of the GNU General Public License (GPL)
 */

//#include <QtGui>

#include "mshv_support.h"
#include "pack_unpack_msg77.h"
#include "config_str_exc.h"
// #include "../../../config_str_exc.h"

//static const QString c_77_04(){return " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/";}//for the future [-Wclazy-non-pod-global-static]

//// static decoders array ////////////////////////
static bool inicialize_static_arrays = false;
#define MAXHASHD 600+4+5//decoders +4=+my +his + r1 +r2  +5=mam slots
static int ihash10d[MAXHASHD+5];            //+5=for any case
static int ihash12d[MAXHASHD+5];  			//+5=for any case
static int ihash22d[MAXHASHD+5];  			//+5=for any case
static QString callsign_hashd[MAXHASHD+5];	//+5=for any case
static int nzhash_pos_writed = 9;
static int nzhash_start_writed = 9;
//// static decoders array end ////////////////////////
//// static generators array ////////////////////////
#define MAXHASHG 3//generators max 3 cals need
static int ihash10g[MAXHASHG+5];  			//+5=for any case
static int ihash12g[MAXHASHG+5];  			//+5=for any case
static int ihash22g[MAXHASHG+5];  			//+5=for any case
static QString callsign_hashg[MAXHASHG+5];	//+5=for any case
static int nzhash_pos_writeg = 0;
//// static generators array end ////////////////////////

void PackUnpackMsg77::initPackUnpack77(bool f_dec_gen)//f_dec_gen -> dec=true gen=false
{

    if (c_77_04.count() < 10) {
         // debugPrintf("c_77_04 len = %d", c_77_04.count());
         c_77_04=" 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/";
         c_77_txt =" 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?";
         a1_28 = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
         a2_28 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
         a3_28 = "0123456789";
         a4_28 = " ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    }

    //qDebug()<<f_dec_gen;
    sf_dec_gen = f_dec_gen;
    if (inicialize_static_arrays) return;

    for (int i = 0; i<MAXHASHD; ++i)
    {
        ihash10d[i]=-1;
        ihash12d[i]=-1;
        ihash22d[i]=-1;
        callsign_hashd[i]=" ";
    }
    for (int i = 0; i<MAXHASHG; ++i)
    {
        ihash10g[i]=-1;
        ihash12g[i]=-1;
        ihash22g[i]=-1;
        callsign_hashg[i]=" ";
    }
    inicialize_static_arrays = true;
}	
/////// Unpack //////////////////////////////////////////////////////////
void PackUnpackMsg77::hash10(int n10,QString &c13)
{
    c13="<0:"+std::to_string(n10)+">";
    if(n10<0 || n10>1023) return;//2.47 not from here
    if (sf_dec_gen)
    {
        for (int i = 0; i < MAXHASHD; ++i)
        {
            if (ihash10d[i]==n10)
            {
                c13=callsign_hashd[i];
                if (c13.at(0)!='<')
                {
                    c13.prepend("<");
                    c13.append(">");
                }
                return;
            }
        }
    }
    else
    {
        for (int i = 0; i < MAXHASHG; ++i)
        {
            if (ihash10g[i]==n10)
            {
                c13=callsign_hashg[i];
                if (c13.at(0)!='<')
                {
                    c13.prepend("<");
                    c13.append(">");
                }
                return;
            }
        }
    }
}
void PackUnpackMsg77::hash12(int n12,QString &c13)
{
//    c13="<...>";//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    c13="<1:"+std::to_string(n12)+">";
    if(n12<0 || n12>4095) return;//2.47 not from here
    if (sf_dec_gen)
    {
        for (int i = 0; i < MAXHASHD; ++i)
        {
            if (ihash12d[i]==n12)
            {
                c13=callsign_hashd[i];
                if (c13.at(0)!='<')
                {
                    c13.prepend("<");
                    c13.append(">");
                }
                return;
            }
        }
    }
    else
    {
        for (int i = 0; i < MAXHASHG; ++i)
        {
            if (ihash12g[i]==n12)
            {
                c13=callsign_hashg[i];
                if (c13.at(0)!='<')
                {
                    c13.prepend("<");
                    c13.append(">");
                }
                return;
            }
        }
    }
}
void PackUnpackMsg77::hash22(int n22,QString &c13)
{
    c13="<2:"+std::to_string(n22)+">";
    if (sf_dec_gen)
    {
        for (int i = 0; i < MAXHASHD; ++i)
        {
            if (ihash22d[i]==n22)
            {
                c13=callsign_hashd[i];
                if (c13.at(0)!='<')
                {
                    c13.prepend("<");
                    c13.append(">");
                }
                return;
            }
        }
    }
    else
    {
        for (int i = 0; i < MAXHASHG; ++i)
        {
            if (ihash22g[i]==n22)
            {
                c13=callsign_hashg[i];
                if (c13.at(0)!='<')
                {
                    c13.prepend("<");
                    c13.append(">");
                }
                return;
            }
        }
    }
}
int PackUnpackMsg77::BinToInt32(bool*a,int b_a,int bits_sz)
{
    int k = 0;
    for (int i = b_a; i < bits_sz; ++i)
    {
        k <<= 1;
        k |= a[i];//-0
    }
    return k;
}
void PackUnpackMsg77::mp_short_div(unsigned char *w,unsigned char *u,int b_u,int n,int iv,int &ir)
{
    ir=0;
    for (int j = 0; j < n; ++j)
    {//do j=1,n
        int i=256*ir+u[j+b_u];
        w[j]=i/iv;
        //ir=fmod(i,iv);
        ir=(i % iv); // c++ mod = % to lost presision in 64-bit
    }
}
void PackUnpackMsg77::unpacktext77(bool *c77,QString &c13)
{
    //////////// LZ2HV Bin Math Method ////////////////
    /*int c_77_txt_cou = c_77_txt.count();
    bool c_77_txt_bin_count[BITS_71];
    Int32ToBin(c_77_txt_cou,c_77_txt_bin_count,BITS_71);
    bool tmp_c71[BITS_71];
    for (int z =0; z<BITS_71; z++)
        tmp_c71[z]=c77[z];
    bool tmp_arr1[BITS_71];
    bool tmp_arr3[BITS_71];
    for (int i = 13; i >=0; --i)
    {
        for (int z =0; z<BITS_71; z++)
        {
            tmp_arr1[z]=tmp_c71[z];
            tmp_arr3[z]=tmp_c71[z];
        }
        //HV modulo = a - (n*(a/n)) ////
        Divide71BitBinByMax32BitBinArray(tmp_arr1,c_77_txt_bin_count);
        Multiplay71BitBinByMax6BitBinArray(tmp_arr1,c_77_txt_bin_count);
        SubstarctBinArray(tmp_arr3,tmp_arr1,BITS_71);
        //HV modulo end //////////////////
        int k = BinToInt32(tmp_arr3,BITS_71-8,BITS_71);//-8bit max 8bit k->max=6bit  -6 bit
        if (k>-1 && k<c_77_txt_cou)
            c13.prepend(c_77_txt[k]);
        Divide71BitBinByMax32BitBinArray(tmp_c71,c_77_txt_bin_count);
    }*/
    //////////// LZ2HV Bin Math END ////////////////

    /*integer*1   ia(10)
    character*1 qa(10),qb(10)
    character*13 c13
    character*71 c71
    character*42 c
    equivalence (qa,ia),(qb,ib)

    qa(1)=char(0)
    read(c71,1010) qa(2:10)
    1010 format(b7.7,8b8.8)

    do i=13,1,-1
     call mp_short_div(qb,qa(2:10),9,42,ir)
     c13(i:i)=c(ir+1:ir+1)
     qa(2:10)=qb(1:9)
    enddo*/
    int c_77_txt_cou = c_77_txt.count();
    int c_77 = 0;
    unsigned char qa[10],qb[10];
    int c_qa = 1;
    qa[0]=0;
    for (int i = 0; i < 9; ++i)
    {
        int k = 0;
        int end = 8;
        if (i==0)
            end = 7;
        for (int j = 0; j < end; ++j)
        {
            k <<= 1;
            k |= c77[c_77];
            c_77++;
        }
        qa[c_qa]=k;
        c_qa++;
    }
    for (int i = 12; i >=0; --i)
    {//do i=13,1,-1
        int ir = 0;
        mp_short_div(qb,qa,1,9,42,ir);
        if (ir>-1 && ir<c_77_txt_cou)
            c13.prepend(c_77_txt[ir]);
        for (int x = 0; x < 9; ++x)
            qa[x+1]=qb[x];
    }
}

void PackUnpackMsg77::unpack28(int n28_0,QString &c13,bool &success)
{
    /*parameter (NTOKENS=2063592,MAX22=4194304)
    integer nc(6)
    character*13 c13
    character*37 c1
    character*36 c2
    character*10 c3
    character*27 c4
    data c1/' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'/
    data c2/'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'/
    data c3/'0123456789'/
    data c4/' ABCDEFGHIJKLMNOPQRSTUVWXYZ'/
    data nc/37,36,19,27,27,27/*/
    int MAX22=4194304;
    int NTOKENS=2063592;
    int n,i1,i2,i3,i4,i5,i6;
    //QString c13p;

    // debugPrintf("           call unpack28 begin");
    success=true;
    int n28=n28_0;
    if (n28<NTOKENS) //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {
        //! Special tokens DE, QRZ, CQ, CQ_nnn, CQ_aaaa
        // debugPrintf("           n28=%d", n28);
        if (n28==0) c13="DE";
        if (n28==1) c13="QRZ";
        if (n28==2) c13="CQ";
        if (n28<=2) goto c900;
        if (n28<=1002)
        {
            c13 = "CQ_"; //write(c13,1002) n28-3
            //c13.append(QString("%1").arg(n28-3));      //1002    format('CQ_',i3.3)
            c13.append(QString("%1").arg(n28-3,3,10,QChar('0')));// 2.33
            goto c900;
        }
        if (n28<=532443)// TEST RU WW FD
        {
            n=n28-1003;
            //n0=n;
            i1=n/(27*27*27);
            n=n-27*27*27*i1;
            i2=n/(27*27);
            n=n-27*27*i2;
            i3=n/27;
            i4=n-27*i3;
            //c13=c4(i1+1:i1+1)//c4(i2+1:i2+1)//c4(i3+1:i3+1)//c4(i4+1:i4+1)
            c13.append(a4_28[i1]);
            c13.append(a4_28[i2]);
            c13.append(a4_28[i3]);
            c13.append(a4_28[i4]);
            //c13=adjustl(c13);  //c13=adjustl(c13)
            //QString c13p = c13.leftJustified(4,' '); qDebug()<<"c13p"<<c13p;
            c13="CQ_"+c13.trimmed();  //c13='CQ_'//c13(1:10)
            goto c900;
        }
    }
    n28=n28-NTOKENS; //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    // debugPrintf("           n28_2=%d", n28);
    //qDebug()<<c13<<n28<<MAX22;
    if (n28<MAX22)
    {
        // debugPrintf("           hash..");
        //! This is a 22-bit hash of a callsign
        int n22=n28;
        hash22(n22,c13);     //!Retrieve callsign from hash table
        //c13.append(" ");
        goto c900;
    }

    //! Standard callsign
    n=n28 - MAX22;
    i1=n/(36*10*27*27*27);
    n=n-36*10*27*27*27*i1;
    i2=n/(10*27*27*27);
    n=n-10*27*27*27*i2;
    i3=n/(27*27*27);
    n=n-27*27*27*i3;
    i4=n/(27*27);
    n=n-27*27*i4;
    i5=n/27;
    i6=n-27*i5;
    //c13=c1(i1+1:i1+1)//c2(i2+1:i2+1)//c3(i3+1:i3+1)//c4(i4+1:i4+1)//     &
    //c4(i5+1:i5+1)//c4(i6+1:i6+1)//'       '
    //c13=adjustl(c13)
    // debugPrintf("           append..");
    c13.append(a1_28[i1]);
    c13.append(a2_28[i2]);
    c13.append(a3_28[i3]);
    c13.append(a4_28[i4]);
    c13.append(a4_28[i5]);
    c13.append(a4_28[i6]);
    //c13p = c13.leftJustified(6,' ');
    // debugPrintf("           trimmed");
    c13 = c13.trimmed();
    // debugPrintf("           trimmed2");
    //c13.append(" ");
c900:  //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    //int i0=c13.indexOf(" ");            //i0=index(c13,' ');
    //if (i0<c13.count()-1) c13="QU1RK "; //if(i0<len(trim(c13))) c13='QU1RK';
    //c13 = "   K1A  ";
    //c13 = c13.trimmed(); qDebug()<<c13;
    //if (c13.contains(" "))
    /*c13 = "       ";
    c13.append("  ");
    int i0=c13.indexOf(" ")+1;//qDebug()<<c13;*/
    //qDebug()<<c13;
    // debugPrintf("           trimmed3...");
    c13 = c13.trimmed(); //qDebug()<<c13;
    // debugPrintf("           trimmed3");
    if (c13.contains(" "))
    {
        c13="QU1RK";
        success=false;
    }
}
bool PackUnpackMsg77::to_grid4(int n,QString &grid4)
{
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    int j1=n/(18*10*10);
    if (j1<0 || j1>17) return false;
    n=n-j1*18*10*10;
    int j2=n/(10*10);
    if (j2<0 || j2>17) return false;
    n=n-j2*10*10;
    int j3=n/10;
    if (j3<0 || j3>9) return false;
    int j4=n-j3*10;
    if (j4<0 || j4>9) return false;
    grid4[0]=QChar(j1+(int)('A'));
    grid4[1]=QChar(j2+(int)('A'));
    grid4[2]=QChar(j3+(int)('0'));
    grid4[3]=QChar(j4+(int)('0'));
    return true;
}
bool PackUnpackMsg77::to_grid6(int n,QString &grid6)
{
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    int j1=n/(18*10*10*24*24);
    if (j1<0 || j1>17) return false;
    n=n-j1*18*10*10*24*24;
    int j2=n/(10*10*24*24);
    if (j2<0 || j2>17) return false;
    n=n-j2*10*10*24*24;
    int j3=n/(10*24*24);
    if (j3<0 || j3>9) return false;
    n=n-j3*10*24*24;
    int j4=n/(24*24);
    if (j4<0 || j4>9) return false;
    n=n-j4*24*24;
    int j5=n/24;
    if (j5<0 || j5>23) return false;
    int j6=n-j5*24;
    if (j6<0 || j6>23) return false;
    grid6[0]=QChar(j1+(int)('A'));
    grid6[1]=QChar(j2+(int)('A'));
    grid6[2]=QChar(j3+(int)('0'));
    grid6[3]=QChar(j4+(int)('0'));
    grid6[4]=QChar(j5+(int)('A'));
    grid6[5]=QChar(j6+(int)('A'));
    return true;
}
QString PackUnpackMsg77::unpack77(bool *c77,bool &unpk77_success)
{
    //QString aaa = "A4A";   qDebug()<<aaa.count();
    // debugPrintf("         unpack77 begin");
    int MAXGRID4 = 32400;
    QString call_1,call_2,call_3;
    QString msg = "";
    bool unpk28_success;

    c1_rx_calls = "";
    c2_rx_calls = "";
    unpk77_success=true;
    //! Check for bad data LZ2HV This no posyble in MSHV c77 is bool
    /*for (int i = 0; i < 77; ++i)
    {//do i=1,77
       if(c77[i]!=0 && c77[1]!=1) 
       	{
          msg="failed unpack";
          unpk77_success=false;
          return msg; 
       }
    }*/
    //read(c77(72:77),'(2b3)') n3,i3
    //msg=repeat(' ',37)
    int n3=4*c77[71] + 2*c77[72] + c77[73];
    int i3=4*c77[74] + 2*c77[75] + c77[76]; //qDebug()<<i3<<n3;

    if (i3==0 && n3==0)//if(i3.eq.0 .and. n3.eq.0) then
    {
        // debugPrintf("         free text");
        //! 0.0  Free text
        unpacktext77(c77,msg);     
        //msg(14:)='                        '
        //msg=adjustl(msg)
        msg = msg.trimmed(); //2.59
        msg = msg.leftJustified(18,' ');//2.47  qDebug()<<msg;
        if (msg.at(0)==' ') //if(msg(1:1).eq.' ') then
        {
            unpk77_success=false;
            return "";
        }
    }
    else if (i3==0 && n3==1)
    {
        // debugPrintf("         i3==0 n3==1");
                //! 0.1  K1ABC RR73; W9XYZ <KH1/KH7Z> -11   28 28 10 5       71   DXpedition Mode
        //   read(c77,1010) n28a,n28b,n10,n5
        //1010 format(2b28,b10,b5)
        int n28a = BinToInt32(c77,0,28);
        int n28b = BinToInt32(c77,28,56);
        int n10 = BinToInt32(c77,56,66);
        int n5 = BinToInt32(c77,66,71);
        int irpt=2*n5 - 30;
        //QString crpt = QString("%1").arg(irpt,3,10,QChar('0'));    //write(crpt,1012) irpt 1012 format(i3.2)
        //if (irpt>=0) crpt.prepend("+"); //qDebug()<<crpt; //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        QString crpt;
        // debugPrintf("         crpt.");
        if (irpt>-1)
            crpt="+"+QString("%1").arg(irpt,2,10,QChar('0'));
        else
            crpt="-"+QString("%1").arg((int)fabs(irpt),2,10,QChar('0'));
        unpack28(n28a,call_1,unpk28_success);
        if (!unpk28_success || n28a<=2) unpk77_success=false;
        unpack28(n28b,call_2,unpk28_success);
        if (!unpk28_success || n28b<=2) unpk77_success=false;
        hash10(n10,call_3);
        if (call_3.at(0)=='<')
        {
            //msg=trim(call_1)//' RR73; '//trim(call_2)//' '//trim(call_3)//' '//crpt
            msg=call_1+" RR73; "+call_2+" "+call_3+" "+crpt;
        }
        else
        {
            //msg=trim(call_1)//' RR73; '//trim(call_2)//' <'//trim(call_3)//'> '//crpt
            msg=call_1+" RR73; "+call_2+" "+"<"+call_3+">"+" "+crpt;
        }
    }
    else if (i3==0 && n3==2)//2.42 222
    {
        unpk77_success=false;
    }
    /*else if (i3==0 && n3==2) //2.39 old EU VHF contest
    {
        //! 0.2  PA3XYZ/P R 590003 IO91NP           28 1 1 3 12 25   70   EU VHF contest
        //read(c77,1020) n28a,ip,ir,irpt,iserial,igrid6
        //1020 format(b28,2b1,b3,b12,b25)
        int n28a = BinToInt32(c77,0,28);
        int ip = BinToInt32(c77,28,29);
        int ir = BinToInt32(c77,29,30);
        int irpt = BinToInt32(c77,30,33);
        int iserial = BinToInt32(c77,33,45);
        int igrid6 = BinToInt32(c77,45,70);
        unpack28(n28a,call_1,unpk28_success);
        if (!unpk28_success || n28a<=2) unpk77_success=false;
        QString cexch = QString("%1").arg((52+irpt),2,10,QChar('0'))+QString("%1").arg(iserial,4,10,QChar('0'));//nrs=52+irpt
        if (ip==1) call_1=call_1+"/P";//'        ';//call_1=trim(call_1)//'/P'//'        '
        //write(cexch,1022) nrs,iserial
        //1022 format(i2,i4.4)
        int n=igrid6;
        int j1=n/(18*10*10*24*24);
        n=n-j1*18*10*10*24*24;
        int j2=n/(10*10*24*24);
        n=n-j2*10*10*24*24;
        int j3=n/(10*24*24);
        n=n-j3*10*24*24;
        int j4=n/(24*24);
        n=n-j4*24*24;
        int j5=n/24;
        int j6=n-j5*24;
        QString grid6 = "      ";
        grid6[0]=QChar(j1+(int)('A'));//grid6(1:1)=char(j1+ichar('A'))
        grid6[1]=QChar(j2+(int)('A'));//grid6(2:2)=char(j2+ichar('A'))
        grid6[2]=QChar(j3+(int)('0'));//grid6(3:3)=char(j3+ichar('0'))
        grid6[3]=QChar(j4+(int)('0'));//grid6(4:4)=char(j4+ichar('0'))
        grid6[4]=QChar(j5+(int)('A'));//grid6(5:5)=char(j5+ichar('A'))
        grid6[5]=QChar(j6+(int)('A'));//grid6(6:6)=char(j6+ichar('A'))
        msg=call_1+" "+cexch+" "+grid6;//msg=trim(call_1)//' '//cexch//' '//grid6
        if (ir==1) msg=call_1+" R "+cexch+" "+grid6;//msg=trim(call_1)//' R '//cexch//' '//grid6
    }*/
    else if (i3==0 && (n3==3 || n3==4))
    {
        //! 0.3   WA9XYZ KA1ABC R 16A EMA            28 28 1 4 3 7    71   ARRL Field Day
        //! 0.4   WA9XYZ KA1ABC R 32A EMA            28 28 1 4 3 7    71   ARRL Field Day
        //read(c77,1030) n28a,n28b,ir,intx,nclass,isec
        //1030 format(2b28,b1,b4,b3,b7)   //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        int n28a = BinToInt32(c77,0,28);
        int n28b = BinToInt32(c77,28,56);
        int ir = BinToInt32(c77,56,57);
        int intx = BinToInt32(c77,57,61);
        int nclass = BinToInt32(c77,61,64);
        int isec = BinToInt32(c77,64,71);
        if (isec>NSEC || isec<1)
        {
            unpk77_success=false;//HV ???
            isec=1;
        }
        unpack28(n28a,call_1,unpk28_success);
        if (!unpk28_success || n28a<=2) unpk77_success=false;
        unpack28(n28b,call_2,unpk28_success);
        if (!unpk28_success || n28b<=2) unpk77_success=false;
        int ntx=intx+1;
        if (n3==4) ntx=ntx+16;
        QString cntx = QString("%1").arg((ntx));//write(cntx(1:2),1032) ntx  1032 format(i2)
        cntx.append(QChar((int)('A')+nclass));//cntx(3:3)=char(ichar('A')+nclass)
        if (ir==0 && ntx<10)  msg=call_1+" "+call_2+" "+cntx+" "+csec_77[isec-1];//msg=trim(call_1)//' '//trim(call_2)//cntx//' '//csec(isec)
        if (ir==1 && ntx<10)  msg=call_1+" "+call_2+" R "+cntx+" "+csec_77[isec-1];//msg=trim(call_1)//' '//trim(call_2)//' R'//cntx//' '//csec(isec)
        if (ir==0 && ntx>=10) msg=call_1+" "+call_2+" "+cntx+" "+csec_77[isec-1];//msg=trim(call_1)//' '//trim(call_2)//' '//cntx//' '//csec(isec)
        if (ir==1 && ntx>=10) msg=call_1+" "+call_2+" R "+cntx+" "+csec_77[isec-1];//msg=trim(call_1)//' '//trim(call_2)//' R '//cntx//' '//csec(isec)
        //qDebug()<<ir<<ntx;
    }
    else if (i3==0 && n3==5)
    {
        //! 0.5   0123456789abcdef01                 71               71   Telemetry (18 hex)
        //read(c77,1006) ntel
        //1006 format(b23,2b24) //write(msg,1007) ntel //1007 format(3z6.6)
        int b23  = BinToInt32(c77,0,23);
        int b24a = BinToInt32(c77,23,47);
        int b24b = BinToInt32(c77,47,71);
        msg = QString("%1").arg(b23,0,16);
        msg.append(QString("%1").arg(b24a,0,16));
        msg.append(QString("%1").arg(b24b,0,16));
        msg=msg.toUpper();
        msg.append("                          ");
        //qDebug()<<"HEX msg"<<msg;
        for (int i = 0; i < 18; ++i)//no start with 0 hex
        {//do i=1,18
            if (msg.at(i)!='0') break;
            msg[i]=' ';
        }
        msg=msg.trimmed();
    }
    else if (i3==0 && n3==6)// not used in MSHV   //else if(i3.eq.0 .and. n3.eq.6) then
    {
        //WSPR Type 1, WSPR Type 2, WSPR Type 3
        unpk77_success=false;// for any case
    }
    else if (i3==0 && n3>6) //2.47 if(i3.eq.0 .and. n3.gt.6)
    {
        unpk77_success=false;
    }
    else if (i3==1 || i3==2)
    {
        //! Type 1 (standard message) or Type 2 ("/P" form for EU VHF contest)
        //read(c77,1000) n28a,ipa,n28b,ipb,ir,igrid4,i3
        //1000 format(2(b28,b1),b1,b15,b3)
        int n28a = BinToInt32(c77,0,28);
        int ipa = BinToInt32(c77,28,29);
        int n28b = BinToInt32(c77,29,57);
        int ipb = BinToInt32(c77,57,58);
        int ir = BinToInt32(c77,58,59);
        int igrid4 = BinToInt32(c77,59,74);
        i3 = BinToInt32(c77,74,77); //no need 2.12  //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        unpack28(n28a,call_1,unpk28_success); //qDebug()<<"C1 unpk77_success"<<call_1<<unpk77_success;
        /*if(nrx.eq.1 .and. mycall13_set .and. hashmy22.eq.(n28a-2063592)) then
        call_1='<'//trim(mycall13)//'>'
        unpk28_success=.true.
        endif*/
        if (!unpk28_success) unpk77_success=false;
        unpack28(n28b,call_2,unpk28_success); //qDebug()<<"C2 unpk77_success"<<call_2<<unpk77_success;
        if (!unpk28_success) unpk77_success=false;
        bool is_p = false;//for pouse in msg  CQ_NA, CQ_TEST 2.29
        if (call_1.mid(0,3)=="CQ_")
        {
            call_1[2]=' ';
            is_p = true;
        }
        if (call_1.indexOf("<")<0) //hv -1  if(index(call_1,'<').le.0) then
        {
            int i=call_1.count();//2.21 A4A count=3  i=index(call_1,' ')
            if (is_p) i = 2;//reset have pouse CQ NA, CQ TEST.... 2.29
            if (i>=3 && ipa==1 && i3==1) call_1.append("/R"); //if(i.ge.4 .and. ipa.eq.1 .and. i3.eq.1) call_1(i:i+1)='/R'  "WA9XYZ/R KA1ABC/R R FN42           " call_1(i:i+1)="/R";
            if (i>=3 && ipa==1 && i3==2) call_1.append("/P"); //if(i.ge.4 .and. ipa.eq.1 .and. i3.eq.2) call_1(i:i+1)='/P'  call_1(i:i+1)='/P'
            if (i>=3)//2.21 A4A count=3
            {
                c1_rx_calls = call_1;//.trimmed();
                QString cha = call_1+"            ";//+12 pouses
                int n10,n12,n22;
                save_hash_call(cha,n10,n12,n22);//cha+spaces 2.20
            }
        }
        if (call_2.indexOf("<")<0) //hv -1  if(index(call_2,'<').le.0) then
        {
            int i=call_2.count();//2.21 A4A count=3  i=index(call_2,' ')
            if (i>=3 && ipb==1 && i3==1) call_2.append("/R"); //if(i.ge.4 .and. ipb.eq.1 .and. i3.eq.1) call_2(i:i+1)='/R'
            if (i>=3 && ipb==1 && i3==2) call_2.append("/P"); //if(i.ge.4 .and. ipb.eq.1 .and. i3.eq.2) call_2(i:i+1)='/P'
            if (i>=3)//2.21 A4A count=3
            {
                c2_rx_calls = call_2;//.trimmed();
                QString cha = call_2+"            ";//+12 pouses
                int n10,n12,n22;
                save_hash_call(cha,n10,n12,n22);//cha+spaces 2.20
            }
        }
        if (igrid4<=MAXGRID4)
        {
            QString grid4 = "    ";
            bool unpkg4_success = to_grid4(igrid4,grid4);
            if (!unpkg4_success) unpk77_success=false;
            if (ir==0) msg=call_1+" "+call_2+" "+grid4;//msg=trim(call_1)//' '//trim(call_2)//' '//grid4
            if (ir==1) msg=call_1+" "+call_2+" R "+grid4;//msg=trim(call_1)//' '//trim(call_2)//' R '//grid4
            if (msg.mid(0,3)=="CQ " && ir==1) unpk77_success=false;//if(msg(1:3).eq.'CQ ' .and. ir.eq.1) unpk77_success=.false.
        }
        else
        {
            int irpt=igrid4-MAXGRID4;  //qDebug()<<call_1<<call_2<<irpt<<igrid4<<ir;
            if (irpt==1) msg=call_1+" "+call_2;//msg=trim(call_1)//' '//trim(call_2)
            if (irpt==2) msg=call_1+" "+call_2+" RRR";//msg=trim(call_1)//' '//trim(call_2)//' RRR'
            if (irpt==3) msg=call_1+" "+call_2+" RR73";//msg=trim(call_1)//' '//trim(call_2)//' RR73'
            if (irpt==4) msg=call_1+" "+call_2+" 73";//msg=trim(call_1)//' '//trim(call_2)//' 73'
            if (irpt>=5)
            {
                int isnr = irpt-35;//2.47
                if (isnr>50) isnr=isnr-101;
                QString crpt;
                if (isnr>-1)
                    crpt="+"+QString("%1").arg(isnr,2,10,QChar('0'));
                else
                    crpt="-"+QString("%1").arg((int)fabs(isnr),2,10,QChar('0'));
                if (ir==0) msg=call_1+" "+call_2+" "+crpt;//msg=trim(call_1)//' '//trim(call_2)//' '//crpt
                if (ir==1) msg=call_1+" "+call_2+" R"+crpt;//msg=trim(call_1)//' '//trim(call_2)//' R'//crpt
            }
            if (msg.mid(0,3)=="CQ " && irpt>=2) unpk77_success=false; //if(msg(1:3).eq.'CQ ' .and. irpt.ge.2) unpk77_success=.false.
        }
        //qDebug()<<msg<<unpk77_success;
    }
    else if (i3==3)
    {
        //! Type 3: ARRL RTTY Contest
        //read(c77,1040) itu,n28a,n28b,ir,irpt,nexch,i3
        //1040 format(b1,2b28.28,b1,b3.3,b13.13,b3.3)  TU; W9XYZ K1ABC R 579 MA
        int itu = BinToInt32(c77,0,1);               //TU; W9XYZ G8ABC R 559 0013
        int n28a = BinToInt32(c77,1,29);
        int n28b = BinToInt32(c77,29,57);
        int ir = BinToInt32(c77,57,58);
        int irpt = BinToInt32(c77,58,61);
        int nexch = BinToInt32(c77,61,74);
        //int i3 = BinToInt32(c77,74,77);
        QString crpt = "5"+QString("%1").arg(irpt+2)+"9";// write(crpt,1042) irpt+2  //1042 format('5',i1,'9')
        int nserial=nexch;
        int imult=-1;
        if (nexch>8000) //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        {
            imult=nexch-8000;
            nserial=-1;
        }
        unpack28(n28a,call_1,unpk28_success);
        if (!unpk28_success) unpk77_success=false;
        unpack28(n28b,call_2,unpk28_success);
        if (!unpk28_success) unpk77_success=false;
        imult=0; //??
        nserial=0; //??
        if (nexch>8000) imult=nexch-8000;
        if (nexch<8000) nserial=nexch;

        if (imult>=1 && imult<=NUSCAN)
        {
            if (itu==0 && ir==0) //msg=trim(call_1)//' '//trim(call_2)//' '//crpt//' '//cmult(imult)
                msg=call_1+" "+call_2+" "+crpt+" "+cmult_77[imult-1];
            if (itu==1 && ir==0) //msg='TU; '//trim(call_1)//' '//trim(call_2)//' '//crpt//' '//cmult(imult)
                msg="TU; "+call_1+" "+call_2+" "+crpt+" "+cmult_77[imult-1];
            if (itu==0 && ir==1) //msg=trim(call_1)//' '//trim(call_2)//' R '//crpt//' '//cmult(imult)
                msg=call_1+" "+call_2+" R "+crpt+" "+cmult_77[imult-1];
            if (itu==1 && ir==1) //msg='TU; '//trim(call_1)//' '//trim(call_2)//' R '//crpt//' '//cmult(imult)
                msg="TU; "+call_1+" "+call_2+" R "+crpt+" "+cmult_77[imult-1];
        }
        else if (nserial>=1 && nserial<=7999)
        {
            QString cserial = QString("%1").arg(nserial,4,10,QChar('0'));//write(cserial,'(i4.4)') nserial
            if (itu==0 && ir==0) //msg=trim(call_1)//' '//trim(call_2)//' '//crpt//' '//cserial
                msg=call_1+" "+call_2+" "+crpt+" "+cserial;
            if (itu==1 && ir==0) //msg='TU; '//trim(call_1)//' '//trim(call_2)//' '//crpt//' '//cserial
                msg="TU; "+call_1+" "+call_2+" "+crpt+" "+cserial;
            if (itu==0 && ir==1) //msg=trim(call_1)//' '//trim(call_2)//' R '//crpt//' '//cserial
                msg=call_1+" "+call_2+" R "+crpt+" "+cserial;
            if (itu==1 && ir==1) //msg='TU; '//trim(call_1)//' '//trim(call_2)//' R '//crpt//' '//cserial
                msg="TU; "+call_1+" "+call_2+" R "+crpt+" "+cserial;
        }
    }
    else if (i3==4)
    {
        //read(c77,1050) n12,n58,iflip,nrpt,icq
        //1050 format(b12,b58,b1,b2,b1)
        int n120 = BinToInt32(c77,0,12);
        //long long int n58; = BinToInt64(c77,12,70);
        long long int n58 = 0;//unsigned
        for (int i = 12; i < 70; ++i)
        {
            n58 <<= 1;
            n58 |= c77[i];//-0
        }
        int iflip = BinToInt32(c77,70,71);
        int nrpt = BinToInt32(c77,71,73);
        int icq = BinToInt32(c77,73,74);
        QString c11 = "                "; //qDebug()<<"1"<<n58<<fmod(n58,38);
        for (int i = 10; i>=0; --i)
        {//do i=11,1,-1
            //int j=fmod(n58,38);
            int j=(n58 % 38);  // c++ mod = % to lost presision in 64-bit
            c11[i]=c_77_04[j];
            n58=n58/38;
            //qDebug()<<"2"<<c11<<j;
        }
        hash12(n120,call_3); //qDebug()<<"RX n12="<<n12<<call_3;
        if (iflip==0)
        {
            call_1=call_3;
            call_2=c11.trimmed();//adjustl(c11)//'  '
            c2_rx_calls = call_2; //add_call_to_recent_calls(call_2)
            int n10,n12,n22;
            QString cha = call_2+"            ";//+12 pouses
            save_hash_call(cha,n10,n12,n22);//cha+spaces 2.29
            //qDebug()<<"1"<<call_2<<n12;
        }
        else
        {
            call_1=c11.trimmed();//adjustl(c11)//'  '
            call_2=call_3;
            c1_rx_calls = call_1; //add_call_to_recent_calls(call_1)
            int n10,n12,n22;
            QString cha = call_1+"            ";//+12 pouses
            save_hash_call(cha,n10,n12,n22);//cha+spaces 2.29
            //qDebug()<<"2"<<call_1;
        }
        if (icq==0)
        {
            if (nrpt==0) msg=call_1+" "+call_2;        //msg=trim(call_1)//' '//trim(call_2)
            if (nrpt==1) msg=call_1+" "+call_2+" RRR"; //msg=trim(call_1)//' '//trim(call_2)//' RRR'
            if (nrpt==2) msg=call_1+" "+call_2+" RR73";//msg=trim(call_1)//' '//trim(call_2)//' RR73'
            if (nrpt==3) msg=call_1+" "+call_2+" 73";  //msg=trim(call_1)//' '//trim(call_2)//' 73'
        }
        else
            msg="CQ "+call_2;//'CQ '//trim(call_2)
        //qDebug()<<call_1<<call_2<<msg<<unpk77_success;//<<i3<<n3;
    }
    /*else if (i3==5) //2.39 old WWROF contest
    {											//1 28 28 1 3 13
        //! 5    TU; W9XYZ K1ABC R-09 FN             1 28 28 1 7 9       74   WWROF contest
        //read(c77,1041) itu,n28a,n28b,ir,irpt,nexch,i3
        //1041 format(b1,2b28.28,b1,b7.7,b9.9,b3.3)
        int itu = BinToInt32(c77,0,1);
        int n28a = BinToInt32(c77,1,29);
        int n28b = BinToInt32(c77,29,57);
        int ir = BinToInt32(c77,57,58);
        int irpt = BinToInt32(c77,58,65);
        int nexch = BinToInt32(c77,65,74);
        unpack28(n28a,call_1,unpk28_success);
        if (!unpk28_success) unpk77_success=false;
        unpack28(n28b,call_2,unpk28_success);
        if (!unpk28_success) unpk77_success=false;
        QString crpt;// = QString("%1").arg(irpt-35); //write(crpt,'(i3.2)') irpt-35
        //if(crpt.mid(1,1)!="-") crpt.prepend("+");//if(crpt(1:1).eq.' ') crpt(1:1)='+'
        irpt-=35;
        if (irpt>-1)
            crpt="+"+QString("%1").arg(irpt,2,10,QChar('0'));
        else
            crpt="-"+QString("%1").arg((int)fabs(irpt),2,10,QChar('0'));

        int n1=nexch/18;
        int n2=nexch - 18*n1;
        QString cfield = "  ";
        cfield [0]=QChar((int)('A')+n1); //cfield(1:1)=char((int)('A')+n1)
        cfield [1]=QChar((int)('A')+n2); //cfield(2:2)=char((int)('A')+n2)
        if (itu==0 && ir==0) msg=call_1+" "+call_2+" "+crpt+" "+cfield;//trim(call_1)//' '//trim(call_2)//' '//crpt//' '//cfield
        if (itu==1 && ir==0) msg="TU; "+call_1+" "+call_2+" "+crpt+" "+cfield;//'TU; '//trim(call_1)//' '//trim(call_2)//' '//crpt//' '//cfield
        if (itu==0 && ir==1) msg=call_1+" "+call_2+" R"+crpt+" "+cfield;//trim(call_1)//' '//trim(call_2)//' R'//crpt//' '//cfield
        if (itu==1 && ir==1) msg="TU; "+call_1+" "+call_2+" R"+crpt+" "+cfield;//'TU; '//trim(call_1)//' '//trim(call_2)//' R'//crpt//' '//cfield
    }*/
    else if (i3==5)//2.39 new EU VHF contest
    {
        //! Type 5  <PA3XYZ> <G4ABC/P> R 590003 IO91NP      h12 h22 r1 s3 S11 g25
        //! EU VHF contest
        //read(c77,1060) n12,n22,ir,irpt,iserial,igrid6
        //1060 format(b12,b22,b1,b3,b11,b25)
        int n12     = BinToInt32(c77,0,12);
        int n22     = BinToInt32(c77,12,34);
        int ir      = BinToInt32(c77,34,35);
        int irpt    = BinToInt32(c77,35,38);
        int iserial = BinToInt32(c77,38,49);
        int igrid6  = BinToInt32(c77,49,74);

        if (igrid6<0 || igrid6>18662399)// c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        {
            unpk77_success=false;
            return "";
        }

        hash12(n12,call_1); //call hash12(n12,call_1)
        //if(n12.eq.hashmy12) call_1='<'//trim(mycall13)//'>'//???
        hash22(n22,call_2);//call hash22(n22,call_2)
        QString cexch = QString("%1").arg((52+irpt),2,10,QChar('0'))+QString("%1").arg(iserial,4,10,QChar('0'));//nrs=52+irpt
        //write(cexch,1022) nrs,iserial
        //1022 format(i2,i4.4)
        //call to_grid6(igrid6,grid6)
        QString grid6 = "      ";
        bool unpkg6_success = to_grid6(igrid6,grid6);
        if (!unpkg6_success) unpk77_success=false;
        //if(ir.eq.0) msg=trim(call_1)//' '//trim(call_2)//' '//cexch//' '//grid6
        //if(ir.eq.1) msg=trim(call_1)//' '//trim(call_2)//' R '//cexch//' '//grid6  */
        msg=call_1+" "+call_2+" "+cexch+" "+grid6;//msg=trim(call_1)//' '//cexch//' '//grid6
        if (ir==1) msg=call_1+" "+call_2+" R "+cexch+" "+grid6;//msg=trim(call_1)//' R '//cexch//' '//grid6
    }
    else if (i3>=6) //then ! i3 values 6 and 7 are not yet defined  //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {
        unpk77_success=false;
    }

    if (msg.mid(0,4)=="CQ <") unpk77_success=false;//start in wsjt220rc2 2.34 stop in wsjt210 ???
    // LZ2HV for full hash msk40  no only in ->else if (i3==1 || i3==2)
    /*c1_rx_calls = call_1;
    c2_rx_calls = call_2;
    c1_rx_calls = c1_rx_calls.remove("<");
    c1_rx_calls = c1_rx_calls.remove(">");
    c2_rx_calls = c2_rx_calls.remove("<");
    c2_rx_calls = c2_rx_calls.remove(">");
    qDebug()<<msg<<c1_rx_calls<<c2_rx_calls;*/
    // LZ2HV hash msk40

    msg=msg.trimmed();  //qDebug()<<"unpk77 ="<<msg<<i3<<n3;

    return msg + "|"+call_1+";"+call_2+";"+call_3;
}
////////////// END Unpack //////////////////////////////////////////////////////////////

QString PackUnpackMsg77::RemBegEndWSpaces(QString str)
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
QString PackUnpackMsg77::RemWSpacesInside(QString s)
{
    for (int i = 0; i<s.count(); i++)
        s.replace("  "," ");
    return s;
}
void PackUnpackMsg77::chkcall(QString w,QString &bc,bool &cok)
{
    int i0 = -1;
    int nbc = 0;
    int i1 = 0;
    int n = 0;

    //w="SP/LZ2HVV   ";//"sp/lz2"
    cok=true;
    bc=w.mid(0,6);//(1:6)
    int n1=w.indexOf(" ");//qDebug()<<n1;//w.count();//n1=len_trim(w)
    if (n1>11) goto c100; //VP8H/LZ2HVV c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (w.indexOf(".")>=0) goto c100; //>=1 index(w,'.')>=1
    if (w.indexOf("+")>=0) goto c100; //>=1
    if (w.indexOf("-")>=0) goto c100; //>=1
    if (w.indexOf("?")>=0) goto c100; //>=1
    if (n1>6 && w.indexOf("/")<0) goto c100; //no slash if(n1.gt.6 .and. index(w,'/')<=0) go to 100

    i0=w.indexOf("/");
    if (fmax(i0,n1-i0-1)>6) goto c100; //SP/LZ2HVV  LZ2HVV/P if(max(i0-1,n1-i0).gt.6) go to 100      !Base call must be < 7 characters
    if (i0>=1 && i0<=n1-2) // if(i0>=2 .and. i0<=n1-1) then       !Extract base call from compound call
    {
        if (i0<=n1-i0-1) bc=w.mid(i0+1,n1-i0-1);//+"          "; //back SP/xxxxx if(i0-1<=n1-i0) bc=w(i0+1:n1)//'   '
        if (i0>n1-i0-1)  bc=w.mid(0,i0);//+"          ";         //front xxxxxx/P if(i0-1>n1-i0) bc=w(1:i0-1)//'   '
    }
    bc.append("            ");//HV need
    nbc=bc.indexOf(" "); //qDebug()<<"1111"<<w<<bc<<nbc; //len_trim(bc)
    if (nbc>6) goto c100;//  !Base call should have no more than 6 characters
    //! One of first two characters (c1 or c2) must be a letter
    //if((.not.isletter(bc(1:1))) .and. (.not.isletter(bc(2:2)))) go to 100
    if ((!bc.at(0).isLetter()) && (!bc.at(1).isLetter())) goto c100;
    if (bc.at(0)=='Q') goto c100;             //!Calls don't start with Q

    //! Must have a digit in 2nd or 3rd position
    i1=0;
    if (bc.at(1).isDigit()) i1=1;//if(isdigit(bc(2:2))) i1=2;
    if (bc.at(2).isDigit()) i1=2;//if(isdigit(bc(3:3))) i1=3;

    if (i1==0) goto c100;
    //qDebug()<<bc<<n<<i1<<nbc-1<<cok;
    //! Callsign must have a suffix of 1-3 letters
    if (i1==(nbc-1)) goto c100;
    n=0;
    for (int i = i1+1; i<nbc; ++i)
    {//do i=i1+1,nbc
        int j=(int)bc.at(i).toLatin1(); //qDebug()<<bc.at(i)<<j<<(int)'A'<<(int)'Z';
        if (j<(int)'A' || j>(int)'Z') goto c100;//if(j.lt.ichar('A') .or. j.gt.ichar('Z')) go to 100
        n++;
    }
    //qDebug()<<"chkcall"<<w<<n;
    if (n>=1 && n<=3) return;//goto c200;
c100:
    cok = false; //100 cok=.false.
    //c200:
    //return;
}
void PackUnpackMsg77::split77(QString &msg,int &nwords,/*int *nw,*/QString *w)
{
    //! Convert msg to upper case; collapse multiple blanks; parse into words.
    bool ok1 = true;
    QString bcall_1; //qDebug()<<w[0]<<w[1]<<w[2]<<w[3]<<w[4];
    msg = RemWSpacesInside(msg);
    msg = RemBegEndWSpaces(msg);
    msg = msg.toUpper();
    QStringList ls = msg.split(" ");
    nwords = ls.count();
    if (nwords>20) nwords=20;//hv
    for (int i = 0; i<nwords; ++i)
    {
        QString s = ls[i];//"12345678901234567"
        for (int j = 0; j<s.count(); ++j)
        {
            if (j<13)//max 13 char
                w[i][j]=(s[j]);
        }
    }
    if (nwords<=0) return;//goto c900;       //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    msg.append("                                     ");
    if (nwords<2) return;//goto c900;        //if(nwords<3) go to 900

    //qDebug()<<"SP/LZ2HVV"<<w[2];
    chkcall(w[2],bcall_1,ok1);      //call chkcall(w(3),bcall_1,ok1)
    if (ok1 && w[0].mid(0,3)=="CQ ") //if(ok1 .and. w(1)(1:3).eq.'CQ ') then
    {
        w[0]="CQ_"+w[1].mid(0,10);//w(1)='CQ_'//w(2)(1:10) !Make "CQ " into "CQ_"
        for (int i = 0; i<10; ++i)//only 10 words ???
            w[i+1]=w[i+2];       //w(2:12)=w(3:13)        !Move all remeining words down by one
        nwords-=1;
    }
    //qDebug()<<w[0]<<w[1]<<w[2]<<w[3]<<w[4]<<nwords<<bcall_1<<ok1;
    //c900:
    //return;
}

int PackUnpackMsg77::ihashcall(QString c0,int m)
{
    /*integer*8 n8
    character*13 c0,c1
    character*38 c
    data c/' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/'/*/ //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    int res = 0;
    QString c1;
    QString c = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/";

    c1=c0;
    if (c1.at(0)==(QChar)'<') c1=c1.mid(1,c1.count()-1)+"             "; //if(c1(1:1).eq.'<') c1=c1(2:)
    int i=c1.indexOf(">");//i=index(c1,'>')
    if (i>-1) //c1.replace(">"," ");//c1(i:)='         ';
        for (int j = i; j<c1.count(); ++j)
            c1[j]=(QChar)' ';
    unsigned long long int n8=0;
    //qDebug()<<"-----------------"<<c1;
    for (int j = 0; j<11; ++j)
    {//do i=1,11
        int z=c.indexOf(c1[j]);  //j=index(c,c1(i:i)) - 1
        n8=38*n8 + z;
    }
    // Left Shift 	ISHFT 	ISHFT(N,M) (M > 0) 	<< 	n<<m 	n shifted left by m bits
    //Right Shift 	ISHFT 	ISHFT(N,M) (M < 0) 	>> 	n>>m 	n shifted right by m bits
    //qDebug()<<"1 n8"<<n8;
    n8 *= 47055833459;//47055833459=36bit
    //qDebug()<<"2 n8"<<n8; // 3631928919119574816  110010011001110011
    //long long int m1 = (64-m);//11001001100111001101000001101111110001100101110011011100100000
    res=(n8 >> (64-m));  //=ishft(47055833459_8*n8,m-64)<-ne taka na c++ = 64-m
    //qDebug()<<res<<c1<<m;
    return res;
}
void PackUnpackMsg77::save_hash_call_mam(QStringList ls)
{
    if (!sf_dec_gen) return;// only decoders	for any case
    int f_pos = 4;
    for (int i = 0; i < ls.count(); ++i)
    {
        QString c13 = ls.at(i)+"                    ";
        int n10,n12,n22;
        n10=ihashcall(c13,10);
        n12=ihashcall(c13,12);
        n22=ihashcall(c13,22);

        if (ihash22d[f_pos]==n22)
        {
            if (f_pos<nzhash_start_writed-1)
                f_pos++;
            continue;
        }

        if(n10>=0 && n10<=1023) ihash10d[f_pos]=n10;//2.47
        if(n12>=0 && n12<=4095) ihash12d[f_pos]=n12;//2.47
        ihash22d[f_pos]=n22;
        int idx = c13.indexOf(" ");
        callsign_hashd[f_pos]=c13.mid(0,idx);
        if (f_pos<nzhash_start_writed-1)
            f_pos++;
    }
}
void PackUnpackMsg77::save_hash_call_my_his_r1_r2(QString call,int pos)
{
    if (!sf_dec_gen) return;// only decoders	for any case //pos 0=my 1=his 2=r1 3=r2
    if (call.isEmpty())	return;//2.47
    QString c13 = call+"                    ";
    int n10,n12,n22;
    n10=ihashcall(c13,10);
    n12=ihashcall(c13,12);
    n22=ihashcall(c13,22);
    if(n10>=0 && n10<=1023) ihash10d[pos]=n10;//2.47
    if(n12>=0 && n12<=4095) ihash12d[pos]=n12;//2.47
    ihash22d[pos]=n22;
    int idx = c13.indexOf(" ");
    callsign_hashd[pos]=c13.mid(0,idx);
}
void PackUnpackMsg77::save_hash_call(QString c13,int &n10,int &n12,int &n22)
{
    //qDebug()<<"-> inportent c13+pouses <-   "<<c13<<sf_dec_gen;
    if (c13.at(0)==' ' || c13.mid(0,5)=="<...>") return;
    if (sf_dec_gen)
    {
        n10=ihashcall(c13,10);
        n12=ihashcall(c13,12);
        n22=ihashcall(c13,22);
        //qDebug()<<c13<<n10<<n12<<n22;
        for (int i = nzhash_start_writed; i<MAXHASHD; ++i)
            if (ihash22d[i]==n22) return; //if(ihash22(i).eq.n22) go to 900    // !This one is already in the table

        if(n10>=0 && n10<=1023) ihash10d[nzhash_pos_writed]=n10;//2.47
        if(n12>=0 && n12<=4095) ihash12d[nzhash_pos_writed]=n12;//2.47
        ihash22d[nzhash_pos_writed]=n22;
        int idx = c13.indexOf(" ");
        callsign_hashd[nzhash_pos_writed]=c13.mid(0,idx);
        //qDebug()<<nzhash_pos_writed<<callsign_hashd[nzhash_pos_writed]<<n10<<n12<<n22;
        nzhash_pos_writed++;
        if (nzhash_pos_writed > (MAXHASHD-1))
            nzhash_pos_writed = nzhash_start_writed;

        /*QString sss = "";
        for (int i= 0; i < 15; i++)
        {
            sss.append("\n");
            sss.append(QString("%1").arg(i)+" = ");
            sss.append(callsign_hashd[i]+" ");
            sss.append(QString("%1").arg(ihash10d[i])+" ");
            sss.append(QString("%1").arg(ihash12d[i])+" ");
            sss.append(QString("%1").arg(ihash22d[i]));
        }
        qDebug()<<"Decoders Hash Sums ------------------> "<<sss;*/
    }
    else
    {
        n10=ihashcall(c13,10);
        n12=ihashcall(c13,12);
        n22=ihashcall(c13,22);
        //qDebug()<<nzhash_pos_writeg<<c13<<n10<<n12<<n22;
        for (int i = 0; i<MAXHASHG; ++i)
            if (ihash22g[i]==n22) return; //if(ihash22(i).eq.n22) go to 900    // !This one is already in the table

        if(n10>=0 && n10<=1023) ihash10g[nzhash_pos_writeg]=n10;//2.47
        if(n12>=0 && n12<=4095) ihash12g[nzhash_pos_writeg]=n12;//2.47
        ihash22g[nzhash_pos_writeg]=n22;
        int idx = c13.indexOf(" ");
        callsign_hashg[nzhash_pos_writeg]=c13.mid(0,idx);
        //qDebug()<<nzhash_pos_writeg<<callsign_hashg[nzhash_pos_writeg]<<n10<<n12<<n22;
        nzhash_pos_writeg++;
        if (nzhash_pos_writeg > (MAXHASHG-1))
            nzhash_pos_writeg = 0;

        /*QString sss = "";
        for (int i= 0; i < MAXHASHG; i++)
        {
            sss.append("\n");
            sss.append(QString("%1").arg(i)+" = ");
            sss.append(callsign_hashg[i]+" ");
            sss.append(QString("%1").arg(ihash10g[i])+" ");
            sss.append(QString("%1").arg(ihash12g[i])+" ");
            sss.append(QString("%1").arg(ihash22g[i]));
        }
        qDebug()<<"Generators Hash Sums ------------------> "<<sss;*/
    }
}
void PackUnpackMsg77::pack28(QString c13,int &n28)
{
    /*! Pack a special token, a 22-bit hash code, or a valid base call into a 28-bit
    ! integer.

     parameter (NTOKENS=2063592,MAX22=4194304)
     integer nc(6)
     logical is_digit,is_letter
     character*13 c13
     character*6 callsign
     character*1 c
     character*4 c4
     character*37 a1
     character*36 a2
     character*10 a3
     character*27 a4
     data a1/' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'/
     data a2/'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'/
     data a3/'0123456789'/
     data a4/' ABCDEFGHIJKLMNOPQRSTUVWXYZ'/
     data nc/37,36,19,27,27,27/
     
     is_digit(c)=c.ge.'0' .and. c.le.'9'
     is_letter(c)=c.ge.'A' .and. c.le.'Z'*/
    QString callsign;
    int NTOKENS=2063592;
    int MAX22=4194304;
    int n10 = 0;
    int n12 = 0;
    int n22 = 0;
    int iarea=-1;
    int n,i,nplet,npdig,nslet;
    int i1,i2,i3,i4,i5,i6;
    /*QString a1 = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    QString a2 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    QString a3 = "0123456789";
    QString a4 = " ABCDEFGHIJKLMNOPQRSTUVWXYZ";*/

    n28=-1; //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    //! Work-around for Swaziland prefix:
    if (c13.mid(0,4)=="3DA0") callsign="3D0"+c13.mid(4,4)+"      ";  //c13(5:7)
    //! Work-around for Guinea prefixes:
    if (c13.mid(0,2)=="3X" && c13.at(2).toLatin1()>=(int)'A' && c13.at(2)<=(int)'Z') callsign="Q"+c13.mid(2,5)+"      ";//c13(3:6)
    //! Check for special tokens first
    if (c13.mid(0,3)=="DE ")
    {
        n28=0;
        goto c900;
    }
    if (c13.mid(0,4)=="QRZ ")
    {
        n28=1;
        goto c900;
    }
    if (c13.mid(0,3)=="CQ ")
    {
        n28=2;
        goto c900;
    }
    if (c13.mid(0,3)=="CQ_")
    {
        //qDebug()<<c13;
        n=c13.indexOf(" ")-1;//n=len(trim(c13)) // len(trim(c13)) //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        if (n>=3 && n<=6)  // if(n>=4 && n<=7)
        {
            int nlet=0;
            int nnum=0;
            for (i = 3; i<n+1; ++i)
            {//do i=4,n
                int c=(int)c13.at(i).toLatin1();
                if (c>=(int)'A' && c<=(int)'Z') nlet++;
                if (c>=(int)'0' && c<=(int)'9') nnum++;
            }
            if (nnum==3 && nlet==0) //3=4?? ima 3 cifri if(nnum.eq.3 .and. nlet.eq.0) then
            {
                int nqsy = c13.mid(3,nnum).toInt(); //read(c13(4:3+nnum),*) nqsy
                n28=3+nqsy;  //qDebug()<<"nnum="<<nqsy<<nnum<<n28;
                goto c900;
            }
            if (nlet>=1 && nlet<=4 && nnum==0) //if(nlet.ge.1 .and. nlet.le.4 .and. nnum.eq.0) then
            {
                //QString c4=c13.mid(3,n-3+1)+"   ";  //c4=c13(4:n)//'   '
                //c4=adjustr(c4);
                QString c4=c13.mid(3,n-3+1);
                c4 = c4.rightJustified(4,' ');
                int m=0;
                for (i = 0; i<4; ++i)
                {//do i=1,4
                    int j=0;
                    int c=(int)c4.at(i).toLatin1();  //c=c4(i:i)
                    if (c>=(int)'A' && c<=(int)'Z') j=c-(int)('A')+1; //j=ichar(c)-ichar('A')+1
                    m=27*m + j;
                }
                n28=3+1000+m;  //qDebug()<<"let="<<c4<<nlet<<n28;
                goto c900;
            }
        }
    }
    //! Check for <...> callsign
    if (c13.at(0)==(QChar)'<')
    {
        save_hash_call(c13,n10,n12,n22);   //!Save callsign in hash table
        n28=NTOKENS + n22;  // qDebug()<<n28;
        goto c900;
    }
    //! Check for standard callsign
    iarea=-1;
    n=c13.indexOf(" ")-1; //n=len(trim(c13))
    for (i = n; i>=1; --i)
    {//do i=n,2,-1
        if (c13.at(i).isDigit()) break;  // if(is_digit(c13(i:i))) exit
    }
    iarea=i;                                   //!Call-area digit
    npdig=0;                                   //!Digits before call area
    nplet=0;                                   //!Letters before call area
    for (i = 0; i<iarea; ++i)
    {//do i=1,iarea-1
        if (c13.at(i).isDigit())  npdig++;//if(is_digit(c13(i:i))) npdig=npdig+1
        if (c13.at(i).isLetter()) nplet++;//if(is_letter(c13(i:i))) nplet=nplet+1
    }
    nslet=0;
    for (i = iarea+1; i<n+1; ++i)
    {//do i=iarea+1,n
        if (c13.at(i).isLetter()) nslet++;//if(is_letter(c13(i:i))) nslet=nslet+1
    }
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    //if(iarea<2 .or. iarea>3 .or. nplet==0 .or. npdig>=iarea-1 .or. nslet>3)
    if (iarea<1 || iarea>2 || nplet==0 || npdig>=iarea-0 || nslet>3)
    {
        //! Treat this as a nonstandard callsign: compute its 22-bit hash
        save_hash_call(c13,n10,n12,n22);   //!Save callsign in hash table
        n28=NTOKENS + n22;
        //qDebug()<<"c13 test"<<c13<<iarea<<nplet<<npdig<<nslet;
        goto c900;
    }

    //n=c13.indexOf(" ")-1;//n=len(trim(c13))
    //! This is a standard callsign
    // HV for all SWL cals
    save_hash_call(c13,n10,n12,n22);                 //!Save callsign in hash table
    if (iarea==1) callsign=" "+c13.mid(0,5)+"      "; // if(iarea.eq.2) callsign=' '//c13(1:5)
    if (iarea==2) callsign=c13.mid(0,6)+"      ";     //if(iarea.eq.3) callsign=c13(1:6)
    i1=a1_28.indexOf(callsign[0]); 					  //i1=index(a1,callsign(1:1))-1
    i2=a2_28.indexOf(callsign[1]);						  //i2=index(a2,callsign(2:2))-1
    i3=a3_28.indexOf(callsign[2]);						  //i3=index(a3,callsign(3:3))-1
    i4=a4_28.indexOf(callsign[3]);						  //i4=index(a4,callsign(4:4))-1
    i5=a4_28.indexOf(callsign[4]);						  //i5=index(a4,callsign(5:5))-1
    i6=a4_28.indexOf(callsign[5]);						  //i6=index(a4,callsign(6:6))-1
    n28=36*10*27*27*27*i1 + 10*27*27*27*i2 + 27*27*27*i3 + 27*27*i4 + 27*i5 + i6;
    n28=n28 + NTOKENS + MAX22; //qDebug()<<"1 n28"<<n28;
c900:
    //int two_on_27 = pow(2,(28-1));//pow(x,3.0)  (x**3)
    //n28=(n28 & two_on_27);//n28=iand(n28,2**28-1)

    // Left Shift 	ISHFT 	ISHFT(N,M) (M > 0) 	<< 	n<<m 	n shifted left by m bits
    //Right Shift 	ISHFT 	ISHFT(N,M) (M < 0) 	>> 	n>>m 	n shifted right by m bits
    n28=(n28 & ((1<<28)-1));               //NEW K1JT n28=iand(n28,ishft(1,28)-1)
    //qDebug()<<"2 n28"<<n28;
    return;
}

void PackUnpackMsg77::SetArrayBits(int in,int in_bits,bool *ar,int &co)
{
    int izz = in_bits-1;
    for (int i = 0; i < in_bits; ++i)
    {
        ar[co]=(1 & (in >> -(i-izz)));
        co++;
    }
}
void PackUnpackMsg77::pack77_01(int nwords,QString *w,int &i3,int &n3,bool *c77)
{
    /*! Pack a Type 0.1 message: DXpedition mode
     //! Example message:  "K1ABC RR73; W9XYZ <KH1/KH7Z> -11"   28 28 10 5*/
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    //qDebug()<<w[0]<<w[1]<<w[2]<<w[3]<<w[4]<<w[5]<<nwords;
    int n;
    int n5;
    bool ok1,ok2;
    QString bcall_1,bcall_2;
    int n28a = 0;
    int n28b = 0;
    int n10 = 0;
    int n12 = 0;
    int n22 = 0;

    if (nwords!=5) return;               //!Must have 5 words
    //if (!w[1].contains("RR73;")) return;//if(trim(w(2)).ne.'RR73;') go to 900    //!2nd word must be "RR73;"
    if (w[1].trimmed()!="RR73;") return;//if(trim(w(2)).ne.'RR73;') go to 900    //!2nd word must be "RR73;"
    if (w[3].mid(0,1)!="<") return;   //if(w(4)(1:1).ne.'<') go to 900         //!4th word must have <...>
    if (w[3].indexOf(">")<0) return; //if(index(w(4),'>')<1) go to 900
    n = w[4].toInt();//n=-99;
    if (n<-50 || n>59 ) return;//2.12 //??? 59 mskms             //read(w(5),*,err=1) n
    //if(n==-99) goto c900; //if(n.eq.-99) go to 900 !5th word must be a valid report
    n5=(n+30)/2;
    if (n5<0) n5=0;//if(n5.lt.0) n5=0
    if (n5>31) n5=31;
    chkcall(w[0],bcall_1,ok1);         //chkcall(w(1),bcall_1,ok1)
    if (!ok1) return;                  //!1st word must be a valid basecall
    chkcall(w[2],bcall_2,ok2);         //chkcall(w(3),bcall_2,ok2)
    if (!ok2) return;                  //!3rd word must be a valid basecall

    //! Type 0.1:  K1ABC RR73; W9XYZ <KH1/KH7Z> -11   28 28 10 5       71   DXpedition Mode
    i3=0;
    n3=1;
    pack28(w[0],n28a); //pack28(w(1),n28a);
    pack28(w[2],n28b); //call pack28(w(3),n28b)
    save_hash_call(w[3],n10,n12,n22);//save_hash_call(w(4),n10,n12,n22)
    /*write(c77,1010) n28a,n28b,n10,n5,n3,i3
    1010 format(2b28.28,b10.10,b5.5,2b3.3)
    */ //  28 28 10 5 3 3 = 77
    int co_t = 0;
    SetArrayBits(n28a,28,c77,co_t);
    SetArrayBits(n28b,28,c77,co_t);
    SetArrayBits(n10,10,c77,co_t);
    SetArrayBits(n5,5,c77,co_t);
    SetArrayBits(n3,3,c77,co_t);
    SetArrayBits(i3,3,c77,co_t);
    //qDebug()<<"pack77_01n"<<28a<<n28b<<n10<<n5<<n3<<i3<<co_t;
    /*QString sss = "";
    for (int i= 0; i < 77; i++)//2 pove4e
    {
        sss.append(QString("%1").arg((int)c77[i]));
        //if (i==28 || i==65 || i==71 || i==74)
        //sss.append("|");
    }
    qDebug()<<"1mm="<<sss<<co_t;

    qDebug()<<n28a<<n28b<<n10;*/
    //c900:
}
bool PackUnpackMsg77::is_grid6(QString s)
{
    bool res = false;
    if (s.count()==6)
    {
        int c1 = (int)s.at(0).toLatin1();
        int c2 = (int)s.at(1).toLatin1();
        int c3 = (int)s.at(2).toLatin1();
        int c4 = (int)s.at(3).toLatin1();
        int c5 = (int)s.at(4).toLatin1();
        int c6 = (int)s.at(5).toLatin1();
        if (c1>=(int)'A' && c1<=(int)'R' && c2>=(int)'A' && c2<=(int)'R' &&
                c3>=(int)'0' && c3<=(int)'9' && c4>=(int)'0' && c4<=(int)'9' &&
                c5>=(int)'A' && c5<=(int)'X' && c6>=(int)'A' && c6<=(int)'X' )
            res = true;
    }
    return res;
}
/* old eu cont
void PackUnpackMsg77::pack77_02(int nwords,QString *w,int &i3,int &n3,bool *c77)
{
    bool ok1;
    QString bcall_1;
    int n28a = 0;

    chkcall(w[0],bcall_1,ok1);
    if (!ok1) return;                           //!bcall_1 must be a valid basecall
    if (nwords<3 || nwords>4) return;//if(nwords.lt.3 .or. nwords.gt.4) return        //!nwords must be 3 or 4
    int nx=-1;  //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (nwords>=2) nx=w[nwords-2].toInt(); //if(nwords.ge.2) read(w(nwords-1),*,err=2) nx
    if (nx<520001 || nx>594095) return;     //!Exchange between 520001 - 594095
    if (!is_grid6(w[nwords-1].mid(0,6))) return;      //!Last word must be a valid grid6
    //! Type 0.2:   PA3XYZ/P R 590003 IO91NP           28 1 1 3 12 25   70   EU VHF contest
    i3=0;
    n3=2;
    int ip=0;
    QString c13=w[0];
    int i=w[0].indexOf("/P ");  //2.13  //i=index(w(1)//' ','/P ')
    if (i>=3) //if(i.ge.4) then  //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {
        ip=1;
        c13=w[0].mid(0,i)+"            ";   //w(1)(1:i-1)//'         '
    }
    pack28(c13,n28a);
    int ir=0;
    if (w[1].mid(0,2)=="R ") ir=1;//if(w(2)(1:2).eq.'R ') ir=1
    int irpt=nx/10000 - 52;
    //int iserial=fmod(nx,10000);
    int iserial=(nx % 10000);
    QString grid6=w[nwords-1].mid(0,6);
    int j1=((int)grid6.at(0).toLatin1()-(int)('A'))*18*10*10*24*24;
    int j2=((int)grid6.at(1).toLatin1()-(int)('A'))*10*10*24*24;
    int j3=((int)grid6.at(2).toLatin1()-(int)('0'))*10*24*24;
    int j4=((int)grid6.at(3).toLatin1()-(int)('0'))*24*24;
    int j5=((int)grid6.at(4).toLatin1()-(int)('A'))*24;
    int j6=((int)grid6.at(5).toLatin1()-(int)('A'));
    int igrid6=j1+j2+j3+j4+j5+j6;
    //write(c77,1010) n28a,ip,ir,irpt,iserial,igrid6,n3,i3
    //1010 format(b28.28,2b1,b3.3,b12.12,b25.25,b4.4,b3.3)
    //28 1 1 3 12 25 4 3    77   EU VHF contest
    int co_t = 0;
    SetArrayBits(n28a,28,c77,co_t);
    SetArrayBits(ip,1,c77,co_t);
    SetArrayBits(ir,1,c77,co_t);
    SetArrayBits(irpt,3,c77,co_t);
    SetArrayBits(iserial,12,c77,co_t);
    SetArrayBits(igrid6,25,c77,co_t);
    SetArrayBits(n3,4,c77,co_t);
    SetArrayBits(i3,3,c77,co_t);
    //qDebug()<<n28a<<ip<<ir<<irpt<<iserial<<igrid6<<n3<<i3<<grid6<<c13<<co_t;
}
*/
void PackUnpackMsg77::pack77_03(int nwords,QString *w,int &i3,int &n3,bool *c77)
{
    /*! Check 0.3 and 0.4 (ARRL Field Day exchange)
    parameter (NSEC=85)      !Number of ARRL Sections
    character*13 w(19)
    character*77 c77
    character*6 bcall_1,bcall_2
    character*3 csec(NSEC)*/
    //qDebug()<<w[0]<<w[1]<<w[2]<<w[3]<<w[4]<<w[5]<<nwords;
    bool ok1,ok2;
    QString bcall_1,bcall_2;
    //const int NSEC=85;
    int n28a=0;
    int n28b=0;

    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (nwords<4 || nwords>5) return; //WA9XYZ KA1ABC R 16A EMA
    chkcall(w[0],bcall_1,ok1);
    chkcall(w[1],bcall_2,ok2);
    if (!ok1 || !ok2) return;
    int isec=-1;
    QString test = w[nwords-1].mid(0,3);
    for (int i = 0; i<NSEC; ++i)
    {//do i=1,NSEC
        if (test==csec_77[i])
        {
            isec=i+1;//HV +1
            break;//exit
        }
    }
    if (isec==-1) return;
    //if (nwords==5 && !w[2].contains("R")) return;//if(nwords==5 && trim(w(3)).ne.'R') return;
    if (nwords==5 && w[2].trimmed()!="R") return;//if(nwords==5 && trim(w(3)).ne.'R') return;

    int ntx=-1;                       //WA9XYZ KA1ABC R 16A EMA
    int j=w[nwords-2].indexOf(" ")-1;  // j=len(trim(w(nwords-1)))-1
    ntx=w[nwords-2].mid(0,j).toInt();  //read(w(nwords-1)(1:j),*,err=1) ntx                !Number of transmitters
    if (ntx<1 || ntx>32) return;
    QChar crt = w[nwords-2].at(j);
    int nclass=(int)crt.toLatin1() - (int)('A');//nclass=ichar(w(nwords-1)(j+1:j+1))-ichar('A')
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    int m=w[nwords-1].indexOf(" ");//len(trim(w(nwords)))                            !Length of section abbreviation
    if (m<2 || m>3) return;
    //! 0.3   WA9XYZ KA1ABC R 16A EMA            28 28 1 4 3 7    71   ARRL Field Day
    //! 0.4   WA9XYZ KA1ABC R 32A EMA            28 28 1 4 3 7    71   ARRL Field Day
    i3=0;
    n3=3;                                // !Type 0.3 ARRL Field Day
    int intx=ntx-1;
    if (intx>=16) //if(intx.ge.16)
    {
        n3=4;                              //!Type 0.4 ARRL Field Day
        intx=ntx-17;
    }
    pack28(w[0],n28a);
    pack28(w[1],n28b);
    int ir=0;
    if (w[2].mid(0,2)=="R ") ir=1; //if(w(3)(1:2).eq.'R ') ir=1
    //write(c77,1010) n28a,n28b,ir,intx,nclass,isec,n3,i3
    //1010 format(2b28.28,b1,b4.4,b3.3,b7.7,2b3.3)
    // 28 28 1 4 3 7 3 3  71   ARRL Field Day
    int co_t = 0;
    SetArrayBits(n28a,28,c77,co_t);
    SetArrayBits(n28b,28,c77,co_t);
    SetArrayBits(ir,1,c77,co_t);
    SetArrayBits(intx,4,c77,co_t);
    SetArrayBits(nclass,3,c77,co_t);
    SetArrayBits(isec,7,c77,co_t);
    SetArrayBits(n3,3,c77,co_t);
    SetArrayBits(i3,3,c77,co_t);
    //qDebug()<<n28a<<n28b<<ir<<intx<<nclass<<isec<<n3<<i3<<co_t;
}
bool PackUnpackMsg77::is_grid4(QString s)
{
    bool res = false;
    if (s.count()==4)
    {
        int c1 = (int)s.at(0).toLatin1();
        int c2 = (int)s.at(1).toLatin1();
        int c3 = (int)s.at(2).toLatin1();
        int c4 = (int)s.at(3).toLatin1();
        if (c1>=(int)'A' && c1<=(int)'R' && c2>=(int)'A' && c2<=(int)'R' &&
                c3>=(int)'0' && c3<=(int)'9' && c4>=(int)'0' && c4<=(int)'9')
            res = true;
    }
    return res;
}
void PackUnpackMsg77::pack77_1(int nwords,QString *w,int &i3,int &n3,bool *c77)
{
    //! Check Type 1 (Standard 77-bit message) and Type 2 (ditto, with a "/P" call)
    /*parameter (MAXGRID4=32400)
    character*13 w(19),c13
    character*77 c77
    character*6 bcall_1,bcall_2
    character*4 grid4
    character c1*1,c2*2
    logical is_grid4
    logical ok1,ok2*/
    int MAXGRID4 = 32400;
    bool ok1,ok2;
    QString bcall_1,bcall_2,c1,c2,c3,c13,grid4;
    int c3_c = -1;
    int ir = 0;
    int irpt = 0;
    int n28a = 0;
    int n28b = 0;
    int ipa = 0;
    int ipb = 0;
    int igrid4 = 0;
    //qDebug()<<w[0]<<w[1]<<w[2]<<w[3]<<w[4]<<w[5]<<nwords;
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (nwords<2 || nwords>4) return; //WA9XYZ/R KA1ABC/R R FN42
    chkcall(w[0],bcall_1,ok1);
    chkcall(w[1],bcall_2,ok2);
    if (w[0].mid(0,3)=="DE " || w[0].mid(0,3)=="CQ_" || w[0].mid(0,3)=="CQ " || w[0].mid(0,3)=="QRZ ") ok1=true;
    if (w[0][0]=='<' && w[0].indexOf(">")>=5) ok1=true; //<KH1/KH7Z> <zz2zz>
    if (w[1][0]=='<' && w[1].indexOf(">")>=5) ok2=true;
    if (!ok1 || !ok2) return;
    if (w[0][0]=='<' && w[1].indexOf("/")>-1) return;//if(w(1)(1:1).eq.'<' .and. index(w(2),'/').gt.0) return
    if (w[1][0]=='<' && w[0].indexOf("/")>-1) return;//if(w(2)(1:1).eq.'<' .and. index(w(1),'/').gt.0) return;
    if (nwords==2 && (!ok2 || w[1].indexOf("/")>=1)) return;//if(nwords.eq.2 .and. (.not.ok2 .or. index(w(2),'/').ge.2)) return
    if (nwords==2) goto c10;

    c1=QString(w[nwords-1][0]);
    c2=w[nwords-1].mid(0,2);
    c3_c=w[nwords-1].indexOf(" ");
    c3=w[nwords-1].mid(0,c3_c);
    if (!is_grid4(w[nwords-1].mid(0,4)) && c1!="+" && c1!="-" && c2!="R+" && c2!="R-" &&
            c3!="RRR" && c3!="RR73" && c3!="73") return;
       
    if (c1=="+" || c1=="-")
    {
        ir=0; 
        bool okk = false; //2.59
        irpt = c3.toInt(&okk);//read(w(nwords),*,err=900) irpt
        if (!okk) return;//not a digit
        if(irpt>=-50 && irpt<=-31) irpt=irpt+101;//2.47
        irpt+=35;
    }
    else if (c2=="R+" || c2=="R-")
    {
        ir=1;
        bool okk = false; //2.59
        irpt = c3.mid(1,c3_c-1).toInt(&okk);//read(w(nwords)(2:),*,err=900) irpt
        if (!okk) return;//not a digit
        if(irpt>=-50 && irpt<=-31) irpt=irpt+101;//2.47
        irpt=irpt+35;
    }
    else if (c3=="RRR")
    {
        ir=0;
        irpt=2;
    }
    else if (c3=="RR73")
    {
        ir=0;
        irpt=3;
    }
    else if (c3=="73")
    {
        ir=0;
        irpt=4;
    }
    //! 1     WA9XYZ/R KA1ABC/R R FN42           28 1 28 1 1 15   74   Standard msg
    //! 2     PA3XYZ/P GM4ABC/P R JO22           28 1 28 1 1 15   74   EU VHF contest
c10:
    int i1psuffix = w[0].indexOf("/P ");//2.13 qDebug()<<i1psuffix<<w[0];//i1psuffix=index(w(1)//' ' ,'/P ')
    int i2psuffix = w[1].indexOf("/P ");//2.13 qDebug()<<i2psuffix<<w[1];//i2psuffix=index(w(2)//' ','/P ')
    if (nwords==2 || nwords==3 || (nwords==4 && w[2].mid(0,2)=="R ")) //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {
        n3=0;
        i3=1;                          //!Type 1: Standard message, possibly with "/R"
        if (i1psuffix>=3 || i2psuffix>=3) i3=2; //!Type 2, with "/P"
        //if (i1psuffix.ge.4.or.i2psuffix.ge.4) i3=2 !Type 2, with "/P"
    }
    c13=bcall_1+"            "; //c13=bcall_1//'       '
    if (c13.mid(0,3)=="CQ_" || w[0].mid(0,1)=="<") c13=w[0];
    pack28(c13,n28a);
    c13=bcall_2+"            ";//c13=bcall_2//'       '
    if (w[1].mid(0,1)=="<") c13=w[1];
    pack28(c13,n28b);
    ipa=0;
    ipb=0;  //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (i1psuffix>=3 || w[0].indexOf("/R ")>=3) ipa=1;//WA9XYZ/R KA1ABC/R R FN42
    if (i2psuffix>=3 || w[1].indexOf("/R ")>=3) ipb=1;
    //if(i1psuffix.ge.4.or.index(w(1)//' ','/R ').ge.4) ipa=1
    //if(i2psuffix.ge.4.or.index(w(2)//' ','/R ').ge.4) ipb=1

    grid4=w[nwords-1].mid(0,4);
    if (is_grid4(grid4))
    {
        ir=0;
        if (w[2].mid(0,2)=="R ") ir=1;
        int j1=((int)grid4.at(0).toLatin1()-(int)('A'))*18*10*10;//(int)grid4.at(0).toLatin1()
        int j2=((int)grid4.at(1).toLatin1()-(int)('A'))*10*10;
        int j3=((int)grid4.at(2).toLatin1()-(int)('0'))*10;
        int j4=((int)grid4.at(3).toLatin1()-(int)('0'));
        igrid4=j1+j2+j3+j4;
    }
    else
        igrid4=MAXGRID4 + irpt;

    if (nwords==2)
    {
        ir=0;
        irpt=1;
        igrid4=MAXGRID4+irpt;
    }
    /*write(c77,1000) n28a,ipa,n28b,ipb,ir,igrid4,i3
    1000 format(2(b28.28,b1),b1,b15.15,b3.3)
    return*/  //  28 1 28 1 1 15
    int co_t = 0;
    SetArrayBits(n28a,28,c77,co_t);
    SetArrayBits(ipa,1,c77,co_t);
    SetArrayBits(n28b,28,c77,co_t);
    SetArrayBits(ipb,1,c77,co_t);
    SetArrayBits(ir,1,c77,co_t);
    SetArrayBits(igrid4,15,c77,co_t);
    SetArrayBits(i3,3,c77,co_t);
    //qDebug()<<n28a<<ipa<<n28b<<ipb<<ir<<igrid4<<i3<<co_t;
}
void PackUnpackMsg77::pack77_3(int nwords,QString *w,int &i3,int &n3,bool *c77)
{
    /*! Check Type 2 (ARRL RTTY contest exchange)
    !ARRL RTTY   - US/Can: rpt state/prov      R 579 MA
    !     	     - DX:     rpt serial          R 559 0013

     parameter (NUSCAN=65)    !Number of US states and Canadian provinces/territories
     character*13 w(19)
     character*77 c77
     character*6 bcall_1,bcall_2
     character*3 cmult(NUSCAN),mult
     character crpt*3*/
    //bool ok1,ok2;
    QString bcall_1,bcall_2;


    //qDebug()<<w[0]<<w[1]<<w[2]<<w[3]<<w[4]<<w[5]<<w[6]<<nwords;
    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (w[0].mid(0,1)=="<" && w[1].mid(0,1)=="<") return; //220rc2 if(w(1)(1:1).eq.'<' .and. w(2)(1:1).eq.'<') go to 900
    if (nwords==4 || nwords==5 || nwords==6)
    {
        bool ok1,ok2;
        int i1=0;
        //if (w[0].mid(0,3)=="TU;") i1=1;//if(trim(w(1))=="TU;") i1=2;
        if (w[0].trimmed()=="TU;") i1=1;//if(trim(w(1))=="TU;") i1=2;
        chkcall(w[i1],bcall_1,ok1);
        chkcall(w[i1+1],bcall_2,ok2);
        if (!ok1 || !ok2) return;//go to 900          TU; W9XYZ G8ABC R 559 0013
        QString crpt=w[nwords-2].mid(0,3);   //qDebug()<<"gggggggg";  // TU; W9XYZ K1ABC R 579 MA
        int nserial=-1;
        if (crpt.mid(0,1)=="5" && crpt.mid(1,1)>="2" && crpt.mid(1,1)<="9" && crpt.mid(2,1)=="9")
        {
            nserial=0;
            nserial = w[nwords-1].toInt();//read(w(nwords),*,err=1) nserial
            //!1       i3=3
            //!        n3=0
        }
        QString mult="   ";
        int imult=-1;
        for (int i = 0; i<NUSCAN; ++i)
        {//do i=1,NUSCAN
            //qDebug()<<cmult_77[i]<<i;
            if (w[nwords-1].mid(0,3) == cmult_77[i])
            {
                imult=i+1;
                mult=cmult_77[i];
                break;//exit
            }
        }
        int nexch=0;
        if (nserial>0) nexch=nserial;
        if (imult>0) nexch=8000+imult;  //qDebug()<<mult<<nserial;
        if (mult!="   " || nserial>0)
        {
            int n28a = 0;
            int n28b = 0;
            i3=3;
            n3=0;
            int itu=0;
            //if (w[0].mid(0,3)=="TU;") itu=1;//if(trim(w(1)).eq.'TU;') itu=1
            if (w[0].trimmed()=="TU;") itu=1;//if(trim(w(1)).eq.'TU;') itu=1
            pack28(w[0+itu],n28a);
            pack28(w[1+itu],n28b);
            int ir=0;
            if (w[2+itu].mid(0,2)=="R ") ir=1;
            int irpt = w[2+itu+ir].toInt(); //read(w(3+itu+ir),*,err=900) irpt
            irpt=(irpt-509)/10 - 2;
            if (irpt<0) irpt=0;
            if (irpt>7) irpt=7;
            //! 3     TU; W9XYZ K1ABC R 579 MA             1 28 28 1 3 13       74   ARRL RTTY contest
            //! 3     TU; W9XYZ G8ABC R 559 0013           1 28 28 1 3 13       74   ARRL RTTY (DX)
            /*write(c77,1010) itu,n28a,n28b,ir,irpt,nexch,i3
            1010    format(b1,2b28.28,b1,b3.3,b13.13,b3.3)*/
            // 1 28 28 1 3 13 3
            int co_t = 0;
            SetArrayBits(itu,1,c77,co_t);
            SetArrayBits(n28a,28,c77,co_t);
            SetArrayBits(n28b,28,c77,co_t);
            SetArrayBits(ir,1,c77,co_t);
            SetArrayBits(irpt,3,c77,co_t);
            SetArrayBits(nexch,13,c77,co_t);
            SetArrayBits(i3,3,c77,co_t);
            //qDebug()<<itu<<n28a<<n28b<<ir<<irpt<<nexch<<i3<<co_t;
        }
    }
}
void PackUnpackMsg77::pack77_4(int nwords,QString *w,int &i3,int &n3,bool *c77)
{
    /*! Check Type 3 (One nonstandard call and one hashed call)
    integer*8 n58
    character*13 w(19)
    character*77 c77
    character*13 call_1,call_2
    character*11 c11
    character*6 bcall_1,bcall_2
    character*38 c*/
    //QString c={" 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/"};

    //int iflip=0;
    i3=-1;
    if (nwords==2 || nwords==3)
    {
        int iflip=0;
        bool ok1,ok2;
        QString bcall_1,bcall_2;                	
        QString call_1=w[0];                      
        int c1_c = call_1.indexOf(" ");
        if (call_1.mid(0,1)=="<") call_1=w[0].mid(1,c1_c-2)+"            ";//(2:len(trim(w(1)))-1)
        QString call_2=w[1];       
        int c2_c = call_2.indexOf(" ");
        if (call_2.mid(0,1)=="<") call_2=w[1].mid(1,c2_c-2)+"            ";//(2:len(trim(w(2)))-1)
        chkcall(call_1,bcall_1,ok1); 
        chkcall(call_2,bcall_2,ok2); //qDebug()<<call_1<<bcall_1<<ok1<<call_2<<bcall_2<<ok2<<i3;
        
        //2.59 rc5
        QString c001 = call_1.trimmed();
        QString bc001 = bcall_1.trimmed();
        QString c002 = call_2.trimmed();
        QString bc002 = bcall_2.trimmed();
        if(c001==bc001 && c002==bc002 && ok1 && ok2) return; //qDebug()<<call_1<<bcall_1<<ok1<<call_2<<bcall_2<<ok2;
        //2.59 end rc5        	
        	
        int icq=0;
        if (w[0].trimmed()=="CQ" || (ok1 && ok2)) //if(trim(w(1)).eq.'CQ' .or. (ok1.and.ok2))  //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        {
            if (w[0].trimmed()=="CQ" && w[1].indexOf(" ")<=4) return;//go to 900;//if(trim(w(1))=='CQ' && len(trim(w(2)))<=4) go to 900
            i3=4;
            n3=0;
            if (w[0].trimmed()=="CQ") icq=1;//if(trim(w(1))=="CQ") icq=1;
        }
        //qDebug()<<call_1<<call_2;
        int n10 = 0;
        int n12 = 0;
        int n22 = 0;
        QString c11;
        if (icq==1)
        {
            iflip=0;
            n12=0;
            //c11=adjustr(call_2.mid(0,11));  //adjustr(call_2(1:11))
            c11 = call_2.mid(0,11).rightJustified(11,' ');
            save_hash_call(w[1],n10,n12,n22);
        }
        else if (w[0].mid(0,1)=="<")
        {
            iflip=0;
            i3=4;
            save_hash_call(w[0],n10,n12,n22);
            //c11=adjustr(call_2.mid(0,11));
            c11 = call_2.mid(0,11).rightJustified(11,' ');
        }
        else if (w[1].mid(0,1)=="<")
        {
            iflip=1;
            i3=4;
            save_hash_call(w[1],n10,n12,n22);
            //c11=adjustr(call_1.mid(0,11));
            c11 = call_1.mid(0,11).rightJustified(11,' ');
        }
        long long int n58=0;//unsigned
        for (int i = 0; i<11; ++i)
        {//do i=1,11
            int nn = i < c11.length() ? c_77_04.indexOf(c11[i]) : -1;//index(c,c11(i:i)) - 1
            if (nn<0) nn = 0;  //HV Clean up any illegal chars
            n58=n58*38 + nn;
        }
        //qDebug()<<"pp0000"<<n58;
        int nrpt=0;
        //if (w[2].mid(0,3)=="RRR") nrpt=1;
        if (w[2].trimmed()=="RRR") nrpt=1;
        //if (w[2].mid(0,4)=="RR73") nrpt=2;
        if (w[2].trimmed()=="RR73") nrpt=2;
        //if (w[2].mid(0,2)=="73") nrpt=3;
        if (w[2].trimmed()=="73") nrpt=3;
        if (icq==1)
        {
            iflip=0;
            nrpt=0;
        }
        //qDebug()<<"TX n12"<<n12<<w[0]<<ihash12[0]<<ihash12[1]<<ihash12[2]<<ihash12[3];
        /*write(c77,1010) n12,n58,iflip,nrpt,icq,i3
        1010 format(b12.12,b58.58,b1,b2.2,b1,b3.3)*/
        //  12 58 1 2 1 3
        int co_t = 0;
        SetArrayBits(n12,12,c77,co_t);

        //SetArrayBits(n58,58,c77,co_t);
        int izz = 58-1;
        for (int i = 0; i < 58; ++i)
        {
            c77[co_t]=(1 & (n58 >> -(i-izz)));
            co_t++;
        }
        SetArrayBits(iflip,1,c77,co_t);
        SetArrayBits(nrpt,2,c77,co_t);
        SetArrayBits(icq,1,c77,co_t);
        SetArrayBits(i3,3,c77,co_t);
        //int n120 = BinToInt32(c77,0,12);
        //qDebug()<<"n120"<<n120;
        //qDebug()<<n12<<n58<<iflip<<nrpt<<icq<<i3<<co_t;
        /*for (int i = 0; i<77; ++i)
        {//do i=1,77
           if(c77(i:i).eq.'*') c77(i:i)='0'     //!### Clean up any illegal chars ###
        }*/
    }
}
/* 2.39 old WWROF
void PackUnpackMsg77::pack77_5(int nwords,QString *w,int &i3,int &n3,bool *c77)
{
    //CQ TEST K1ABC FN42
    //K1ABC W9XYZ -16 EN
    //W9XYZ K1ABC R-07 FN
    //K1ABC W9XYZ RR73
    //K1ABC G3AAA -11 IO
    //TU; G3AAA K1ABC R-09 FN
    //TU; G3AAA K1ABC +09 FN
    //K1ABC G3AAA RR73
    //! Check Type 5 (WWROF contest exchange)
    if (nwords==4 || nwords==5 || nwords==6)
    {
        int n28a = 0;
        int n28b = 0;
        int ir=0;
        bool ok1,ok2;
        QString bcall_1,bcall_2;
        int i1=0;
        if (w[0].trimmed()=="TU;") i1=1;   //if(trim(w(1)).eq.'TU;') i1=2
        chkcall(w[i1],bcall_1,ok1);
        chkcall(w[i1+1],bcall_2,ok2);
        if (!ok1 || !ok2) return; //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
        QString crpt=w[nwords-2].mid(0,4);
        //if(index(crpt,'-').lt.1 .and. index(crpt,'+').lt.1) go to 900
        if (crpt.indexOf("-")<0 && crpt.indexOf("+")<0) return;

        QString c1=crpt.mid(0,1);
        QString c2=crpt.mid(0,2);
        int irpt=-1;
        if (c1=="+" || c1=="-")
        {
            ir=0;
            irpt = crpt.toInt()+35;    //read(w(nwords-1),*,err=900) irpt  irpt=irpt+35
        }
        else if (c2=="R+" || c2=="R-")
        {
            ir=1;
            irpt = crpt.mid(1,3).toInt()+35;//read(w(nwords-1)(2:),*) irpt   irpt=irpt+35
        }
        QString loc = w[nwords-1];
        loc = loc.trimmed();
        if (irpt==-1 || loc.count()!=2) return; //if(irpt.eq.-1 .or. len(trim(w(nwords))).ne.2) go to 900
        //c2=w[nwords](1:2)
        int n1=((int)loc.at(0).toLatin1() - (int)('A'));//n1=ichar(c2(1:1)) - ichar('A')
        int n2=((int)loc.at(1).toLatin1() - (int)('A'));//n2=ichar(c2(2:2)) - ichar('A')
        if (n1<0 || n1>17) return;
        if (n2<0 || n2>17) return;
        int nexch=18*n1 + n2;
        i3=5;
        n3=0;
        int itu=0;
        if (w[0].trimmed()=="TU;") itu=1;
        pack28(w[0+itu],n28a);
        pack28(w[1+itu],n28b);
        //! 5    TU; W9XYZ K1ABC R-09 FN             1 28 28 1 7 9       74   WWROF contest
        //write(c77,1010) itu,n28a,n28b,ir,irpt,nexch,i3
        //1010 format(b1,2b28.28,b1,b7.7,b9.9,b3.3)
        int co_t = 0;
        SetArrayBits(itu,1,c77,co_t);
        SetArrayBits(n28a,28,c77,co_t);
        SetArrayBits(n28b,28,c77,co_t);
        SetArrayBits(ir,1,c77,co_t);
        SetArrayBits(irpt,7,c77,co_t);
        SetArrayBits(nexch,9,c77,co_t);
        SetArrayBits(i3,3,c77,co_t);
        //qDebug()<<itu<<n28a<<n28b<<ir<<irpt<<nexch<<i3<<co_t;
    }
}
*/
void PackUnpackMsg77::pack77_5(int nwords,QString *w,int &i3,int &n3,bool *c77)
{
    //! Pack a Type 0.2 message: EU VHF Contest mode
    //! Example message:  PA3XYZ/P R 590003 IO91NP           28 1 1 3 12 25
    //!                 <PA3XYZ> <G4ABC/P> R 590003 IO91NP   h10 h20 r1 s3 s12 g25

    //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (nwords<4 || nwords>5) return;       //!nwords must be 4 or 5
    if (w[0].mid(0,1)!="<" || w[1].mid(0,1)!="<") return; //if(w(1)(1:1).ne.'<' .or. w(2)(1:1).ne.'<') return !Both calls must be hashed
    int nx=-1;
    nx=w[nwords-2].toInt(); //read(w(nwords-1),*,err=2) nx
    if (nx<520001 || nx>594095) return;//2 if(nx.lt.520001 .or. nx.gt.594095) return   !Exchange between 520001 - 594095
    if (!is_grid6(w[nwords-1].mid(0,6))) return;//if(.not.is_grid6(w(nwords)(1:6))) return    !Last word must be a valid grid6
    //! Type 0.2: <PA3XYZ> <G4ABC/P> R 590003 IO91NP     h10 h20 r1 s3 s12 g25
    i3=5;
    n3=0;

    int n10 = 0;
    int n10a = 0;
    int n12 = 0;
    int n12a = 0;
    int n22 = 0;
    save_hash_call(w[0],n10,n12,n22);//call save_hash_call(w(1),n10,n12,n22)
    //i2=index(w(1),'>')
    //c13=w(1)(2:i2-1)
    int i2 = w[0].indexOf(">");
    QString c13 = w[0].mid(1,i2-1)+"             ";
    n12=ihashcall(c13,12);  //qDebug()<<c13<<w[0];

    save_hash_call(w[1],n10a,n12a,n22); //call save_hash_call(w(2),n10a,n12a,n22)
    //i2=index(w(2),'>')
    //c13=w(2)(2:i2-1)
    i2 = w[1].indexOf(">");
    c13 = w[1].mid(1,i2-1)+"             ";
    n22=ihashcall(c13,22); //qDebug()<<c13<<w[1];

    int ir=0;//ir=0
    if (w[2].mid(0,2)=="R ") ir=1; //if(w(3)(1:2).eq.'R ') ir=1
    int irpt=nx/10000 - 52;//irpt=nx/10000 - 52
    int iserial=(nx % 10000);//iserial=mod(nx,10000)
    if (iserial>2047) iserial=2047;// ????  4095
    QString grid6=w[nwords-1].mid(0,6);//grid6=w(nwords)(1:6)
    int j1=((int)grid6.at(0).toLatin1()-(int)('A'))*18*10*10*24*24;
    int j2=((int)grid6.at(1).toLatin1()-(int)('A'))*10*10*24*24;
    int j3=((int)grid6.at(2).toLatin1()-(int)('0'))*10*24*24;
    int j4=((int)grid6.at(3).toLatin1()-(int)('0'))*24*24;
    int j5=((int)grid6.at(4).toLatin1()-(int)('A'))*24;
    int j6=((int)grid6.at(5).toLatin1()-(int)('A'));
    int igrid6=j1+j2+j3+j4+j5+j6;
    //write(c77,1010) n12,n22,ir,irpt,iserial,igrid6,i3
    //1010 format(b12.12,b22.22,b1,b3.3,b11.11,b25.25,b3.3)
    int co_t = 0;
    SetArrayBits(n12,12,c77,co_t);
    SetArrayBits(n22,22,c77,co_t);
    SetArrayBits(ir,1,c77,co_t);
    SetArrayBits(irpt,3,c77,co_t);
    SetArrayBits(iserial,11,c77,co_t);
    SetArrayBits(igrid6,25,c77,co_t);
    SetArrayBits(i3,3,c77,co_t);
    //qDebug()<<n12<<n22<<ir<<irpt<<iserial<<igrid6<<n3<<i3<<grid6<<c13<<co_t;
}
void PackUnpackMsg77::int_to_8bit(int in,unsigned char *creg)
{
    //int itmp;
    for (int i = 0; i<4; ++i)
    {
        int itmp = 0;
        for (int j = 0; j<8; ++j)
            itmp=itmp+itmp+(1 & ((in >> i*8)>> -(j-7)));
        //itmp=itmp+itmp+(1 & ( in >> i*4 -(j-3)));
        creg[i]=itmp;
    }
}
void PackUnpackMsg77::mp_short_add(unsigned char *w,unsigned char *u,int beg_u,int n,int iv)
{
    /*ireg=256*iv
    do j=n,1,-1
       ireg=ichar(u(j))+ichar(creg(ii2))
       w(j+1)=creg(ii1)
    enddo
    w(1)=creg(ii2)*/
    //HV no need this 0 and 1 always is-> ii1=0 ii2=1;
    int ireg=256*iv;
    unsigned char creg[4];
    int_to_8bit(ireg,creg);

    for (int j = n-1; j >= 0; --j)
    {//do j=n,1,-1
        ireg=u[j+beg_u]+creg[1];
        int_to_8bit(ireg,creg);
        w[j+1]=creg[0];
    }
    w[0]=creg[1];
}
void PackUnpackMsg77::mp_short_mult(unsigned char *w,unsigned char *u,int beg_u,int n,int iv)
{
    /*ireg=0;
    do j=n,1,-1
       ireg=ichar(u(j))*iv+ichar(creg(ii2))
       w(j+1)=creg(ii1)
    enddo
    w(1)=creg(ii2)*/
    //HV no need this 0 and 1 always is-> ii1=0 ii2=1;
    int ireg=0;
    unsigned char creg[4];
    int_to_8bit(ireg,creg);
    for (int j = n-1; j >= 0; --j)
    {//do j=n,1,-1
        ireg=u[j+beg_u]*iv+creg[1];
        int_to_8bit(ireg,creg);
        w[j+1]=creg[0];
    }
    w[0]=creg[1];
}
void PackUnpackMsg77::packtext77(QString c13,int &i3,int &n3,bool *c71)
{
    ////////////// LZ2HV Binary Math Method ////////////
    /*i3=0;
    n3=0;
    QString w=adjustr(c13);
    for (int i = 0; i < 13; ++i)
    {
        int j=c_77_txt.indexOf(w[i]);//index(c,w(i:i))-1
        if (j<0) j=0;
        bool j_arr[BITS_71];
        Int32ToBin(j,j_arr,BITS_71);
        bool c_c_arr[BITS_71];
        Int32ToBin(c_77_txt.count(),c_c_arr,BITS_71);
        Multiplay71BitBinByMax6BitBinArray(c71,c_c_arr);
        SumBinArray(c71,j_arr,BITS_71);
    }
    int co_t = 71;
    SetArrayBits(n3,3,c71,co_t);
    SetArrayBits(i3,3,c71,co_t);*/
    ////////////// LZ2HV Binary Math End////////////

    /*character*13 c13,w
    character*71 c71
    character*42 c
    character*1 qa(10),qb(10)
    data c/' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?'/

    call mp_short_init
    qa=char(0)
    w=adjustr(c13)
    do i=1,13
       j=index(c,w(i:i))-1
       if(j.lt.0) j=0
       call mp_short_mult(qb,qa(2:10),9,42)     !qb(1:9)=42*qa(2:9)
       call mp_short_add(qa,qb(2:10),9,j)      !qa(1:9)=qb(2:9)+j
    enddo

    write(c71,1010) qa(2:10)
    1010 format(b7.7,8b8.8)*/

    unsigned char qa[10],qb[10];
    for (int i = 0; i < 10; ++i)
    {
        qa[i]=0;
        qb[i]=0;
    }
    /*init_creg(); //HV no need this init, always is 0 and 1 -> ii1=0 ii2=1;
    int ii1 = 0;
    int ii2 = 0;
    void init_creg()
    {
    unsigned char creg[4];
    int ireg=256*(int)'2'+(int)'1';
    int itmp;
    for (int i = 0; i<4; ++i)
    {
    	itmp = 0;
    	for (int j = 0; j<8; j++)
            itmp=itmp+itmp+(1 & ((ireg >> i*8)>> -(j-7)));
            //itmp=itmp+itmp+(1 & ( ireg >> i*4 -(j-3)));
        creg[i]=itmp; 
        if (creg[i]==(int)'1') ii1=i;
        if (creg[i]==(int)'2') ii2=i;
    }    
    }*/
    i3=0;
    n3=0;
    //QString w=adjustr(c13);
    QString w = c13.rightJustified(13,' ');
    for (int i = 0; i < 13; ++i)
    {//do i=1,13
        int j=c_77_txt.indexOf(w[i]);
        if (j<0) j=0;
        mp_short_mult(qb,qa,1,9,42);
        mp_short_add(qa,qb,1,9,j);
    }
    int co_t = 0;
    SetArrayBits(qa[1],7,c71,co_t);
    SetArrayBits(qa[2],8,c71,co_t);
    SetArrayBits(qa[3],8,c71,co_t);
    SetArrayBits(qa[4],8,c71,co_t);
    SetArrayBits(qa[5],8,c71,co_t);
    SetArrayBits(qa[6],8,c71,co_t);
    SetArrayBits(qa[7],8,c71,co_t);
    SetArrayBits(qa[8],8,c71,co_t);
    SetArrayBits(qa[9],8,c71,co_t);
    SetArrayBits(n3,3,c71,co_t);
    SetArrayBits(i3,3,c71,co_t);
}
void PackUnpackMsg77::pack77(QString msg,int &i3,int n3,bool *c77)
{
    int nwords = 0;
    QString w[19+5];
    int i0;
    QString c18;
    int ntel[6];

    //msg = "SP9HWY SP/L2HV  ";
    //msg = "ZZ9ZZZ RR73; ZZ9ZZZ <LZ2HV> -04   ";
    //msg = "PA3XYZ/P R 540103 IO91NP       ";
    //msg = "WA9XYZ KA1ABC 5A DX      ";
    //msg = "WA9XYZ KA1ABC R 32A EMA      ";

    //msg = "00000000001234789a bc def012      ";  i3=0; n3=5;

    //msg = "WA9XYZ/R KA1ABC/R R FN42             "; //"WA9XYZ/R KA1ABC/P P FN42             ";<----problem
    //msg = "PA3XYZ/P GM4ABC/P R JO22   ";
    //msg = "sp9hwy G4AB R-04   ";
    //msg = "CQ test sp9hwy JO91   ";

    //msg = "TU; W9XYZ K1ABC R 579 AL    ";//
    //msg = "W9XYZ G8ABC R 559 0000    ";
    //msg = "<WA9XSS> PA4/K1ABK RR73     ";//problem heshing
    //msg = "CQ PJ4/K1SSS     ";
    //msg = "W9XYZ <PJ4/K1SSS> -11  ";
    //msg = "W9XYZ LZ2HV -11  ";
    //msg = "? ?g? ??/?? ???? ?????????????????   ";

    //msg = "PA4/K1ABK <WA9XYZ> RR73  ";
    //msg = "<WA9XYZ> PA4/K1ABK RR73  ";
    //msg = "LZ/SP9HWY LZ2HV KN23";

    //QString bcall_1;
    //bool ok1;
    //chkcall("GK1ABC/P  ",bcall_1,ok1); qDebug()<<"bbbbb"<<bcall_1<<ok1;
    //int n28 = 0;
    //pack28("ZZ9ZZZ         ",n28);
    //pack28("ZZ9ZZZ         ",n28);
    //qDebug()<<"3 n28"<<msg;

    //Old WWROF FT8/FT4 contest
    //msg = "K1ABC W9XYZ -16 EN   ";
    //msg = "W9XYZ K1ABC R-07 FN   ";
    //msg = "K1ABC G3AAA -11 IO   ";
    //msg = "TU; G3AAA K1ABC R-09 FN   ";
    //msg = "TU; G3AAA K1ABC +09 FN   ";
    //msg = "TU; G3AAA K1ABC +09 FN   ";

    msg.append(' ');//for any case for label->c5
    for (int i = 0; i<19; ++i)
        w[i]="             ";//13 blinks char

    //i3_hint=i3 //2.47 no need for the moment
    //n3_hint=n3 //2.47 no need for the moment
    //msg=msg0 ???
    if (i3==0 && n3==5) goto c5;//if(i3_hint.eq.0 .and. n3_hint.eq.5) go to 5
    //! Convert msg to upper case; collapse multiple blanks; parse into words.
    split77(msg,nwords,/*nw,*/w);
    i3=-1;
    n3=-1;
    if (msg.mid(0,3)=="CQ " || msg.mid(0,3)=="DE " || msg.mid(0,4)=="QRZ ") goto c100;
    //! Check 0.1 (DXpedition mode)
    pack77_01(nwords,w,i3,n3,c77);  //c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    if (i3>=0 || n3>=1) return;//0,1
    //old ! Check 0.2 (EU VHF contest exchange)
    //pack77_02(nwords,w,i3,n3,c77);
    //if (i3>=0) return;//0,2
    //! Check 0.3 and 0.4 (ARRL Field Day exchange)
    pack77_03(nwords,w,i3,n3,c77);  
    if (i3>=0) return;//0,3
    //qDebug()<<nwords;
    if (nwords>=2) goto c100; // if(nwords.ge.2) go to 100

    //! Check 0.5 (telemetry)
c5:
    i0=msg.indexOf(" ");//i0=index(msg,' ')
    c18=msg.mid(0,i0);//c18=msg(1:i0-1)//'
    c18 = c18.rightJustified(18,'0'); //qDebug()<<c18;
    //c18=adjustr(c18);
    //for (int i = 0; i<3; ++i)
    //ntel[i]=-99;//ntel=-99
    //read(c18,1005,err=6) ntel
    //1005 format(3z6)
    bool ok;
    ntel[0]=c18.midRef(0,6).toInt(&ok,16);
    if (!ok) ntel[0]= -1;
    ntel[1]=c18.midRef(6,6).toInt(&ok,16);
    if (!ok) ntel[1]= -1;
    ntel[2]=c18.midRef(12,6).toInt(&ok,16);
    if (!ok) ntel[2]= -1;
    if (ntel[0]>=pow(2,23)) goto c800;
    //6 if(ntel(1).ge.0 .and. ntel(2).ge.0 .and. ntel(3).ge.0) then
    //qDebug()<<ntel[0]<<ntel[1]<<ntel[2];
    if (ntel[0]>=0 && ntel[1]>=0 && ntel[2]>=0)
    {
        i3=0;
        n3=5;
        //write(c77,1006) ntel,n3,i3
        //1006 format(b23.23,2b24.24,2b3.3)
        //23 24 24 3 3   77
        int co_t = 0;
        SetArrayBits(ntel[0],23,c77,co_t);
        SetArrayBits(ntel[1],24,c77,co_t);
        SetArrayBits(ntel[2],24,c77,co_t);
        SetArrayBits(n3,3,c77,co_t);
        SetArrayBits(i3,3,c77,co_t);
        //qDebug()<<ntel[0]<<ntel[1]<<ntel[2]<<n3<<i3<<co_t;
        return;
    }
    //! Check Type 1 (Standard 77-bit message) or Type 2, with optional "/P"
c100:

    //WSPR Type 1,2,3
    //call pack77_06(nwords,w,i3,n3,c77)
    //if(i3>=0) go to 900

    //! Check Type 1 (Standard 77-bit message) or Type 2, with optional "/P"
    pack77_1(nwords,w,i3,n3,c77);
    if (i3>=0) return;//1,6 & 2,6

    //! Check Type 3 (ARRL RTTY contest exchange)
    pack77_3(nwords,w,i3,n3,c77);
    if (i3>=0) return;//3,5

    //! Check Type 4 (One nonstandard call and one hashed call)
    //qDebug()<<"3 n28"<<w[0]<<w[1]<<w[2]<<w[3]<<w[4]<<nwords;
    pack77_4(nwords,w,i3,n3,c77);
    if (i3>=0) return;//4,

    //old 2.39 ! Check Type 5 (WWROF contest exchange)
    /*pack77_5(nwords,w,i3,n3,c77);
    if (i3>=0) return;*/
    //2.39 new ! Check Type 5 (EU VHF Contest with 2 hashed calls, report, serial, and grid6)
    pack77_5(nwords,w,i3,n3,c77);
    if (i3>=0) return;

    //! It defaults to free text*/
c800:
    //i3=0;
    //n3=0;
    //qDebug()<<"Pack  ="<<msg;
    msg = msg.mid(0,13);//+"                             "; //msg(14:)='                        '
    packtext77(msg,i3,n3,c77);
    //write(c77(72:77),'(2b3.3)') n3,i3
    // 72 3 3

}
