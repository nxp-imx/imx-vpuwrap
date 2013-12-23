/*
 *  Copyright (c) 2010-2012, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */ 

/* 
 *  enc_auto_test.c
 *	vpu encoder auto test application
 *	Date	(y.m.d)		Author			Version			Description
 *	2011-01-04		eagle zhou		0.1				Created
 */


#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "math.h"
#include "decode_stream.h"
#include "encode_stream.h"
#include "sqlite_wrapper.h"

#ifdef __WINCE
#include "windows.h"
#else
#include "sys/time.h"
#endif

#define OUT_SHORT_FILE_NAME	//if file name is too long, some pc tool can not open it
#define REDUCE_REDUNDANT_LOOP	//avoid no meaning repeated loop
#define PRE_LOOKUP_DB			//improve performance: lookup db before encode/decode/compute_statistic, but not before inserting db


#ifdef APP_DEBUG
#define APP_DEBUG_PRINTF printf
#define APP_ERROR_PRINTF printf
#else
#define APP_DEBUG_PRINTF
#define APP_ERROR_PRINTF
#endif

//#define NULL  (void*)0

#define NAME_SIZE 512
#define MAX_FRM_NUMS	1000
//#define MAX_NODE_NUMS	60
#define DEFAULT_DEC_FILL_DATA_UNIT	(16*1024)
#define DEFAULT_DEC_FILL_DATA_NUM	(0x7FFFFFFF)

#define DEFAULT_PLATFORM_ID		0
#define DEFAULT_VPULIB_VER_ID		0
#define DEFAULT_VPUFW_VER_ID		0

#define CASE_FIRST(x)   if (strncmp(argv[0], x, strlen(x)) == 0)
#define CASE(x)         else if (strncmp(argv[0], x, strlen(x)) == 0)
#define DEFAULT         else

//#define MAX_PARAM_INDEX	64
#define MAX_LINE_BYTES	256
#define MAX_VAR_NAME	64
#define COMMENT_CHAR	';'
#define SET_PARAMNODE(node,valbeg,valend,valstep) \
	{\
		node.beg=valbeg;\
		node.end=valend;\
		node.step=valstep;\
	}

#define FOR_LOOP(var,node) for(var=node.beg;var<=node.end;var+=node.step)
#ifdef REDUCE_REDUNDANT_LOOP
#define FOR_LOOP_BEG(var,valbeg,node) for(var=valbeg;var<=node.end;var+=node.step)
#else
//ignore valbeg value
#define FOR_LOOP_BEG(var,valbeg,node) for(var=node.beg;var<=node.end;var+=node.step)
#endif

#define SET_SQLVAL(pItem,index,val) pItem[index].pVal=(void*)(&val)
#define SET_SQLPVAL(pItem,index,ptr) pItem[index].pVal=(void*)(ptr)

#define DB_MAX_DIG_SIZE	32
#define DBInsertStrValue(pCmd,pTmpStr,tail,len) \
	{ \
		strcat(pCmd,pTmpStr); \
		strcat(pCmd,tail); \
		len=strlen(pTmpStr); \
		len+=strlen(tail);\
	}

#define DBInsertIntValue(pCmd,pTmpStr,value,tail,len) \
	{ \
		sprintf(pTmpStr,"%d",value); \
		strcat(pCmd,pTmpStr); \
		strcat(pCmd,tail); \
		len=strlen(pTmpStr); \
		len+=strlen(tail);\
	}



typedef struct timeval TimeValue;

typedef struct
{
	char 	infile[NAME_SIZE];	// -i
	char 	outfile[NAME_SIZE];	// -o

	int     saveBitstream;	// -o
	int     maxnum;		// -n

	int     codec;			// -f
	int     width;			// -w
	int     height;			// -h

	int     display;		// -d
	int     fbno;			// -d

	int     log;			// -log
#if 0
	int     rotation;			// -r
	int     bitStep;              // -bstep
	int     qpStep;              // -qpstep
	int     fStep;                 // -fstep	

	int     avcDeblkOffsetAlphaStep;
	int     avcDeblkOffsetBetaStep;
	int     avcChromaQpOffsetStep;	
#endif
	int     db;                     // -db
	char  database[NAME_SIZE];          // -db
	int     tbl;			// -tbl
	char  table[NAME_SIZE];          // -tbl	

	int     plt;			// -plt
	char  platform[NAME_SIZE];   // -plt

	char script[NAME_SIZE];		//-config
	int    usingConfig;				//-config

	int    maxcase;		//-m
}
EncIOParams;

typedef struct
{
	int frmCnt;	// 0: no valid frames; [1:MAX_FRM_NUMS]: valid frame numbers
	double avgCompressedRate;	//average compressed rate
	double avgKBPS;	//average kbps/frame
	double avgTime;	//average time for encode one frame
	double frmAvgBytes;	//average bytes/frame	
	double frmAvgPSNR[3];  //average PSNR/frame
	double frmAvgSSIM[3];  //average SSIM/frame
	
	double minKBPS;
	double maxKBPS;
	double frmMinTime;
	double frmMaxTime;	
	double frmMinBytes;
	double frmMaxBytes;
	double frmMinPSNR[3];
	double frmMaxPSNR[3];
	double frmMinSSIM[3];
	double frmMaxSSIM[3];

	double frmTime[MAX_FRM_NUMS];	// encode time per frame
	double frmBytes[MAX_FRM_NUMS];	// byte numbers per frame
	double frmPSNR[3][MAX_FRM_NUMS];	// PSNR per frame	
	double frmSSIM[3][MAX_FRM_NUMS];	// SSIM per frame	

	TimeValue tm_beg;	//temporary variable for compute time
	TimeValue tm_end;	//temporary variable for compute time
}
EncStatisInfo;

typedef struct 
{
	int beg;
	int end;
	int step;
}EncParamsRange;

typedef struct 
{
	char name[MAX_VAR_NAME];
	int index;
	EncParamsRange defaultval;
}EncParamsIndexTable;

typedef enum 
{
	/* general */
	PARAM_GOPSIZE_INDEX = 0,
	PARAM_FPS_INDEX,
	PARAM_QP_INDEX,
	PARAM_KBPS_INDEX,

/*
	advanced parameters:
	--chromaInterleave		
		YUV or NV21 ?
	--enableAutoSkip //for (bps!=0)
		The value 0 disables automatic skip and 1 enables automatic skip in encoder operation. 
		Automatic skip means encoder can skip frame encoding when generated Bitstream so far is too big considering target bitrate. 
		This parameter will be ignored if rate control is not used (bitRate = 0).
	--mirror	
	--rotate
	--initialDelay 
		Time delay (in ms) for the bit stream to reach initial occupancy of the vbv buffer from zero level. 
		This value is ignored if rate control is disabled. 
		The value 0 means	the encoder does not check for reference decoder buffer delay constraints.
	--vbvBufferSize 
		vbv_buffer_size in bits. 
		This value is ignored if rate control is disabled or initialDelay is 0. 
		The value 0 means the encoder does not check for reference decoder buffer size constraints.	
	--enc slice mode
		sliceMode 
			0 = One slice per picture, 
			1 = Multiple slices per picture.
			In normal MPEG-4 mode, the resync-marker and packet header are inserted	between slice boundaries. 
			In short video header with Annex K = 0, the GOB header is inserted at every GOB layer start. 
			In short video header with Annex	K = 1, multiple slices are generated. 
			In AVC mode, multiple slice layer RBSP is generated.
		sliceSizeMode 
			Size of a generated slice when sliceMode = 1, 
			0 means sliceSize is define by amount of bits, and 
			1 means sliceSize is defined by the number of Mbytes in a	slice. 
			This parameter is ignored when sliceMode = 0 or in short video header mode with Annex K = 0.
		sliceSize 
			Size of a slice in bits or Mbytes specified by sliceSizeMode. 
			This parameter is ignored when sliceMode = 0 or in short video header mode with Annex K = 0.
	--intraRefresh: 
		0 = Intra MB refresh is not used. 
		Otherwise = At least N MB's in every P-frame are encoded as intra MB's. 
		This value is ignored in for STD_MJPG.
	--rcIntraQp
		Quantization parameter for I frame. 
		When this value is -1, the quantization	parameter for I frames is automatically determined by the VPU. 
		In MPEG4/H.263 mode, the range is 1-31; 
		In H.264 mode, the range is from 0-51. 
		This is ignored for STD_MJPG
		Even if rate control is enabled, VPU encoder will use this fixed quantization step for all I-frames.
	--userQpMin	//for (bps!=0) 
		Sets the Minimum quantized step parameter for encoding process. 
		-1 = disables this setting and the VPU uses the default minimum quantize step(Qp(H.264 12,MPEG-4/H.263 2). 
		In MPEG-4/H.263 mode, the value of userQpMix shall be in the range of 1 to 31 and less than userQpMax. 
		In H.264 mode, the value of userQpMix shall be in the range of 0 to 51 and less than userQpMax.
	--userQpMax	//for (bps!=0) 
		Sets the maximum quantized step parameter for the encoding process.
		-1 = disables this setting and the VPU uses the default maximum quantized step.
		In MPEG-4/H.263 mode, the value of userQpMax shall be in the range of 1 to 31.
		In H.264 mode, the value of userQpMax shall be in the range of 0 to 51.
		userQpMin and userQpMax must be set simultaneously.		
	--userQpMinEnable 
		userQpMinEable equal to 1 indicates that macroblock QP, generated in rate control, 
		is cropped to be bigger than or equal to userQpMin.
	--userQpMaxEnable 
		userQpMaxEable equal to 1 indicates that macroblock QP, generated in rate control, 
		is cropped to be smaller than or equal to userQpMax		
	--userGamma	//factor*32768: factor=[0,1]
		Smoothing factor in the estimation. 
		A value for gamma is factor*32768, where the	value for factor must be between 0 and 1. 
		If the smoothing factor is close to 0, Qp changes slowly. 
		If the smoothing factor is close to 1, Qp changes quickly. 
		The default Gamma value is 0.75*32768.
	--RcIntervalMode 
		Encoder rate control mode setting. The host sets the bitrate control mode according to the required case. 
		The default value is 1.
			0 = normal mode rate control
			1 = FRAME_LEVEL rate control
			2 = SLICE_LEVEL rate control
			3 = USER DEFINED MB LEVEL rate control
	--MbInterval 
		User defined Mbyte interval value. 
		The default value is 2 macroblock rows. 
		For example, if the resolution is 720x470, then the two macroblock row is 2x(720/16) = 90. 
		This value is used only when the RcIntervalMode is 3.		
	--avcIntra16x16OnlyModeEnable
		Avc Intra 16x16 only mode.
		0 = disable, 
		1 = enable
*/	
	PARAM_CHROMAINTERLEAVE_INDEX,
	PARAM_ENABLEAUTOSKIP_INDEX,
	PARAM_MIRROR_INDEX,
	PARAM_ROTATE_INDEX,

	PARAM_INITIALDELAY_INDEX,
	PARAM_VBVBUFSIZE_INDEX,
	PARAM_SLICEMODE_INDEX,
	PARAM_SLICESIZEMODE_INDEX,
	PARAM_SLICESIZE_INDEX,

	PARAM_INTRAREFRESH_INDEX,	
	PARAM_RCINTRAQP_INDEX,
	PARAM_USERQPMIN_INDEX,
	PARAM_USERQPMAX_INDEX,
	PARAM_USERQPMINENABLE_INDEX,
	PARAM_USERQPMAXENABLE_INDEX,	
	PARAM_GAMMA_INDEX,
	PARAM_RCINTERVALMODE_INDEX,
	PARAM_MBINTERVAL_INDEX,

	/*H.264*/
	PARAM_AVC_INTRA16X16ONLY_INDEX,
/*
	avc_constrainedIntraPredFlag
		0 = disable, 1 = enable
	avc_disableDeblk
		0 = enable, 1 = disable, 2 = disable deblocking filter at slice boundaries
	avc_deblkFilterOffsetAlpha
		deblk_filter_offset_alpha (每6 to 6)
	avc_deblkFilterOffsetBeta
		deblk_filter_offset_beta (每6 to 6)
	avc_chromaQpOffset 
		chroma_qp_offset (每12 to 12)
	avc_audEnable
		0 = disable, 1 = enable and the encoder generates AUD RBSP at the start of every picture
	avc_fmoEnable 
		Not used on the i.MX5x since FMO encoding is not supported
	avc_fmoSliceNum 
		Not used on the i.MX5x since FMO encoding is not supported
	avc_fmoType 
		Not used on the i.MX5x since FMO encoding is not supported
	avc_fmoSliceSaveBufSize 
		Not used on the i.MX5x since FMO encoding is not supported	
*/

	PARAM_AVC_IPREDFLAG_INDEX,
	PARAM_AVC_DISDBLK_INDEX,
	PARAM_AVC_DBLKALPHA_INDEX,
	PARAM_AVC_DBLKBETA_INDEX,
	PARAM_AVC_DBLKCHROMQPOFF_INDEX,
	PARAM_AVC_AUDENABLE_INDEX,
	PARAM_AVC_FMOENABLE_INDEX,
	PARAM_AVC_FMOSLICENUM_INDEX,
	PARAM_AVC_FMOTYPE_INDEX,

	/*H.263*/
	/*
	h263_annexJEnable 
		0 = disable, 1 = enable
	h263_annexKEnable 
		0 = disable, 1 = enable
	h263_annexTEnable 
		0 = disable, 1 = enable	
	*/
	PARAM_H263_ANNEXJENABLE_INDEX,
	PARAM_H263_ANNEXKENABLE_INDEX,
	PARAM_H263_ANNEXTENABLE_INDEX,
	
	/*MPEG4*/
	/*
	mp4_dataPartitionEnable 
		0 = disable, 1 = enable
	mp4_reversibleVlcEnable 
		0 = disable, 1 = enable
	mp4_intraDcVlcThr 
		Value of intra_dc_vlc_thr in MPEG-4 part 2 standard, valid range is 0每7
	mp4_hecEnable 
		0 = disable, 1 = enable
	mp4_verid 
		Value of MPEG-4 part 2 standard version ID, version 1 and 2 are allowed
	*/
	PARAM_MPEG4_DPENABLE_INDEX,
	PARAM_MPEG4_RVLCENABLE_INDEX,
	PARAM_MPEG4_INTRADCVLCTHR_INDEX,
	PARAM_MPEG4_HECENABLE_INDEX,
	PARAM_MPEG4_VERID_INDEX,	
	
	PARAM_MAX_INDEX,
}EncScriptParamIndex;

typedef struct 
{
	char name[MAX_VAR_NAME];
	char type[MAX_VAR_NAME];
	int index;
	SQLiteItemType eType;
}EncSQLiteIndexTable;

typedef enum 
{
	/*we should get the sole item through (name and param_id)*/
	/* general */
	SQL_NAME_INDEX = 0,
	SQL_PARAM_ID_INDEX,		/*parameters combination*/
	SQL_WIDTH_INDEX,
	SQL_HEIGHT_INDEX,
	SQL_NUM_INDEX,
	
	SQL_COMPRESS_RATE_INDEX,	
	SQL_AVGTIME_INDEX,
	SQL_MINTIME_INDEX,
	SQL_MAXTIME_INDEX,

	SQL_AVGKBPS_INDEX,
	SQL_MINKBPS_INDEX,
	SQL_MAXKBPS_INDEX,

	SQL_AVGPSNRY_INDEX,
	SQL_AVGPSNRU_INDEX,
	SQL_AVGPSNRV_INDEX,

	SQL_MINPSNRY_INDEX,
	SQL_MINPSNRU_INDEX,
	SQL_MINPSNRV_INDEX,

	SQL_MAXPSNRY_INDEX,
	SQL_MAXPSNRU_INDEX,
	SQL_MAXPSNRV_INDEX,

	SQL_AVGSSIMY_INDEX,
	SQL_AVGSSIMU_INDEX,
	SQL_AVGSSIMV_INDEX,

	SQL_MINSSIMY_INDEX,
	SQL_MINSSIMU_INDEX,
	SQL_MINSSIMV_INDEX,

	SQL_MAXSSIMY_INDEX,
	SQL_MAXSSIMU_INDEX,
	SQL_MAXSSIMV_INDEX,
	
	SQL_CODEC_INDEX,	
	SQL_PLATFORM_INDEX,
	SQL_VPULIB_INDEX,
	SQL_VPUFW_INDEX,

	SQL_QP_INDEX,
	SQL_FPS_INDEX,
	SQL_KBPS_INDEX,	
	SQL_GOPSIZE_INDEX,

	SQL_CHROMAINTERLEAVE_INDEX,
	SQL_ENABLEAUTOSKIP_INDEX,
	SQL_MIRROR_INDEX,
	SQL_ROTATE_INDEX,	

	SQL_INITIALDELAY_INDEX,
	SQL_VBVBUFSIZE_INDEX,

	SQL_SLICEMODE_INDEX,
	SQL_SLICESIZEMODE_INDEX,
	SQL_SLICESIZE_INDEX,

	SQL_INTRAREFRESH_INDEX,	
	SQL_RCINTRAQP_INDEX,

	SQL_USERQPMIN_INDEX,
	SQL_USERQPMAX_INDEX,
	SQL_USERQPMINENABLE_INDEX,
	SQL_USERQPMAXENABLE_INDEX,	
	SQL_USERGAMMA_INDEX,

	SQL_RCINTERVALMODE_INDEX,
	SQL_MBINTERVAL_INDEX,

	/*H.264*/	
	SQL_INTRA16X16ONLY_INDEX,
	SQL_IPREDFLAG_INDEX,	
	SQL_DISDEBLK_INDEX,
	SQL_DEBLKALPHA_INDEX,
	SQL_DEBLKBETA_INDEX,
	SQL_DEBLKCHROMAQPOFF_INDEX,
	SQL_AUDENABLE_INDEX,
	SQL_FMOENABLE_INDEX,
	SQL_FMOSLICENUM_INDEX,
	SQL_FMOTYPE_INDEX,

	/*H.263*/
	SQL_ANNEXJENABLE_INDEX,
	SQL_ANNEXKENABLE_INDEX,
	SQL_ANNEXTENABLE_INDEX,
	
	/*MPEG4*/
	SQL_DPENABLE_INDEX,
	SQL_RVLCENABLE_INDEX,
	SQL_INTRADCVLCTHR_INDEX,
	SQL_HECENABLE_INDEX,
	SQL_VERID_INDEX,	


	SQL_MAX_INDEX,
}EncSQLiteIndex;

EncParamsIndexTable gParamsIndexTable[PARAM_MAX_INDEX]=
{
	//supported input parameters in script config file.
	{"gopsize",PARAM_GOPSIZE_INDEX,{15,15,1}},
	{"framerate",PARAM_FPS_INDEX,{30,30,1}},		
	{"quanparam",PARAM_QP_INDEX,{10,10,1}},
	{"bitrate",PARAM_KBPS_INDEX,{0,0,1}},

	{"chromainterleave",PARAM_CHROMAINTERLEAVE_INDEX,{0,0,1}},	
	{"autoskip",PARAM_ENABLEAUTOSKIP_INDEX,{0,0,1}},
	{"mirror",PARAM_MIRROR_INDEX,{0,0,1}},
	{"rotate",PARAM_ROTATE_INDEX,{0,0,1}},

	{"initialdelay",PARAM_INITIALDELAY_INDEX,{0,0,1}},
	{"vbvbufsize",PARAM_VBVBUFSIZE_INDEX,{0,0,1}},
	
	{"slicemode",PARAM_SLICEMODE_INDEX,{0,0,1}},	
	{"slicesizemode",PARAM_SLICESIZEMODE_INDEX,{0,0,1}},
	{"slicesize",PARAM_SLICESIZE_INDEX,{4000,4000,1}},

	{"intrarefresh",PARAM_INTRAREFRESH_INDEX,{0,0,1}},
	{"rcintraqp",PARAM_RCINTRAQP_INDEX,{-1,-1,1}},

	{"userqpmin",PARAM_USERQPMIN_INDEX,{0,0,1}},
	{"userqpmax",PARAM_USERQPMAX_INDEX,{0,0,1}},
	{"userqpminenable",PARAM_USERQPMINENABLE_INDEX,{0,0,1}},
	{"userqpmaxenable",PARAM_USERQPMAXENABLE_INDEX,{0,0,1}},

	{"usergamma",PARAM_GAMMA_INDEX,{(0.75*32768),(0.75*32768),1}},
	{"rcintervalmode",PARAM_RCINTERVALMODE_INDEX,{0,0,1}},
	{"mbinterval",PARAM_MBINTERVAL_INDEX,{0,0,1}},

	{"avcintra16x16only",PARAM_AVC_INTRA16X16ONLY_INDEX,{0,0,1}},
	{"avcintrapredflag",PARAM_AVC_IPREDFLAG_INDEX,{0,0,1}},
	{"avcdisdeblk",PARAM_AVC_DISDBLK_INDEX,{0,0,1}},
	{"avcdeblkalpha",PARAM_AVC_DBLKALPHA_INDEX,{6,6,1}},
	{"avcdeblkbeta",PARAM_AVC_DBLKBETA_INDEX,{0,0,1}},
	{"avcdeblkchromqpoff",PARAM_AVC_DBLKCHROMQPOFF_INDEX,{10,10,1}},

	{"avcaudenable",PARAM_AVC_AUDENABLE_INDEX,{0,0,1}},
	{"avcfmoenable",PARAM_AVC_FMOENABLE_INDEX,{0,0,1}},
	{"avcfmoslicenum",PARAM_AVC_FMOSLICENUM_INDEX,{0,0,1}},
	{"avcfmotype",PARAM_AVC_FMOTYPE_INDEX,{0,0,1}},

	{"h263annexj",PARAM_H263_ANNEXJENABLE_INDEX,{0,0,1}},
	{"h263annexk",PARAM_H263_ANNEXKENABLE_INDEX,{0,0,1}},
	{"h263annext",PARAM_H263_ANNEXTENABLE_INDEX,{0,0,1}},

	{"mp4datapartitionenable",PARAM_MPEG4_DPENABLE_INDEX,{0,0,1}},
	{"mp4rvlcenable",PARAM_MPEG4_RVLCENABLE_INDEX,{0,0,1}},
	{"mp4intradcvlcthr",PARAM_MPEG4_INTRADCVLCTHR_INDEX,{0,0,1}},
	{"mp4hecenable",PARAM_MPEG4_HECENABLE_INDEX,{0,0,1}},
	{"mp4verid",PARAM_MPEG4_VERID_INDEX,{0,0,1}},
	
};

EncSQLiteIndexTable gSQLiteIndexTable[SQL_MAX_INDEX]=
{
	//supported sqlite columns definition
	/*general info*/
	{"name","varchar(256)",SQL_NAME_INDEX,SQL_STRING},
	{"param_id","varchar(256)",SQL_PARAM_ID_INDEX,SQL_STRING},	
	{"width","INT",SQL_WIDTH_INDEX,SQL_INT},
	{"height","INT",SQL_HEIGHT_INDEX,SQL_INT},
	{"number","INT",SQL_NUM_INDEX,SQL_INT},

	/*statistic info*/
	{"compressedrate","DOUBLE",SQL_COMPRESS_RATE_INDEX,SQL_DOUBLE},

	{"avgtime","DOUBLE",SQL_AVGTIME_INDEX,SQL_DOUBLE},
	{"mintime","DOUBLE",SQL_MINTIME_INDEX,SQL_DOUBLE},
	{"maxtime","DOUBLE",SQL_MAXTIME_INDEX,SQL_DOUBLE},

	{"avgkbps","DOUBLE",SQL_AVGKBPS_INDEX,SQL_DOUBLE},
	{"minkbps","DOUBLE",SQL_MINKBPS_INDEX,SQL_DOUBLE},
	{"maxkbps","DOUBLE",SQL_MAXKBPS_INDEX,SQL_DOUBLE},
	
	{"avgpsnry","DOUBLE",SQL_AVGPSNRY_INDEX,SQL_DOUBLE},
	{"avgpsnru","DOUBLE",SQL_AVGPSNRU_INDEX,SQL_DOUBLE},
	{"avgpsnrv","DOUBLE",SQL_AVGPSNRV_INDEX,SQL_DOUBLE},

	{"minpsnry","DOUBLE",SQL_MINPSNRY_INDEX,SQL_DOUBLE},
	{"minpsnru","DOUBLE",SQL_MINPSNRU_INDEX,SQL_DOUBLE},
	{"minpsnrv","DOUBLE",SQL_MINPSNRV_INDEX,SQL_DOUBLE},

	{"maxpsnry","DOUBLE",SQL_MAXPSNRY_INDEX,SQL_DOUBLE},
	{"maxpsnru","DOUBLE",SQL_MAXPSNRU_INDEX,SQL_DOUBLE},
	{"maxpsnrv","DOUBLE",SQL_MAXPSNRV_INDEX,SQL_DOUBLE},

	{"avgssimy","DOUBLE",SQL_AVGSSIMY_INDEX,SQL_DOUBLE},
	{"avgssimu","DOUBLE",SQL_AVGSSIMU_INDEX,SQL_DOUBLE},
	{"avgssimv","DOUBLE",SQL_AVGSSIMV_INDEX,SQL_DOUBLE},

	{"minssimy","DOUBLE",SQL_MINSSIMY_INDEX,SQL_DOUBLE},
	{"minssimu","DOUBLE",SQL_MINSSIMU_INDEX,SQL_DOUBLE},
	{"minssimv","DOUBLE",SQL_MINSSIMV_INDEX,SQL_DOUBLE},

	{"maxssimy","DOUBLE",SQL_MAXSSIMY_INDEX,SQL_DOUBLE},
	{"maxssimu","DOUBLE",SQL_MAXSSIMU_INDEX,SQL_DOUBLE},
	{"maxssimv","DOUBLE",SQL_MAXSSIMV_INDEX,SQL_DOUBLE},

	/*envrionment info*/
	{"codec","varchar(10)",SQL_CODEC_INDEX,SQL_STRING},
	{"platform","varchar(10)",SQL_PLATFORM_INDEX,SQL_STRING},
	{"vpulib","varchar(10)",SQL_VPULIB_INDEX,SQL_STRING},
	{"vpufw","varchar(10)",SQL_VPUFW_INDEX,SQL_STRING},

	/*user input parameters*/
	{"qp","INT",SQL_QP_INDEX,SQL_INT},
	{"fps","INT",SQL_FPS_INDEX,SQL_INT},
	{"kbps","INT",SQL_KBPS_INDEX,SQL_INT},	
	{"gop","INT",SQL_GOPSIZE_INDEX,SQL_INT},

	{"chromainterleave","INT",SQL_CHROMAINTERLEAVE_INDEX,SQL_INT},	
	{"autoskip","INT",SQL_ENABLEAUTOSKIP_INDEX,SQL_INT},
	{"mirror","INT",SQL_MIRROR_INDEX,SQL_INT},
	{"rotate","INT",SQL_ROTATE_INDEX,SQL_INT},	

	{"initialdelay","INT",SQL_INITIALDELAY_INDEX,SQL_INT},
	{"vbvbufsize","INT",SQL_VBVBUFSIZE_INDEX,SQL_INT},

	{"slicemode","INT",SQL_SLICEMODE_INDEX,SQL_INT},
	{"slicesizemode","INT",SQL_SLICESIZEMODE_INDEX,SQL_INT},
	{"slicesize","INT",SQL_SLICESIZE_INDEX,SQL_INT},

	{"intrarefresh","INT",SQL_INTRAREFRESH_INDEX,SQL_INT},
	{"rcintraqp","INT",SQL_RCINTRAQP_INDEX,SQL_INT},

	{"userqpmin","INT",SQL_USERQPMIN_INDEX,SQL_INT},
	{"userqpmax","INT",SQL_USERQPMAX_INDEX,SQL_INT},
	{"userqpminenable","INT",SQL_USERQPMINENABLE_INDEX,SQL_INT},
	{"userqpmaxenable","INT",SQL_USERQPMAXENABLE_INDEX,SQL_INT},

	{"usergamma","INT",SQL_USERGAMMA_INDEX,SQL_INT},
	{"rcintervalmode","INT",SQL_RCINTERVALMODE_INDEX,SQL_INT},
	{"mbinterval","INT",SQL_MBINTERVAL_INDEX,SQL_INT},

	{"avcintra16only","INT",SQL_INTRA16X16ONLY_INDEX,SQL_INT},
	{"avcipredflag","INT",SQL_IPREDFLAG_INDEX,SQL_INT},
	{"avcdisdeblk","INT",SQL_DISDEBLK_INDEX,SQL_INT},
	{"avcdeblkalpha","INT",SQL_DEBLKALPHA_INDEX,SQL_INT},
	{"avcdeblkbeta","INT",SQL_DEBLKBETA_INDEX,SQL_INT},
	{"avcdeblkchromqpoff","INT",SQL_DEBLKCHROMAQPOFF_INDEX,SQL_INT},

	{"avcaudenable","INT",SQL_AUDENABLE_INDEX,SQL_INT},
	{"avcfmoenable","INT",SQL_FMOENABLE_INDEX,SQL_INT},
	{"avcfmoslicenum","INT",SQL_FMOSLICENUM_INDEX,SQL_INT},
	{"avcfmotype","INT",SQL_FMOTYPE_INDEX,SQL_INT},
	
	{"h263annexj","INT",SQL_ANNEXJENABLE_INDEX,SQL_INT},
	{"h263annexk","INT",SQL_ANNEXKENABLE_INDEX,SQL_INT},
	{"h263annext","INT",SQL_ANNEXTENABLE_INDEX,SQL_INT},
	
	{"mp4datapartition","INT",SQL_DPENABLE_INDEX,SQL_INT},
	{"mp4rvlc","INT",SQL_RVLCENABLE_INDEX,SQL_INT},
	{"mp4intradcvlcthr","INT",SQL_INTRADCVLCTHR_INDEX,SQL_INT},
	{"mp4hecenable","INT",SQL_HECENABLE_INDEX,SQL_INT},
	{"mp4verid","INT",SQL_VERID_INDEX,SQL_INT},

};


char* gpCodecName[]=
{
	"mpeg4", /*0*/
	"h263", /*1*/		
	"h264", /*2*/
};

char* gpPlatformName[]=
{
	"iMX51", /*0*/
};

char* gpVpuLibName[]=
{
	"none", /*0*/
};

char* gpVpuFWName[]=
{
	"none", /*0*/
};


static void usage(char*program)
{
	APP_ERROR_PRINTF("\nUsage: %s [options] -i yuv_file -w width -h height \n", program);
	APP_ERROR_PRINTF("options:\n"
		   "	-o <file_name>	:prefix name for output encoded bitstream,\n"
		   "			encoder will pack the file name according to related parameters\n"
		   "			[default: no output file]\n"
		   "	-n <frame_num>	:encode max <frame_num> frames\n"
		   "			[default: all frames will be encoded]\n"
		   "	-m <case_num>	:run max <case_num> cases\n"
		   "			[default: all cases will be run]\n"

		   "	-f <codec>	:set codec format with <codec>.\n"
		   "			Mpeg4:	0 (default)\n"
		   "			H263:	1 \n"
		   "			H264:	2 \n"
		   "	-w <width>	:input picture width(16 pixels alignment) \n"
		   "	-h <height>	:input picture height(16 pixels alignment) \n"
		   "	-d <fb_no>	:notify internal decoder to use frame buffer <fb_no> for render.\n"
		   "	-log 		:output log file for statistic info.\n"
#if 0
		   "	-r <rotation>	:rotation , only support 0(default),90,180,270 \n"
		   "	-bstep <step>	:advanced option: step for bitrate: 1,2,3,4(default),5,6,7,...\n"
		   "	-qpstep <step>	:advanced option: step for quantization: 1,2,3,..5(default),6,7....\n"
		   "	-fstep <step>	:advanced option: step for frame rate: 1,2,3(default),4,5,...\n"
#endif		   
		   "	-db <database>	:insert related info into sqlite database \n"
		   "	-tbl <table>	:table name\n"
		   "	-plt <platform>	:platform name: iMX51(default),iMX61,...\n"
		   "	-config <config>:set all related parameters\n"
		   );
	exit(0);
}

static void GetUserInput(EncIOParams *pIO, int argc, char *argv[])
{
	int bitFileDone = 0;
	int validWidth=0;
	int validHeight=0;

	argc--;
	argv++;

	while (argc)
	{
		if (argv[0][0] == '-')
		{
			CASE_FIRST("-i")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					strcpy((char *)pIO->infile, argv[0]);
					bitFileDone = 1;
				}
			}
			CASE("-o")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					strcpy((char *)pIO->outfile, argv[0]);
					pIO->saveBitstream= 1;
				}
			}
			CASE("-n")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->maxnum);
				}
			}
			CASE("-m")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->maxcase);
				}
			}			
/*			
			CASE("-fstep")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->fStep);
					APP_DEBUG_PRINTF("fstep: %d \r\n",pIO->fStep);
				}
			}
*/			
			CASE("-f")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->codec);
				}
			}
			CASE("-w")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->width);
					validWidth=1;
				}
			}
			CASE("-h")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->height);
					validHeight=1;
				}
			}
			CASE("-db")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					strcpy((char *)pIO->database, argv[0]);
					//APP_DEBUG_PRINTF("database: %s \r\n",pIO->database);
					pIO->db=1;
				}
			}				
			CASE("-d")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->fbno);
					//APP_DEBUG_PRINTF("display: %d \r\n",pIO->fbno);
				}			
				pIO->display = 1;
			}
			CASE("-log")
			{
				pIO->log = 1;
			}
/*			
			CASE("-r")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->rotation);
				}
			}
			CASE("-bstep")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->bitStep);
					APP_DEBUG_PRINTF("bitStep: %d \r\n",pIO->bitStep);
				}
			}
			CASE("-qpstep")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->qpStep);
					APP_DEBUG_PRINTF("qpStep: %d \r\n",pIO->qpStep);
				}
			}
*/			
			CASE("-tbl")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					strcpy((char *)pIO->table, argv[0]);
					//APP_DEBUG_PRINTF("database: %s \r\n",pIO->database);
					pIO->tbl=1;
				}
			}
			CASE("-plt")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					strcpy((char *)pIO->platform, argv[0]);
					pIO->plt=1;
				}
			}	
			CASE("-config")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					strcpy((char *)pIO->script, argv[0]);
					pIO->usingConfig= 1;
				}
			}			
			DEFAULT                             // Has to be last
			{
				APP_ERROR_PRINTF("Unsupported option %s\n", argv[0]);
				usage(pIO->infile);
			}
		}
		else
		{
			APP_ERROR_PRINTF("Unsupported option %s\n", argv[0]);
			usage(pIO->infile);
		}
		argc--;
		argv++;
	}

	if(pIO->tbl != pIO->db)
	{
		APP_ERROR_PRINTF("miss database or table name \r\n");
		usage(pIO->infile);
	}
	
	if ((0==bitFileDone)||(0==validWidth)||(0==validHeight))
	{
		usage(pIO->infile);
	}
}


void TimerGet(TimeValue* pTime)
{
	gettimeofday(pTime, 0);
	return;
}

void TimerDiffUs(TimeValue* pTimeBeg,TimeValue* pTimeEnd, double * pDiff)
{
	double tm_1, tm_2;

	tm_1 = pTimeBeg->tv_sec * 1000000 + pTimeBeg->tv_usec;
	tm_2 = pTimeEnd->tv_sec * 1000000 + pTimeEnd->tv_usec;
	*pDiff= (tm_2-tm_1);
	return;
}

int CheckIOParams(EncIOParams* pIO)
{
	int noerr=1;
	
	if ((pIO->width%16)!=0)
	{
		APP_ERROR_PRINTF("width is not 16 alignment \r\n");
		noerr=0;
	}
	if ((pIO->height%16)!=0)
	{
		APP_ERROR_PRINTF("height is not 16 alignment \r\n");
		noerr=0;
	}	
	return noerr;
}

double ComputeMinValue(double * pVal, int cnt)
{
	int i;
	double min=0;
	if(cnt==0)
	{
		min=0;
	}
	else
	{
		min=pVal[0];
		for(i=1;i<cnt;i++)
		{
			if(pVal[i]<min)
			{
				min=pVal[i];
			}
		}
	}
	return min;
}

double ComputeMaxValue(double * pVal, int cnt)
{
	int i;
	double max=0;
	if(cnt==0)
	{
		max=0;
	}
	else
	{
		max=pVal[0];
		for(i=1;i<cnt;i++)
		{
			if(pVal[i]>max)
			{
				max=pVal[i];
			}
		}
	}
	return max;
}

double ComputeAvgValue(double * pVal, int cnt)
{
	int i;
	double total=0;
	double avg=0;

	if(cnt==0)
	{
		avg=0;
	}
	else
	{
		for(i=0;i<cnt;i++)
		{
			total+=pVal[i];
		}
		avg=total/cnt;
	}
	return avg;
}

int ComputeStatisticInfo(EncStatisInfo* pStatisticInfo,EncContxt* pEncCxt)
{
	//compute average encode time
	pStatisticInfo->avgTime=ComputeAvgValue(pStatisticInfo->frmTime, pStatisticInfo->frmCnt);

	//compute average bps
	pStatisticInfo->frmAvgBytes=ComputeAvgValue(pStatisticInfo->frmBytes,pStatisticInfo->frmCnt);
	pStatisticInfo->avgKBPS=pStatisticInfo->frmAvgBytes*8*pEncCxt->nFrameRate/1000;

	//compute average compressed rate
	pStatisticInfo->avgCompressedRate=((double)pEncCxt->nPicWidth*pEncCxt->nPicHeight*3/2)/pStatisticInfo->frmAvgBytes;

	//compute average PSNR
	//Y
	pStatisticInfo->frmAvgPSNR[0]=ComputeAvgValue(pStatisticInfo->frmPSNR[0],pStatisticInfo->frmCnt);
	//U
	pStatisticInfo->frmAvgPSNR[1]=ComputeAvgValue(pStatisticInfo->frmPSNR[1],pStatisticInfo->frmCnt);
	//V
	pStatisticInfo->frmAvgPSNR[2]=ComputeAvgValue(pStatisticInfo->frmPSNR[2],pStatisticInfo->frmCnt);

	//compute average SSIM
	//Y
	pStatisticInfo->frmAvgSSIM[0]=ComputeAvgValue(pStatisticInfo->frmSSIM[0],pStatisticInfo->frmCnt);
	//U
	pStatisticInfo->frmAvgSSIM[1]=ComputeAvgValue(pStatisticInfo->frmSSIM[1],pStatisticInfo->frmCnt);
	//V
	pStatisticInfo->frmAvgSSIM[2]=ComputeAvgValue(pStatisticInfo->frmSSIM[2],pStatisticInfo->frmCnt);

	//compute min encode time
	pStatisticInfo->frmMinTime=ComputeMinValue(pStatisticInfo->frmTime,pStatisticInfo->frmCnt);
	
	//compute min bps
	pStatisticInfo->frmMinBytes=ComputeMinValue(pStatisticInfo->frmBytes,pStatisticInfo->frmCnt);
	pStatisticInfo->minKBPS=pStatisticInfo->frmMinBytes*8*pEncCxt->nFrameRate/1000;

	//compute min PSNR
	//Y
	pStatisticInfo->frmMinPSNR[0]=ComputeMinValue(pStatisticInfo->frmPSNR[0],pStatisticInfo->frmCnt);
	//U
	pStatisticInfo->frmMinPSNR[1]=ComputeMinValue(pStatisticInfo->frmPSNR[1],pStatisticInfo->frmCnt);
	//V
	pStatisticInfo->frmMinPSNR[2]=ComputeMinValue(pStatisticInfo->frmPSNR[2],pStatisticInfo->frmCnt);

	//compute min SSIM
	//Y
	pStatisticInfo->frmMinSSIM[0]=ComputeMinValue(pStatisticInfo->frmSSIM[0],pStatisticInfo->frmCnt);
	//U
	pStatisticInfo->frmMinSSIM[1]=ComputeMinValue(pStatisticInfo->frmSSIM[1],pStatisticInfo->frmCnt);
	//V
	pStatisticInfo->frmMinSSIM[2]=ComputeMinValue(pStatisticInfo->frmSSIM[2],pStatisticInfo->frmCnt);

	//compute max encode time
	pStatisticInfo->frmMaxTime=ComputeMaxValue(pStatisticInfo->frmTime,pStatisticInfo->frmCnt);	
	
	//compute max bps
	pStatisticInfo->frmMaxBytes=ComputeMaxValue(pStatisticInfo->frmBytes,pStatisticInfo->frmCnt);
	pStatisticInfo->maxKBPS=pStatisticInfo->frmMaxBytes*8*pEncCxt->nFrameRate/1000;

	//compute max PSNR
	//Y
	pStatisticInfo->frmMaxPSNR[0]=ComputeMaxValue(pStatisticInfo->frmPSNR[0],pStatisticInfo->frmCnt);
	//U
	pStatisticInfo->frmMaxPSNR[1]=ComputeMaxValue(pStatisticInfo->frmPSNR[1],pStatisticInfo->frmCnt);
	//V
	pStatisticInfo->frmMaxPSNR[2]=ComputeMaxValue(pStatisticInfo->frmPSNR[2],pStatisticInfo->frmCnt);

	//compute max SSIM
	//Y
	pStatisticInfo->frmMaxSSIM[0]=ComputeMaxValue(pStatisticInfo->frmSSIM[0],pStatisticInfo->frmCnt);
	//U
	pStatisticInfo->frmMaxSSIM[1]=ComputeMaxValue(pStatisticInfo->frmSSIM[1],pStatisticInfo->frmCnt);
	//V
	pStatisticInfo->frmMaxSSIM[2]=ComputeMaxValue(pStatisticInfo->frmSSIM[2],pStatisticInfo->frmCnt);

	return 1;
}


int DBInitColumn(SQLiteColumn* pSQLColumn)
{
	int i;
	int noerr=1;
	EncSQLiteIndexTable* pSQLItem;

	pSQLItem=gSQLiteIndexTable;	
	for(i=0;i<SQL_MAX_INDEX;i++)
	{
		strcpy(pSQLColumn[i].name,pSQLItem[i].name);
		strcpy(pSQLColumn[i].type,pSQLItem[i].type);
		pSQLColumn[i].eType=pSQLItem[i].eType;
		pSQLColumn[i].nLength=0;	//not support
		pSQLColumn[i].pVal=0;
	}

	return noerr;
}

int DBCopy(SQLiteColumn* pDstSQLColumn,SQLiteColumn* pSrcSQLColumn,int nDstIndex,int nSrcIndex)
{
	int noerr=1;

	strcpy(pDstSQLColumn[nDstIndex].name,pSrcSQLColumn[nSrcIndex].name);
	strcpy(pDstSQLColumn[nDstIndex].type,pSrcSQLColumn[nSrcIndex].type);
	pDstSQLColumn[nDstIndex].eType=pSrcSQLColumn[nSrcIndex].eType;
	pDstSQLColumn[nDstIndex].nLength=pSrcSQLColumn[nSrcIndex].nLength;
	pDstSQLColumn[nDstIndex].pVal=pSrcSQLColumn[nSrcIndex].pVal;

	return noerr;
}

int DBGenParamIDStringBasedInputColumn(char* pParamID,SQLiteColumn* pSQLColumn,int nMaxLen)
{
	int noerr=1;
	char str[DB_MAX_DIG_SIZE];
	int len;
	int totalLen=0;
	int i;
	char delimiter[]="_";
	char end[]="";
	char* pSep;	

	//environment: codec_platform_libver
	DBInsertStrValue(pParamID, (char*)pSQLColumn[SQL_CODEC_INDEX].pVal, "_", len);
	totalLen+=len;
	DBInsertStrValue(pParamID, (char*)pSQLColumn[SQL_PLATFORM_INDEX].pVal, "_", len);
	totalLen+=len;
	DBInsertStrValue(pParamID, (char*)pSQLColumn[SQL_VPULIB_INDEX].pVal, "_", len);	
	totalLen+=len;
	DBInsertStrValue(pParamID, (char*)pSQLColumn[SQL_VPUFW_INDEX].pVal, "_", len);	
	totalLen+=len;

	pSep=delimiter;
	/* user input parameters */
	for(i=SQL_QP_INDEX;i<SQL_MAX_INDEX;i++)
	{
		if(i==(SQL_MAX_INDEX-1))
		{
			//the last column
			pSep=end;
		}
	
		switch(pSQLColumn[i].eType)
		{
			case SQL_INT:
				DBInsertIntValue(pParamID, str, *((int*)pSQLColumn[i].pVal), pSep, len);
				break;
			//case SQL_DOUBLE:
			//	break;
			case SQL_STRING:
				DBInsertStrValue(pParamID, (char*)pSQLColumn[i].pVal, pSep, len);	
				break;
			default:
				//error
				noerr=0;
				goto EXIT;
		}
		totalLen+=len;
	}

EXIT:

	if(totalLen>nMaxLen)
	{
		APP_ERROR_PRINTF("param string overflow \r\n");
		noerr=0;
	}

	return noerr;
}

int DBSetGeneralColumn(EncIOParams * pIOParams,EncContxt* pEncCxt,SQLiteColumn* pSQLColumn)
{
	char* pName=NULL;
	int noerr=1;

	//search the file name, and remove path
	/* general */
	pName=strrchr(pIOParams->infile,'/');	//point to last '/' location
	if(NULL==pName)
	{
		//no '/' in path
		SET_SQLPVAL(pSQLColumn,SQL_NAME_INDEX,pIOParams->infile);
	}
	else
	{
		SET_SQLPVAL(pSQLColumn,SQL_NAME_INDEX,pName+1);	//+1 to skip '/'
	}

	SET_SQLVAL(pSQLColumn,SQL_WIDTH_INDEX,pEncCxt->nPicWidth);
	SET_SQLVAL(pSQLColumn,SQL_HEIGHT_INDEX,pEncCxt->nPicHeight);
	SET_SQLVAL(pSQLColumn,SQL_NUM_INDEX,pEncCxt->nFrameNum);	
	return noerr;
}

int DBSetParamIDColumn(char* pParamIDstr,SQLiteColumn* pSQLColumn)
{
	int noerr=1;

	SET_SQLPVAL(pSQLColumn, SQL_PARAM_ID_INDEX, pParamIDstr);
	return noerr;
}

int DBSetStatisticColumn(EncStatisInfo* pStatisticInfo,SQLiteColumn* pSQLColumn)
{
	int noerr=1;

	/* statistic */
	SET_SQLVAL(pSQLColumn,SQL_COMPRESS_RATE_INDEX,pStatisticInfo->avgCompressedRate);
	SET_SQLVAL(pSQLColumn,SQL_AVGTIME_INDEX,pStatisticInfo->avgTime);
	SET_SQLVAL(pSQLColumn,SQL_MINTIME_INDEX,pStatisticInfo->frmMinTime);
	SET_SQLVAL(pSQLColumn,SQL_MAXTIME_INDEX,pStatisticInfo->frmMaxTime);
	SET_SQLVAL(pSQLColumn,SQL_AVGKBPS_INDEX,pStatisticInfo->avgKBPS);
	SET_SQLVAL(pSQLColumn,SQL_MINKBPS_INDEX,pStatisticInfo->minKBPS);
	SET_SQLVAL(pSQLColumn,SQL_MAXKBPS_INDEX,pStatisticInfo->maxKBPS);

	SET_SQLVAL(pSQLColumn,SQL_AVGPSNRY_INDEX,pStatisticInfo->frmAvgPSNR[0]);
	SET_SQLVAL(pSQLColumn,SQL_AVGPSNRU_INDEX,pStatisticInfo->frmAvgPSNR[1]);
	SET_SQLVAL(pSQLColumn,SQL_AVGPSNRV_INDEX,pStatisticInfo->frmAvgPSNR[2]);

	SET_SQLVAL(pSQLColumn,SQL_MINPSNRY_INDEX,pStatisticInfo->frmMinPSNR[0]);
	SET_SQLVAL(pSQLColumn,SQL_MINPSNRU_INDEX,pStatisticInfo->frmMinPSNR[1]);
	SET_SQLVAL(pSQLColumn,SQL_MINPSNRV_INDEX,pStatisticInfo->frmMinPSNR[2]);

	SET_SQLVAL(pSQLColumn,SQL_MAXPSNRY_INDEX,pStatisticInfo->frmMaxPSNR[0]);
	SET_SQLVAL(pSQLColumn,SQL_MAXPSNRU_INDEX,pStatisticInfo->frmMaxPSNR[1]);
	SET_SQLVAL(pSQLColumn,SQL_MAXPSNRV_INDEX,pStatisticInfo->frmMaxPSNR[2]);

	SET_SQLVAL(pSQLColumn,SQL_AVGSSIMY_INDEX,pStatisticInfo->frmAvgSSIM[0]);
	SET_SQLVAL(pSQLColumn,SQL_AVGSSIMU_INDEX,pStatisticInfo->frmAvgSSIM[1]);
	SET_SQLVAL(pSQLColumn,SQL_AVGSSIMV_INDEX,pStatisticInfo->frmAvgSSIM[2]);

	SET_SQLVAL(pSQLColumn,SQL_MINSSIMY_INDEX,pStatisticInfo->frmMinSSIM[0]);
	SET_SQLVAL(pSQLColumn,SQL_MINSSIMU_INDEX,pStatisticInfo->frmMinSSIM[1]);
	SET_SQLVAL(pSQLColumn,SQL_MINSSIMV_INDEX,pStatisticInfo->frmMinSSIM[2]);

	SET_SQLVAL(pSQLColumn,SQL_MAXSSIMY_INDEX,pStatisticInfo->frmMaxSSIM[0]);
	SET_SQLVAL(pSQLColumn,SQL_MAXSSIMU_INDEX,pStatisticInfo->frmMaxSSIM[1]);
	SET_SQLVAL(pSQLColumn,SQL_MAXSSIMV_INDEX,pStatisticInfo->frmMaxSSIM[2]);
	
	return noerr;
}

int DBSetInputColumn(EncIOParams * pIOParams,EncContxt* pEncCxt,SQLiteColumn* pSQLColumn)
{
	int noerr=1;
	//WARNING: we should not use local array[] to restore string !!!, since the local arrary[] is located in current function stack
	
	/* envrionment */
	SET_SQLPVAL(pSQLColumn,SQL_CODEC_INDEX,gpCodecName[pIOParams->codec]);

	if(0==pIOParams->plt)
	{
		//default: iMX51
		SET_SQLPVAL(pSQLColumn,SQL_PLATFORM_INDEX,gpPlatformName[DEFAULT_PLATFORM_ID]);
	}
	else
	{
		SET_SQLPVAL(pSQLColumn,SQL_PLATFORM_INDEX,pIOParams->platform);
	}

	/* user input */
	SET_SQLPVAL(pSQLColumn,SQL_VPULIB_INDEX,gpVpuLibName[DEFAULT_VPULIB_VER_ID]);
	SET_SQLPVAL(pSQLColumn,SQL_VPUFW_INDEX,gpVpuFWName[DEFAULT_VPUFW_VER_ID]);

	SET_SQLVAL(pSQLColumn,SQL_QP_INDEX,pEncCxt->nQuantParam);
	SET_SQLVAL(pSQLColumn,SQL_FPS_INDEX,pEncCxt->nFrameRate);
	SET_SQLVAL(pSQLColumn,SQL_KBPS_INDEX,pEncCxt->nBitRate);
	SET_SQLVAL(pSQLColumn,SQL_GOPSIZE_INDEX,pEncCxt->nGOPSize);

	SET_SQLVAL(pSQLColumn,SQL_CHROMAINTERLEAVE_INDEX,pEncCxt->nChromaInterleave);
	SET_SQLVAL(pSQLColumn,SQL_ENABLEAUTOSKIP_INDEX,pEncCxt->nEnableAutoSkip);
	SET_SQLVAL(pSQLColumn,SQL_MIRROR_INDEX,pEncCxt->nMirror);
	SET_SQLVAL(pSQLColumn,SQL_ROTATE_INDEX,pEncCxt->nRotAngle);	

	SET_SQLVAL(pSQLColumn,SQL_INITIALDELAY_INDEX,pEncCxt->nInitialDelay);
	SET_SQLVAL(pSQLColumn,SQL_VBVBUFSIZE_INDEX,pEncCxt->nVbvBufSize);

	SET_SQLVAL(pSQLColumn,SQL_SLICEMODE_INDEX,pEncCxt->nSliceMode);
	SET_SQLVAL(pSQLColumn,SQL_SLICESIZEMODE_INDEX,pEncCxt->nSliceSizeMode);
	SET_SQLVAL(pSQLColumn,SQL_SLICESIZE_INDEX,pEncCxt->nSliceSize);

	SET_SQLVAL(pSQLColumn,SQL_INTRAREFRESH_INDEX,pEncCxt->nIntraRefresh);
	SET_SQLVAL(pSQLColumn,SQL_RCINTRAQP_INDEX,pEncCxt->nRcIntraQp);

	SET_SQLVAL(pSQLColumn,SQL_USERQPMIN_INDEX,pEncCxt->nUserQPMin);
	SET_SQLVAL(pSQLColumn,SQL_USERQPMAX_INDEX,pEncCxt->nUserQPMax);
	SET_SQLVAL(pSQLColumn,SQL_USERQPMINENABLE_INDEX,pEncCxt->nUserQPMinEnable);
	SET_SQLVAL(pSQLColumn,SQL_USERQPMAXENABLE_INDEX,pEncCxt->nUserQPMaxEnable);
	SET_SQLVAL(pSQLColumn,SQL_USERGAMMA_INDEX,pEncCxt->nUserGamma);
	
	SET_SQLVAL(pSQLColumn,SQL_RCINTERVALMODE_INDEX,pEncCxt->nRcIntervalMode);
	SET_SQLVAL(pSQLColumn,SQL_MBINTERVAL_INDEX,pEncCxt->nMBInterval);

	/*H.264 related*/
	SET_SQLVAL(pSQLColumn,SQL_INTRA16X16ONLY_INDEX,pEncCxt->nAvc_Intra16x16Only);
	SET_SQLVAL(pSQLColumn,SQL_IPREDFLAG_INDEX,pEncCxt->nAvc_constrainedIntraPredFlag);
	SET_SQLVAL(pSQLColumn,SQL_DISDEBLK_INDEX,pEncCxt->nAvc_disableDeblk);
	SET_SQLVAL(pSQLColumn,SQL_DEBLKALPHA_INDEX,pEncCxt->nAvc_deblkFilterOffsetAlpha);
	SET_SQLVAL(pSQLColumn,SQL_DEBLKBETA_INDEX,pEncCxt->nAvc_deblkFilterOffsetBeta);
	SET_SQLVAL(pSQLColumn,SQL_DEBLKCHROMAQPOFF_INDEX,pEncCxt->nAvc_chromaQpOffset);

	SET_SQLVAL(pSQLColumn,SQL_AUDENABLE_INDEX,pEncCxt->nAvc_audEnable);
	SET_SQLVAL(pSQLColumn,SQL_FMOENABLE_INDEX,pEncCxt->nAvc_fmoEnable);
	SET_SQLVAL(pSQLColumn,SQL_FMOSLICENUM_INDEX,pEncCxt->nAvc_fmoSliceNum);
	SET_SQLVAL(pSQLColumn,SQL_FMOTYPE_INDEX,pEncCxt->nAvc_fmoType);

	/*H.263 related*/
	SET_SQLVAL(pSQLColumn,SQL_ANNEXJENABLE_INDEX,pEncCxt->nH263_annexJEnable);
	SET_SQLVAL(pSQLColumn,SQL_ANNEXKENABLE_INDEX,pEncCxt->nH263_annexKEnable);
	SET_SQLVAL(pSQLColumn,SQL_ANNEXTENABLE_INDEX,pEncCxt->nH263_annexTEnable);

	/*MPEG4 related*/	
	SET_SQLVAL(pSQLColumn,SQL_DPENABLE_INDEX,pEncCxt->nMp4_dataPartitionEnable);
	SET_SQLVAL(pSQLColumn,SQL_RVLCENABLE_INDEX,pEncCxt->nMp4_reversibleVlcEnable);
	SET_SQLVAL(pSQLColumn,SQL_INTRADCVLCTHR_INDEX,pEncCxt->nMp4_intraDcVlcThr);
	SET_SQLVAL(pSQLColumn,SQL_HECENABLE_INDEX,pEncCxt->nMp4_hecEnable);
	SET_SQLVAL(pSQLColumn,SQL_VERID_INDEX,pEncCxt->nMp4_verid);

	return noerr;
}

int DBLookupItem(EncIOParams * pIOParams,EncContxt* pEncCxt,char* pParamIDstr,SQLiteColumn* pSQLColumn,int* nExist)
{
	//WARNING: we should not use local array[] to restore string !!!, since the local arrary[] is located in current function stack
	int nIsExist=0;
	SQLiteColumn sqliteSelect[2];
	int noerr=1;

	//set input columns firstly
	noerr=DBSetInputColumn(pIOParams, pEncCxt, pSQLColumn);

	//generate param id based on input columns
	pParamIDstr[0]='\0';
	noerr=DBGenParamIDStringBasedInputColumn(pParamIDstr,pSQLColumn,MAX_LINE_BYTES);
	if(0==noerr)
	{
		APP_ERROR_PRINTF("generate param id failure \r\n");
		goto EXIT;
	}
	//set param_id column
	noerr=DBSetParamIDColumn(pParamIDstr, pSQLColumn);
	//seach whether current item is exist: select two columns: name and param_id
	DBCopy(sqliteSelect,pSQLColumn,0,SQL_NAME_INDEX);
	DBCopy(sqliteSelect,pSQLColumn,1,SQL_PARAM_ID_INDEX);
	nIsExist=0;
	noerr=SQLiteNodeIsExist(pIOParams->database,pIOParams->table,pSQLColumn, 2, &nIsExist);

	if(0==noerr)
	{
		APP_ERROR_PRINTF("%s: search item failure \r\n",__FUNCTION__);
		goto EXIT;
	}

	if(1==nIsExist)
	{
		APP_DEBUG_PRINTF("the item %s is already exist \r\n",(char*)sqliteSelect[1].pVal);
	}

EXIT:
	*nExist=nIsExist;
	return noerr;
}

int DBInsertItem(EncIOParams * pIOParams,EncContxt* pEncCxt,EncStatisInfo* pStatisticInfo,SQLiteColumn* pSQLColumn)
{
	int noerr=1;
	int nIsExist=0;
	
#ifdef PRE_LOOKUP_DB	
	//already lookup the item before
	nIsExist=0;
#else
	char paramIDstr[MAX_LINE_BYTES];
	noerr=DBSetGeneralColumn(pIOParams, pEncCxt, pSQLColumn);
	noerr=DBLookupItem(pIOParams, pEncCxt,paramIDstr,pSQLColumn, &nIsExist);
	if(0==noerr)
	{
		APP_ERROR_PRINTF("%s: search item failure \r\n",__FUNCTION__);
		return noerr;
	}
#endif
	noerr=DBSetStatisticColumn(pStatisticInfo, pSQLColumn);

	if(0==nIsExist)
	{
		//insert item into database
		noerr=SQLiteInsertNode(pIOParams->database,pIOParams->table,pSQLColumn,SQL_MAX_INDEX);
	}
	else
	{
		//avoid insert the same item.		
	}
	
	return noerr;
}

int LogFrameInfo(FILE* fp, double * pVal,int cnt)
{
	int i;
	for(i=0;i<cnt;i++)
	{
		fprintf(fp,"    [%d]: %.2f \r\n",i+1,pVal[i]);
	}
	return 1;
}

int LogStatisticSummaryInfo(FILE* fp,EncContxt* pEncCxt,EncStatisInfo* pStatisticInfo)
{
	//summary info
	fprintf(fp,"summary info: \r\n");
	fprintf(fp,"    frame numbers:\t\t%d\r\n",pEncCxt->nFrameNum);
	fprintf(fp,"    frame rate(fps):\t%d\r\n",pEncCxt->nFrameRate);
	fprintf(fp,"    bitrate(kbps):\t\t%d\r\n",pEncCxt->nBitRate);	
	fprintf(fp,"    qp:\t\t\t\t\t\t\t\t%d\r\n",pEncCxt->nQuantParam);
	fprintf(fp,"\r\n");
	fprintf(fp,"    average compressed rate:\t%.2f\r\n",pStatisticInfo->avgCompressedRate);	
	fprintf(fp,"    average frame bytes:\t\t\t%.2f\r\n",pStatisticInfo->frmAvgBytes);
	fprintf(fp,"    min frame bytes:\t\t\t\t\t%.2f\r\n",pStatisticInfo->frmMinBytes);
	fprintf(fp,"    max frame bytes:\t\t\t\t\t%.2f\r\n",pStatisticInfo->frmMaxBytes);	
	fprintf(fp,"\r\n");
	fprintf(fp,"    average encode time(us):\t%.2f\r\n",pStatisticInfo->avgTime);
	fprintf(fp,"    min encode time(us):\t\t\t%.2f\r\n",pStatisticInfo->frmMinTime);
	fprintf(fp,"    max encode time(us):\t\t\t%.2f\r\n",pStatisticInfo->frmMaxTime);	
	fprintf(fp,"\r\n");
	fprintf(fp,"    average bitrate(kbps):\t%.2f\r\n",pStatisticInfo->avgKBPS);
	fprintf(fp,"    min bitrate(kbps):\t\t\t%.2f\r\n",pStatisticInfo->minKBPS);
	fprintf(fp,"    max bitrate(kbps):\t\t\t%.2f\r\n",pStatisticInfo->maxKBPS);
	fprintf(fp,"\r\n");
	fprintf(fp,"    average PSNR[Y,U,V]:\t[%.2f,%.2f,%.2f]\r\n",pStatisticInfo->frmAvgPSNR[0],pStatisticInfo->frmAvgPSNR[1],pStatisticInfo->frmAvgPSNR[2]);
	fprintf(fp,"    min PSNR[Y,U,V]:\t\t\t[%.2f,%.2f,%.2f]\r\n",pStatisticInfo->frmMinPSNR[0],pStatisticInfo->frmMinPSNR[1],pStatisticInfo->frmMinPSNR[2]);
	fprintf(fp,"    max PSNR[Y,U,V]:\t\t\t[%.2f,%.2f,%.2f]\r\n",pStatisticInfo->frmMaxPSNR[0],pStatisticInfo->frmMaxPSNR[1],pStatisticInfo->frmMaxPSNR[2]);	
	fprintf(fp,"\r\n");

	return 1;
}

int LogStatisticInfo(EncContxt* pEncCxt,EncStatisInfo* pStatisticInfo,char* outfile)
{
	FILE* fp;
	char logfile[NAME_SIZE];
	int noerr=1;

	FILE* fpAllCases=NULL;	
	char allCases[NAME_SIZE]="test_cases.log";
	static int caseCnt=1;

	sprintf(logfile,"%s.log",outfile);
	fp = fopen(logfile, "wb");
	if(fp==NULL)
	{
		APP_ERROR_PRINTF("can not open log file %s.\n",logfile);
		noerr=0;
		goto EXIT;
	}

	if(1==caseCnt)
	{
		remove(allCases);
	}
	
	fpAllCases = fopen(allCases, "a+wb");
	if(fpAllCases==NULL)
	{
		APP_ERROR_PRINTF("can not open summary log file %s.\n",allCases);
		noerr=0;
		goto EXIT;
	}
	
	//summary info
	fprintf(fpAllCases,"========== test case[%d]: kbps: %d, qp: %d, fps: %d ==========\r\n",caseCnt++,pEncCxt->nBitRate,pEncCxt->nQuantParam,pEncCxt->nFrameRate);
	LogStatisticSummaryInfo(fpAllCases, pEncCxt, pStatisticInfo);

	LogStatisticSummaryInfo(fp, pEncCxt, pStatisticInfo);

	//frame bps info
	fprintf(fp,"frame bytes info: \r\n");
	LogFrameInfo(fp,pStatisticInfo->frmBytes,pStatisticInfo->frmCnt);
	//frame PSNR info
	fprintf(fp,"frame PSNR info: [Y]: \r\n");
	LogFrameInfo(fp,pStatisticInfo->frmPSNR[0],pStatisticInfo->frmCnt);	
	fprintf(fp,"frame PSNR info: [U]: \r\n");
	LogFrameInfo(fp,pStatisticInfo->frmPSNR[1],pStatisticInfo->frmCnt);	
	fprintf(fp,"frame PSNR info: [V]: \r\n");
	LogFrameInfo(fp,pStatisticInfo->frmPSNR[2],pStatisticInfo->frmCnt);		
	
EXIT:
	if(fp)
	{
		fclose(fp);
	}
	if(fpAllCases)
	{
		fclose(fpAllCases);
	}
	return noerr;
}

int CallBack_EncOneFrameOK(void* pApp,unsigned char* pFrmBits,int frameBytes)
{
	EncStatisInfo * pStatisticInfo;
	//static int frmCnt=0;
	//frmCnt++;
	//APP_DEBUG_PRINTF("[%d]:  length: %d \r\n",frmCnt,frameBytes);

	//record statistic info array
	pStatisticInfo=(EncStatisInfo *)pApp;
	if(pStatisticInfo->frmCnt<MAX_FRM_NUMS)
	{
		pStatisticInfo->frmBytes[pStatisticInfo->frmCnt]=frameBytes;

		//CallBack_EncOneFrameOK() should be called after CallBack_EncOneFrameBeg()/CallBack_EncOneFrameEnd()
		pStatisticInfo->frmCnt++;
	}	
	
	return 1;
}

int CallBack_EncOneFrameBeg(void* pApp)
{
	EncStatisInfo * pStatisticInfo;

	//record statistic info array
	pStatisticInfo=(EncStatisInfo *)pApp;
	if(pStatisticInfo->frmCnt<MAX_FRM_NUMS)
	{
		TimerGet(&pStatisticInfo->tm_beg);
	}	
	
	return 1;
}

int CallBack_EncOneFrameEnd(void* pApp)
{
	EncStatisInfo * pStatisticInfo;

	//record statistic info array
	pStatisticInfo=(EncStatisInfo *)pApp;
	if(pStatisticInfo->frmCnt<MAX_FRM_NUMS)
	{
		double diff;
		TimerGet(&pStatisticInfo->tm_end);
		TimerDiffUs(&pStatisticInfo->tm_beg, &pStatisticInfo->tm_end, &diff);
		pStatisticInfo->frmTime[pStatisticInfo->frmCnt]=diff;
	}	
	
	return 1;
}

int ConvertDecCodecFormat(int encCodec, int* pDecCodec)
{
	switch (encCodec)
	{
		case 0:	/*MPEG4*/
			*pDecCodec=2;
			break;
		case 1:	/*H263*/
			*pDecCodec=7;
			break;			
		case 2:	/*H264*/
			*pDecCodec=8;
			break;
		default:
			*pDecCodec=2;
			return 0;			
	}
	return 1;
}

int ConvertOutFileNameCodecRelated(EncIOParams * pIOParams,EncContxt* pEncCxt,char* pOutFileNameCodecRelated)
{
	int noerr=1;
	switch(pIOParams->codec)		
	{
		case 0:	/*MPEG4*/
			sprintf(pOutFileNameCodecRelated,"_dp%d_rvlc%d_dcvlc%d_hec%d_verid%d",
				pEncCxt->nMp4_dataPartitionEnable,pEncCxt->nMp4_reversibleVlcEnable,
				pEncCxt->nMp4_intraDcVlcThr,pEncCxt->nMp4_hecEnable,
				pEncCxt->nMp4_verid);
			break;
		case 1:	/*H263*/
			sprintf(pOutFileNameCodecRelated,"_annexj%d_annexk%d_annext%d",
				pEncCxt->nH263_annexJEnable,pEncCxt->nH263_annexKEnable,
				pEncCxt->nH263_annexTEnable);
			break;			
		case 2:	/*H264*/
			sprintf(pOutFileNameCodecRelated,"_16Only%d_IP%d_DisDblk%d_Alpha%d_Beta%d_CQPOff%d_aud%d_fmo%d_%d_%d",
				pEncCxt->nAvc_Intra16x16Only,
				pEncCxt->nAvc_constrainedIntraPredFlag,pEncCxt->nAvc_disableDeblk,pEncCxt->nAvc_deblkFilterOffsetAlpha,
				pEncCxt->nAvc_deblkFilterOffsetBeta,pEncCxt->nAvc_chromaQpOffset,
				pEncCxt->nAvc_audEnable,pEncCxt->nAvc_fmoEnable,pEncCxt->nAvc_fmoSliceNum,pEncCxt->nAvc_fmoType);
			break;
		default:
			return 0;	
	}
	return noerr;
}

int ConvertOutputFileName(EncIOParams * pIOParams,EncContxt* pEncCxt,char* pOutFileName)
{
	int noerr=1;
	
	char filename_delim[]=".";
	char filename_temp[NAME_SIZE];
	char filename_codecrelated[NAME_SIZE];
	char* pfilename_first;
	char* pfilename_suffix;
	char filename_default_suffix[]=".bits";
	char filename_default[]="temp.bits";
	char* pCodeName=gpCodecName[pIOParams->codec];

	if(pIOParams->saveBitstream)
	{
		//user have set '-o' option
		strcpy(filename_temp,pIOParams->outfile); 
		pfilename_first=strtok(filename_temp,filename_delim);
		pfilename_suffix=strstr(pIOParams->outfile,filename_delim);
	}
	else
	{
		//use "temp.bits"
		strcpy(filename_temp,filename_default); 
		pfilename_first=strtok(filename_temp,filename_delim);
		pfilename_suffix=strstr(filename_default,filename_delim);			
	}
	if(NULL==pfilename_suffix)
	{
		pfilename_suffix=filename_default_suffix;
	}
	filename_codecrelated[0]='\0';	//clear null firstly

	ConvertOutFileNameCodecRelated(pIOParams, pEncCxt,filename_codecrelated);
	APP_DEBUG_PRINTF("file name: %s: =>  first name: %s , suffix: %s \r\n",pIOParams->outfile,pfilename_first,pfilename_suffix);
#ifdef OUT_SHORT_FILE_NAME
	sprintf(pOutFileName,"%s_%s_%d_%d_%dgop_%dfps_%dkbps_%dqp_advance(%d_%d_%d_%d_%d_%d_%d_%d_%d_%d_%d_%d_%d_%d_%d_%d_%d_%d)%s%s",pfilename_first,pCodeName,
		pEncCxt->nPicWidth,pEncCxt->nPicHeight,pEncCxt->nGOPSize,
		pEncCxt->nFrameRate,pEncCxt->nBitRate,pEncCxt->nQuantParam,
		pEncCxt->nChromaInterleave,pEncCxt->nEnableAutoSkip,pEncCxt->nMirror,pEncCxt->nRotAngle,
		pEncCxt->nInitialDelay,pEncCxt->nVbvBufSize,pEncCxt->nSliceMode,pEncCxt->nSliceSizeMode,
		pEncCxt->nSliceSize,pEncCxt->nIntraRefresh,pEncCxt->nRcIntraQp,pEncCxt->nUserQPMin,
		pEncCxt->nUserQPMax,pEncCxt->nUserQPMinEnable,pEncCxt->nUserQPMaxEnable,
		pEncCxt->nUserGamma,pEncCxt->nRcIntervalMode,pEncCxt->nMBInterval,
		filename_codecrelated,pfilename_suffix);	
#else
	sprintf(pOutFileName,"%s_%s_%d_%d_%dgop_%dfps_%dkbps_%dqp_%dchromleave_%dautoskip_%dmirror_%drotate_%ddelay_%dvbv_%dslicem_%dslicesm_%dslices_%dintrafresh_%drcintraqp_%dqpmin_%dqpmax_%dqpmine_%dqpmaxe_%dgamma_%drcinter_%dmbinter%s%s",pfilename_first,pCodeName,
		pEncCxt->nPicWidth,pEncCxt->nPicHeight,pEncCxt->nGOPSize,
		pEncCxt->nFrameRate,pEncCxt->nBitRate,pEncCxt->nQuantParam,
		pEncCxt->nChromaInterleave,pEncCxt->nEnableAutoSkip,pEncCxt->nMirror,pEncCxt->nRotAngle,
		pEncCxt->nInitialDelay,pEncCxt->nVbvBufSize,pEncCxt->nSliceMode,pEncCxt->nSliceSizeMode,
		pEncCxt->nSliceSize,pEncCxt->nIntraRefresh,pEncCxt->nRcIntraQp,pEncCxt->nUserQPMin,
		pEncCxt->nUserQPMax,pEncCxt->nUserQPMinEnable,pEncCxt->nUserQPMaxEnable,
		pEncCxt->nUserGamma,pEncCxt->nRcIntervalMode,pEncCxt->nMBInterval,
		filename_codecrelated,pfilename_suffix);
#endif
	return noerr;
}

int compute_BufSSIM(unsigned char* pRef, unsigned char* pDut, int Width, int Height, EncStatisInfo* pStatisticInfo,int yuv,int frmCnt)
{
#define SSIM_WINDOW_WIDTH   8
#define SSIM_WINDOW_HEIGHT  8
#define SSIM_STEP_HORI      1
#define SSIM_STEP_VERT      1

	static const double K1 = 0.01, K2 = 0.03;
	//static float max_pix_value_sqd;
	double C1, C2;
	double flWindowSize = (double) (SSIM_WINDOW_WIDTH * SSIM_WINDOW_HEIGHT);
	double flSSIMMB, flMeanRef, flMeanCur;
	double flVarRef, flVarCur, flCovRefCur;
	int iMeanRef, iMeanCur, iVarRef, iVarCur, iCovRefCur;
	double flSSIMFrm = 0.0;
	//unsigned char * pRef[3];
	int iPixCur, iPixRef;
	int iStrideCur, iStrideRef;
	//int iPaddingRef;
	int i, j, n, m, iWndCnt = 0;

	iStrideCur = Width;//iWidthMB + ( iPadding    << 1 );
	iStrideRef  = Width;//iWidthMB + ( iPaddingRef << 1 );

	C1 = K1 * K1 * 255 * 255;
	C2 = K2 * K2 * 255 * 255;

	for ( j = 0; j <= Height - SSIM_WINDOW_HEIGHT; j += SSIM_STEP_VERT )
	{
		for ( i = 0; i <= Width - SSIM_WINDOW_WIDTH; i += SSIM_STEP_HORI )
		{
			unsigned char * pWndRef = pRef + j * iStrideRef + i;
			unsigned char * pWndCur = pDut + j * iStrideCur + i;
			iMeanRef = 0;
			iMeanCur = 0; 
			iVarRef  = 0;
			iVarCur  = 0;
			iCovRefCur = 0;

			for ( n = j; n < j + SSIM_WINDOW_HEIGHT; n++ )
			{
				for ( m = i; m < i + SSIM_WINDOW_WIDTH; m++ )
				{
					iPixRef     = pWndRef[m];
					iPixCur     = pWndCur[m];
					iMeanRef   += iPixRef;
					iMeanCur   += iPixCur;
					iVarRef    += iPixRef * iPixRef;
					iVarCur    += iPixCur * iPixCur;
					iCovRefCur += iPixRef * iPixCur;
				}
				pWndCur += iStrideCur;
				pWndRef += iStrideRef;
			}

			flMeanRef = (double) iMeanRef / flWindowSize;
			flMeanCur = (double) iMeanCur / flWindowSize;

			flVarRef    = ((double)iVarRef - ((double)iMeanRef) * flMeanRef) / flWindowSize;
			flVarCur    = ((double)iVarCur - ((double)iMeanCur) * flMeanCur) / flWindowSize;
			flCovRefCur = ((double)iCovRefCur - ((double)iMeanRef) * flMeanCur) / flWindowSize;

			flSSIMMB  = (double) ((2.0 * flMeanRef * flMeanCur + C1) * (2.0 * flCovRefCur + C2));
			flSSIMMB /= (double) (flMeanRef * flMeanRef + flMeanCur * flMeanCur + C1) * (flVarRef + flVarCur + C2);

			flSSIMFrm += flSSIMMB;
			iWndCnt++;
		}
	}

	flSSIMFrm /= (double)iWndCnt;

	if (flSSIMFrm >= 1.0 && flSSIMFrm < 1.01) // avoid float accuracy problem at very low QP(e.g.2)
	{
		flSSIMFrm = 1.0;
	}
	//APP_DEBUG_PRINTF("%d: %d ssim: %.2f (0x%X, 0x%X)\r\n",frmCnt,yuv,flSSIMFrm,pRef,pDut);
	pStatisticInfo->frmSSIM[yuv][frmCnt]=flSSIMFrm;
	return 1;
}



int compute_BufPSNR(unsigned char* pRef, unsigned char* pDut, int Width, int Height, EncStatisInfo* pStatisticInfo,int yuv,int frmCnt)
{
	int i,j;
	double psnr=0;
	double sse=0;
	double picSize;
	unsigned char* pCurRef;
	unsigned char* pCurDut;
	int pixRef;
	int pixDut;

	pCurRef=pRef;
	pCurDut=pDut;

	//compute sse
	for(i=0;i<Height;i++)
	{
		for(j=0;j<Width;j++)
		{
			pixRef=pCurRef[j];
			pixDut=pCurDut[j];
#if 1			
			sse+=(double)((pixDut-pixRef)*(pixDut-pixRef));
#else
			if(pixDut>=pixRef)
			{
				sse+=(double)(pixDut*pixDut-pixRef*pixRef);
			}
			else
			{
				sse+=(double)(pixRef*pixRef-pixDut*pixDut);
			}
#endif			
		}
		pCurDut+=Width;
		pCurRef+=Width;
	}

	//compute psnr
	picSize=(double)Width*Height;
	//expression: MSE=sse/picSize; psnr = 10*log10(255*255/MSE)
	psnr=(0 == sse) ? 100 :(10 * log10(((double)(255*255*picSize))/sse));	

	//APP_DEBUG_PRINTF("%d: %d psnr: %.2f (0x%X, 0x%X)\r\n",frmCnt,yuv,psnr,pRef,pDut);
	pStatisticInfo->frmPSNR[yuv][frmCnt]=psnr;

	return 1;
}


int compute_BufSNR(unsigned char* pRef, unsigned char* pDut, int Width, int Height,EncStatisInfo* pStatisticInfo,int yuv,int frmCnt)
{
	int i, j;
	double v1, s1, s2, e2;
	double v; /* variance */
	double e; /* MSE */
	unsigned char* pCurRef;
	unsigned char* pCurDut;

	pCurRef=pRef;
	pCurDut=pDut;

	s1 = s2 = e2 = 0.0;

	for (j=0; j<Height; j++)
	{
		for (i=0; i<Width; i++)
		{
			v1 = pCurDut[i];
			s1+= v1;
			s2+= v1*v1;
			v1-= pCurRef[i];
			e2+= v1*v1;
		}
		pCurDut+=Width;
		pCurRef+=Width;		
	}

	s1 /= Width*Height;
	s2 /= Width*Height;
	e2 /= Width*Height;

	/* since e2 will be used as the denumerator in a future equation,
	make sure it does not equal zero, but some small arbitrary factor 
	*/
	if(e2==0.0)
	{
		e2 = 0.00000001;
	}

	v = s2 - s1*s1; /* variance */
	e = e2;         /* MSE */

	pStatisticInfo->frmPSNR[yuv][frmCnt]=10.0*log10(v/e);
	return 1;
}


int compute_YUVFileQuality(FILE* fpRefYUV, FILE* fpDutYUV, int frmWidth, int frmHeight, int frmCnt,EncStatisInfo* pStatisticInfo)
{
	int i;
	int YUVSize;
	int YSize;
	int UVSize;
	unsigned char* pRefYUV=NULL;
	unsigned char* pDutYUV=NULL;
	unsigned char* pRef=NULL;
	unsigned char* pDut=NULL;	
	int noerr=1;
	int readbytes=0;

	//allocate yuv buffers
	YSize=frmWidth*frmHeight;
	UVSize=YSize/4;
	YUVSize=YSize+UVSize*2;
	pRefYUV=malloc(2*YUVSize);
	if(NULL==pRefYUV)
	{
		APP_ERROR_PRINTF("%s: allocat buffer failure: size: %d \r\n",__FUNCTION__,2*YUVSize);
		noerr=0;
		goto EXIT;
	}
	pDutYUV=pRefYUV+YUVSize;

	for(i=0;i<frmCnt;i++)
	{
		//read file into yuv buffers
		readbytes=fread(pRefYUV,1,YUVSize,fpRefYUV);
		if(readbytes!=YUVSize)
		{
			APP_ERROR_PRINTF("%s: read ref yuv file failure:  \r\n",__FUNCTION__);
			noerr=0;
			goto EXIT;		
		}
		readbytes=fread(pDutYUV,1,YUVSize,fpDutYUV);
		if(readbytes!=YUVSize)
		{
			APP_ERROR_PRINTF("%s: read dut yuv file failure:  cnt: %d, readsize: %d \r\n",__FUNCTION__,i,readbytes);
			noerr=0;
			goto EXIT;		
		}
		//compute psnr per frame
		pRef=pRefYUV;
		pDut=pDutYUV;
		compute_BufPSNR(pRef, pDut, frmWidth, frmHeight, pStatisticInfo,0,i);	//Y: PSNR
		compute_BufSSIM(pRef, pDut, frmWidth, frmHeight, pStatisticInfo,0,i);	//Y: SSIM
		pRef=pRefYUV+YSize;
		pDut=pDutYUV+YSize;
		compute_BufPSNR(pRef, pDut, frmWidth/2, frmHeight/2, pStatisticInfo,1,i);	//Cb: PSNR
		compute_BufSSIM(pRef, pDut, frmWidth/2, frmHeight/2, pStatisticInfo,1,i);	//Cb: SSIM
		pRef=pRefYUV+YSize+UVSize;
		pDut=pDutYUV+YSize+UVSize;
		compute_BufPSNR(pRef, pDut, frmWidth/2, frmHeight/2, pStatisticInfo,2,i);	//Cr: PSNR
		compute_BufSSIM(pRef, pDut, frmWidth/2, frmHeight/2, pStatisticInfo,2,i);	//Cr: SSIM
	}


EXIT:
	if(pRefYUV)
	{
		free(pRefYUV);
	}
	return noerr;
}

int compute_quality(FILE* fpRefYUV, FILE* fpInBits, int frmWidth, int frmHeight, int frmCnt,int codec,EncStatisInfo* pStatisticInfo,int fb)
{
	FILE* fout=NULL;
	DecContxt decCxt;
	char temp_outfile[]="dec_dut_temp.yuv";
	int noerr=1;

	fout = fopen(temp_outfile, "wb+");
	if(NULL==fout)
	{
		APP_ERROR_PRINTF("can not open temporary decoder output file %s.\n", temp_outfile);
		noerr=0;
		goto EXIT;
	}

	//set decode parameters
	memset(&decCxt,0,sizeof(DecContxt));
	decCxt.fin=fpInBits;
	decCxt.fout=fout;
	decCxt.nMaxNum=MAX_FRM_NUMS;
	if(fb<0)
	{
		decCxt.nDisplay=0;
		decCxt.nFbNo=0;
	}
	else
	{
		decCxt.nDisplay=1;
		decCxt.nFbNo=fb;
	}
	decCxt.nCodec=codec;
	//decCxt.nInWidth=
	//decCxt.nInHeight=
	decCxt.nSkipMode=0;
	decCxt.nRepeatNum=0;
	decCxt.nUnitDataSize=DEFAULT_DEC_FILL_DATA_UNIT;
	decCxt.nUintDataNum=DEFAULT_DEC_FILL_DATA_NUM;	
	noerr=decode_stream(&decCxt);
	if(0!=decCxt.nErr)
	{
		APP_ERROR_PRINTF("%s: decode stream failure \r\n",__FUNCTION__);
		noerr=0;
		goto EXIT;
	}		

	//compute PSNR
	fflush(fout);
	fseek(fout,0,SEEK_SET);	// dut yuv
	noerr=compute_YUVFileQuality(fpRefYUV, fout, frmWidth, frmHeight, frmCnt, pStatisticInfo);
	if(0==noerr)
	{
		APP_ERROR_PRINTF("compute yuv frame PNSR failure: dut file: %s \r\n",temp_outfile);
	}
EXIT:

	if(fout)
	{
		fclose(fout);
	}

	//delete temporary yuv file
	remove(temp_outfile);
	
	return noerr;
}

int ParamSetDefault(EncParamsRange* pParamNode,EncParamsIndexTable* pDefaultTable)
{
	int noerr=1;
	int index;
	EncParamsRange* pRange;
	int i;
	
	for(i=0;i<PARAM_MAX_INDEX;i++)
	{
		pRange=&pDefaultTable->defaultval;
		index=pDefaultTable->index;
		SET_PARAMNODE(pParamNode[index],pRange->beg,pRange->end,pRange->step);
		//APP_DEBUG_PRINTF("%d: beg: %d, end: %d, step: %d \r\n",index,pRange->beg,pRange->end,pRange->step);
		pDefaultTable++;
	}

	return noerr;
}

int ParamIsNotValid(EncIOParams * pIOParams,EncContxt* pEncCxt)
{
	int notValid=0;
	int qpbeg;
	int qpend;
	
	if(2==pIOParams->codec)
	{
		/*H.264*/
		qpbeg=0;
		qpend=51;
	}
	else
	{
		/*other codec*/
		qpbeg=1;
		qpend=31;		
	}

	if(0==pEncCxt->nBitRate)
	{
		/* no rate control */
		//qp is meaningful
		if((pEncCxt->nQuantParam<qpbeg) ||(pEncCxt->nQuantParam>qpend))
		{
			notValid=1;
			goto EXIT;
		}
		//qp min/max/enable/... is no meaning
	}
	else
	{
		/* rate control */
		//qp is not meaning
		//qp min/max/enable and RcIntraQp is meaningful
		if(2!=pIOParams->codec)
		{
			/*not H.264*/
			if((0==pEncCxt->nUserQPMin)||(0==pEncCxt->nUserQPMax)
				|| (31<pEncCxt->nUserQPMin)||(31<pEncCxt->nUserQPMax))
			{
				notValid=1;
				goto EXIT;
			}
			if((0==pEncCxt->nRcIntraQp)||(31<pEncCxt->nRcIntraQp))
			{
				notValid=1;
				goto EXIT;
			}
		}
	}
EXIT:	
	return notValid;
}

int ParserconfigScriptFindIndex(char* name,EncParamsIndexTable* pTable)
{
	int index;
	int i;

	index=-1;
	for(i=0;i<PARAM_MAX_INDEX;i++)
	{
		if(strlen(name)==strlen(pTable->name))
		{
			if (0==strncmp(name, pTable->name, strlen(name)))
			{
				index=i;
				break;
			}
		}
		pTable++;
	}
	
	return index;
}

int ParserConfigScriptInsertValue(EncParamsRange* pParamNode,EncParamsIndexTable* pTable,char* name,int beg,int end,int step)
{
	int noerr=1;
	int index;

	index=ParserconfigScriptFindIndex(name,pTable);
	if(-1!=index)
	{
		//find valid name
		APP_DEBUG_PRINTF("set index: %d \r\n",index);
		SET_PARAMNODE(pParamNode[index],beg,end,step);
	}
	else
	{
		//unknown name, do nothing
	}
	
	return noerr;
}

int ParserConfigScriptParam(char* scriptFileName,EncParamsRange* pParamNode,EncParamsIndexTable* pTable)
{
	int noerr=1;
	FILE* fp=NULL;
	char line[MAX_LINE_BYTES];
	char delim[]=":";	
	char line_tmp[MAX_LINE_BYTES];
	char* name;
	char* value;
	
	int beg,end,step;
	
	fp=fopen(scriptFileName,"rb");
	if(NULL==fp)
	{
		APP_ERROR_PRINTF("can not open script file: %s \r\n",scriptFileName);
		noerr=0;
		goto EXIT;
	}

	while(fgets(line,sizeof(line),fp))
	{
		if(COMMENT_CHAR==line[0])
		{
			//APP_DEBUG_PRINTF("comment line: %s \r\n",line);
			continue;
		}
		name=NULL;
		value=NULL;
		beg=0;
		end=0;
		step=0;
		/*for example:
		line:  "id:[beg,end],step"
		name="id"
		value=":[beg,end],step"
		*/
		strcpy(line_tmp,line); 
		name=strtok(line_tmp,delim);
		value=strstr(line,delim);
		if((NULL==name) || (NULL==value))
		{
			//null line or unvalid line
			continue;
			//APP_DEBUG_PRINTF("unsupported format line: %s \r\n",line);
			//noerr=0;
			//goto EXIT;
		}
		//sscanf(value,"%d,%d,%d",&beg,&end,&step);
		sscanf(value,":[%d,%d],%d",&beg,&end,&step);
		APP_DEBUG_PRINTF("name:%s: range: [%d,%d], step: %d \r\n",name,beg,end,step);
		ParserConfigScriptInsertValue(pParamNode,pTable,name,beg, end, step);
	}

EXIT:	
	if(fp)
	{
		fclose(fp);
	}
	return noerr;
}

int test_case(EncIOParams * pIOParams,EncContxt* pEncCxt,SQLiteColumn* pSQLColumn)
{
	FILE* fout=NULL;
	FILE* fin=NULL;
	int noerr=1;
	EncStatisInfo encStatisInfo;	
	char outFile[NAME_SIZE];
	int decCodec;
	int fbnum=-1;

#ifdef PRE_LOOKUP_DB
	char paramIDstr[MAX_LINE_BYTES];
	if(pIOParams->db)
	{
		int nExist=0;
		//if user set db option, the existed case will not be run.
		//if user only want to output some info, he should not set db option.
		noerr=DBSetGeneralColumn(pIOParams, pEncCxt, pSQLColumn);
		noerr=DBLookupItem(pIOParams,pEncCxt,paramIDstr,pSQLColumn,&nExist);
		if(0==noerr)
		{
			APP_ERROR_PRINTF("lookup db failure \r\n");
			return noerr;
		}
		if(1==nExist)
		{
			//the test case is already exist in db, skip current test case
			APP_DEBUG_PRINTF("this case is already exist in db, skip it \r\n");
			return noerr;
		}
	}
#endif

	//open in/out files
	fin = fopen(pIOParams->infile, "rb");
	if(fin==NULL)
	{
		APP_ERROR_PRINTF("can not open input file %s.\n",pIOParams->infile);
		noerr=0;
		goto EXIT;
	}

	//convert output filename 
	ConvertOutputFileName(pIOParams, pEncCxt, outFile);
	//open output file
	fout = fopen(outFile, "wb+");
	if(NULL==fout)
	{
		APP_ERROR_PRINTF("can not open output file %s.\n", outFile);
		noerr=0;
		goto EXIT;
	}
		
	//clear statistic info
	memset(&encStatisInfo,0,sizeof(EncStatisInfo));
	pEncCxt->pApp=&encStatisInfo;	
	pEncCxt->pfOneFrameOk=CallBack_EncOneFrameOK;
	pEncCxt->pfOneFrameBeg=CallBack_EncOneFrameBeg;
	pEncCxt->pfOneFrameEnd=CallBack_EncOneFrameEnd;
	
	//encode
	pEncCxt->fin=fin;
	pEncCxt->fout=fout;

	noerr=encode_stream(pEncCxt);
	if(0!=pEncCxt->nErr)
	{
		noerr=0;
		goto EXIT;
	}

	APP_DEBUG_PRINTF("Encoded Frame Num: %d,  [width x height] = [%d x %d] \r\n",pEncCxt->nFrameNum,pEncCxt->nPicWidth,pEncCxt->nPicHeight);

	//decode and compute PSNR per frame
	noerr=ConvertDecCodecFormat(pIOParams->codec,&decCodec);
	if(0==noerr)
	{
		APP_ERROR_PRINTF("unsupported decode codec: %d \r\n",pIOParams->codec);
		noerr=0;
		goto EXIT;
	}

	fseek(fin,0,SEEK_SET);		// ref YUV 
	fflush(fout);
	fseek(fout,0,SEEK_SET);	// dut bitstream

	if(pIOParams->display)
	{
		fbnum=pIOParams->fbno;
	}
	noerr=compute_quality(fin, fout,pEncCxt->nPicWidth,pEncCxt->nPicHeight,pEncCxt->nFrameNum,decCodec,&encStatisInfo,fbnum);
	if(0==noerr)
	{
		APP_ERROR_PRINTF("compute psnr failure \r\n");
		noerr=0;
		goto EXIT;
	}

	//compute average statistic info
	ComputeStatisticInfo(&encStatisInfo,pEncCxt);
	//APP_DEBUG_PRINTF("frame rate: %d, bitrate: %d, qp: %d \r\n",pEncCxt->nFrameRate,pEncCxt->nBitRate,pEncCxt->nQuantParam);
	//APP_DEBUG_PRINTF("average bitrate(kbps): %.2f , average PSNR: [%.2f,%.2f,%.2f] \r\n",encStatisInfo.avgKBPS,encStatisInfo.frmAvgPSNR[0],encStatisInfo.frmAvgPSNR[1],encStatisInfo.frmAvgPSNR[2]);
	//APP_DEBUG_PRINTF("min frame bytes: %.2f , min PSNR: [%.2f,%.2f,%.2f] \r\n",encStatisInfo.frmMinBytes,encStatisInfo.frmMinPSNR[0],encStatisInfo.frmMinPSNR[1],encStatisInfo.frmMinPSNR[2]);
	//APP_DEBUG_PRINTF("max frame bytes: %.2f , max PSNR: [%.2f,%.2f,%.2f] \r\n",encStatisInfo.frmMaxBytes,encStatisInfo.frmMaxPSNR[0],encStatisInfo.frmMaxPSNR[1],encStatisInfo.frmMaxPSNR[2]);	

	APP_DEBUG_PRINTF("summary info: \r\n");
	APP_DEBUG_PRINTF("    frame number:            %d\r\n",pEncCxt->nFrameNum);	
	APP_DEBUG_PRINTF("    frame rate(fps):         %d\r\n",pEncCxt->nFrameRate);
	APP_DEBUG_PRINTF("    bitrate(kbps):           %d\r\n",pEncCxt->nBitRate);	
	APP_DEBUG_PRINTF("    qp:                      %d\r\n",pEncCxt->nQuantParam);
	APP_DEBUG_PRINTF("\r\n");
	APP_DEBUG_PRINTF("    average compressed rate: %.2f\r\n",encStatisInfo.avgCompressedRate);
	APP_DEBUG_PRINTF("    average frame bytes:     %.2f\r\n",encStatisInfo.frmAvgBytes);
	APP_DEBUG_PRINTF("    min frame bytes:         %.2f\r\n",encStatisInfo.frmMinBytes);
	APP_DEBUG_PRINTF("    max frame bytes:         %.2f\r\n",encStatisInfo.frmMaxBytes);	
	APP_DEBUG_PRINTF("\r\n");
	APP_DEBUG_PRINTF("    average encode time(us): %.2f\r\n",encStatisInfo.avgTime);
	APP_DEBUG_PRINTF("    min encode time(us):     %.2f\r\n",encStatisInfo.frmMinTime);
	APP_DEBUG_PRINTF("    max encode time(us):     %.2f\r\n",encStatisInfo.frmMaxTime);	
	APP_DEBUG_PRINTF("\r\n");
	APP_DEBUG_PRINTF("    average bitrate(kbps): %.2f\r\n",encStatisInfo.avgKBPS);
	APP_DEBUG_PRINTF("    min bitrate(kbps):     %.2f\r\n",encStatisInfo.minKBPS);
	APP_DEBUG_PRINTF("    max bitrate(kbps):     %.2f\r\n",encStatisInfo.maxKBPS);
	APP_DEBUG_PRINTF("\r\n");
	APP_DEBUG_PRINTF("    average PSNR[Y,U,V]: [%.2f,%.2f,%.2f]\r\n",encStatisInfo.frmAvgPSNR[0],encStatisInfo.frmAvgPSNR[1],encStatisInfo.frmAvgPSNR[2]);
	APP_DEBUG_PRINTF("    min PSNR[Y,U,V]:     [%.2f,%.2f,%.2f]\r\n",encStatisInfo.frmMinPSNR[0],encStatisInfo.frmMinPSNR[1],encStatisInfo.frmMinPSNR[2]);
	APP_DEBUG_PRINTF("    max PSNR[Y,U,V]:     [%.2f,%.2f,%.2f]\r\n",encStatisInfo.frmMaxPSNR[0],encStatisInfo.frmMaxPSNR[1],encStatisInfo.frmMaxPSNR[2]);	
	APP_DEBUG_PRINTF("\r\n");
	APP_DEBUG_PRINTF("    average SSIM[Y,U,V]: [%.2f,%.2f,%.2f]\r\n",encStatisInfo.frmAvgSSIM[0],encStatisInfo.frmAvgSSIM[1],encStatisInfo.frmAvgSSIM[2]);
	APP_DEBUG_PRINTF("    min SSIM[Y,U,V]:     [%.2f,%.2f,%.2f]\r\n",encStatisInfo.frmMinSSIM[0],encStatisInfo.frmMinSSIM[1],encStatisInfo.frmMinSSIM[2]);
	APP_DEBUG_PRINTF("    max SSIM[Y,U,V]:     [%.2f,%.2f,%.2f]\r\n",encStatisInfo.frmMaxSSIM[0],encStatisInfo.frmMaxSSIM[1],encStatisInfo.frmMaxSSIM[2]);	
	APP_DEBUG_PRINTF("\r\n");

	if(pIOParams->log)
	{
		noerr=LogStatisticInfo(pEncCxt,&encStatisInfo,outFile);
	}

	if(pIOParams->db)
	{
		//insert item into database
		noerr=DBInsertItem(pIOParams, pEncCxt, &encStatisInfo,pSQLColumn);
	}

EXIT:

	//release 
	if(fout)
	{
		fclose(fout);
	}
	if(fin)
	{
		fclose(fin);
	}	

	//delete outfile if user don't set '-o' options
	if(0==pIOParams->saveBitstream)
	{
		APP_DEBUG_PRINTF("remove temporary bitstream file: %s \r\n",outFile);
		remove(outFile);
	}
	APP_DEBUG_PRINTF("\r\n\r\n\r\n");
	return noerr;
}

int test_caseMPEG4(EncIOParams * pIOParams,EncContxt* pEncCxt,EncParamsRange* pParamNode,int* pCaseCnt,SQLiteColumn* pSQLColumn)
{
	int noerr=1;
	int dataPartitionEnable;
	int reversibleVlcEnable;
	int intraDcVlcThr;
	int hecEnable;
	int verid;	
	int nCaseCnt=*pCaseCnt;

	FOR_LOOP(dataPartitionEnable,pParamNode[PARAM_MPEG4_DPENABLE_INDEX])
	{
		FOR_LOOP(reversibleVlcEnable,pParamNode[PARAM_MPEG4_RVLCENABLE_INDEX])
		{
			FOR_LOOP(intraDcVlcThr,pParamNode[PARAM_MPEG4_INTRADCVLCTHR_INDEX])
			{
				FOR_LOOP(hecEnable,pParamNode[PARAM_MPEG4_HECENABLE_INDEX])
				{
					FOR_LOOP(verid,pParamNode[PARAM_MPEG4_VERID_INDEX])
					{
						pEncCxt->nMp4_dataPartitionEnable=dataPartitionEnable;
						pEncCxt->nMp4_reversibleVlcEnable=reversibleVlcEnable;
						pEncCxt->nMp4_intraDcVlcThr=intraDcVlcThr;
						pEncCxt->nMp4_hecEnable=hecEnable;
						pEncCxt->nMp4_verid=verid;
						APP_DEBUG_PRINTF("test MPEG4 case[%d]: (%d,%d,%d,%d,%d) \r\n",
							nCaseCnt,pEncCxt->nMp4_dataPartitionEnable,pEncCxt->nMp4_reversibleVlcEnable,pEncCxt->nMp4_intraDcVlcThr,pEncCxt->nMp4_hecEnable,pEncCxt->nMp4_verid);
						noerr=test_case(pIOParams,pEncCxt,pSQLColumn);
						if(0==noerr)
						{
							APP_ERROR_PRINTF("MPEG4 test case error: cnt: %d \r\n",nCaseCnt);
							goto EXIT;
						}
						if(nCaseCnt>=pIOParams->maxcase)
						{
							//APP_DEBUG_PRINTF("reach max cases number: %d \r\n",nCaseCnt);
							goto EXIT;
						}
						nCaseCnt++;
					}
				}
			}
		}
	}
	
EXIT:
	*pCaseCnt=nCaseCnt;
	return noerr;
}

int test_caseH263(EncIOParams * pIOParams,EncContxt* pEncCxt,EncParamsRange* pParamNode,int* pCaseCnt,SQLiteColumn* pSQLColumn)
{
	int noerr=1;
	int annexj;
	int annexk;
	int annext;
	int nCaseCnt=*pCaseCnt;

	FOR_LOOP(annexj,pParamNode[PARAM_H263_ANNEXJENABLE_INDEX])
	{
		FOR_LOOP(annexk,pParamNode[PARAM_H263_ANNEXKENABLE_INDEX])
		{
			FOR_LOOP(annext,pParamNode[PARAM_H263_ANNEXTENABLE_INDEX])
			{
				pEncCxt->nH263_annexJEnable=annexj;
				pEncCxt->nH263_annexKEnable=annexk;
				pEncCxt->nH263_annexTEnable=annext;
				APP_DEBUG_PRINTF("test H263 case[%d]: annex (J,K,T)=(%d,%d,%d) \r\n",
					nCaseCnt,pEncCxt->nH263_annexJEnable,pEncCxt->nH263_annexKEnable,pEncCxt->nH263_annexTEnable);
				noerr=test_case(pIOParams,pEncCxt,pSQLColumn);
				if(0==noerr)
				{
					APP_ERROR_PRINTF("H.263 test case error: cnt: %d \r\n",nCaseCnt);
					goto EXIT;
				}
				if(nCaseCnt>=pIOParams->maxcase)
				{
					//APP_DEBUG_PRINTF("reach max cases number: %d \r\n",nCaseCnt);
					goto EXIT;
				}
				nCaseCnt++;
			}
		}
	}
	
EXIT:
	*pCaseCnt=nCaseCnt;
	return noerr;
}

int test_caseH264(EncIOParams * pIOParams,EncContxt* pEncCxt,EncParamsRange* pParamNode,int* pCaseCnt,SQLiteColumn* pSQLColumn)
{
	int noerr=1;
	int n16x16only;
	int nPredFlag;
	int nDeblk;
	int nOffsetAlpha;
	int nOffsetBeta;
	int nQpOffset;
	int nAudEnable;
	int nFMOEnable;
	int nFMOSliceNum;
	int nFMOType;	
	int nCaseCnt=*pCaseCnt;

	int nOffsetAlphaBase;
	int nOffsetBetaBase;
	int nFMOSliceNumBase;
	int nFMOTypeBase;

	FOR_LOOP(nAudEnable,pParamNode[PARAM_AVC_AUDENABLE_INDEX]){
	FOR_LOOP(nFMOEnable,pParamNode[PARAM_AVC_FMOENABLE_INDEX]){
	if(nFMOEnable==0)
	{
		//fmo setting is no meaning
		nFMOSliceNumBase=pParamNode[PARAM_AVC_FMOSLICENUM_INDEX].end;
		nFMOTypeBase=pParamNode[PARAM_AVC_FMOTYPE_INDEX].end;
	}
	else
	{
		//fmo setting is meaningful
		nFMOSliceNumBase=pParamNode[PARAM_AVC_FMOSLICENUM_INDEX].beg;
		nFMOTypeBase=pParamNode[PARAM_AVC_FMOTYPE_INDEX].beg;
	}
	FOR_LOOP_BEG(nFMOSliceNum,nFMOSliceNumBase,pParamNode[PARAM_AVC_FMOSLICENUM_INDEX]){
	FOR_LOOP_BEG(nFMOType,nFMOTypeBase,pParamNode[PARAM_AVC_FMOTYPE_INDEX]){	
	FOR_LOOP(n16x16only,pParamNode[PARAM_AVC_INTRA16X16ONLY_INDEX])
	{
		FOR_LOOP(nPredFlag,pParamNode[PARAM_AVC_IPREDFLAG_INDEX])
		{
			FOR_LOOP(nDeblk,pParamNode[PARAM_AVC_DISDBLK_INDEX])
			{
				if(1==nDeblk)
				{
					/*disable deblock*/
					//alpha/beta is no meaning
					nOffsetAlphaBase=pParamNode[PARAM_AVC_DBLKALPHA_INDEX].end;
					nOffsetBetaBase=pParamNode[PARAM_AVC_DBLKBETA_INDEX].end;
				}
				else
				{
					/*enable deblock or disable at slice boundaries*/
					//alpha/beta is meaningful
					nOffsetAlphaBase=pParamNode[PARAM_AVC_DBLKALPHA_INDEX].beg;
					nOffsetBetaBase=pParamNode[PARAM_AVC_DBLKBETA_INDEX].beg;					
				}
				FOR_LOOP_BEG(nOffsetAlpha,nOffsetAlphaBase,pParamNode[PARAM_AVC_DBLKALPHA_INDEX])
				{
					FOR_LOOP_BEG(nOffsetBeta,nOffsetBetaBase,pParamNode[PARAM_AVC_DBLKBETA_INDEX])
					{
						FOR_LOOP(nQpOffset,pParamNode[PARAM_AVC_DBLKCHROMQPOFF_INDEX])
						{
							pEncCxt->nAvc_Intra16x16Only=n16x16only;
							pEncCxt->nAvc_constrainedIntraPredFlag=nPredFlag;
							pEncCxt->nAvc_disableDeblk=nDeblk;
							pEncCxt->nAvc_deblkFilterOffsetAlpha=nOffsetAlpha;
							pEncCxt->nAvc_deblkFilterOffsetBeta=nOffsetBeta;
							pEncCxt->nAvc_chromaQpOffset=nQpOffset;
							pEncCxt->nAvc_audEnable=nAudEnable;
							pEncCxt->nAvc_fmoEnable=nFMOEnable;
							pEncCxt->nAvc_fmoSliceNum=nFMOSliceNum;
							pEncCxt->nAvc_fmoType=nFMOType;
							APP_DEBUG_PRINTF("test H264 case[%d]: kbps: %d, qp: %d, fps: %d, rcIntra: %d, gamma: %d, 16only:%d, pred: %d, deblk: %d, alpha: %d, beta: %d, chromQP: %d, aud: %d, fmo(%d,%d,%d) \r\n",
								nCaseCnt,pEncCxt->nBitRate,pEncCxt->nQuantParam,pEncCxt->nFrameRate,
								pEncCxt->nRcIntraQp,pEncCxt->nUserGamma,
								pEncCxt->nAvc_Intra16x16Only,pEncCxt->nAvc_constrainedIntraPredFlag,pEncCxt->nAvc_disableDeblk,
								pEncCxt->nAvc_deblkFilterOffsetAlpha,pEncCxt->nAvc_deblkFilterOffsetBeta,pEncCxt->nAvc_chromaQpOffset,
								pEncCxt->nAvc_audEnable,pEncCxt->nAvc_fmoEnable,pEncCxt->nAvc_fmoSliceNum,pEncCxt->nAvc_fmoType);
							noerr=test_case(pIOParams,pEncCxt,pSQLColumn);
							if(0==noerr)
							{
								APP_ERROR_PRINTF("H.264 test case error: cnt: %d \r\n",nCaseCnt);
								goto EXIT;
							}
							if(nCaseCnt>=pIOParams->maxcase)
							{
								//APP_DEBUG_PRINTF("reach max cases number: %d \r\n",nCaseCnt);
								goto EXIT;
							}
							nCaseCnt++;
						}
					}
				}
			}
		}
	}
	}}}}
	
EXIT:
	*pCaseCnt=nCaseCnt;
	return noerr;
}

int auto_test(EncIOParams * pIOParams)
{
	int noerr=1;
	EncContxt encContxt;	
	EncParamsRange encParamNode[PARAM_MAX_INDEX];
	SQLiteColumn sQLColumn[SQL_MAX_INDEX];
	
	int fr;
	int br;
	int qp;
	int gop;
	int initialdelay;
	int vbvbufsize;
	int slicemode;
	int slicesizemode;
	int slicesize;
	int intrarefresh;
	int rcintraqp;
	int userqpmin;
	int userqpmax;
	int userqpminenable;
	int userqpmaxenable;
	int gamma;
	int rcintervalmode;
	int mbinterval;
	int chromainterleave;
	int autoskip;
	int mirror;
	int rotate;
	
	int caseCnt=1;
	int qpBase;
	int rcintraqpBase;
	int qpminBase;
	int qpmaxBase;
	int qpminenableBase;
	int qpmaxenableBase;
	int gammaBase;
	int rcintervalmodeBase;
	int mbintervalBase;
	int autoskipBase;
	
	int slicesizemodeBase;
	int slicesizeBase;

	EncParamsIndexTable* pTable;
	pTable=&gParamsIndexTable[0];

	//set default parameters
	memset(encParamNode,0,sizeof(encParamNode));
	ParamSetDefault(encParamNode,pTable);

	//replace params with specified by script file
	if(pIOParams->usingConfig)
	{		
		noerr=ParserConfigScriptParam(pIOParams->script,encParamNode,pTable);
		if(0==noerr)
		{
			APP_ERROR_PRINTF("parser script failure: %s \r\n",pIOParams->script);
			goto EXIT;
		}
	}

	//init column
	noerr=DBInitColumn(sQLColumn);
	if(0==noerr)
	{
		APP_ERROR_PRINTF("init sqlite column failure: \r\n");
		goto EXIT;
	}

	//APP_DEBUG_PRINTF("bitstep: %d, qpstep: %d,  fstep: %d \r\n",pIOParams->bitStep,pIOParams->qpStep,pIOParams->fStep);
	//encode test loop

	FOR_LOOP(gop,encParamNode[PARAM_GOPSIZE_INDEX])
	{
		FOR_LOOP(br,encParamNode[PARAM_KBPS_INDEX])
		{
			if(br==0)
			{
				/*no rate control*/
				//qp is meaningful
				qpBase=encParamNode[PARAM_QP_INDEX].beg;
				//rcintraqp is no meaning
				rcintraqpBase=encParamNode[PARAM_RCINTRAQP_INDEX].end;
				//qp min/max/enable is no meaning
				qpminBase=encParamNode[PARAM_USERQPMIN_INDEX].end;
				qpminenableBase=encParamNode[PARAM_USERQPMINENABLE_INDEX].end;
				qpmaxBase=encParamNode[PARAM_USERQPMAX_INDEX].end;
				qpmaxenableBase=encParamNode[PARAM_USERQPMAXENABLE_INDEX].end;
				//gamma is no meaning
				gammaBase=encParamNode[PARAM_GAMMA_INDEX].end;	
				//rcintervalmode is no meaning
				rcintervalmodeBase=encParamNode[PARAM_RCINTERVALMODE_INDEX].end;
				//auto skip is no meaning
				autoskipBase=encParamNode[PARAM_ENABLEAUTOSKIP_INDEX].end;
			}
			else
			{
				/*rate control */
				//qp is no meaning
				qpBase=encParamNode[PARAM_QP_INDEX].end;
				//rcintraqp is meaningful
				rcintraqpBase=encParamNode[PARAM_RCINTRAQP_INDEX].beg;
				//qp min/max/enable is meaningful
				qpminBase=encParamNode[PARAM_USERQPMIN_INDEX].beg;
				qpminenableBase=encParamNode[PARAM_USERQPMINENABLE_INDEX].beg;
				qpmaxBase=encParamNode[PARAM_USERQPMAX_INDEX].beg;
				qpmaxenableBase=encParamNode[PARAM_USERQPMAXENABLE_INDEX].beg;
				//gamma is meaningful
				gammaBase=encParamNode[PARAM_GAMMA_INDEX].beg;
				//rcintervalmode is meaningful
				rcintervalmodeBase=encParamNode[PARAM_RCINTERVALMODE_INDEX].beg;
				//auto skip is meaningful
				autoskipBase=encParamNode[PARAM_ENABLEAUTOSKIP_INDEX].beg;
			}
			FOR_LOOP_BEG(qp, qpBase, encParamNode[PARAM_QP_INDEX])
			{
				FOR_LOOP(fr,encParamNode[PARAM_FPS_INDEX])
				{

					FOR_LOOP(initialdelay,encParamNode[PARAM_INITIALDELAY_INDEX]){
					FOR_LOOP(vbvbufsize,encParamNode[PARAM_VBVBUFSIZE_INDEX]){
					FOR_LOOP(slicemode,encParamNode[PARAM_SLICEMODE_INDEX]){
					if(0==slicemode)
					{
						//slice mode/size is no meaning
						slicesizemodeBase=encParamNode[PARAM_SLICESIZEMODE_INDEX].end;
						slicesizeBase=encParamNode[PARAM_SLICESIZE_INDEX].end;
					}
					else
					{
						//slice mode/size is meaningful
						slicesizemodeBase=encParamNode[PARAM_SLICESIZEMODE_INDEX].beg;
						slicesizeBase=encParamNode[PARAM_SLICESIZE_INDEX].beg;
					}
					FOR_LOOP_BEG(slicesizemode,slicesizemodeBase,encParamNode[PARAM_SLICESIZEMODE_INDEX]){
					FOR_LOOP_BEG(slicesize,slicesizeBase,encParamNode[PARAM_SLICESIZE_INDEX]){
					FOR_LOOP(intrarefresh,encParamNode[PARAM_INTRAREFRESH_INDEX]){
					FOR_LOOP_BEG(rcintraqp,rcintraqpBase,encParamNode[PARAM_RCINTRAQP_INDEX]){
					FOR_LOOP(chromainterleave,encParamNode[PARAM_CHROMAINTERLEAVE_INDEX]){

					FOR_LOOP_BEG(userqpmin, qpminBase, encParamNode[PARAM_USERQPMIN_INDEX]){
					FOR_LOOP_BEG(userqpmax, qpmaxBase, encParamNode[PARAM_USERQPMAX_INDEX]){
					FOR_LOOP_BEG(userqpminenable, qpminenableBase, encParamNode[PARAM_USERQPMINENABLE_INDEX]){
					FOR_LOOP_BEG(userqpmaxenable, qpmaxenableBase, encParamNode[PARAM_USERQPMAXENABLE_INDEX]){
					FOR_LOOP_BEG(gamma,gammaBase,encParamNode[PARAM_GAMMA_INDEX]){

					FOR_LOOP_BEG(rcintervalmode,rcintervalmodeBase,encParamNode[PARAM_RCINTERVALMODE_INDEX]){
					if((3==rcintervalmode)&&(br!=0))
					{
						//mbinterval is meaningful
						mbintervalBase=encParamNode[PARAM_MBINTERVAL_INDEX].beg;
					}
					else
					{
						//mbinterval is no meaning
						mbintervalBase=encParamNode[PARAM_MBINTERVAL_INDEX].end;
					}
					FOR_LOOP_BEG(mbinterval,mbintervalBase,encParamNode[PARAM_MBINTERVAL_INDEX]){
					FOR_LOOP_BEG(autoskip,autoskipBase,encParamNode[PARAM_ENABLEAUTOSKIP_INDEX]){	
					FOR_LOOP(mirror,encParamNode[PARAM_MIRROR_INDEX]){
					FOR_LOOP(rotate,encParamNode[PARAM_ROTATE_INDEX]){						
					//clear 0
					memset(&encContxt,0,sizeof(EncContxt));

					//set encode parameters
					encContxt.nMaxNum=pIOParams->maxnum;
					encContxt.nCodec=pIOParams->codec;
					encContxt.nPicWidth=pIOParams->width;
					encContxt.nPicHeight=pIOParams->height;
					encContxt.nRotAngle=rotate;//pIOParams->rotation;
					encContxt.nFrameRate=fr;
					encContxt.nBitRate=br;
					encContxt.nGOPSize=gop;
					encContxt.nChromaInterleave=chromainterleave;
					encContxt.nMirror=mirror;
					encContxt.nQuantParam=qp;

					encContxt.nInitialDelay=initialdelay;
					encContxt.nVbvBufSize=vbvbufsize;
					encContxt.nSliceMode=slicemode;
					encContxt.nSliceSizeMode=slicesizemode;
					encContxt.nSliceSize=slicesize;
					encContxt.nIntraRefresh=intrarefresh;
					encContxt.nRcIntraQp=rcintraqp;
					encContxt.nUserQPMin=userqpmin;
					encContxt.nUserQPMax=userqpmax;
					encContxt.nUserQPMinEnable=userqpminenable;
					encContxt.nUserQPMaxEnable=userqpmaxenable;
					encContxt.nUserGamma=gamma;
					encContxt.nRcIntervalMode=rcintervalmode;
					encContxt.nMBInterval=mbinterval;
					encContxt.nEnableAutoSkip=autoskip;

					if(1==ParamIsNotValid(pIOParams,&encContxt))
					{
						//skip current test case
						APP_DEBUG_PRINTF("current case is not valid, skip it \r\n");
						continue;
					}

					if(0==pIOParams->codec)
					{
						noerr=test_caseMPEG4(pIOParams,&encContxt,encParamNode,&caseCnt,sQLColumn);
					}				
					else if(1==pIOParams->codec)
					{
						noerr=test_caseH263(pIOParams,&encContxt,encParamNode,&caseCnt,sQLColumn);
					}				
					else if(2==pIOParams->codec)
					{
						noerr=test_caseH264(pIOParams,&encContxt,encParamNode,&caseCnt,sQLColumn);
					}
					else
					{
#if 1					
						APP_ERROR_PRINTF("unknown format: %d  \r\n",pIOParams->codec);
						noerr=0;
						goto EXIT;
#else						
						APP_DEBUG_PRINTF("======= test case[%d]: kbps: %d, qp: %d, fps: %d, rcIntra: %d, gamma: %d =======\r\n",
							caseCnt,encContxt.nBitRate,encContxt.nQuantParam,encContxt.nFrameRate,
							encContxt.nRcIntraQp,encContxt.nUserGamma);
						noerr=test_case(pIOParams,&encContxt,sQLColumn);
						caseCnt++;
#endif						
					}
					if(0==noerr)
					{
						//APP_ERROR_PRINTF("%s: item test error: frame rate: %d, bitrate: %d,  qp: %d, rcIntra: %d, gamma: %d  \r\n",__FUNCTION__,
						//	encContxt.nFrameRate,encContxt.nBitRate,encContxt.nQuantParam,
						//	encContxt.nRcIntraQp,encContxt.nUserGamma);
						APP_ERROR_PRINTF("%s: all public parameters: \
							gop(%d),br(%d),qp(%d),fr(%d),initialdelay(%d),\
							vbvbufsize(%d),slicemode(%d),slicesizemode(%d),slicesize(%d),intrarefresh(%d),\
							rcintraqp(%d),chromainterleave(%d),userqpmin(%d),userqpmax(%d),userqpminenable(%d),\
							userqpmaxenable(%d),gamma(%d),rcintervalmode(%d),mbinterval(%d),autoskip(%d),\
							mirror(%d),rotate(%d) \r\n",__FUNCTION__,
							gop,br,qp,fr,initialdelay,vbvbufsize,slicemode,slicesizemode,slicesize,intrarefresh,rcintraqp,chromainterleave,
							userqpmin,userqpmax,userqpminenable,userqpmaxenable,gamma,
							rcintervalmode,mbinterval,autoskip,mirror,rotate);
						goto EXIT;
					}
					if(caseCnt>=pIOParams->maxcase)
					{
						APP_DEBUG_PRINTF("reach max cases number: %d \r\n",caseCnt);
						goto EXIT;
					}							
					}}}}}}}}}}}}}}}}}}
				}//fps
			}
		}
	}	//gop size

EXIT:
	return noerr;
}

int main(int argc, char **argv)
{
	EncIOParams ioParams;	

	int noerr=1;

	// Defaults: 0
	memset(&ioParams,0,sizeof(EncIOParams));
	// set maxnum to infinity
	ioParams.maxnum = MAX_FRM_NUMS;//0x7FFFFFFF;
	ioParams.maxcase = 0x7FFFFFFF;
#if 0
	ioParams.bitStep=4;
	ioParams.qpStep=5;//10;
	ioParams.fStep=3;

	ioParams.avcDeblkOffsetAlphaStep=4;
	ioParams.avcDeblkOffsetBetaStep=4;
	ioParams.avcChromaQpOffsetStep=8;
#endif
	//get input from user
	GetUserInput(&ioParams, argc, argv);

	noerr=CheckIOParams(&ioParams);
	if(0==noerr)
	{
		APP_ERROR_PRINTF("parameters is not correct \r\n");
		goto EXIT;
	}

	APP_DEBUG_PRINTF("input YUV file[%d x %d] : %s \r\n",ioParams.width,ioParams.height,ioParams.infile);
	APP_DEBUG_PRINTF("max encoded frame numbers : %d  \r\n",ioParams.maxnum);
	//APP_DEBUG_PRINTF("bit rate step: %d  \r\n",ioParams.bitStep);
	//APP_DEBUG_PRINTF("quantization step: %d  \r\n",ioParams.qpStep);
	//APP_DEBUG_PRINTF("frame rate step: %d  \r\n",ioParams.fStep);
	
	noerr=auto_test(&ioParams);

	if(0==noerr)
	{
		APP_ERROR_PRINTF("Encode Test %s Failure \r\n",ioParams.infile);
	}
	else
	{
		APP_DEBUG_PRINTF("Encode Test %s OK  \r\n",ioParams.infile);
	}

EXIT:
	return 1;
}

