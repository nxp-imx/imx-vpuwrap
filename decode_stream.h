/*
 * Copyright (c) 2010-2012, Freescale Semiconductor, Inc. 
 *
 */

/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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

