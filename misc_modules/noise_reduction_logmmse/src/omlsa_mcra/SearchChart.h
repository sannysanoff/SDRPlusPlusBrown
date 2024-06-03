#pragma once
#define _CRT_SECURE_NO_WARNINGS
#include "head.h"
#include<iostream>
#include "G_calculate.h"
#include <utils/flog.h>
using namespace std;

template<class T>
T* G_calculate::file_read(const char* Filename) {
	FILE* ff;
	fopen_s(&ff,Filename, "rb");
	if (NULL == ff) {
		printf("open out file err4! \n");
        return nullptr;
	}
	fseek(ff, 0, 2);
	int DataLength = ftell(ff);
	fseek(ff, 0, 0);
	T* T_int_value = new T[DataLength / sizeof(T)];
	int filecount=fread(T_int_value, sizeof(T), DataLength / sizeof(T), ff);
	fclose(ff);
	return T_int_value;
}

int G_calculate::expintpow_solution(int v_subscript) {
	int vec = 0;
	int g = 0;

	vec = ((__int64)v_subscript * 100 ) >>24;  // / 0.0001;
	vec = vec < 1 ? 1 : vec;
	 
	//g = (m_int_value[vec - 1]) ;
	g= (m_int_value1[vec - 1]);
	return g;
}

int G_calculate::subexp_solution(int v_subscript) {

	int vec = 0;
	int g = 0;

	vec = ((__int64)v_subscript * 100) >> 24;  // / 0.0001;
	vec = vec < 1 ? 1 : vec;
	//g = (m_expsub_value[vec - 1]);
	g = (m_expsub_value1[vec - 1]);
	return g;
}

int G_calculate::Gvalue_solution(int Gh1_subscript,int pp_subscript) {
	int veci = 0,vecj=0;    // j:m_pp  i:m_Gh1
	int g = 0;				
	veci = min(Gh1_subscript * 100 >> 14,6999);
	vecj = max((pp_subscript * 100 >> 14)-1,0);

	//g= dp1[veci][vecj];
	//cout << sizeof(m_G_value) << sizeof(m_G_value) / sizeof(m_G_value[0]);
    int index = vecj* 7000 + veci;
    if (index >= 700000 || index < 0) {
        flog::info("ERROR: index >= 700000 || index < 0, aborting");
        ::abort();
    }
//    static bool bits[700000] = {false};
//    if (!bits[index]) {
//        flog::info("OMLSA: New bit: {}", index);
//        bits[index] = true;
//    }
	g = m_G_value[index];
	return g;
}
