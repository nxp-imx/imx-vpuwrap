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
 *	encode_stream.h
 *	header file contain all related struct info in encode_stream.c
 *	History :
 *	Date	(y.m.d)		Author			Version			Description
 *	2010-12-31		eagle zhou		0.1				Created
 */

#ifndef ENCODE_STREAM_H
#define ENCODE_STREAM_H

typedef int (EncOneFrameOK)(void* pvContxt,unsigned char* pFrmBits,int nFrmLen);
typedef int (EncOneFrame)(void* pvContxt);

typedef struct 
{
	// input setting
	FILE* fin;
	FILE* fout;
	int nMaxNum;

	int nCodec;
	int nPicWidth;
	int nPicHeight;	
	int nRotAngle;
	int nFrameRate;
	int nBitRate;		/*unit: kbps*/
	int nGOPSize;
	int nQuantParam;

	//advance options
	int nChromaInterleave;
	int nMirror;
	int nEnableAutoSkip;
	int nMapType;
	int nLinear2TiledEnable;
	int nColor;

	int nInitialDelay;
	int nVbvBufSize;
	int nSliceMode;
	int nSliceSizeMode;
	int nSliceSize;
	int nIntraRefresh;
	int nRcIntraQp;
	int nUserQPMin;
	int nUserQPMax;
	int nUserQPMinEnable;
	int nUserQPMaxEnable;
	int nUserGamma;
	int nRcIntervalMode;
	int nMBInterval;
	
	/*for H.264*/
	int nAvc_Intra16x16Only;
	int nAvc_constrainedIntraPredFlag;
	int nAvc_disableDeblk;
	int nAvc_deblkFilterOffsetAlpha;
	int nAvc_deblkFilterOffsetBeta;
	int nAvc_chromaQpOffset;

	int nAvc_audEnable;
	int nAvc_fmoEnable;
	int nAvc_fmoSliceNum;
	int nAvc_fmoType;

	/*for H.263*/
	int nH263_annexJEnable;
	int nH263_annexKEnable;
	int nH263_annexTEnable;
	
	/*for MPEG4*/
	int nMp4_dataPartitionEnable;
	int nMp4_reversibleVlcEnable;
	int nMp4_intraDcVlcThr;
	int nMp4_hecEnable;
	int nMp4_verid;

	// output info
	int nFrameNum;
	int nErr;

	// app context
	void* pApp;
	
	//callback functions
	EncOneFrameOK* pfOneFrameOk;
	EncOneFrame* pfOneFrameBeg;
	EncOneFrame* pfOneFrameEnd;

	// internal testing for repeat
	int nRepeatNum;		
}EncContxt;


int encode_stream(EncContxt * encContxt);
//int encode_reset();

#endif  //#ifndef ENCODE_STREAM_H

