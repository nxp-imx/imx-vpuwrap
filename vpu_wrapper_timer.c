/*
 *  Copyright (c) 2010-2012, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

/* 
 *  vpu_wrapper_timer.c
 *	this file is used for get the system time
 *	History :
 *	Date	(y.m.d)		Author			Version			Description
 *	2010-12-10		eagle zhou		0.1				Created
 */


#ifdef USE_VPU_WRAPPER_TIMER
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#define MAX_TIMER_ID		10
#define ID_IS_VALID(id)		((id>=0)&&(id<MAX_TIMER_ID))
#define MAX_TIMER_CNT		10000
#define CNT_IS_VALID(cnt)	((cnt>=0)&&(cnt<MAX_TIMER_CNT))
#define MAX_FILE_NAME		255

#define LOG			printf
#define OUTPUTLOG	fprintf

#ifdef __WINCE
#include <windows.h>
#else
#include <sys/time.h>
#endif


//used by timer_mark
unsigned int interval_timer[MAX_TIMER_ID][MAX_TIMER_CNT];
unsigned int interval_cnt[MAX_TIMER_ID];
unsigned int tm_mark[MAX_TIMER_ID];		//last mark time

//used by timer_start/timer_stop
unsigned int every_timer[MAX_TIMER_ID][MAX_TIMER_CNT];
unsigned int total_timer[MAX_TIMER_ID];			
unsigned int every_cnt[MAX_TIMER_ID];
struct timeval tm_beg[MAX_TIMER_ID];
struct timeval tm_end[MAX_TIMER_ID];



// init all timers
void timer_init()
{
	LOG("init timer: MAX ID: %d, MAX CNT: %d \r\n",MAX_TIMER_ID,MAX_TIMER_CNT);
	LOG("sizeof(interval_timer): %d \r\n",sizeof(interval_timer));
	LOG("sizeof(interval_cnt): %d \r\n",sizeof(interval_cnt));
	LOG("sizeof(tm_mark): %d \r\n",sizeof(tm_mark));
	LOG("sizeof(every_timer): %d \r\n",sizeof(every_timer));
	LOG("sizeof(total_timer): %d \r\n",sizeof(total_timer));
	LOG("sizeof(every_cnt): %d \r\n",sizeof(every_cnt));

	memset(interval_timer,0,sizeof(interval_timer));
	memset(interval_cnt,0,sizeof(interval_cnt));
	memset(tm_mark,0,sizeof(tm_mark));
	
	memset(every_timer,0,sizeof(every_timer));
	memset(total_timer,0,sizeof(total_timer));
	memset(every_cnt,0,sizeof(every_cnt));
	
}


#ifdef __WINCE         // for taking timing on wince platform

void timer_mark(int id)
{
	//
}

void timer_start(int id)
{
	//
}

void timer_stop(int id)
{
	//
}

#else	// for linux system

void timer_mark(int id)
{
	if(ID_IS_VALID(id)&&CNT_IS_VALID(interval_cnt[id]))
	{
		unsigned int tm_1, tm_2;
		struct timeval tm_cur;	

		gettimeofday(&tm_cur, 0);
		tm_2 = tm_cur.tv_sec * 1000000 + tm_cur.tv_usec;
		if(0==interval_cnt[id])
		{
			tm_1=tm_2;
		}
		else
		{
			tm_1 = tm_mark[id];
		}
		interval_timer[id][interval_cnt[id]]=(tm_2-tm_1);
		tm_mark[id]=tm_2;
		LOG("id: %d, interval: %d (0x%X), mark timer: %d (0x%X) \r\n",id,interval_timer[id][interval_cnt[id]],interval_timer[id][interval_cnt[id]],tm_2,tm_2);
		interval_cnt[id]++;
	}
}

void timer_start(int id)
{
	if(ID_IS_VALID(id))
	{
		gettimeofday(&tm_beg[id], 0);
	}
}

void timer_stop(int id)
{
	if(ID_IS_VALID(id)&& CNT_IS_VALID(every_cnt[id]))
	{
		unsigned int tm_1, tm_2;
		gettimeofday(&tm_end[id], 0);

		tm_1 = tm_beg[id].tv_sec * 1000000 + tm_beg[id].tv_usec;
		tm_2 = tm_end[id].tv_sec * 1000000 + tm_end[id].tv_usec;
		total_timer[id] = total_timer[id] + (tm_2-tm_1);
		every_timer[id][every_cnt[id]]=(tm_2-tm_1);
		every_cnt[id]++;
	}
}

#endif	//#ifdef __WINCE

void timer_mark_report(int id)
{
	if(ID_IS_VALID(id))
	{
		FILE* fp=NULL;
		char out_str[MAX_FILE_NAME];
		int cnt;
		
		sprintf(out_str,"%s_%d.log","timer_mark",id);
		fp=fopen(out_str,"wb");
		if(fp)
		{
			OUTPUTLOG(fp,"timer mark %d: \r\n",id);
			OUTPUTLOG(fp,"============ \r\n");
			for(cnt=0;cnt<interval_cnt[id];cnt++)
			{
				OUTPUTLOG(fp,"%d\r\n",interval_timer[id][cnt]);				
			}
		}
		else
		{
			LOG("open file %s failed \r\n",out_str);
		}
	}
}

void timer_report(int id)
{
	if(ID_IS_VALID(id))
	{
		FILE* fp=NULL;
		char out_str[MAX_FILE_NAME];
		int cnt;
		
		sprintf(out_str,"%s_%d.log","timer",id);
		fp=fopen(out_str,"wb");
		if(fp)
		{
			OUTPUTLOG(fp,"timer %d: \r\n",id);
			OUTPUTLOG(fp,"============ \r\n");
			for(cnt=0;cnt<every_cnt[id];cnt++)
			{
				OUTPUTLOG(fp,"%d\r\n",every_timer[id][cnt]);				
			}
			OUTPUTLOG(fp,"============ \r\n");
			OUTPUTLOG(fp,"total time: %d\r\n",total_timer[id]);
		}
		else
		{
			LOG("open file %s failed \r\n",out_str);
		}
	}
}

#endif


