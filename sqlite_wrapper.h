/*
 *  Copyright (c) 2010-2012, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

/*
 *  sqlite_log.h
 *	header file for sqlite_log.c 
 *	History :
 *	Date	(y.m.d)		Author			Version			Description
 *	2011-01-06		eagle zhou		0.1				Created
 */


#ifndef SQLITE_LOG_H
#define SQLITE_LOG_H

#define VER_NAME_SIZE			10
#define CODEC_NAME_SIZE		10
#define PLATFORM_NAME_SIZE	10
#define MAX_FILE_NAME			256

#define MAX_COLUMN_STR			64

typedef enum
{
	SQL_INT=0,
	SQL_DOUBLE	,
	SQL_STRING,
}SQLiteItemType;

typedef struct
{
	char name[MAX_FILE_NAME];	//yuv file name
	int    nFrmWidth;
	int    nFrmHeight;
	int 	nFrmCnt;				//valid frame numbers
	
	double avgCompressedRate;	//average compressed rate

	double avgTime;				//average encode time
	double frmMinTime;
	double frmMaxTime;

	double avgKBPS;				//average kbps/frame
	double minKBPS;
	double maxKBPS;

	double frmAvgPSNR[3];  		//average PSNR/frame	
	double frmMinPSNR[3];
	double frmMaxPSNR[3];	

	double frmAvgSSIM[3];  		//average SSIM/frame	
	double frmMinSSIM[3];
	double frmMaxSSIM[3];

	char codec[CODEC_NAME_SIZE]; //h.264;mpeg4;h.263;...
	char platform[PLATFORM_NAME_SIZE]; //iMX51;iMX61;...
	char vpulib[VER_NAME_SIZE];		//vpu lib version
	char vpufw[VER_NAME_SIZE];		//vpu firmware version

	/*general parameters*/
	int nQuantParam;	
	int nFrameRate;
	int nBitRate;
	int nGOPSize;	
	//int nRotation;
	//int nMirror;
	int nRcIntraQP;
	int nUserGamma;

	/*codec related parameters*/
	/*H.264*/
	int n16x16Only;
	int nIPredFlag;
	int nDisDeblk;
	int nDblkAlpha;
	int nDblkBeta;
	int nDblkChromQPOff;

	/*H.263*/
	/*MPEG4*/
}
SQLiteItem;


typedef struct
{
	char name[MAX_COLUMN_STR];	//column name
	char type[MAX_COLUMN_STR];	//type: such as INT;DOUBLE;varchar(n);...
	SQLiteItemType eType;
	int nLength;		//length:only for string type
	void* pVal;		//value 
}SQLiteColumn;


int SQLiteInsertNode(char* pDBName,char* pTableName,SQLiteColumn*pColumn,int nColumnNum);
int SQLiteNodeIsExist(char* pDBName,char* pTableName,SQLiteColumn*pColumn,int nColumnNum,int* pIsExist);

#endif  //SQLITE_LOG_H

