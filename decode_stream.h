/*
 *  Copyright (c) 2010-2014, Freescale Semiconductor Inc.,
 *  Copyright 2019-2020, 2026 NXP
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef DECODE_STREAM_H
#define DECODE_STREAM_H

typedef enum
{
	DEC_OUT_420,
	DEC_OUT_422H,
	DEC_OUT_422V,	
	DEC_OUT_444,	
	DEC_OUT_400,	
	DEC_OUT_UNKNOWN
}DecOutColorFmt;

typedef struct 
{
	// input setting
	FILE* fin;
	FILE* fout;
	FILE* fcodecdata;
	int isavcc;
	int nMaxNum;
	int nDisplay;	
	int nFbNo;
	int nCodec;
	int nInWidth;
	int nInHeight;
	int nSkipMode;
	int nDelayBufSize; /*<0: invalid*/

	// internal testing for repeat
	int nRepeatNum;
	int nOffset;
	int nUnitDataSize;
	int nUintDataNum;

	// output info
	int nWidth;
	int nHeight;
	int nFrameNum;
	int nErr;
	DecOutColorFmt eOutColorFmt;
	int nDecFps;
	int nTotalFps;

	//advance option
	int nChromaInterleave;
	int nMapType;
	int nTile2LinearEnable;
}DecContxt;


int decode_stream(DecContxt * decContxt);
int decode_reset();

#endif  //#ifndef DECODE_STREAM_H

