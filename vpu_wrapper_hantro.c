/*!
 *	CopyRight Notice:
 *	The following programs are the sole property of Freescale Semiconductor Inc.,
 *	and contain its proprietary and confidential information.
 *	Copyright (c) 2016, Freescale Semiconductor Inc.,
 *	All Rights Reserved
 *	Copyright 2017 NXP
 *
 *	History :
 *	Date	(y.m.d)		Author			Version			Description
 *	2016-12-20		Song Bing		0.1				Created
 */

/** Vpu_wrapper_hantro.c
 *	vpu wrapper file contain all related hantro video decoder api exposed to
 *	application
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "dwl.h"
#include "codec.h"
#include "codec_h264.h"
#include "codec_jpeg.h"
#include "codec_mpeg4.h"
#include "codec_vc1.h"
#include "codec_rv.h"
#include "codec_mpeg2.h"
#include "codec_vp6.h"
#include "codec_avs.h"
#include "codec_vp8.h"
#include "codec_webp.h"
#include "codec_hevc.h"
#include "codec_vp9.h"

#include "utils.h"
#include "vpu_wrapper.h"

static int nVpuLogLevel=0;		//bit 0: api log; bit 1: raw dump; bit 2: yuv dump
#ifdef ANDROID
#include "Log.h"
#define LOG_PRINTF LogOutput
#define VPU_LOG_LEVELFILE "/data/vpu_log_level"
#define VPU_DUMP_RAWFILE "/data/temp_wrapper.bit"
#define VPU_DUMP_YUVFILE "/data/temp_wrapper.yuv"
#else
#define LOG_PRINTF printf
#define VPU_LOG_LEVELFILE "/etc/vpu_log_level"
#define VPU_DUMP_RAWFILE "temp_wrapper.bit"
#define VPU_DUMP_YUVFILE "temp_wrapper.yuv"
#endif

#define MAX_YUV_FRAME  (20)
#define DUMP_ALL_DATA		1
static int g_seek_dump=DUMP_ALL_DATA;	/*0: only dump data after seeking; otherwise: dump all data*/

#define VPU_LOG(...) if(nVpuLogLevel&0x1) {LOG_PRINTF(__VA_ARGS__);}
#define VPU_TRACE
#define VPU_API(...) if(nVpuLogLevel&0x1) {LOG_PRINTF(__VA_ARGS__);}
#define VPU_ERROR(...) if(nVpuLogLevel&0x1) {LOG_PRINTF(__VA_ARGS__);}
#define ASSERT(exp) if((!(exp))&&(nVpuLogLevel&0x1)) {LOG_PRINTF("%s: %d : assert condition !!!\r\n",__FUNCTION__,__LINE__);}

#define VPU_DUMP_RAW  (nVpuLogLevel&0x2)
#define VPU_DUMP_YUV  (nVpuLogLevel&0x4)

/****************************** binary version info *********************************/
#define SEPARATOR " "
#define BASELINE_SHORT_NAME "VPUWRAPPER_ARM64"
#define OS_NAME "_LINUX"

#define VPUWRAPPER_BINARY_VERSION_STR \
  (BASELINE_SHORT_NAME OS_NAME \
   SEPARATOR "Build on" \
   SEPARATOR __DATE__ SEPARATOR __TIME__)

#define VPU_MEM_ALIGN			0x10
#define VPU_BITS_BUF_SIZE		(12*1024*1024)		//bitstream buffer size : big enough contain two big frames

#define VC1_MAX_SEQ_HEADER_SIZE	256		//for clip: WVC1_stress_a0_stress06.wmv, its header length = 176 (>128)
#define VC1_MAX_FRM_HEADER_SIZE	32
#define RCV_HEADER_LEN_HANTRO			20
#define VP8_SEQ_HEADER_SIZE	32
#define VP8_FRM_HEADER_SIZE	12
#define DIV3_SEQ_HEADER_SIZE	32
#define DIV3_FRM_HEADER_SIZE	12
#define VC1_IS_NOT_NAL(id)		(( id & 0x00FFFFFF) != 0x00010000)

#define VPU_MAX_FRAME_INDEX	30

#define VIRT_INDEX	0
#define PHY_INDEX	1

#define VPU_FRAME_STATE_FREE			0	//clear them by memset() at init step
#define VPU_FRAME_STATE_DEC			1	//decoded by vpu, but not send out
#define VPU_FRAME_STATE_DISP			2	//send out by vpu for display

#define ALIGN(ptr,align)       ((align) ? ((((unsigned long)(ptr))+(align)-1)/(align)*(align)) : ((unsigned long)(ptr)))
#define MemAlign(mem,align)	((((unsigned int)mem)%(align))==0)
#define MemNotAlign(mem,align)	((((unsigned int)mem)%(align))!=0)

#define NotEnoughInitData(free)	(((VPU_BITS_BUF_SIZE)-(free))<(VPU_MIN_INIT_SIZE))
#define NotEnoughDecData(free,min_validsize)	(((VPU_BITS_BUF_SIZE)-(free))<(min_validsize))


typedef enum
{
  VPU_DEC_STATE_OPEN=0,
  VPU_DEC_STATE_INITOK,
  VPU_DEC_STATE_REGFRMOK,
  VPU_DEC_STATE_DEC,
  VPU_DEC_STATE_STARTFRAMEOK,
  VPU_DEC_STATE_OUTOK,
  VPU_DEC_STATE_EOS,
  VPU_DEC_STATE_CORRUPT
}VpuDecState;

typedef struct {
  int offset;
  int is_valid;
} RvDecSliceInfo;

typedef struct
{
  /* open parameters */
  VpuCodStd CodecFormat;

  const void *pdwl;
  /* hantro decoder */
  CODEC_PROTOTYPE *codec;
  OMX_VIDEO_PARAM_CONFIGTYPE config;

  /* decode parameters */
  int iframeSearchEnable;
  int skipFrameMode;
  int skipFrameNum;

  int inputType;			/*normal, kick, drain(EOS)*/
  int streamBufDelaySize;	/*unit: bytes. used in stream mode:  valid data size should reach the threshold before decoding*/
  int initDataCountThd;
  VpuDecErrInfo nLastErrorInfo;  /*it record the last error info*/

  /*resolution for some special formats, such as package VC1 header,...*/
  int picWidth;
  int picHeight;

  /* init info */
  VpuDecInitInfo initInfo;

  /* out frame info */
  VpuDecOutFrameInfo frameInfo;

  /*used to store extended frame info*/
  VpuFrameExtInfo frmExtInfo;

  /* frame buffer management */
  int frameNum;
  VpuFrameBuffer frameBuf[VPU_MAX_FRAME_INDEX];	 /*buffer node*/
  int frameBufState[VPU_MAX_FRAME_INDEX];  /*record frame state for clearing display frame(if user forgot to clear them)*/

  /* bitstream buffer pointer info */
  unsigned char* pBsBufVirtStart;
  unsigned char* pBsBufPhyStart;
  unsigned char* pBsBufPhyEnd;
  int nBsBufLen;
  int nBsBufOffset;

  /* state */
  VpuDecState state;
  /*management of consumed bytes: used to sync with frame boundary*/
  int nDecFrameRptEnabled;			/*1:support frame reported; 0: not support*/
  int nAccumulatedConsumedStufferBytes;/*stuffer size between frames: if it <0, indicate that some frames are contained in config data*/
  int nAccumulatedConsumedFrmBytes;	/*frame size: >=0*/
  int nAccumulatedConsumedBytes;		/*it should match with the input data size == nAccumulatedConsumedStufferBytes+nAccumulatedConsumedFrmBytes*/	
  VpuFrameBuffer* pLastDecodedFrm;	/*the nearest decoded frame*/
  int nAdditionalSeqBytes;				/*seq header inserted by wrapper itself , or config data */
  int nAdditionalFrmHeaderBytes; 		/*frame header inserted by wrapper itself */
  unsigned int nLastFrameEndPosPhy;	/*point to the previous frame tail: used to compute the stuff data length between frames*/


  int nDecResolutionChangeEnabled;		/*1: support resolution change notification; 0: not support*/

  int nPrivateSeqHeaderInserted;
  int nIsAvcc;	/*for H.264/HEVC format*/
  int nNalSizeLen;
  int nNalNum; /*added for nal_size_length = 1 or 2*/
  bool eosing;
  bool ringbuffer;
  int nFrameSize;
  int nOutFrameCount;
  int total_frames;
  long long total_time;
  int slice_info_num;
  RvDecSliceInfo slice_info[128];
  int frame_size;
  bool bSecureMode;
  bool bConsumeInputLater;
}VpuDecObj;

typedef struct 
{
  VpuDecObj obj;
}VpuDecHandleInternal;

int VpuLogLevelParse(int * pLogLevel)
{
  int level=0;
  FILE* fpVpuLog;
  fpVpuLog=fopen(VPU_LOG_LEVELFILE,"r");
  if (NULL==fpVpuLog){
    //LOG_PRINTF("no vpu log level file: %s \r\n",VPU_LOG_LEVELFILE);
  }
  else	{
    char symbol;
    int readLen = 0;

    readLen = fread(&symbol,1,1,fpVpuLog);
    if(feof(fpVpuLog) != 0){
      //LOG_PRINTF("\n End of file reached.");
    }
    else	{
      level=atoi(&symbol);
      //LOG_PRINTF("vpu log level: %d \r\n",level);
      if((level<0) || (level>255)){
        level=0;
      }
    }
    fclose(fpVpuLog);
  }
  nVpuLogLevel=level;
  //*pLogLevel=level;
  return 1;
}

VpuDecRetCode VPU_DecLoad()
{
  VpuLogLevelParse(NULL);

  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecGetVersionInfo(VpuVersionInfo * pOutVerInfo)
{
  if(pOutVerInfo==NULL)
  {
    VPU_ERROR("%s: failure: invalid parameterl \r\n",__FUNCTION__);
    return VPU_DEC_RET_INVALID_PARAM;
  }

  pOutVerInfo->nFwMajor=1;
  pOutVerInfo->nFwMinor=1;
  pOutVerInfo->nFwRelease=1;
  pOutVerInfo->nLibMajor=1;
  pOutVerInfo->nLibMinor=1;
  pOutVerInfo->nLibRelease=1;

  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecGetWrapperVersionInfo(VpuWrapperVersionInfo * pOutVerInfo)
{
  pOutVerInfo->nMajor= (VPU_WRAPPER_VERSION_CODE >> (16)) & 0xff;
  pOutVerInfo->nMinor= (VPU_WRAPPER_VERSION_CODE >> (8)) & 0xff;
  pOutVerInfo->nRelease= (VPU_WRAPPER_VERSION_CODE) & 0xff;
  pOutVerInfo->pBinary=(char*)VPUWRAPPER_BINARY_VERSION_STR;

  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecQueryMem(VpuMemInfo* pOutMemInfo)
{
  VpuMemSubBlockInfo * pMem;

  if(pOutMemInfo==NULL)
  {
    VPU_ERROR("%s: failure: invalid parameterl \r\n",__FUNCTION__);
    return VPU_DEC_RET_INVALID_PARAM;
  }
  pMem=&pOutMemInfo->MemSubBlock[VIRT_INDEX];
  pMem->MemType=VPU_MEM_VIRT;
  pMem->nAlignment=VPU_MEM_ALIGN;
  pMem->nSize=sizeof(VpuDecHandleInternal);
  pMem->pVirtAddr=NULL;
  pMem->pPhyAddr=NULL;

  pMem=&pOutMemInfo->MemSubBlock[PHY_INDEX];
  pMem->MemType=VPU_MEM_PHY;
  pMem->nAlignment=VPU_MEM_ALIGN;
  pMem->nSize=VPU_BITS_BUF_SIZE;
  pMem->pVirtAddr=NULL;
  pMem->pPhyAddr=NULL;

  pOutMemInfo->nSubBlockNum=2;

  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecOpen(VpuDecHandle *pOutHandle, VpuDecOpenParam * pInParam,VpuMemInfo* pInMemInfo)
{
  VpuMemSubBlockInfo * pMemPhy;
  VpuMemSubBlockInfo * pMemVirt;
  VpuDecHandleInternal* pVpuObj;
  bool bDeblock = true;
  bool bIsMvcStream = false;
  VpuDecObj* pObj;
  struct DWLInitParam dwlInit;

  pMemVirt=&pInMemInfo->MemSubBlock[VIRT_INDEX];
  pMemPhy=&pInMemInfo->MemSubBlock[PHY_INDEX];
  if ((pMemVirt->pVirtAddr==NULL) || MemNotAlign(pMemVirt->pVirtAddr,VPU_MEM_ALIGN)
      ||(pMemVirt->nSize!=sizeof(VpuDecHandleInternal)))
  {
    VPU_ERROR("%s: failure: invalid parameter ! \r\n",__FUNCTION__);
    return VPU_DEC_RET_INVALID_PARAM;
  }

  if ((pMemPhy->pVirtAddr==NULL) || MemNotAlign(pMemPhy->pVirtAddr,VPU_MEM_ALIGN)
      ||(pMemPhy->pPhyAddr==NULL) || MemNotAlign(pMemPhy->pPhyAddr,VPU_MEM_ALIGN)
      ||(pMemPhy->nSize!=(VPU_BITS_BUF_SIZE)))
  {
    VPU_ERROR("%s: failure: invalid parameter !! \r\n",__FUNCTION__);
    return VPU_DEC_RET_INVALID_PARAM;
  }

  pVpuObj=(VpuDecHandleInternal*)pMemVirt->pVirtAddr;
  pObj=&pVpuObj->obj;

  memset(pObj, 0, sizeof(VpuDecObj));

  if (pInParam->CodecFormat == VPU_V_HEVC || pInParam->CodecFormat == VPU_V_VP9)
  {
    dwlInit.client_type = DWL_CLIENT_TYPE_HEVC_DEC;
  }
  else
  {
    dwlInit.client_type = DWL_CLIENT_TYPE_H264_DEC;
  }
  pObj->pdwl = (void*)DWLInit(&dwlInit);
  if (!pObj->pdwl)
  {
    VPU_ERROR("%s: DWLInit failed !! \r\n",__FUNCTION__);
    return VPU_DEC_RET_FAILURE;
  }
  pObj->config.g2_conf.bEnableTiled = false;
  pObj->config.g1_conf.bEnableTiled = false;
  if (pInParam->nTiled2LinearEnable)
  {
    pObj->config.g2_conf.bEnableTiled = true;
    pObj->config.g1_conf.bEnableTiled = true;
  }
  pObj->config.g2_conf.ePixelFormat = OMX_VIDEO_G2PixelFormat_Default;
  if (pInParam->nPixelFormat)
    pObj->config.g2_conf.ePixelFormat = OMX_VIDEO_G2PixelFormat_8bit;
  pObj->config.g2_conf.bEnableRFC = false;
  if (pInParam->nEnableVideoCompressor)
    pObj->config.g2_conf.bEnableRFC = true;
  pObj->config.g2_conf.bEnableRingBuffer = pObj->ringbuffer = false;
  pObj->config.g2_conf.bEnableFetchOnePic = true;
  pObj->config.g1_conf.bAllowFieldDBP = false;
  
  if(pInParam->nAdaptiveMode == 1){
    pObj->config.g1_conf.bEnableAdaptiveBuffers = true;
    pObj->config.g1_conf.nGuardSize = 0;
    pObj->config.g2_conf.bEnableAdaptiveBuffers = true;
    pObj->config.g2_conf.nGuardSize = 0;
    VPU_LOG("VPU_DecOpen enable nAdaptiveMode");
  }
  if(pInParam->nSecureMode == 1){
    pObj->config.g1_conf.bEnableSecureMode = true;
    pObj->config.g2_conf.bEnableSecureMode = true;
    pObj->bSecureMode = true;
  }

  VPU_LOG("format: %d \r\n",pInParam->CodecFormat);
  switch (pInParam->CodecFormat) {
    case VPU_V_AVC:
      pObj->codec = HantroHwDecOmx_decoder_create_h264(pObj->pdwl,
          bIsMvcStream, &pObj->config.g1_conf);
      VPU_LOG("open H.264 \r\n");
      break;
    case VPU_V_MPEG2: 	 /**< AKA: H.262 */
      pObj->codec = HantroHwDecOmx_decoder_create_mpeg2(pObj->pdwl,
          &pObj->config.g1_conf);
      VPU_LOG("open Mpeg2 \r\n");
      break;
    case VPU_V_H263:		 /**< H.263 */
      pObj->codec = HantroHwDecOmx_decoder_create_mpeg4(pObj->pdwl,
          bDeblock, MPEG4FORMAT_H263, &pObj->config.g1_conf);
      VPU_LOG("open H263 \r\n");
      break;
    case VPU_V_MPEG4: 	 /**< MPEG-4 */
      pObj->codec = HantroHwDecOmx_decoder_create_mpeg4(pObj->pdwl,
          bDeblock, MPEG4FORMAT_MPEG4, &pObj->config.g1_conf);
      VPU_LOG("open Mpeg4 \r\n");
      break;
    case VPU_V_SORENSON: 	 /**< Sorenson */
      pObj->codec = HantroHwDecOmx_decoder_create_mpeg4(pObj->pdwl,
          bDeblock, MPEG4FORMAT_SORENSON, &pObj->config.g1_conf);
      VPU_LOG("open Mpeg4 \r\n");
      break;
    case VPU_V_DIVX4:		/**< DIVX 4 */
    case VPU_V_DIVX56:		/**< DIVX 5/6 */
      pObj->codec = HantroHwDecOmx_decoder_create_mpeg4(pObj->pdwl,
          bDeblock, MPEG4FORMAT_CUSTOM_1, &pObj->config.g1_conf);
      VPU_LOG("open DIVX 4 \r\n");
      VPU_LOG("open DIVX 56 \r\n");
      break;
    case VPU_V_XVID:		/**< XVID */
      pObj->codec = HantroHwDecOmx_decoder_create_mpeg4(pObj->pdwl,
          bDeblock, MPEG4FORMAT_MPEG4, &pObj->config.g1_conf);
      VPU_LOG("open XVID \r\n");
      break;
    case VPU_V_DIVX3:		/**< DIVX 3 */
      pObj->codec = HantroHwDecOmx_decoder_create_mpeg4(pObj->pdwl,
          bDeblock, MPEG4FORMAT_CUSTOM_1_3, &pObj->config.g1_conf);
      VPU_LOG("open DIVX 3 \r\n");
      break;
    case VPU_V_RV:		
      VPU_LOG("open RV \r\n");
      break;
    case VPU_V_VC1:		 /**< all versions of Windows Media Video */
    case VPU_V_VC1_AP:
      pObj->codec = HantroHwDecOmx_decoder_create_vc1(pObj->pdwl,
          &pObj->config.g1_conf);
      VPU_LOG("open VC1 \r\n");
      break;
    case VPU_V_AVC_MVC:
      bIsMvcStream = true;
      pObj->codec = HantroHwDecOmx_decoder_create_h264(pObj->pdwl,
          bIsMvcStream, &pObj->config.g1_conf);
      VPU_LOG("open H.264 MVC \r\n");
      break;
    case VPU_V_MJPG:
      pObj->codec = HantroHwDecOmx_decoder_create_jpeg(true);
      VPU_LOG("open MJPEG \r\n");
      break;
    case VPU_V_WEBP:
      pObj->codec = HantroHwDecOmx_decoder_create_webp(pObj->pdwl);
      VPU_LOG("open WEBP \r\n");
      break;
    case VPU_V_AVS:
      pObj->codec = HantroHwDecOmx_decoder_create_avs(pObj->pdwl,
          &pObj->config.g1_conf);
      VPU_LOG("open AVS \r\n");
      break;
    case VPU_V_VP6:
      pObj->codec = HantroHwDecOmx_decoder_create_vp6(pObj->pdwl,
          &pObj->config.g1_conf);
      VPU_LOG("open VP6 \r\n");
      break;
    case VPU_V_VP8:
      pObj->codec = HantroHwDecOmx_decoder_create_vp8(pObj->pdwl,
          &pObj->config.g1_conf);
      VPU_LOG("open VP8 \r\n");
      break;
    case VPU_V_HEVC:
      if(!pObj->bSecureMode){
        pObj->config.g2_conf.bEnableRingBuffer = pObj->ringbuffer = true;
      }
      pObj->codec = HantroHwDecOmx_decoder_create_hevc(pObj->pdwl,
          &pObj->config.g2_conf);
      VPU_LOG("open HEVC \r\n");
      break;
    case VPU_V_VP9:
      pObj->codec = HantroHwDecOmx_decoder_create_vp9(pObj->pdwl,
          &pObj->config.g2_conf);
      VPU_LOG("open VP9 \r\n");
      break;
    default:
      VPU_ERROR("%s: failure: invalid format !!! \r\n",__FUNCTION__);
      return VPU_DEC_RET_INVALID_PARAM;
  }

  PP_ARGS pp_args;  // post processor parameters
  memset(&pp_args, 0, sizeof(PP_ARGS));
  if (pObj->codec)
    if (pObj->codec->setppargs(pObj->codec, &pp_args) != CODEC_OK)
    {
    }

  //record resolution for some special formats (such as VC1,...)
  pObj->picWidth = pInParam->nPicWidth;	
  pObj->picHeight = pInParam->nPicHeight;


  pObj->CodecFormat= pInParam->CodecFormat;
  pObj->pBsBufVirtStart= pMemPhy->pVirtAddr;
  pObj->pBsBufPhyStart= pMemPhy->pPhyAddr;
  pObj->pBsBufPhyEnd=pMemPhy->pPhyAddr+VPU_BITS_BUF_SIZE;
  pObj->nFrameSize = 0;
  pObj->state=VPU_DEC_STATE_OPEN;

  *pOutHandle=(VpuDecHandle)pVpuObj;

  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecGetCapability(VpuDecHandle InHandle,VpuDecCapability eInCapability, int* pOutCapbility)
{
  VpuDecHandleInternal * pVpuObj=NULL;
  VpuDecObj* pObj=NULL;

  if (InHandle)
  {
    pVpuObj=(VpuDecHandleInternal *)InHandle;
    pObj=&pVpuObj->obj;
  }

  switch(eInCapability)
  {
    case VPU_DEC_CAP_FILEMODE:
      *pOutCapbility=1;
      break;
    case VPU_DEC_CAP_TILE:
      *pOutCapbility=1;
      break;
    case VPU_DEC_CAP_FRAMESIZE:
      *pOutCapbility=1;
      break;
    case VPU_DEC_CAP_RESOLUTION_CHANGE:
      *pOutCapbility=1;
      break;
    default:
      VPU_ERROR("%s: unknown capability: 0x%X \r\n",__FUNCTION__,eInCapability);
      return VPU_DEC_RET_INVALID_PARAM;
  }

  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecDisCapability(VpuDecHandle InHandle,VpuDecCapability eInCapability)
{
  VpuDecHandleInternal * pVpuObj=NULL;
  VpuDecObj* pObj=NULL;

  if (InHandle==NULL)	{
    return VPU_DEC_RET_INVALID_PARAM;
  }

  pVpuObj=(VpuDecHandleInternal *)InHandle;
  pObj=&pVpuObj->obj;
  if(pObj==NULL){
    VPU_ERROR("%s: get capability(%d) failure: vpu hasn't been opened \r\n",__FUNCTION__,eInCapability);
    return VPU_DEC_RET_INVALID_PARAM;
  }

  switch(eInCapability)	{
    case VPU_DEC_CAP_FRAMESIZE:
      pObj->nDecFrameRptEnabled=0;
      break;
    case VPU_DEC_CAP_RESOLUTION_CHANGE:
      /* user always allocate enough frames(size/count) */
      pObj->nDecResolutionChangeEnabled=0;
      break;
    default:
      VPU_ERROR("%s: unsupported capability: 0x%X \r\n",__FUNCTION__,eInCapability);
      return VPU_DEC_RET_INVALID_PARAM;
  }

  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecConfig(VpuDecHandle InHandle, VpuDecConfig InDecConf, void* pInParam)
{
  VpuDecHandleInternal * pVpuObj;
  VpuDecObj* pObj;
  int para;

  if(InHandle==NULL)
  {
    VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);
    return VPU_DEC_RET_INVALID_HANDLE;
  }

  pVpuObj=(VpuDecHandleInternal *)InHandle;
  pObj=&pVpuObj->obj;

  switch(InDecConf)
  {
    case VPU_DEC_CONF_SKIPMODE:
      break;
    case VPU_DEC_CONF_INPUTTYPE:
      para=*((int*)pInParam);
      if((para!=VPU_DEC_IN_NORMAL)&&(para!=VPU_DEC_IN_KICK)&&(para!=VPU_DEC_IN_DRAIN))
      {
        VPU_ERROR("%s: failure: invalid inputtype parameter: %d \r\n",__FUNCTION__,para);
        return VPU_DEC_RET_INVALID_PARAM;
      }
      pObj->inputType=para;
      break;
      //case VPU_DEC_CONF_BLOCK:
      //	pObj->blockmode=1;
      //	break;
      //case VPU_DEC_CONF_NONEBLOCK:
      //	pObj->blockmode=0;
      //	break;
    case VPU_DEC_CONF_BUFDELAY:
      para=*((int*)pInParam);
      pObj->streamBufDelaySize=para;
      break;
    case VPU_DEC_CONF_INIT_CNT_THRESHOLD:
      para=*((int*)pInParam);
      if(para<=0){
        return VPU_DEC_RET_INVALID_PARAM;
      }
      pObj->initDataCountThd=para;
      break;
    case VPU_DEC_CONF_ENABLE_TILED:
      pObj->config.g2_conf.bEnableTiled = false;
      pObj->config.g1_conf.bEnableTiled = false;
      if ((*((int*)pInParam)) == 1)
      {
        pObj->config.g2_conf.bEnableTiled = true;
        pObj->config.g1_conf.bEnableTiled = true;
      }
      pObj->codec->setinfo(pObj->codec, &pObj->config);
      break;
    default:
      VPU_ERROR("%s: failure: invalid setting \r\n",__FUNCTION__);
      return VPU_DEC_RET_INVALID_PARAM;
  }

  return VPU_DEC_RET_SUCCESS;
}
static void WrapperFileDumpBitstrem(unsigned char* pBits, unsigned int nSize)
{
    int nWriteSize=0;
    if(nSize==0)
    {
        return;
    }

    FILE * pfile;
    pfile = fopen(VPU_DUMP_RAWFILE,"ab");
    if(pfile){
        fwrite(pBits,1,nSize,pfile);
        fclose(pfile);
    }
	return;
}
static void WrapperFileDumpYUV(VpuDecObj* pObj, VpuFrameBuffer *pDisplayBuf)
{
    static int cnt=0;
    int nCScale=1;
    int nWriteSize=0;
#define FRAME_ALIGN	 (16)
#define Alignment(ptr,align)	(((unsigned int)(ptr)+(align)-1)/(align)*(align))
    int colorformat=0;
    int nPadStride=0;
    int nFrameSize = 0;
    nPadStride = Alignment(pObj->picWidth,FRAME_ALIGN);
    nFrameSize = nPadStride * pObj->picHeight;
    if(cnt<MAX_YUV_FRAME)
    {
        FILE * pfile;
        pfile = fopen(VPU_DUMP_YUVFILE,"ab");
        if(pfile){
            fwrite(pDisplayBuf->pbufVirtY,1,nFrameSize,pfile);
            fwrite(pDisplayBuf->pbufVirtCb,1,nFrameSize/4,pfile);
            fwrite(pDisplayBuf->pbufVirtCr,1,nFrameSize/4,pfile);
            fclose(pfile);
        }
        cnt++;
    }
    return;
}
static void VpuPutInBuf(VpuDecObj* pObj, unsigned char *pIn, unsigned int len, unsigned char *pInPhyc)
{
  //do not use ring buffer in secure mode
  if(pObj->bSecureMode && pInPhyc != NULL){
    pObj->pBsBufVirtStart = pIn;
    pObj->nBsBufLen = len;
    VPU_LOG("VpuPutInBuf size=%d",len);
    if(VPU_DUMP_RAW)
        WrapperFileDumpBitstrem(pIn,len);
    return;
  }

  if(pObj->nBsBufOffset+pObj->nBsBufLen+len > VPU_BITS_BUF_SIZE)
  {
    if(pObj->ringbuffer)
    {
      memcpy(pObj->pBsBufVirtStart+pObj->nBsBufOffset+pObj->nBsBufLen, pIn, VPU_BITS_BUF_SIZE-pObj->nBsBufOffset-pObj->nBsBufLen);
      memcpy(pObj->pBsBufVirtStart, pIn+VPU_BITS_BUF_SIZE-pObj->nBsBufOffset-pObj->nBsBufLen, len+pObj->nBsBufOffset+pObj->nBsBufLen-VPU_BITS_BUF_SIZE);
    }
    else
    {
      if(pObj->nBsBufOffset)
        memmove(pObj->pBsBufVirtStart, pObj->pBsBufVirtStart + pObj->nBsBufOffset, pObj->nBsBufLen);
      pObj->nBsBufOffset = 0;
      memcpy(pObj->pBsBufVirtStart+pObj->nBsBufOffset+pObj->nBsBufLen, pIn, len);
    }
  }
  else
  {
    memcpy(pObj->pBsBufVirtStart+pObj->nBsBufOffset+pObj->nBsBufLen, pIn, len);
  }
  pObj->nBsBufLen += len;
  if(VPU_DUMP_RAW)
    WrapperFileDumpBitstrem(pIn,len);
}

static VpuDecRetCode VPU_DecProcessInBuf(VpuDecObj* pObj, VpuBufferNode* pInData)
{
  unsigned char* pHeader=NULL;
  unsigned int headerLen=0;
  unsigned int headerAllocated=0;
  int pNoErr = 1;

  if(pInData->pVirAddr == (unsigned char *)0x01 && pInData->nSize == 0)
    pObj->eosing = true;

  if(pInData->pVirAddr == NULL || pInData->nSize == 0)
    return VPU_DEC_RET_SUCCESS;

  if(pObj->nPrivateSeqHeaderInserted == 0)
  {

    if(pObj->CodecFormat==VPU_V_DIVX3)
    {
      unsigned char aDivx3Head[8];
      int nWidth = pObj->picWidth;
      int nHeight = pObj->picHeight;
      int i = 0;
      pHeader=aDivx3Head;
      headerLen = 8;

      VPU_LOG("%s: [width x height]=[%d x %d] , frame size =%d \r\n",
          __FUNCTION__,pObj->picWidth,pObj->picHeight,pInData->nSize);
      //Width
      pHeader[i++] = (unsigned char)nWidth;
      pHeader[i++] = (unsigned char)(((nWidth >> 8) & 0xff));
      pHeader[i++] = (unsigned char)(((nWidth >> 16) & 0xff)); 
      pHeader[i++] = (unsigned char)(((nWidth >> 24) & 0xff));
      //Height
      pHeader[i++] = (unsigned char)nHeight;
      pHeader[i++] = (unsigned char)(((nHeight >> 8) & 0xff));
      pHeader[i++] = (unsigned char)(((nHeight >> 16) & 0xff));
      pHeader[i++] = (unsigned char)(((nHeight >> 24) & 0xff));
      VpuPutInBuf(pObj, pHeader, headerLen, pInData->pPhyAddr);
    }
    else if(pObj->CodecFormat==VPU_V_WEBP)
    {
      char signature[] = "WEBP";
      char format_[] = "VP8 ";
      u8 tmp[4];
      if(pInData->nSize < 20)
        return VPU_DEC_RET_SUCCESS;

      memcpy(tmp, pInData->pVirAddr+8, 4);
      if (strncmp(signature, (const char *)tmp, 4))
        return VPU_DEC_RET_FAILURE;
      memcpy(tmp, pInData->pVirAddr+12, 4);
      if (strncmp(format_, (const char *)tmp, 4))
        return VPU_DEC_RET_FAILURE;
      memcpy(tmp, pInData->pVirAddr+16, 4);
      pObj->frame_size =
        tmp[0] +
        (tmp[1] << 8) +
        (tmp[2] << 16) +
        (tmp[3] << 24);
      pInData->pVirAddr += 20;
      pInData->nSize -= 20;
    }

    if(0 != pInData->sCodecData.nSize)
    {
      if((pObj->CodecFormat==VPU_V_AVC || pObj->CodecFormat==VPU_V_HEVC)
          &&(0==pObj->nIsAvcc)){
        if(pObj->CodecFormat==VPU_V_AVC)
          VpuDetectAvcc(pInData->sCodecData.pData,pInData->sCodecData.nSize,
              &pObj->nIsAvcc,&pObj->nNalSizeLen,&pObj->nNalNum);
        else
          VpuDetectHvcc(pInData->sCodecData.pData,pInData->sCodecData.nSize,
              &pObj->nIsAvcc,&pObj->nNalSizeLen,&pObj->nNalNum);
      }
      if(pObj->nIsAvcc){
        if(pObj->CodecFormat==VPU_V_AVC)
          VpuConvertAvccHeader(pInData->sCodecData.pData,pInData->sCodecData.nSize,
              &pHeader,&headerLen);
        else
          VpuConvertHvccHeader(pInData->sCodecData.pData,pInData->sCodecData.nSize,
              &pHeader,&headerLen);
        if(pInData->sCodecData.pData != pHeader){
          headerAllocated=1;
        }
      }
      else if(pObj->CodecFormat==VPU_V_VC1_AP)
      {
        if((pInData->pVirAddr==NULL) || (pInData->nSize<4))
        {
          //we need pInData->pVirAddr to create correct VC1 header
          //TODO: or define one default value when pInData->pVirAddr is NULL
          VPU_LOG("%s: no input buffer, return and do nothing \r\n",__FUNCTION__);	
          return VPU_DEC_RET_SUCCESS;
        }
        pHeader = malloc(VC1_MAX_SEQ_HEADER_SIZE);
        headerAllocated=1;
        VC1CreateNALSeqHeader(pHeader, (int*)(&headerLen),pInData->sCodecData.pData,
            (int)pInData->sCodecData.nSize, (unsigned int*)pInData->pVirAddr,
            VC1_MAX_SEQ_HEADER_SIZE);
        if(VC1_IS_NOT_NAL(((unsigned int*)pInData->pVirAddr)[0]))
          headerLen -= 4;
      }
      else if(pObj->CodecFormat==VPU_V_VC1)
      {
        //1 nSize must == frame size ??? 
        VPU_LOG("%s: [width x height]=[%d x %d] , frame size =%d \r\n",
            __FUNCTION__,pObj->picWidth,pObj->picHeight,pInData->nSize);
        pHeader = malloc(VC1_MAX_SEQ_HEADER_SIZE);
        headerAllocated=1;
        VC1CreateRCVSeqHeader(pHeader, (int*)(&headerLen),pInData->sCodecData.pData,
            pInData->nSize,pObj->picWidth,pObj->picHeight, &pNoErr);
        headerLen = RCV_HEADER_LEN_HANTRO;
      }
      else{
        pHeader=pInData->sCodecData.pData;
        headerLen=pInData->sCodecData.nSize;
      }
      VPU_LOG("put CodecData len=%d",headerLen);
      VpuPutInBuf(pObj, pHeader, headerLen, pInData->pPhyAddr);
      pObj->nAccumulatedConsumedFrmBytes -= headerLen;

      if(headerAllocated){
        free(pHeader);
      }
    }
    pObj->nPrivateSeqHeaderInserted=1;
  }			

  if(pObj->nIsAvcc){
    unsigned char* pFrm=NULL;
    unsigned int nFrmSize;
    VpuConvertAvccFrame(pInData->pVirAddr,pInData->nSize,pObj->nNalSizeLen,
        &pFrm,&nFrmSize,&pObj->nNalNum);
    VpuPutInBuf(pObj, pFrm, nFrmSize,pInData->pPhyAddr);
    if(pFrm!=pInData->pVirAddr){
      free(pFrm);
    }
  } else {
    if(pObj->CodecFormat==VPU_V_VC1_AP) {
      unsigned char aVC1Head[VC1_MAX_FRM_HEADER_SIZE];
      pHeader=aVC1Head;
      VC1CreateNalFrameHeader(pHeader,(int*)(&headerLen),(unsigned int*)(pInData->pVirAddr));
      VpuPutInBuf(pObj, pHeader, headerLen,pInData->pPhyAddr);
    } else if(pObj->CodecFormat==VPU_V_MJPG) {
      pObj->nBsBufOffset = 0;
    }

    VpuPutInBuf(pObj, pInData->pVirAddr, pInData->nSize,pInData->pPhyAddr);
  }

  return VPU_DEC_RET_SUCCESS;
}

static int VpuSearchFrameIndex(VpuDecObj* pObj, unsigned char *pInPhysY)
{
  int index;
  int i;

  for(i=0;i<pObj->frameNum;i++)
  {
    if(pObj->frameBuf[i].pbufY == pInPhysY)
    {
      VPU_LOG("%s: find frame index: %d \r\n",__FUNCTION__, i);
      index=i;
      break;
    }
  }

  if (i>=pObj->frameNum)
  {
    VPU_LOG("%s: error: can not find frame index \r\n",__FUNCTION__);
    index=-1;
  }
  return index;
}

static VpuDecRetCode VPU_DecGetFrame(VpuDecObj* pObj, int* pOutBufRetCode)
{
  CODEC_STATE state = CODEC_OK;
  FRAME frm;
  int index;

  memset(&frm, 0, sizeof(FRAME));

  if (pObj->codec)
    state = pObj->codec->getframe(pObj->codec, &frm, pObj->eosing);

  VPU_LOG("VPU_DecGetFrame state=%d",state);
  switch (state)
  {
    case CODEC_HAS_FRAME:
      index=VpuSearchFrameIndex(pObj, (unsigned char *)frm.fb_bus_address);
      if (-1==index)
      {
        VPU_ERROR("%s: failure: vpu can not find the frame buf, pInFrameBuf=0x%X \r\n",__FUNCTION__,(unsigned int)frm.fb_bus_address);
        return VPU_DEC_RET_INVALID_PARAM;
      }
      pObj->frameInfo.pDisplayFrameBuf = &pObj->frameBuf[index];
      pObj->frameInfo.pExtInfo=&pObj->frmExtInfo;
      //pObj->frameInfo.pExtInfo->nFrmWidth=pSrcInfo->width;
      //pObj->frameInfo.pExtInfo->nFrmHeight=pSrcInfo->height;
      //pObj->frameInfo.pExtInfo->FrmCropRect=pSrcInfo->frameCrop;
      pObj->frameInfo.pExtInfo->FrmCropRect.nRight=frm.outBufPrivate.nFrameWidth;
      pObj->frameInfo.pExtInfo->FrmCropRect.nBottom=frm.outBufPrivate.nFrameHeight;
      //pObj->frameInfo.pExtInfo->nQ16ShiftWidthDivHeightRatio=pSrcInfo->Q16ShiftWidthDivHeightRatio;
      if(frm.outBufPrivate.sRfcTable.nLumaBusAddress && frm.outBufPrivate.sRfcTable.nChromaBusAddress)
      {
        pObj->frameInfo.pExtInfo->rfc_luma_offset=frm.outBufPrivate.sRfcTable.nLumaBusAddress - frm.fb_bus_address;
        pObj->frameInfo.pExtInfo->rfc_chroma_offset=frm.outBufPrivate.sRfcTable.nChromaBusAddress - frm.fb_bus_address;
      }
      VPU_LOG("crop: %d %d\n", frm.outBufPrivate.nFrameWidth, frm.outBufPrivate.nFrameHeight);
      VPU_LOG("video frame base: %p: RFC table Luma: %p Chroma: %p\n", frm.fb_bus_address,
          frm.outBufPrivate.sRfcTable.nLumaBusAddress, frm.outBufPrivate.sRfcTable.nChromaBusAddress);

      *pOutBufRetCode |= VPU_DEC_OUTPUT_DIS;
      pObj->state=VPU_DEC_STATE_OUTOK;
      pObj->nOutFrameCount ++;
      pObj->total_frames ++;
      VPU_LOG("nOutFrameCount=%d",pObj->nOutFrameCount);
      //if (pObj->nOutFrameCount > 5)
      //*pOutBufRetCode |= VPU_DEC_NO_ENOUGH_BUF;
      if(VPU_DUMP_YUV)
        WrapperFileDumpYUV(pObj,pObj->frameInfo.pDisplayFrameBuf);
      break;
    case CODEC_END_OF_STREAM:
      VPU_ERROR("Got EOS from video decoder.\n");
      *pOutBufRetCode |= VPU_DEC_OUTPUT_EOS;
      break;
    default:
      break;
  }

  return VPU_DEC_RET_SUCCESS;
}

long long monotonic_time (void)
{
  struct timespec ts;

  clock_gettime (CLOCK_MONOTONIC, &ts);

  return (((long long) ts.tv_sec) * 1000000) + (ts.tv_nsec / 1000);
}

static VpuDecRetCode VPU_DecDecode(VpuDecObj* pObj, int* pOutBufRetCode)
{
  while(pObj->nBsBufLen > 0)
  {
    unsigned int first = 0;
    unsigned int last = 0;
    STREAM_BUFFER stream;
    memset(&stream, 0, sizeof(STREAM_BUFFER));

    stream.bus_data = pObj->pBsBufVirtStart + pObj->nBsBufOffset;
    stream.bus_address = (OSAL_BUS_WIDTH)pObj->pBsBufPhyStart + pObj->nBsBufOffset;
    stream.streamlen = pObj->nBsBufLen;
    stream.allocsize = VPU_BITS_BUF_SIZE;

    // see if we can find complete frames in the buffer
    int ret = pObj->codec->scanframe(pObj->codec, &stream, &first, &last);

    if (ret == -1 || first == last)
    {
      if (!pObj->eosing)
        break;
      // assume that remaining data contains a single complete decoding unit
      // fingers crossed..
      first = 0;
      last = stream.streamlen;
    }

    stream.streamlen = last - first;
    if (0)//pObj->nBsBufOffset + first > 0)
    {
      pObj->nBsBufLen -= first;
      memmove(pObj->pBsBufVirtStart, pObj->pBsBufVirtStart + pObj->nBsBufOffset + first, pObj->nBsBufLen);
      first = pObj->nBsBufOffset = 0;
    }

    // got at least one complete frame between first and last
    stream.bus_data = pObj->pBsBufVirtStart + pObj->nBsBufOffset + first;
    stream.buf_data = pObj->pBsBufVirtStart;
    stream.bus_address = (OSAL_BUS_WIDTH)pObj->pBsBufPhyStart + pObj->nBsBufOffset + first;
    stream.buf_address = (OSAL_BUS_WIDTH)pObj->pBsBufPhyStart;
    stream.sliceInfoNum =  pObj->slice_info_num;
    stream.pSliceInfo = (OMX_U8 *)pObj->slice_info;
    //stream.picId = pObj->propagateData.picIndex;

    unsigned int bytes = 0;
    FRAME frm;
    memset(&frm, 0, sizeof(FRAME));
#if 0
    printf ("\n");
    {
      char *tmp = stream.bus_data;
      for (int i=0; i<100; i++)
        printf ("%02x", tmp[i]);
    }
    printf ("\n");
#endif

    VPU_LOG("stream.pBsBufPhyStart = %p,offset=%d",(char*)pObj->pBsBufPhyStart,pObj->nBsBufOffset);
    VPU_LOG("decoder input stream length: %d\n", stream.streamlen);
    long long start_time = monotonic_time();
    CODEC_STATE codec =
      pObj->codec->decode(pObj->codec, &stream, &bytes, &frm);
    pObj->total_time += monotonic_time() - start_time;
    VPU_LOG("decoder return: %d byte consumed: %d\n", codec, bytes);

    pObj->nBsBufLen -= (int)bytes + (int)first;
    pObj->nBsBufOffset += (int)bytes + (int)first;
    if(pObj->nBsBufOffset >= VPU_BITS_BUF_SIZE)
      pObj->nBsBufOffset -= VPU_BITS_BUF_SIZE;
    pObj->nAccumulatedConsumedFrmBytes += (int)bytes + (int)first;
    bool dobreak = false;

    switch (codec)
    {
      case CODEC_OK:
        break;
      case CODEC_PENDING_FLUSH:
        if(pObj->nBsBufLen > 0 && bytes > 0 && pObj->bSecureMode)
            *pOutBufRetCode &= ~VPU_DEC_INPUT_USED;
        dobreak = true;
        break;
      case CODEC_NEED_MORE:
        break;
      case CODEC_BUFFER_EMPTY:
        break;
      case CODEC_NO_DECODING_BUFFER:
        *pOutBufRetCode |= VPU_DEC_NO_ENOUGH_BUF;
        dobreak = true;
        break;
      case CODEC_HAS_INFO:
        pObj->state = VPU_DEC_STATE_INITOK;
        if(pObj->nFrameSize == 0)
          *pOutBufRetCode |= VPU_DEC_INIT_OK;
        else
          *pOutBufRetCode |= VPU_DEC_RESOLUTION_CHANGED;
        return VPU_DEC_RET_SUCCESS;
      case CODEC_HAS_FRAME:
        *pOutBufRetCode |= VPU_DEC_ONE_FRM_CONSUMED;
        dobreak = true;
        break;
      case CODEC_ABORTED:
        return VPU_DEC_RET_SUCCESS;
      case CODEC_WAITING_FRAME_BUFFER:
        return VPU_DEC_RET_SUCCESS;
      case CODEC_PIC_SKIPPED:
        break;
      case CODEC_ERROR_STREAM:
        break;
      case CODEC_ERROR_STREAM_NOT_SUPPORTED:
        return VPU_DEC_RET_FAILURE;
      case CODEC_ERROR_FORMAT_NOT_SUPPORTED:
        return VPU_DEC_RET_FAILURE;
      case CODEC_ERROR_INVALID_ARGUMENT:
        return VPU_DEC_RET_INVALID_PARAM;
      case CODEC_ERROR_HW_TIMEOUT:
        return VPU_DEC_RET_FAILURE_TIMEOUT;
      case CODEC_ERROR_HW_BUS_ERROR:
        return VPU_DEC_RET_FAILURE;
      case CODEC_ERROR_SYS:
        return VPU_DEC_RET_FAILURE;
      case CODEC_ERROR_MEMFAIL:
        return VPU_DEC_RET_FAILURE;
      case CODEC_ERROR_NOT_INITIALIZED:
        return VPU_DEC_RET_WRONG_CALL_SEQUENCE;
      case CODEC_ERROR_HW_RESERVED:
        return VPU_DEC_RET_FAILURE;
      default:
        return VPU_DEC_RET_FAILURE;
    }
    if (dobreak)
      break;
  }

  if(pObj->nBsBufLen == 0)
    *pOutBufRetCode |= VPU_DEC_NO_ENOUGH_INBUF;

  if(pObj->eosing && pObj->nBsBufLen == 0)
  {
    VPU_ERROR("send EOS to video decoder.\n");
    pObj->codec->endofstream(pObj->codec);
  }

  return VPU_DEC_RET_SUCCESS;
}

static VpuDecRetCode RvParseHeader(VpuDecObj* pObj, VpuBufferNode* pInData)
{
  u32 tmp, length;
  u8 *buff;
  unsigned int imageSize;
  bool bIsRV8;
  int i, nPicWidth, nPicHeight;
  u32 num_frame_sizes = 0;
  u32 frame_sizes[18];
  u32 size[9] = {0,1,1,2,2,3,3,3,3};


  if (!pObj->codec)
  {
    buff = pInData->sCodecData.pData;

    if (pInData->sCodecData.nSize < 20)
      return VPU_DEC_RET_SUCCESS;

    length = (buff[0] << 24) |
      (buff[1] << 16) |
      (buff[2] <<  8) |
      (buff[3] <<  0);

    VPU_LOG("sequence len: %d\n", length);

    if (strncmp((const char*)(buff+8), "RV30", 4) == 0)
      bIsRV8 = true;
    else
      bIsRV8 = false;

    nPicWidth = (buff[12] << 8) | buff[13];
    nPicHeight = (buff[14] << 8) | buff[15];

    if (bIsRV8) {
      u32 j = 0;
      u8 *p = buff + 26;
      num_frame_sizes = 1 + (p[1] & 0x7);
      p += 8;
      frame_sizes[0] = nPicWidth;
      frame_sizes[1] = nPicHeight;
      for (j = 1; j < num_frame_sizes; j++) {
        frame_sizes[2*j + 0] = (*p++) << 2;
        frame_sizes[2*j + 1] = (*p++) << 2;
      }
    }
    pObj->codec =
      HantroHwDecOmx_decoder_create_rv(pObj->pdwl,
          bIsRV8, size[num_frame_sizes], frame_sizes,
          nPicWidth, nPicHeight,
          &pObj->config.g1_conf);
  }

  pInData->sCodecData.nSize = 0;
  pObj->nBsBufOffset = 0;
  buff = pInData->pVirAddr;

  if (pInData->nSize < 20)
    return VPU_DEC_RET_SUCCESS;

  length = (buff[0] << 24) |
    (buff[1] << 16) |
    (buff[2] <<  8) |
    (buff[3] <<  0);

  VPU_LOG("frame len: %d\n", length);

  buff += 16;
  pObj->slice_info_num = (buff[0] << 24) |
    (buff[1] << 16) |
    (buff[2] <<  8) |
    (buff[3] <<  0);
  VPU_LOG("slice info num: %d\n", pObj->slice_info_num);
  for (i = 0; i < pObj->slice_info_num; i ++)
  {
    buff += 4;
    pObj->slice_info[i].is_valid = (buff[0] << 24) |
      (buff[1] << 16) |
      (buff[2] <<  8) |
      (buff[3] <<  0);
    buff += 4;
    pObj->slice_info[i].offset = (buff[0] << 24) |
      (buff[1] << 16) |
      (buff[2] <<  8) |
      (buff[3] <<  0);
  }
  pInData->pVirAddr += 20 + pObj->slice_info_num * 8;
  pInData->nSize -= 20 + pObj->slice_info_num * 8;
  pObj->nAccumulatedConsumedStufferBytes += 20 + pObj->slice_info_num * 8;
  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecDecodeBuf(VpuDecHandle InHandle, VpuBufferNode* pInData,
    int* pOutBufRetCode)
{
  VpuDecHandleInternal * pVpuObj;
  VpuDecObj* pObj;
  VpuDecRetCode ret = VPU_DEC_RET_SUCCESS;
  if(InHandle==NULL)
  {
    VPU_ERROR("%s: failure: handle is null\n",__FUNCTION__);
    return VPU_DEC_RET_INVALID_HANDLE;
  }

  pVpuObj=(VpuDecHandleInternal *)InHandle;
  pObj=&pVpuObj->obj;

  *pOutBufRetCode = 0;

  VPU_DecGetFrame(pObj, pOutBufRetCode);
  if(*pOutBufRetCode & VPU_DEC_OUTPUT_DIS
      || *pOutBufRetCode & VPU_DEC_OUTPUT_EOS)
  {
    return VPU_DEC_RET_SUCCESS;
  }

  if(!pObj->nBsBufLen || pObj->frame_size)
  {
#if 0
    printf ("\n");
    {
      char *tmp = pInData->sCodecData.pData;
      for (int i=0; i<pInData->sCodecData.nSize; i++)
        printf ("%02x", tmp[i]);
    }
    printf ("\n");
    {
      char *tmp = pInData->pVirAddr;
      for (int i=0; i<100; i++)
        printf ("%02x", tmp[i]);
    }
    printf ("\n");
#endif

    if(pObj->bSecureMode)
        pObj->nBsBufOffset = 0;

    if(pObj->CodecFormat==VPU_V_RV)
      RvParseHeader(pObj, pInData);
    VPU_DecProcessInBuf(pObj, pInData);
    *pOutBufRetCode |= VPU_DEC_INPUT_USED;

    if(pObj->nBsBufLen < pObj->frame_size)
    {
      *pOutBufRetCode |= VPU_DEC_NO_ENOUGH_INBUF;
      return VPU_DEC_RET_SUCCESS;
    }

    if(pInData->pPhyAddr != NULL){
     pObj->pBsBufPhyStart = pInData->pPhyAddr;
    }
  }

  if(pObj->bConsumeInputLater && pObj->bSecureMode){
    *pOutBufRetCode |= VPU_DEC_INPUT_USED;
    pObj->bConsumeInputLater = false;
    VPU_ERROR("VPU_DecDecodeBuf bConsume VPU_DEC_INPUT_USED");
  }

  ret = VPU_DecDecode(pObj, pOutBufRetCode);

  if(!pObj->bSecureMode)
    return ret;

  if(*pOutBufRetCode & VPU_DEC_INIT_OK){
    *pOutBufRetCode &= ~VPU_DEC_INPUT_USED;
    pObj->bConsumeInputLater = true;
    VPU_ERROR("VPU_DecDecodeBuf VPU_DEC_INIT_OK not used");
  }else if(*pOutBufRetCode & VPU_DEC_NO_ENOUGH_BUF){
    *pOutBufRetCode &= ~VPU_DEC_INPUT_USED;
    pObj->bConsumeInputLater = true;
  //when pObj->codec->decode return CODEC_PENDING_FLUSH
  }else if( !(*pOutBufRetCode&VPU_DEC_INPUT_USED)){
    pObj->bConsumeInputLater = true;
  }

  return ret;
}

VpuDecRetCode VPU_DecGetInitialInfo(VpuDecHandle InHandle, VpuDecInitInfo * pOutInitInfo)
{
  VpuDecHandleInternal * pVpuObj;
  VpuDecObj* pObj;
  STREAM_INFO info;

  if(InHandle==NULL)
  {
    VPU_ERROR("%s: failure: handle is null\n",__FUNCTION__);
    return VPU_DEC_RET_INVALID_HANDLE;
  }

  pVpuObj=(VpuDecHandleInternal *)InHandle;
  pObj=&pVpuObj->obj;

  memset(&info, 0, sizeof(STREAM_INFO));

  CODEC_STATE ret = pObj->codec->getinfo(pObj->codec, &info);
  if (ret != CODEC_OK)
  {
    VPU_ERROR("%s: failure: getinfo fail\n",__FUNCTION__);
    return VPU_DEC_RET_FAILURE;
  }

  pOutInitInfo->nPicWidth = info.width;
  pOutInitInfo->nPicHeight = info.height;
  pOutInitInfo->nMinFrameBufferCount = info.frame_buffers;
  if (info.bit_depth == 0)
    pOutInitInfo->nBitDepth = 8;
  else
    pOutInitInfo->nBitDepth = info.bit_depth;
  if (info.crop_available)
  {
    pOutInitInfo->PicCropRect.nLeft = info.crop_left;
    pOutInitInfo->PicCropRect.nTop = info.crop_top;
    pOutInitInfo->PicCropRect.nRight = info.crop_width;
    pOutInitInfo->PicCropRect.nBottom = info.crop_height;
  }
  else
  {
    pOutInitInfo->PicCropRect.nLeft = 0;
    pOutInitInfo->PicCropRect.nTop = 0;
    pOutInitInfo->PicCropRect.nRight = info.width;
    pOutInitInfo->PicCropRect.nBottom = info.height;
  }

  if (info.hdr10_available)
  {
    pOutInitInfo->hasHdr10Meta = true;
    pOutInitInfo->Hdr10Meta.redPrimary[0] = info.hdr10_metadata.redPrimary[0];
    pOutInitInfo->Hdr10Meta.redPrimary[1] = info.hdr10_metadata.redPrimary[1];
    pOutInitInfo->Hdr10Meta.greenPrimary[0] = info.hdr10_metadata.greenPrimary[0];
    pOutInitInfo->Hdr10Meta.greenPrimary[1] = info.hdr10_metadata.greenPrimary[1];
    pOutInitInfo->Hdr10Meta.bluePrimary[0] = info.hdr10_metadata.bluePrimary[0];
    pOutInitInfo->Hdr10Meta.bluePrimary[1] = info.hdr10_metadata.bluePrimary[1];
    pOutInitInfo->Hdr10Meta.whitePoint[0] = info.hdr10_metadata.whitePoint[0];
    pOutInitInfo->Hdr10Meta.whitePoint[1] = info.hdr10_metadata.whitePoint[1];
    pOutInitInfo->Hdr10Meta.maxMasteringLuminance = info.hdr10_metadata.maxMasteringLuminance;
    pOutInitInfo->Hdr10Meta.minMasteringLuminance = info.hdr10_metadata.minMasteringLuminance;
    pOutInitInfo->Hdr10Meta.maxContentLightLevel = info.hdr10_metadata.maxContentLightLevel;
    pOutInitInfo->Hdr10Meta.maxFrameAverageLightLevel = info.hdr10_metadata.maxFrameAverageLightLevel;
  }
  else
  {
    pOutInitInfo->hasHdr10Meta = false;
  }

  if (info.colour_desc_available)
  {
    pOutInitInfo->ColourDesc.colourPrimaries = info.colour_primaries;
    pOutInitInfo->ColourDesc.transferCharacteristics = info.transfer_characteristics;
    pOutInitInfo->ColourDesc.matrixCoeffs = info.matrix_coeffs;
  }

  if (info.chroma_loc_info_available)
  {
    pOutInitInfo->ChromaLocInfo.chromaSampleLocTypeTopField = info.chroma_sample_loc_type_top_field;
    pOutInitInfo->ChromaLocInfo.chromaSampleLocTypeBottomField = info.chroma_sample_loc_type_bottom_field;
  }

  pObj->nFrameSize = info.framesize;
  pOutInitInfo->nFrameSize = info.framesize;
  VPU_LOG("%s: min frame count: %d \r\n",__FUNCTION__, pOutInitInfo->nMinFrameBufferCount);
  VPU_LOG("%s: buffer resolution: %dx%d image: %dx%d crop: %d %d %d %d \r\n",
      __FUNCTION__, info.stride, info.sliceheight, info.width, info.height,
      info.crop_left, info.crop_top, info.crop_width, info.crop_height);
  VPU_ERROR("%s: frame size: %d\n",__FUNCTION__, info.framesize);
  //update state
  pVpuObj->obj.state=VPU_DEC_STATE_REGFRMOK;

  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecRegisterFrameBuffer(VpuDecHandle InHandle,VpuFrameBuffer *pInFrameBufArray, int nNum)
{
  VpuDecHandleInternal * pVpuObj;
  VpuDecObj* pObj;
  int i;
  int targetNum;

  if(InHandle==NULL) 
  {
    VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);
    return VPU_DEC_RET_INVALID_HANDLE;
  }
  pVpuObj=(VpuDecHandleInternal *)InHandle;
  pObj=&pVpuObj->obj;
  targetNum = pVpuObj->obj.frameNum;

  //nNum should be only 1 or nMinFrameBufferCount after resolution changed.
  //we has no interface to reset the frameNum to 0 when resolution changed.
  //so reset it when number count is larger than 1
  if(nNum > 1){
    pVpuObj->obj.frameNum = 0;
    targetNum = 0;
    VPU_LOG("reset buffer cnt to 0");
  }

  if(targetNum + nNum >VPU_MAX_FRAME_INDEX)
  {
    VPU_ERROR("%s: failure: register frame number is too big(%d) \r\n",__FUNCTION__,nNum);
    return VPU_DEC_RET_INVALID_PARAM;
  }

  for(i=targetNum;i<targetNum + nNum;i++)
  {
    BUFFER buffer;
    CODEC_STATE ret = CODEC_ERROR_UNSPECIFIED;

    VPU_LOG("%s: register frame index: %d \r\n",__FUNCTION__, i);
    pVpuObj->obj.frameBuf[i]=*pInFrameBufArray;

    buffer.bus_data = pInFrameBufArray->pbufVirtY;
    buffer.bus_address = (OSAL_BUS_WIDTH)pInFrameBufArray->pbufY;
    buffer.allocsize = pObj->nFrameSize;

    pInFrameBufArray++;

    ret = pObj->codec->setframebuffer(pObj->codec, &buffer, nNum);
    if (ret == CODEC_ERROR_BUFFER_SIZE)
    {
      return VPU_DEC_RET_INVALID_PARAM;
    }
  }
  pVpuObj->obj.frameNum += nNum;

  //update state
  pVpuObj->obj.state=VPU_DEC_STATE_DEC;

  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecGetOutputFrame(VpuDecHandle InHandle, VpuDecOutFrameInfo * pOutFrameInfo)
{
  VpuDecHandleInternal * pVpuObj;
  VpuDecObj* pObj;

  if(InHandle==NULL) 
  {
    VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);
    return VPU_DEC_RET_INVALID_HANDLE;
  }
  pVpuObj=(VpuDecHandleInternal *)InHandle;
  pObj=&pVpuObj->obj;

  if(pVpuObj->obj.state!=VPU_DEC_STATE_OUTOK)
  {
    VPU_ERROR("%s: failure: error state: %d \r\n",__FUNCTION__,pVpuObj->obj.state);
    return VPU_DEC_RET_WRONG_CALL_SEQUENCE;
  }

  //update state
  pVpuObj->obj.state=VPU_DEC_STATE_DEC;
  VPU_TRACE;
  *pOutFrameInfo=pVpuObj->obj.frameInfo;
  VPU_TRACE;

  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecGetConsumedFrameInfo(VpuDecHandle InHandle,VpuDecFrameLengthInfo* pOutFrameLengthInfo)
{
  VpuDecHandleInternal * pVpuObj;
  VpuDecObj* pObj;

  if(InHandle==NULL) 
  {
    VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);
    return VPU_DEC_RET_INVALID_HANDLE;
  }
  pVpuObj=(VpuDecHandleInternal *)InHandle;
  pObj=&pVpuObj->obj;

  pOutFrameLengthInfo->pFrame=pVpuObj->obj.pLastDecodedFrm;
  pOutFrameLengthInfo->nStuffLength=pVpuObj->obj.nAccumulatedConsumedStufferBytes;
  pOutFrameLengthInfo->nFrameLength=pVpuObj->obj.nAccumulatedConsumedFrmBytes;
  VPU_ERROR("%s: Consumed bytes: %d + %d\n",__FUNCTION__, pOutFrameLengthInfo->nStuffLength, pOutFrameLengthInfo->nFrameLength);

  /*clear recorded info*/
  pVpuObj->obj.pLastDecodedFrm=NULL;
  pVpuObj->obj.nAccumulatedConsumedStufferBytes=0;
  pVpuObj->obj.nAccumulatedConsumedFrmBytes=0;
  pVpuObj->obj.nAccumulatedConsumedBytes=0;

  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecOutFrameDisplayed(VpuDecHandle InHandle, VpuFrameBuffer* pInFrameBuf)
{
  VpuDecHandleInternal * pVpuObj;
  VpuDecObj* pObj;
  BUFFER buff;

  if(InHandle==NULL)
  {
    VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);
    return VPU_DEC_RET_INVALID_HANDLE;
  }
  pVpuObj=(VpuDecHandleInternal *)InHandle;
  pObj=&pVpuObj->obj;

  buff.bus_data = pInFrameBuf->pbufVirtY;
  buff.bus_address = (OSAL_BUS_WIDTH)pInFrameBuf->pbufY;

  VpuSearchFrameIndex(pObj, (unsigned char *)buff.bus_address);

  pObj->codec->pictureconsumed(pObj->codec, &buff);
  pObj->nOutFrameCount --;
  VPU_LOG("VPU_DecOutFrameDisplayed nOutFrameCount=%d",pObj->nOutFrameCount);
  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecFlushAll(VpuDecHandle InHandle)
{
  VpuDecHandleInternal * pVpuObj;
  VpuDecObj* pObj;
  BUFFER buff;
  int OutBufRetCode;

  if(InHandle==NULL) 
  {
    VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);
    return VPU_DEC_RET_INVALID_HANDLE;
  }
  pVpuObj=(VpuDecHandleInternal *)InHandle;
  pObj=&pVpuObj->obj;

  do {
    OutBufRetCode = 0;
    VPU_DecGetFrame(pObj, &OutBufRetCode);
    if (OutBufRetCode & VPU_DEC_OUTPUT_DIS)
    {
      buff.bus_data = pVpuObj->obj.frameInfo.pDisplayFrameBuf->pbufVirtY;
      buff.bus_address = (OSAL_BUS_WIDTH)pVpuObj->obj.frameInfo.pDisplayFrameBuf->pbufY;
      pObj->codec->pictureconsumed(pObj->codec, &buff);
      pObj->nOutFrameCount --;
    }
  } while(OutBufRetCode & VPU_DEC_OUTPUT_DIS);

  if (pObj->codec)
  {
    pObj->codec->abort(pObj->codec);
    pObj->codec->abortafter(pObj->codec);
  }

  pObj->nBsBufLen=0;
  pObj->nBsBufOffset=0;
  //pObj->nPrivateSeqHeaderInserted=0;

  pObj->nAccumulatedConsumedStufferBytes=0;
  pObj->nAccumulatedConsumedFrmBytes=0;
  pObj->nAccumulatedConsumedBytes=0;
  pObj->pLastDecodedFrm=NULL;
  pObj->nOutFrameCount = 0;
  pObj->eosing = false;

  pObj->state=VPU_DEC_STATE_EOS;

  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecAllRegFrameInfo(VpuDecHandle InHandle, VpuFrameBuffer** ppOutFrameBuf, int* pOutNum)
{
  VpuDecHandleInternal * pVpuObj;
  int i;

  pVpuObj=(VpuDecHandleInternal *)InHandle;
  for(i=0;i<pVpuObj->obj.frameNum;i++)
  {
    *ppOutFrameBuf++=&pVpuObj->obj.frameBuf[i];
  }
  *pOutNum=pVpuObj->obj.frameNum;
  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecGetNumAvailableFrameBuffers(VpuDecHandle InHandle,int* pOutBufNum)
{
  int i, cnt;
  VpuDecHandleInternal * pVpuObj;
  pVpuObj=(VpuDecHandleInternal *)InHandle;

  cnt=0;
  for (i=0;i<pVpuObj->obj.frameNum;i++){
    if (pVpuObj->obj.frameBufState[i] == VPU_FRAME_STATE_FREE){
      cnt++;
    }
  }
  *pOutBufNum=cnt;
  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecClose(VpuDecHandle InHandle)
{
  VpuDecHandleInternal * pVpuObj;
  VpuDecObj* pObj;

  if(InHandle==NULL) 
  {
    VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);
    return VPU_DEC_RET_INVALID_HANDLE;
  }
  pVpuObj=(VpuDecHandleInternal *)InHandle;
  pObj=&pVpuObj->obj;

  VPU_LOG("Total consumed time: %0.5f\n", ((double)pObj->total_time)/1000000);
  VPU_LOG("Total frames: %d\n", pObj->total_frames);
  if(pObj->total_time > 0)
  {
    VPU_LOG("Video decode fps: %0.2f\n", ((double)pObj->total_frames*1000000)/pObj->total_time);
  }

  if (pObj->codec)
    pObj->codec->destroy(pObj->codec);

  if (pObj->pdwl)
    DWLRelease(pObj->pdwl);
  pObj->pdwl = NULL;

  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecUnLoad()
{
  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecReset(VpuDecHandle InHandle)
{
  VpuDecHandleInternal * pVpuObj;
  VpuDecObj* pObj;
  VPU_LOG("in VPU_DecReset, InHandle: 0x%X  \r\n",InHandle);

  if(InHandle==NULL)
  {
    VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);
    return VPU_DEC_RET_INVALID_HANDLE;
  }
  pVpuObj=(VpuDecHandleInternal *)InHandle;
  pObj=&pVpuObj->obj;

  if (pObj->codec)
  {
    pObj->codec->abort(pObj->codec);
    pObj->codec->abortafter(pObj->codec);
  }

  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecGetErrInfo(VpuDecHandle InHandle,VpuDecErrInfo* pErrInfo)
{
  /*it return the last error info*/
  VpuDecHandleInternal * pVpuObj;
  if(InHandle==NULL){
    VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);
    return VPU_DEC_RET_INVALID_HANDLE;
  }
  pVpuObj=(VpuDecHandleInternal *)InHandle;
  *pErrInfo=pVpuObj->obj.nLastErrorInfo;
  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecGetMem(VpuMemDesc* pInOutMem)
{
  struct DWLLinearMem info;
  struct DWLInitParam dwlInit;
  const void *pdwl = NULL;

  dwlInit.client_type = DWL_CLIENT_TYPE_HEVC_DEC;
  pdwl = (void*)DWLInit(&dwlInit);
  if (!pdwl)
  {
    VPU_ERROR("%s: DWLInit failed !! \r\n",__FUNCTION__);
    return VPU_DEC_RET_FAILURE;
  }

  if(pInOutMem->nType == VPU_MEM_DESC_NORMAL)
    info.mem_type = DWL_MEM_TYPE_CPU;
  else if(pInOutMem->nType == VPU_MEM_DESC_SECURE)
    info.mem_type = DWL_MEM_TYPE_SLICE;

  int ret = DWLMallocLinear(pdwl, pInOutMem->nSize, &info);
  if (ret < 0)
  {
    return VPU_DEC_RET_FAILURE;
  }

  pInOutMem->nPhyAddr=info.bus_address;
  pInOutMem->nVirtAddr=(unsigned long)info.virtual_address;
  pInOutMem->nCpuAddr=info.ion_fd;

  if (pdwl)
    DWLRelease(pdwl);

  return VPU_DEC_RET_SUCCESS;

}

VpuDecRetCode VPU_DecFreeMem(VpuMemDesc* pInMem)
{
  struct DWLLinearMem info;
  struct DWLInitParam dwlInit;
  const void *pdwl = NULL;

  info.size = pInMem->nSize;
  info.virtual_address = (u32*)pInMem->nVirtAddr;
  info.bus_address = pInMem->nPhyAddr;
  info.ion_fd = pInMem->nCpuAddr;
  if(pInMem->nType == VPU_MEM_DESC_NORMAL)
    info.mem_type = DWL_MEM_TYPE_CPU;
  else if(pInMem->nType == VPU_MEM_DESC_SECURE)
    info.mem_type = DWL_MEM_TYPE_SLICE;

  dwlInit.client_type = DWL_CLIENT_TYPE_HEVC_DEC;
  pdwl = (void*)DWLInit(&dwlInit);
  if (!pdwl)
  {
    VPU_ERROR("%s: DWLInit failed !! \r\n",__FUNCTION__);
    return VPU_DEC_RET_FAILURE;
  }
  VPU_LOG("VPU_DecFreeMem fd=%d",info.ion_fd);
  DWLFreeLinear(pdwl, &info);

  if (pdwl)
    DWLRelease(pdwl);

  return VPU_DEC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncLoad()
{
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncUnLoad()
{
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncReset(VpuEncHandle InHandle)
{
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncOpen(VpuEncHandle *pOutHandle, VpuMemInfo* pInMemInfo,VpuEncOpenParam* pInParam)
{
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncOpenSimp(VpuEncHandle *pOutHandle, VpuMemInfo* pInMemInfo,VpuEncOpenParamSimp * pInParam)
{
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncClose(VpuEncHandle InHandle)
{
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncGetInitialInfo(VpuEncHandle InHandle, VpuEncInitInfo * pOutInitInfo)
{
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncGetVersionInfo(VpuVersionInfo * pOutVerInfo)
{
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncGetWrapperVersionInfo(VpuWrapperVersionInfo * pOutVerInfo)
{
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncRegisterFrameBuffer(VpuEncHandle InHandle,VpuFrameBuffer *pInFrameBufArray, int nNum,int nSrcStride)
{
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncQueryMem(VpuMemInfo* pOutMemInfo)
{
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncGetMem(VpuMemDesc* pInOutMem)
{
  return VPU_ENC_RET_SUCCESS;
}


VpuEncRetCode VPU_EncFreeMem(VpuMemDesc* pInMem)
{
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncConfig(VpuEncHandle InHandle, VpuEncConfig InEncConf, void* pInParam)
{
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncEncodeFrame(VpuEncHandle InHandle, VpuEncEncParam* pInOutParam)
{
  return VPU_ENC_RET_SUCCESS;
}
