#pragma once
#include"base4_fft.h"
#include<iostream>
#include"LSA_denoise.h"
#include"head.h"
// pInBuffer:���������ݴ棬�ռ���Ҫ�ӿ��ⲿ���٣���СΪһ�ζ�ȡ������ռ�ռ�����ֵ
// pOutBuffer:��������ݴ棬�ռ���Ҫ�ӿ��ⲿ���٣���СΪpInBuffer+һ֡�����ݣ���ȡһ֡���ݵ����ֵ4096
class Datablock_Read{
public:
	Datablock_Read(int sample_rate, short channels,int MaxDataLen);
	//������򽫻᷵��һֵ������ֵС��0���������쳣����Ҫ�����������ص���ѭ��
	short Data_procese(short* pInBuffer, short* pOutBuffer,int read_length, int& out_length);
	~Datablock_Read();

    short m_derr_code;  //������뷵��ֵ
	//����Χ��-1��-14 base4_fft   -15��-18 LSA_denoise
	//-19��-49 G_calculate  -50��-58 Datablock_Read
	int m_maxdata;
	short m_inc, m_wlen,m_inc_move;
	int m_blockInd;
	short m_channels,m_wlen15,m_inc2;
	int m_sample_rate,m_data_rest_length ;
	short* m_data_in;
	short* m_data_storage;
	int* m_process_storage;
	short* m_buffer;
	short* m_data_resize;
	short* m_DoubDataBuffer;
	short* m_data_out;
	LSA_denoise LSA;
	short Initial(int MaxDataLen);

};

