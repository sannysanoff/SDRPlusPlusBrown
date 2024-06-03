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

#pragma once

#include "mshv_support.h"
#include <math.h>       // fmod fabs 
//#include <stdlib.h>	//      abs
 
class PackUnpackMsg77 //: public QObject 
{

	QString c_77_04 = "X"; // static init in wasm did not work here.
	QString c_77_txt;
	QString a1_28;
	QString a2_28;
	QString a3_28;
	QString a4_28;

	//Q_OBJECT // hv
public: 
	void initPackUnpack77(bool f_dec_gen);
	void pack77(QString mes,int &i3,int n3,bool*c77);  
	QString unpack77(bool *c77,bool &unpk77_success); 
	void save_hash_call(QString c13,int &n10,int &n12,int &n22);
	void save_hash_call_my_his_r1_r2(QString call,int pos);
	void save_hash_call_mam(QStringList ls);
	QString c1_rx_calls;
	QString c2_rx_calls;
	void split77(QString &msg,int &nwords,/*int *nw,*/QString *w);

private: 
    bool sf_dec_gen;
    int BinToInt32(bool*a,int b_a,int bits_sz);
    void mp_short_div(unsigned char *w,unsigned char *u,int b_u,int n,int iv,int &ir);
    void int_to_8bit(int in,unsigned char *creg);
    void mp_short_add(unsigned char *w,unsigned char *u,int beg_u,int n,int iv);
    void mp_short_mult(unsigned char *w,unsigned char *u,int beg_u,int n,int iv);
    
    QString RemWSpacesInside(QString s);
    QString RemBegEndWSpaces(QString str);
    void chkcall(QString w,QString &bc,bool &cok);
    //QString adjustr(QString);

    int ihashcall(QString c0,int m);

    void pack28(QString c13,int &n28);
    void SetArrayBits(int in,int in_bits,bool *ar,int &co);
    //void split77(QString &msg,int &nwords,/*int *nw,*/QString *w);
    void pack77_01(int nwords,QString *w,int &i3,int &n3,bool *c77);
    bool is_grid4(QString);
    bool is_grid6(QString);
    //old EU Cont void pack77_02(int nwords,QString *w,int &i3,int &n3,bool *c77);
    void pack77_03(int nwords,QString *w,int &i3,int &n3,bool *c77);
    void pack77_1(int nwords,QString *w,int &i3,int &n3,bool *c77);
    void pack77_3(int nwords,QString *w,int &i3,int &n3,bool *c77);
    void pack77_4(int nwords,QString *w,int &i3,int &n3,bool *c77);
    void pack77_5(int nwords,QString *w,int &i3,int &n3,bool *c77);
    void packtext77(QString,int &i3,int &n3,bool *c77);
    
    //// unpack //////////////////////////////////////
    bool to_grid4(int n,QString &grid4);
    bool to_grid6(int n,QString &grid6);
    void unpacktext77(bool *c77,QString &c13);
    void hash10(int n10,QString &c13);
    void hash12(int n12,QString &c13);
    void hash22(int n22,QString &c13);
    void unpack28(int n28_0,QString &c13,bool &success);
	
};
