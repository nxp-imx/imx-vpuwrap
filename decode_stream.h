/*
 *  Copyright (c) 2010-2014, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

/*
 *	decode_stream.h
 *	header file contain all related struct info in decode_stream.c
 *	History :
 *	Date	(y.m.d)		Author			Version			Description
 *	2010-09-14		eagle zhou		0.1				Created
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

