/* The algorithms, source code, look-and-feel of WSJT-X and related
 * programs, and protocol specifications for the modes FSK441, FT8, JT4,
 * JT6M, JT9, JT65, JTMS, QRA64, ISCAT, MSK144, are Copyright © 2001-2017
 * by one or more of the following authors: Joseph Taylor, K1JT; Bill
 * Somerville, G4WJS; Steven Franke, K9AN; Nico Palermo, IV3NWV; Greg Beam,
 * KI7MT; Michael Black, W9MDB; Edson Pereira, PY2SDR; Philip Karn, KA9Q;
 * and other members of the WSJT Development Group.
 *
 * MSHV Decoder/Generator
 * Rewritten into C++ and modified by Hrisimir Hristov, LZ2HV 2015-2022
 * May be used under the terms of the GNU General Public License (GPL)
 */

#include "mshv_support.h"
#include "genpom.h"

double gfsk_pulse_(double b,double t)
{
    double out = 0.0;
    double pi=4.*atan(1.0);
    double c=pi*sqrt(2.0/log(2.0));
    out=0.5*(erf(c*b*(t+0.5))-erf(c*b*(t-0.5)));
    return out;
}
void gen_pulse_gfsk_(double *pulse,double k,double bt,int nsps)
{
    for (int i= 0; i < 3*nsps; ++i)
    {
        //double tt=((double)i-k*(double)nsps)/(double)nsps;
        double tt=((double)i-k)/(double)nsps;//for ft2
        pulse[i]=gfsk_pulse_(bt,tt);
    }
}

#include "bpdecode_ft8_174_91.h"

#include "boost/crc.hpp"
#include "boost/boost_14.hpp"


void GenPomFt::initGenPomFt()
{
    first_ft_enc_174_91 = true;
}

short crc14_ft(unsigned char const * data, int length)
{
    return boost::augmented_crc<14, TRUNCATED_POLYNOMIAL14>(data, length);
}

short GenPomFt::crc14(unsigned char const * data, int length)
{
    return crc14_ft(data,length);
}
void GenPomFt::encode174_91(bool *message77,bool *codeword)
{
    const int N=174;
    const int K=91;
    const int M=N-K;
    unsigned char i1MsgBytes[15];
    bool pchecks[95];

    if (first_ft_enc_174_91)
    {
        for (int i = 0; i < 95; ++i)
        {
            for (int j = 0; j < 85; ++j)
                genft_174_91[i][j]=0;
        }
        for (int i = 0; i < 83; ++i)
        {
            for (int j = 0; j < 23; ++j)//23 bb a8 30 e2 3b 6b 6f 50 98 2e
            {
                bool ok;
				std::string temp;
                temp += (g_ft8_174_91[i][j]);
                int istr = toInt(temp, ok, 16); //read(g(i)(j:j),"(Z1)") istr
                for (int jj = 0; jj < 4; ++jj)
                {
                    int icol=(j)*4+jj;
                    if ( icol <= 90 ) genft_174_91[icol][i]=(1 & (istr >> (3-jj)));
                }
            }
        }
        first_ft_enc_174_91=false;
    }
    
    //! Add 14-bit CRC to form 91-bit message+CRC14
    //write(tmpchar,'(77i1)') message77
    //tmpchar(78:80)='000'
    //i1MsgBytes=0
    //read(tmpchar,'(10b8)') i1MsgBytes(1:10)
    // debugPrintf("          genpom:93");

    for (int i = 0; i < 15; ++i) i1MsgBytes[i]=0;
    int c_77 = 0;
    for (int i = 0; i < 10; ++i)
    {
        int k = 0;
        for (int j = 0; j < 8; ++j)
        {
            k <<= 1;
            k |= message77[c_77];//- 0
            c_77++;
        }
        i1MsgBytes[i] = k;
    }

    // debugPrintf("          crc14...");
    int ncrc14=crc14(i1MsgBytes,12);//int ncrc14 = crc14 (c_loc (i1MsgBytes), 12)
    // debugPrintf("          crc14.");

    //write(tmpchar(78:91),'(b14)') ncrc14
    //read(tmpchar,'(91i1)') message
    int izz = 14-1;
    int pos = 77;
    for (int i = 0; i < 14; ++i)
    {
        message77[pos]=(1 & (ncrc14 >> -(i-izz)));
        pos++;//=91
    }

    for (int i = 0; i < 83; ++i)
    {
        int nsum=0;
        for (int j = 0; j < 91; ++j)
        {
            nsum+=message77[j]*genft_174_91[j][i];//nsum=nsum+message(j)*gen(i,j);
        }
        pchecks[i]=fmod(nsum,2);
    }
    // debugPrintf("          mostly");
    // codeword(1:K)=message
    // codeword(K+1:N)=pchecks
    for (int i = 0; i < K; ++i)//91
        codeword[i]=message77[i];
    for (int i = 0; i < M; ++i)//174-91=83
        codeword[i+91]=pchecks[i];
}

