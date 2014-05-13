/*
 *  Copyright (c) 2010-2014, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

/*
 *	Vpu_wrapper.h
 *	header file contain all related vpu interface info
 *	History :
 *	Date	(y.m.d)		Author			Version			Description
 *	2010-09-07		eagle zhou		0.1				Created
 *	2011-02-17		eagle zhou		0.2				Add encoder part
 *	2011-12-22		eagle zhou		1.0				refine api
 *	2012-01-**		eagle zhou		1.0.*			add new features: including tile format,etc
 */

#ifndef VPU_WRAPPER_H
#define VPU_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**************************** version info ***********************************/
#define VPU_WRAPPER_VERSION(major, minor, release)	 \
	(((major) << 16) + ((minor) << 8) + (release))
#define VPU_WRAPPER_VERSION_CODE	VPU_WRAPPER_VERSION(1, 0, 49)

/**************************** decoder part **********************************/

#define VPU_DEC_MAX_NUM_MEM_REQS 2

//typedef RetCode VpuRetCode;
//typedef vpu_versioninfo VpuVersionInfo;
//typedef DecHandle VpuDecHandle;
typedef unsigned int VpuDecHandle;
//typedef DecOpenParam VpuDecOpenParam;
//typedef DecInitialInfo VpuSeqInfo;
//typedef FrameBuffer VpuFrameBuffer;
//typedef DecBufInfo VpuDecBufInfo;
//typedef CodecCommand VpuCodecCommand;
//typedef DecParam VpuDecParam;

typedef enum {
	VPU_DEC_ERR_UNFOUND=0,
	VPU_DEC_ERR_NOT_SUPPORTED, /*the profile/level/features/... outrange the vpu's capability*/
	VPU_DEC_ERR_CORRUPT, /*some syntax errors are detected*/
}VpuDecErrInfo;

typedef enum {
	VPU_V_MPEG4 = 0,
	VPU_V_DIVX3,
	VPU_V_DIVX4,
	VPU_V_DIVX56,
	VPU_V_XVID,
	VPU_V_H263,
	VPU_V_AVC,
	VPU_V_AVC_MVC,
	VPU_V_VC1,
	VPU_V_VC1_AP,
	VPU_V_MPEG2,
	VPU_V_RV,
	VPU_V_MJPG,
	VPU_V_AVS,
	VPU_V_VP8,
} VpuCodStd;

typedef enum {
	VPU_DEC_SKIPNONE=0,
	VPU_DEC_SKIPPB,
	VPU_DEC_SKIPB,
	VPU_DEC_SKIPALL,
	VPU_DEC_ISEARCH,	/*only decode IDR*/
}VpuDecSkipMode;

typedef enum {
	VPU_DEC_IN_NORMAL=0,
	VPU_DEC_IN_KICK,
	VPU_DEC_IN_DRAIN,
}VpuDecInputType;

typedef enum {
	VPU_DEC_CONF_SKIPMODE=0,		/*parameter value: VPU_DEC_SKIPNONE(default); VPU_DEC_SKIPPB; VPU_DEC_SKIPB; VPU_DEC_SKIPALL;VPU_DEC_ISEARCH*/
	VPU_DEC_CONF_INPUTTYPE,		/*parameter value:
										VPU_DEC_IN_NOMAL: normal(default)
										VPU_DEC_IN_KICK: kick -- input data/size in VPU_DecDecodeBuf() will be ignored
										VPU_DEC_IN_DRAIN: drain -- stream reach end, and input data/size in VPU_DecDecodeBuf() will be ignored
									*/
	//VPU_DEC_CONF_BLOCK,
	//VPU_DEC_CONF_NONEBLOCK,
	VPU_DEC_CONF_BUFDELAY,		/*for stream mode:
									    parameter represent buf size(unit: bytes), buffer size==0 indicate no any delay*/
	VPU_DEC_CONF_INIT_CNT_THRESHOLD,  /*at seqinit stage: vpu will report error if data count reach the threshold*/
} VpuDecConfig;

typedef enum 
{
	VPU_DEC_RET_SUCCESS = 0,
	VPU_DEC_RET_FAILURE,
	VPU_DEC_RET_INVALID_PARAM,
	VPU_DEC_RET_INVALID_HANDLE,
	VPU_DEC_RET_INVALID_FRAME_BUFFER,
	VPU_DEC_RET_INSUFFICIENT_FRAME_BUFFERS,
	VPU_DEC_RET_INVALID_STRIDE,
	VPU_DEC_RET_WRONG_CALL_SEQUENCE,
	VPU_DEC_RET_FAILURE_TIMEOUT,
}VpuDecRetCode;

typedef enum
{
	/* bit[0]: input buf info */
	VPU_DEC_INPUT_NOT_USED=0x0,
	VPU_DEC_INPUT_USED=0x1,
	/* bit[1:6]: frame output info */
	VPU_DEC_OUTPUT_EOS=0x2,
	VPU_DEC_OUTPUT_DIS=0x4,			/*one frame is output*/
	VPU_DEC_OUTPUT_NODIS=0x8,		/*no frame output*/
	VPU_DEC_OUTPUT_REPEAT=0x10,		/*one frame is output repeatly: mainly for VC1 specification: user need to get one timestamp*/
	VPU_DEC_OUTPUT_DROPPED=0x20,	/*for unclose gop case: (1) drop B or (2) drop non-I frame: user need to get one timestamp*/
	VPU_DEC_OUTPUT_MOSAIC_DIS=0x40,	/*for unclose gop case: the frame will be output, but not dropped by decoder: user need to get one timestamp*/
	/* bit[7:8]: frame output info */
	VPU_DEC_NO_ENOUGH_BUF=0x80,		/*no enough frame buffer*/
	VPU_DEC_NO_ENOUGH_INBUF=0x100,	/*no enough input buffer: to avoid null run*/
	/* bit[9]: init output info */
	VPU_DEC_INIT_OK=0x200,			/*user need to call VPU_DecGetInitialInfo()*/
	/* bit[10]: skip decode */
	VPU_DEC_SKIP=0x400,				/*added for cases: need to get two time stamp*/
										/*not decoded: interlace or corrupt: user need to get one time stamp*/
	/*bit[11]: reserved to represent one frame is decoded*/
	VPU_DEC_ONE_FRM_CONSUMED=0x800,/*added for case: need to get decoded(or skipped,corrupt...) frame length*/
										/*user may call related api to get the decoded/skipped/.. frame related info*/
	/*bit[12]: reolution changed*/	
	VPU_DEC_RESOLUTION_CHANGED=0x1000,/*added for case: upward change in resolution*/
										/*user need to release all frames, call VPU_DecGetInitialInfo() and re-allocation/register frames according to new bigger resolution*/	
	/* bit[31]: flush is recommended */
	VPU_DEC_FLUSH=0x80000000,			/*for some clisps, special for h.264 TS stream(may has no IDR at all), the random start/seek point may introduce unrecoverable mosaic*/
}VpuDecBufRetCode;

typedef enum {
	VPU_DEC_CAP_FILEMODE=0,	/* file mode is supported ? 0: not; 1: yes*/
	VPU_DEC_CAP_TILE,			/* tile format is supported ? 0: not; 1: yes*/
	VPU_DEC_CAP_FRAMESIZE,	/* reporting frame size  ? 0: not; 1: yes*/
	VPU_DEC_CAP_RESOLUTION_CHANGE, /*resolution change notification ? 0: not; 1: yes*/
}VpuDecCapability;

typedef enum 
{
	VPU_MEM_VIRT   = 0,    	/* 0 for virtual Memory */
	VPU_MEM_PHY    = 1,		/* 1 for physical continuous Memory */
}VpuMemType;

typedef enum
{
	VPU_I_PIC=0,			/*I frame or I sclie(H.264)*/
	VPU_P_PIC,				/*P frame or P sclie(H.264)*/
	VPU_B_PIC,				/*B frame or B sclie(H.264)*/
	VPU_IDR_PIC,			/*IDR frame(H.264)*/
	VPU_BI_PIC,				/*BI frame(VC1)*/
	VPU_SKIP_PIC,			/*Skipped frame(VC1)*/
	VPU_UNKNOWN_PIC,		/*reserved*/
}VpuPicType;

typedef enum
{
	VPU_FIELD_NONE=0,		/*frame*/
	VPU_FIELD_TOP,			/*only top field*/
	VPU_FIELD_BOTTOM,		/*only bottom field*/
	VPU_FIELD_TB,			/*top field + bottom field*/
	VPU_FIELD_BT,			/*bottom field + top field*/
	VPU_FIELD_UNKNOWN,	/*reserved*/
}VpuFieldType;

typedef struct {
	int nAlignment;			/* alignment limitation */
	int	nSize;				/* Size in bytes */
	VpuMemType MemType; /* Flag to indicate Static, Scratch or output data memory */
	unsigned char* pVirtAddr;		/* virtual address:Pointer to the base memory , which will be allocated and filled by the application*/
	unsigned char* pPhyAddr;		/* physical address: Pointer to the base memory , which will be allocated and filled by the application*/

	int nReserved[3];				/*reserved for future extension*/
} VpuMemSubBlockInfo;

typedef struct{
	int nSubBlockNum;
	VpuMemSubBlockInfo MemSubBlock[VPU_DEC_MAX_NUM_MEM_REQS];
}VpuMemInfo;


typedef struct 
{
	int nFwMajor;		/* firmware major version */
	int nFwMinor;		/* firmware minor version */
	int nFwRelease;		/* firmware release version */
	int nFwCode;			/* firmware code version */
	int nLibMajor;		/* library major version */
	int nLibMinor;		/* library minor version */
	int nLibRelease;		/* library release version */
	int nReserved;		/*reserved for future extension*/
}VpuVersionInfo;

typedef struct 
{
	int nMajor;		/* major version */
	int nMinor;		/* minor version */
	int nRelease;		/* release version */
	char* pBinary;	/* version info specified by user(such as build time), below is one example in makefile:
					     CFLAGS+=-DUSER_SPECIFY_BINARY_VER -DSTR_USER_SPECIFY_BINARY_VER=\"binary version specified by user\"
					*/
	int nReserved[4];	/*reserved for future extension*/
}VpuWrapperVersionInfo;

typedef struct {
	VpuCodStd CodecFormat;
	//unsigned int bitstreamBuffer;
	//int bitstreamBufferSize;
	//int qpReport;
	//int mp4DeblkEnable;
	int nReorderEnable;
	int nChromaInterleave;	//should be set to 1 when (nMapType!=0)
	int nMapType;			//registered frame buffer type: 0--linear; 1--frame tile; 2--field tile
	int nTiled2LinearEnable;	//output frame(only valid when nMapType!=0) : 0--tile. eg. same with registered frame ; 1--linear(not supported)  
	//int filePlayEnable;
	int nPicWidth;
	int nPicHeight;
	//int dynamicAllocEnable;
	//int streamStartByteOffset;
	//int mjpg_thumbNailDecEnable;
	//unsigned int psSaveBuffer;
	//int psSaveBufferSize;
	//int mp4Class;
	//int block;
	int nEnableFileMode;

	int nReserved[3];			/*reserved for future extension*/
	void* pAppCxt;			/*reserved for future application extension*/
} VpuDecOpenParam;


typedef struct {
	/* stride info */
	unsigned int nStrideY;
	unsigned int nStrideC;

	/* physical address */
	unsigned char* pbufY;			//luma frame pointer or top field pointer(for field tile)
	unsigned char* pbufCb;		//chroma frame pointer or top field pointer(for field tile)
	unsigned char* pbufCr;
	unsigned char* pbufMvCol;
	unsigned char* pbufY_tilebot;	//for field tile: luma bottom pointer
	unsigned char* pbufCb_tilebot;	//for field tile: chroma bottom pointer
	//unsigned char* pbufCr_tilebot;	//not required since always enable interleave for tile

	/* virtual address */
	unsigned char* pbufVirtY;		//luma frame pointer or top field pointer(for field tile)
	unsigned char* pbufVirtCb;		//chroma frame pointer or top field pointer(for field tile)
	unsigned char* pbufVirtCr;
	unsigned char* pbufVirtMvCol;
	unsigned char* pbufVirtY_tilebot;	//for field tile: luma bottom pointer
	unsigned char* pbufVirtCb_tilebot;	//for field tile: chroma bottom pointer
	//unsigned char* pbufVirtCr_tilebot;	//not required since always enable interleave for tile

	int nReserved[5];				/*reserved for future extension*/
	void* pPrivate;				/*reserved for future special extension*/
} VpuFrameBuffer;

typedef struct {
	unsigned int nLeft;
	unsigned int nTop;
	unsigned int nRight;
	unsigned int nBottom;
} VpuRect;

//typedef struct {
//	unsigned int sliceSaveBuffer;
//	int sliceSaveBufferSize;
//} VpuDecAvcSliceBufInfo;

//typedef struct {
//	VpuDecAvcSliceBufInfo avcSliceBufInfo;
//} VpuDecBufInfo;


typedef struct {
	int nPicWidth;		// {(PicX+15)/16} * 16
	int nPicHeight;		// {(PicY+15)/16} * 16
	int nFrameRateRes;	// frameinfo: numerator.  <=0 represent invalid
	int nFrameRateDiv;	// frameinfo: denominator. <=0 represent invalid
	VpuRect PicCropRect;

	//int mp4_dataPartitionEnable;
	//int mp4_reversibleVlcEnable;
	//int mp4_shortVideoHeader;
	//int h263_annexJEnable;

	int nMinFrameBufferCount;
	//int frameBufDelay;
	//int nextDecodedIdxNum;
	//int normalSliceSize;
	//int worstSliceSize;
	//int mjpg_thumbNailEnable;
	int nMjpgSourceFormat;

	//int streamInfoObtained;
	//int profile;
	//int level;
	int nInterlace;
	//int constraint_set_flag[4];
	//int direct8x8Flag;
	//int vc1_psf;
	unsigned int nQ16ShiftWidthDivHeightRatio;	//fixed point for width/height: 1: 0x10000; 0.5: 0x8000;...
	//Uint32 errorcode;
	int nConsumedByte;		/*reserved to record sequence length: value -1 indicate unknow*/
	//DecReportBufSize reportBufSize;
	int nAddressAlignment;	/*address alignment for Y/Cb/Cr (unit: bytes)*/

	int nReserved[5];			/*reserved for future extension*/
	void* pSpecialInfo;		/*reserved for future special extension*/
} VpuDecInitInfo;

/*
typedef struct {
	//int prescanEnable;
	//int prescanMode;
	//int dispReorderBuf;
	//int iframeSearchEnable;
	int skipframeMode;
	//int skipframeNum;
#if 0  //move into 	VPU_DecDecBuf(..., VpuBufferNode* pInData,...)
	int chunkSize;
	int picStartByteOffset;
	unsigned int picStreamBufferAddr;
#endif	
} VpuDecParam;
*/

typedef struct {
	int nFrmWidth;			/*support dynamic resolution*/
	int nFrmHeight;			/*support dynamic resolution*/
	VpuRect FrmCropRect;	/*support dynamic resolution*/
	unsigned int nQ16ShiftWidthDivHeightRatio;	/*support dynamic ratio, refer to definition in struct 'VpuDecInitInfo'*/
	int nReserved[9];		/*reserved for recording other info*/
}VpuFrameExtInfo;

typedef struct {
	//int indexFrameDisplay;
	//int indexFrameDecoded;
	VpuFrameBuffer * pDisplayFrameBuf;
	//VpuFrameBuffer * pDecodedFrameBuf;
	//int NumDecFrameBuf;
	VpuPicType ePicType;
	//int numOfErrMBs;
	//Uint32 *qpInfo;
	//int hScaleFlag;
	//int vScaleFlag;
	//int indexFrameRangemap;
	//int prescanresult;
	//int notSufficientPsBuffer;
	//int notSufficientSliceBuffer;
	//int decodingSuccess;
	//int interlacedFrame;
	//int mp4PackedPBframe;
	//int h264Npf;

	//int pictureStructure;
	//int nTopFieldFirst;
	//int nRepeatFirstField;
	//union {
	//    int progressiveFrame;
	//    int vc1_repeatFrame;
	//};
	//int fieldSequence;

	//int decPicHeight;
	//int decPicWidth;
	//Rect decPicCrop;

	//DecReportInfo mbInfo;
	//DecReportInfo mvInfo;
	//DecReportInfo frameBufStat;
	//DecReportInfo userData;
	//int nConsumedByte;		/*reserved to record frame length: value -1 indicate unknow*/
	VpuFieldType	eFieldType;	/*added for user to implement deinterlace process*/
	int nMVCViewID;	/*used to indicate which view of MVC clips*/

	VpuFrameExtInfo * pExtInfo;	/*extended info: support dynamic resolution, ...*/
	int nReserved[2];			/*reserved for future extension*/
	void* pPrivate;			/*reserved for future special extension*/
} VpuDecOutFrameInfo;


typedef struct
{
	unsigned char* pData;		/*buffer virtual addr*/
	unsigned int nSize;		/*valid data length */	
}VpuCodecData;


typedef struct
{
	unsigned char* pPhyAddr;	/*buffer physical base addr*/
	unsigned char* pVirAddr;	/*buffer virtual base addr*/
	unsigned int nSize;		/*valid data length */
	VpuCodecData sCodecData;	/*private data specified by codec*/

	int nReserved[2];				/*reserved for future extension*/
	void* pPrivate;				/*reserved for future special extension*/
}VpuBufferNode;

typedef struct 
{
	int nSize;				/*!requested memory size */
	unsigned long nPhyAddr;	/*!physical memory address allocated */
	unsigned long nCpuAddr;	/*!cpu addr for system free usage */
	unsigned long nVirtAddr;	/*!virtual user space address */	
	int nReserved[4];			/*reserved for future extension*/
}VpuMemDesc;

typedef struct {
	VpuFrameBuffer* pFrame;	/*point to the frame buffer. if it is NULL, it represent the frame is skipped by vpu, but the other length info are still valid*/
	int nStuffLength;			/*stuff data length ahead of frame. If it is < 0, mean the config data contain some valid frames, user need to process this case carefully*/
	int nFrameLength;		/*valid frame length: should be > 0*/
	int nReserved[5];			/*reserved for recording other info*/
}VpuDecFrameLengthInfo;

/**************************** encoder part **********************************/

typedef unsigned int VpuEncHandle;

typedef enum
{
	/*the value comply with the vpu lib header file, don't change it !*/
	VPU_COLOR_420=0,
	VPU_COLOR_422H=1,
	VPU_COLOR_422V=2,
	VPU_COLOR_444=3,	
	VPU_COLOR_400=4,
}VpuColorFormat;

typedef enum {
	VPU_ENC_MIRDIR_NONE,
	VPU_ENC_MIRDIR_VER,
	VPU_ENC_MIRDIR_HOR,
	VPU_ENC_MIRDIR_HOR_VER
} VpuEncMirrorDirection;

typedef struct {
	int nMinFrameBufferCount;
	int nAddressAlignment;		/*address alignment for Y/Cb/Cr (unit: bytes)*/
} VpuEncInitInfo;

typedef enum 
{
	VPU_ENC_RET_SUCCESS = 0,
	VPU_ENC_RET_FAILURE,
	VPU_ENC_RET_INVALID_PARAM,
	VPU_ENC_RET_INVALID_HANDLE,
	VPU_ENC_RET_INVALID_FRAME_BUFFER,
	VPU_ENC_RET_INSUFFICIENT_FRAME_BUFFERS,
	VPU_ENC_RET_INVALID_STRIDE,
	VPU_ENC_RET_WRONG_CALL_SEQUENCE,
	VPU_ENC_RET_FAILURE_TIMEOUT,
}VpuEncRetCode;

typedef enum
{
	/* bit[0]: input buf info */
	VPU_ENC_INPUT_NOT_USED=0x0,
	VPU_ENC_INPUT_USED=0x1,
	/* bit[1:4]: frame output info */
	//VPU_ENC_OUTPUT_EOS=,
	VPU_ENC_OUTPUT_SEQHEADER=0x4,	/*sequence header(for H.264: SPS/PPS)*/
	VPU_ENC_OUTPUT_DIS=0x8,
	VPU_ENC_OUTPUT_NODIS=0x10,
	//VPU_ENC_OUTPUT_REPEAT=,	
}VpuEncBufRetCode;

typedef struct {
	VpuCodStd eFormat;
	int nPicWidth;
	int nPicHeight;	
	int nRotAngle;
	int nFrameRate;
	int nBitRate;				/*unit: kbps*/
	int nGOPSize;
	int nIntraRefresh;		/*intra macro block numbers*/
	int nIntraQP;				/*0: auto, >0: qp value*/
	int nChromaInterleave;	/*should be set to 1 when (nMapType!=0)*/
	VpuEncMirrorDirection sMirror;
	//int nQuantParam;
	int nMapType;			/*frame buffer: 0--linear ; 1--frame tile; 2--field tile*/
	int nLinear2TiledEnable; 	/*valid when (nMapType!=0): 0--tile input; 1--yuv input*/
	VpuColorFormat eColorFormat;	/*only MJPG support non-420*/
	int nIsAvcc;				/*it is used for H.264 data format, 0: byte stream ; 1: avcc format*/

	int nReserved[3];				/*reserved for future extension*/
	void* pAppCxt;				/*reserved for future extension*/
} VpuEncOpenParamSimp;

typedef struct {
	int sliceMode;
	int sliceSizeMode;
	int sliceSize;
	int nReserved;			/*reserved for future extension*/
} VpuEncSliceMode;

typedef struct {
	int mp4_dataPartitionEnable;
	int mp4_reversibleVlcEnable;
	int mp4_intraDcVlcThr;
	int mp4_hecEnable;
	int mp4_verid;
	int nReserved[3];			/*reserved for future extension*/
} VpuEncMp4Param;

typedef struct {
	int h263_annexIEnable;	
	int h263_annexJEnable;
	int h263_annexKEnable;
	int h263_annexTEnable;
	int nReserved[4];			/*reserved for future extension*/
} VpuEncH263Param;

typedef struct {
	int avc_constrainedIntraPredFlag;
	int avc_disableDeblk;
	int avc_deblkFilterOffsetAlpha;
	int avc_deblkFilterOffsetBeta;
	int avc_chromaQpOffset;
	int avc_audEnable;
	int avc_fmoEnable;
	int avc_fmoSliceNum;
	int avc_fmoType;
	int avc_fmoSliceSaveBufSize;
	int nReserved[6];				/*reserved for future extension*/
} VpuEncAvcParam;

typedef struct {
	VpuCodStd eFormat;
	int nPicWidth;
	int nPicHeight;	
	int nRotAngle;
	int nFrameRate;
	int nBitRate;				/*unit: kbps*/
	int nGOPSize;
	int nChromaInterleave;	/*should be set to 1 when (nMapType!=0)*/
	VpuEncMirrorDirection sMirror;
	//int nQuantParam;
	int nMapType;			/*frame buffer: 0--linear ; 1--frame tile; 2--field tile*/
	int nLinear2TiledEnable; 	/*valid when (nMapType!=0): 0--tile input; 1--yuv input*/
	VpuColorFormat eColorFormat;	/*only MJPG support non-420*/

	int nUserQpMax;
	int nUserQpMin;
	int nUserQpMinEnable;
	int nUserQpMaxEnable;

	int nIntraRefresh;
	int nRcIntraQp;

	int nUserGamma;
	int nRcIntervalMode;		/* 0:normal, 1:frame_level, 2:slice_level, 3: user defined Mb_level */
	int nMbInterval;			/* use when RcintervalMode is 3 */
	int nAvcIntra16x16OnlyModeEnable;
	
	VpuEncSliceMode sliceMode;

	int nInitialDelay;
	int nVbvBufferSize;
	union {
		VpuEncMp4Param mp4Param;
		VpuEncH263Param h263Param;
		VpuEncAvcParam avcParam;
		//EncMjpgParam mjpgParam;
	} VpuEncStdParam;

	int nMESearchRange;      // 3: 16x16, 2:32x16, 1:64x32, 0:128x64, H.263(Short Header : always 3)
	int nMEUseZeroPmv;       // 0: PMV_ENABLE, 1: PMV_DISABLE
	int nIntraCostWeight;    // Additional weight of Intra Cost for mode decision to reduce Intra MB density
	int nIsAvcc;				/*it is used for H.264 data format, 0: byte stream ; 1: avcc format*/

	int nReserved[8];				/*reserved for future extension*/
	void* pAppCxt;			/*reserved for future extension*/
} VpuEncOpenParam;

typedef struct {
//[IN]	
	VpuCodStd eFormat;
	int nPicWidth;
	int nPicHeight;	
	int nFrameRate;
	int nQuantParam;

	unsigned int nInPhyInput;	//input buffer address
	unsigned int nInVirtInput;
	int nInInputSize;	
	unsigned int nInPhyOutput;	//output frame address
	unsigned int nInVirtOutput;
	unsigned int nInOutputBufLen;

	/*advanced options*/
	int nForceIPicture;
	int nSkipPicture;
	int nEnableAutoSkip;
	
//[OUT]	
	VpuEncBufRetCode eOutRetCode;
	int nOutOutputSize;
//[Reserved]
	VpuFrameBuffer * pInFrame;/*extended for advanced user to set crop info: if this pointer isn't null, the Y/Cb/Cr address in this struct will be adopted*/
	int nReserved[2];			/*reserved for future extension*/
	void* pPrivate;			/*reserved for future extension*/
} VpuEncEncParam;

typedef enum {
	VPU_ENC_CONF_NONE=0,
	//VPU_DEC_CONF_SKIPPB,
	//VPU_DEC_CONF_SKIPB,	
	//VPU_DEC_CONF_SKIPALL,
	//VPU_DEC_CONF_ISEARCH,
	//VPU_DEC_CONF_BLOCK,
	//VPU_DEC_CONF_NONEBLOCK,
	VPU_ENC_CONF_BIT_RATE,  /*parameter: kbps*/
	VPU_ENC_CONF_INTRA_REFRESH, /*intra refresh: minimum number of macroblocks to refresh in a frame*/
	VPU_ENC_CONF_ENA_SPSPPS_IDR, /*some muxers may ignore the sequence or config data(such as ts muxer), so SPS/PPS is needed for every IDR frame, including the first IDR*/
	VPU_ENC_CONF_RC_INTRA_QP, /*intra qp value*/
	VPU_ENC_CONF_INTRA_REFRESH_MODE, /*intra refresh mode: 0: normal; 1: cyclic*/
} VpuEncConfig;



/********************************** decoder APIs ***************************************/

VpuDecRetCode VPU_DecLoad();
VpuDecRetCode VPU_DecGetVersionInfo(VpuVersionInfo * pOutVerInfo);
VpuDecRetCode VPU_DecGetWrapperVersionInfo(VpuWrapperVersionInfo * pOutVerInfo);
VpuDecRetCode VPU_DecQueryMem(VpuMemInfo* pOutMemInfo);
VpuDecRetCode VPU_DecOpen(VpuDecHandle *pOutHandle, VpuDecOpenParam * pInParam,VpuMemInfo* pInMemInfo);
VpuDecRetCode VPU_DecGetCapability(VpuDecHandle InHandle,VpuDecCapability eInCapability, int* pOutCapbility);
VpuDecRetCode VPU_DecDisCapability(VpuDecHandle InHandle,VpuDecCapability eInCapability);

//VpuDecRetCode VPU_DecSeqInit(VpuDecHandle InHandle, VpuBufferNode* pInData, VpuSeqInfo * pOutInfo);
VpuDecRetCode VPU_DecConfig(VpuDecHandle InHandle, VpuDecConfig InDecConf, void* pInParam); 
VpuDecRetCode VPU_DecDecodeBuf(VpuDecHandle InHandle, VpuBufferNode* pInData,int* pOutBufRetCode);
VpuDecRetCode VPU_DecGetInitialInfo(VpuDecHandle InHandle, VpuDecInitInfo * pOutInitInfo);

VpuDecRetCode VPU_DecRegisterFrameBuffer(VpuDecHandle InHandle,VpuFrameBuffer *pInFrameBufArray, int nNum);

VpuDecRetCode VPU_DecGetOutputFrame(VpuDecHandle InHandle, VpuDecOutFrameInfo * pOutFrameInfo);
VpuDecRetCode VPU_DecGetConsumedFrameInfo(VpuDecHandle InHandle,VpuDecFrameLengthInfo* pOutFrameInfo);

VpuDecRetCode VPU_DecOutFrameDisplayed(VpuDecHandle InHandle, VpuFrameBuffer* pInFrameBuf);

//VpuDecRetCode VPU_DecFlushLeftStream(VpuDecHandle InHandle);
//VpuDecRetCode VPU_DecFlushLeftFrame(VpuDecHandle InHandle);
VpuDecRetCode VPU_DecFlushAll(VpuDecHandle InHandle);
VpuDecRetCode VPU_DecAllRegFrameInfo(VpuDecHandle InHandle, VpuFrameBuffer** ppOutFrameBuf, int* pOutNum);
VpuDecRetCode VPU_DecGetNumAvailableFrameBuffers(VpuDecHandle InHandle,int* pOutBufNum);

VpuDecRetCode VPU_DecClose(VpuDecHandle InHandle);
VpuDecRetCode VPU_DecUnLoad();

VpuDecRetCode VPU_DecReset(VpuDecHandle InHandle);
VpuDecRetCode VPU_DecGetErrInfo(VpuDecHandle InHandle,VpuDecErrInfo* pErrInfo);

VpuDecRetCode VPU_DecGetMem(VpuMemDesc* pInOutMem);
VpuDecRetCode VPU_DecFreeMem(VpuMemDesc* pInMem);

/********************************** encoder APIs ***************************************/
VpuEncRetCode VPU_EncLoad();
VpuEncRetCode VPU_EncUnLoad();
VpuEncRetCode VPU_EncReset(VpuEncHandle InHandle);
VpuEncRetCode VPU_EncOpenSimp(VpuEncHandle *pOutHandle, VpuMemInfo* pInMemInfo,VpuEncOpenParamSimp * pInParam);
VpuEncRetCode VPU_EncOpen(VpuEncHandle *pOutHandle, VpuMemInfo* pInMemInfo,VpuEncOpenParam* pInParam);
VpuEncRetCode VPU_EncClose(VpuEncHandle InHandle);
VpuEncRetCode VPU_EncGetInitialInfo(VpuEncHandle InHandle, VpuEncInitInfo * pOutInitInfo);
VpuEncRetCode VPU_EncGetVersionInfo(VpuVersionInfo * pOutVerInfo);
VpuEncRetCode VPU_EncGetWrapperVersionInfo(VpuWrapperVersionInfo * pOutVerInfo);
VpuEncRetCode VPU_EncRegisterFrameBuffer(VpuEncHandle InHandle,VpuFrameBuffer *pInFrameBufArray, int nNum,int nSrcStride);
VpuEncRetCode VPU_EncQueryMem(VpuMemInfo* pOutMemInfo);
VpuEncRetCode VPU_EncGetMem(VpuMemDesc* pInOutMem);
VpuEncRetCode VPU_EncFreeMem(VpuMemDesc* pInMem);
VpuEncRetCode VPU_EncConfig(VpuEncHandle InHandle, VpuEncConfig InEncConf, void* pInParam);
VpuEncRetCode VPU_EncEncodeFrame(VpuEncHandle InHandle, VpuEncEncParam* pInOutParam);
VpuEncRetCode VPU_EncEncodeFrame(VpuEncHandle InHandle, VpuEncEncParam* pInOutParam);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  //#ifndef VPU_WRAPPER_H

