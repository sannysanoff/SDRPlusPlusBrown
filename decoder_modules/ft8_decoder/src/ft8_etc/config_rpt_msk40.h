#ifndef __CONFIG_RPT_MSK40_H__
#define __CONFIG_RPT_MSK40_H__

static const char s8ms[8] = {0,1,0,0,1,1,1,0}; //ok 4  78 invers msk40   14s
static const char s8[8]   = {0,1,1,1,0,0,1,0};
static const char s8r[8]  = {1,0,1,1,0,0,0,1}; 

static const char *rpt_msk40[16] = {"-03 ","+00 ","+03 ","+06 ","+10 ","+13 ","+16 ",
           "R-03","R+00","R+03","R+06","R+10","R+13","R+16",
           "RRR ","73  "};            
           
#endif 
