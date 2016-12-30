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

static int nVpuLogLevel=1;		//bit 0: api log; bit 1: raw dump; bit 2: yuv dump
#ifdef ANDROID_BUILD
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

#define MAX_YUV_FRAME  (1000)
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
#define VPU_BITS_BUF_SIZE		(3*1024*1024)		//bitstream buffer size : big enough contain two big frames

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

static const void *pdwl = NULL;

typedef struct
{
  /* open parameters */
  VpuCodStd CodecFormat;

  /* hantro decoder */
  CODEC_PROTOTYPE *codec;

  /* decode parameters */
  int iframeSearchEnable;
  int skipFrameMode;
  int skipFrameNum;

  int inputType;			/*normal, kick, drain(EOS)*/
  int streamBufDelaySize;	/*unit: bytes. used in stream mode:  valid data size should reach the threshold before decoding*/
  int initDataCountThd;
  VpuDecErrInfo nLastErrorInfo;  /*it record the last error info*/

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
}VpuDecObj;

typedef struct 
{
	VpuDecObj obj;
}VpuDecHandleInternal;

VpuDecRetCode VPU_DecLoad()
{
  struct DWLInitParam dwlInit;

  // Need lock to ensure singleton.
  if (!pdwl)
  {
    dwlInit.client_type = DWL_CLIENT_TYPE_HEVC_DEC;
    pdwl = (void*)DWLInit(&dwlInit);
    if (!pdwl)
    {
      VPU_ERROR("%s: DWLInit failed !! \r\n",__FUNCTION__);
      return VPU_DEC_RET_FAILURE;
    }
  }

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
  OMX_VIDEO_PARAM_G2CONFIGTYPE g2Conf;
  OMX_VIDEO_PARAM_G1CONFIGTYPE g1Conf;
  bool bDeblock, bIsRV8, bIsMvcStream;
  unsigned int imageSize;
  VpuDecObj* pObj;

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

  if (!pdwl)
  {
    VPU_ERROR("%s: DWL haven't Init !! \r\n",__FUNCTION__);
    return VPU_DEC_RET_FAILURE;
  }

  pVpuObj=(VpuDecHandleInternal*)pMemVirt->pVirtAddr;
  pObj=&pVpuObj->obj;

  memset(pObj, 0, sizeof(VpuDecObj));

  g2Conf.bEnableTiled = false;
  if (pInParam->nTiled2LinearEnable)
    g2Conf.bEnableTiled = true;
  g2Conf.ePixelFormat = OMX_VIDEO_G2PixelFormat_8bit;
  g2Conf.bEnableRFC = false;
  if (pInParam->nEnableVideoCompressor)
    g2Conf.bEnableRFC = true;
  g2Conf.bEnableRingBuffer = pObj->ringbuffer = false;
  g1Conf.bEnableTiled = false;
  g1Conf.bAllowFieldDBP = false;

  VPU_LOG("format: %d \r\n",pInParam->CodecFormat);
  switch (pInParam->CodecFormat) {
    case VPU_V_AVC:
      pObj->codec = HantroHwDecOmx_decoder_create_h264(pdwl,
          bIsMvcStream, &g1Conf);
      VPU_LOG("open H.264 \r\n");
      break;
    case VPU_V_MPEG2: 	 /**< AKA: H.262 */
      pObj->codec = HantroHwDecOmx_decoder_create_mpeg2(pdwl,
          &g1Conf);
      VPU_LOG("open Mpeg2 \r\n");
      break;
    case VPU_V_H263:		 /**< H.263 */
      pObj->codec = HantroHwDecOmx_decoder_create_mpeg4(pdwl,
          bDeblock, MPEG4FORMAT_H263, &g1Conf);
      VPU_LOG("open H263 \r\n");
      break;
    case VPU_V_MPEG4: 	 /**< MPEG-4 */
      pObj->codec = HantroHwDecOmx_decoder_create_mpeg4(pdwl,
          bDeblock, MPEG4FORMAT_MPEG4, &g1Conf);
      VPU_LOG("open Mpeg4 \r\n");
      break;
    case VPU_V_DIVX4:		/**< DIVX 4 */
    case VPU_V_DIVX56:		/**< DIVX 5/6 */
      pObj->codec = HantroHwDecOmx_decoder_create_mpeg4(pdwl,
          bDeblock, MPEG4FORMAT_CUSTOM_1, &g1Conf);
      VPU_LOG("open DIVX 4 \r\n");
      VPU_LOG("open DIVX 56 \r\n");
      break;
    case VPU_V_XVID:		/**< XVID */
      VPU_LOG("open XVID \r\n");
      break;
    case VPU_V_DIVX3:		/**< DIVX 3 */
      pObj->codec = HantroHwDecOmx_decoder_create_mpeg4(pdwl,
          bDeblock, MPEG4FORMAT_CUSTOM_1_3, &g1Conf);
      VPU_LOG("open DIVX 3 \r\n");
      break;
    case VPU_V_RV:		
      pObj->codec =
        HantroHwDecOmx_decoder_create_rv(pdwl,
            bIsRV8, imageSize,
            pInParam->nPicWidth, pInParam->nPicHeight,
            &g1Conf);
      VPU_LOG("open RV \r\n");
      break;
    case VPU_V_VC1:		 /**< all versions of Windows Media Video */
    case VPU_V_VC1_AP:
      pObj->codec = HantroHwDecOmx_decoder_create_vc1(pdwl,
          &g1Conf);
      VPU_LOG("open VC1 \r\n");
      break;
    case VPU_V_AVC_MVC:
      pObj->codec = HantroHwDecOmx_decoder_create_h264(pdwl,
          bIsMvcStream, &g1Conf);
      VPU_LOG("open H.264 MVC \r\n");
      break;
    case VPU_V_MJPG:
      pObj->codec = HantroHwDecOmx_decoder_create_jpeg(true);
      VPU_LOG("open MJPEG \r\n");
      break;
    case VPU_V_AVS:
      pObj->codec = HantroHwDecOmx_decoder_create_avs(pdwl,
          &g1Conf);
      VPU_LOG("open AVS \r\n");
      break;
    case VPU_V_VP8:
      pObj->codec = HantroHwDecOmx_decoder_create_vp8(pdwl,
          &g1Conf);
      VPU_LOG("open VP8 \r\n");
      break;
    case VPU_V_HEVC:
      g2Conf.bEnableRingBuffer = pObj->ringbuffer = true;
      pObj->codec = HantroHwDecOmx_decoder_create_hevc(pdwl,
          &g2Conf);
      VPU_LOG("open HEVC \r\n");
      break;
    case VPU_V_VP9:
      g2Conf.bEnableRingBuffer = pObj->ringbuffer = true;
      pObj->codec = HantroHwDecOmx_decoder_create_vp9(pdwl,
          &g2Conf);
      VPU_LOG("open VP9 \r\n");
      break;
    default:
      VPU_ERROR("%s: failure: invalid format !!! \r\n",__FUNCTION__);
      return VPU_DEC_RET_INVALID_PARAM;
  }

  PP_ARGS pp_args;  // post processor parameters
  memset(&pp_args, 0, sizeof(PP_ARGS));
  if (pObj->codec->setppargs(pObj->codec, &pp_args) != CODEC_OK)
  {
  }

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
    default:
      VPU_ERROR("%s: failure: invalid setting \r\n",__FUNCTION__);
      return VPU_DEC_RET_INVALID_PARAM;
  }

  return VPU_DEC_RET_SUCCESS;
}

static void VpuPutInBuf(VpuDecObj* pObj, unsigned char *pIn, unsigned int len)
{
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
}

static VpuDecRetCode VPU_DecProcessInBuf(VpuDecObj* pObj, VpuBufferNode* pInData)
{
  unsigned char* pHeader=NULL;
  unsigned int headerLen=0;
  unsigned int headerAllocated=0;

  if(pInData->pVirAddr == 0x01 && pInData->nSize == 0)
    pObj->eosing = true;

  if(pInData->pVirAddr == NULL || pInData->nSize == 0)
    return VPU_DEC_RET_SUCCESS;

  if(pObj->nPrivateSeqHeaderInserted == 0 && 0 != pInData->sCodecData.nSize)
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
    else{
      pHeader=pInData->sCodecData.pData;
      headerLen=pInData->sCodecData.nSize;
    }
    VpuPutInBuf(pObj, pHeader, headerLen);
    pObj->nAccumulatedConsumedFrmBytes -= headerLen;

    if(headerAllocated){
      free(pHeader);
    }
    pObj->nPrivateSeqHeaderInserted=1;
  }			

  if(pObj->nIsAvcc){
    unsigned char* pFrm=NULL;
    unsigned int nFrmSize;
    VpuConvertAvccFrame(pInData->pVirAddr,pInData->nSize,pObj->nNalSizeLen,
        &pFrm,&nFrmSize,&pObj->nNalNum);
    VpuPutInBuf(pObj, pFrm, nFrmSize);
    if(pFrm!=pInData->pVirAddr){
      free(pFrm);
    }
  } else {
    VpuPutInBuf(pObj, pInData->pVirAddr, pInData->nSize);
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
  CODEC_STATE state = CODEC_HAS_FRAME;
  FRAME frm;
  int index;

  memset(&frm, 0, sizeof(FRAME));

  state = pObj->codec->getframe(pObj->codec, &frm, pObj->eosing);
  switch (state)
  {
    case CODEC_HAS_FRAME:
      index=VpuSearchFrameIndex(pObj, frm.fb_bus_address);
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

      *pOutBufRetCode |= VPU_DEC_OUTPUT_DIS;
      pObj->state=VPU_DEC_STATE_OUTOK;
      pObj->nOutFrameCount ++;
      //if (pObj->nOutFrameCount > 5)
      //*pOutBufRetCode |= VPU_DEC_NO_ENOUGH_BUF;
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

static VpuDecRetCode VPU_DecDecode(VpuDecObj* pObj, int* pOutBufRetCode)
{
  while(pObj->nBsBufLen > 0)
  {
    unsigned int first = 0;
    unsigned int last = 0;
    STREAM_BUFFER stream;

    stream.bus_data = pObj->pBsBufVirtStart + pObj->nBsBufOffset;
    stream.bus_address = pObj->pBsBufPhyStart + pObj->nBsBufOffset;
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
    stream.bus_address = pObj->pBsBufPhyStart + pObj->nBsBufOffset + first;
    stream.buf_address = pObj->pBsBufPhyStart;
    //stream.sliceInfoNum =  pObj->sliceInfoNum;
    //stream.pSliceInfo =  pObj->pSliceInfo;
    //stream.picId = pObj->propagateData.picIndex;

    unsigned int bytes = 0;
    FRAME frm;
    memset(&frm, 0, sizeof(FRAME));

    VPU_LOG("decoder input stream length: %d\n", stream.streamlen);
    CODEC_STATE codec =
      pObj->codec->decode(pObj->codec, &stream, &bytes, &frm);
    VPU_LOG("decoder return: %d byte consumed: %d\n", codec, bytes);

    pObj->nBsBufLen -= bytes + first;
    pObj->nBsBufOffset += bytes + first;
    if(pObj->nBsBufOffset >= VPU_BITS_BUF_SIZE)
      pObj->nBsBufOffset -= VPU_BITS_BUF_SIZE;
    pObj->nAccumulatedConsumedFrmBytes += bytes + first;
    bool dobreak = false;

    switch (codec)
    {
      case CODEC_OK:
        break;
      case CODEC_NEED_MORE:
        *pOutBufRetCode |= VPU_DEC_NO_ENOUGH_INBUF;
        break;
      case CODEC_BUFFER_EMPTY:
        break;
      case CODEC_NO_DECODING_BUFFER:
        *pOutBufRetCode |= VPU_DEC_NO_ENOUGH_BUF;
        dobreak = true;
        break;
      case CODEC_HAS_INFO:
        pObj->state = VPU_DEC_STATE_INITOK;
        break;
      case CODEC_HAS_FRAME:
        *pOutBufRetCode |= VPU_DEC_ONE_FRM_CONSUMED;
        dobreak = true;
        break;
      case CODEC_ABORTED:
        return VPU_DEC_RET_SUCCESS;
      case CODEC_WAITING_FRAME_BUFFER:
        if(pObj->nFrameSize == 0)
          *pOutBufRetCode |= VPU_DEC_INIT_OK;
        else
          *pOutBufRetCode |= VPU_DEC_RESOLUTION_CHANGED;
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

VpuDecRetCode VPU_DecDecodeBuf(VpuDecHandle InHandle, VpuBufferNode* pInData,
    int* pOutBufRetCode)
{
  VpuDecHandleInternal * pVpuObj;
  VpuDecObj* pObj;

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

  if(!pObj->nBsBufLen)
  {
    VPU_DecProcessInBuf(pObj, pInData);
    *pOutBufRetCode |= VPU_DEC_INPUT_USED;
  }

  return VPU_DecDecode(pObj, pOutBufRetCode);
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

  if(pVpuObj->obj.state!=VPU_DEC_STATE_INITOK)
  {
    VPU_ERROR("%s: failure: error state %d \r\n",__FUNCTION__,pVpuObj->obj.state);
    return VPU_DEC_RET_WRONG_CALL_SEQUENCE;
  }

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

  pObj->nFrameSize = info.framesize;
  pOutInitInfo->nFrameSize = info.framesize;
  VPU_LOG("%s: min frame count: %d \r\n",__FUNCTION__, pOutInitInfo->nMinFrameBufferCount);
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

  if(InHandle==NULL) 
  {
    VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);
    return VPU_DEC_RET_INVALID_HANDLE;
  }
  pVpuObj=(VpuDecHandleInternal *)InHandle;
  pObj=&pVpuObj->obj;

  if(pVpuObj->obj.state!=VPU_DEC_STATE_REGFRMOK)
  {
    VPU_ERROR("%s: failure: error state %d \r\n",__FUNCTION__,pVpuObj->obj.state);
    return VPU_DEC_RET_WRONG_CALL_SEQUENCE;
  }

  if(nNum>VPU_MAX_FRAME_INDEX)
  {
    VPU_ERROR("%s: failure: register frame number is too big(%d) \r\n",__FUNCTION__,nNum);
    return VPU_DEC_RET_INVALID_PARAM;
  }

  for(i=0;i<nNum;i++)
  {
    BUFFER buffer;
    CODEC_STATE ret = CODEC_ERROR_UNSPECIFIED;

    VPU_LOG("%s: register frame index: %d \r\n",__FUNCTION__, i);
    pVpuObj->obj.frameBuf[i]=*pInFrameBufArray;

    buffer.bus_data = pInFrameBufArray->pbufVirtY;
    buffer.bus_address = pInFrameBufArray->pbufY;
    buffer.allocsize = pObj->nFrameSize;

    pInFrameBufArray++;

    ret = pObj->codec->setframebuffer(pObj->codec, &buffer, nNum);
    if (ret == CODEC_ERROR_BUFFER_SIZE)
    {
      return VPU_DEC_RET_INVALID_PARAM;
    }
  }
  pVpuObj->obj.frameNum=nNum;

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
  buff.bus_address = pInFrameBuf->pbufY;

  pObj->codec->pictureconsumed(pObj->codec, &buff);
  pObj->nOutFrameCount --;

  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecFlushAll(VpuDecHandle InHandle)
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

  pObj->codec->abort(pObj->codec);
  pObj->codec->abortafter(pObj->codec);

  pObj->nBsBufLen=0;
  pObj->nBsBufOffset=0;
  //pObj->nPrivateSeqHeaderInserted=0;

  pObj->nAccumulatedConsumedStufferBytes=0;
  pObj->nAccumulatedConsumedFrmBytes=0;
  pObj->nAccumulatedConsumedBytes=0;
  pObj->pLastDecodedFrm=NULL;
  pObj->nOutFrameCount = 0;

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

  pObj->codec->destroy(pObj->codec);

  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecUnLoad()
{
  if (pdwl)
    DWLRelease(pdwl);
  pdwl = NULL;

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

  pObj->codec->abort(pObj->codec);
  pObj->codec->abortafter(pObj->codec);

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

  if (!pdwl)
  {
    return VPU_DEC_RET_FAILURE;
  }

  int ret = DWLMallocLinear(pdwl, pInOutMem->nSize, &info);
  if (ret < 0)
  {
    return VPU_DEC_RET_FAILURE;
  }

  pInOutMem->nPhyAddr=info.bus_address;
  pInOutMem->nVirtAddr=info.virtual_address;
  pInOutMem->nCpuAddr=info.ion_fd;

  return VPU_DEC_RET_SUCCESS;

}

VpuDecRetCode VPU_DecFreeMem(VpuMemDesc* pInMem)
{
  struct DWLLinearMem info;
  struct DWLInitParam dwlInit;

  info.size = pInMem->nSize;
  info.virtual_address = pInMem->nVirtAddr;
  info.bus_address = pInMem->nPhyAddr;
  info.ion_fd = pInMem->nCpuAddr;

  dwlInit.client_type = DWL_CLIENT_TYPE_HEVC_DEC;
  if (!pdwl)
  {
    return VPU_DEC_RET_FAILURE;
  }

  DWLFreeLinear(pdwl, &info);

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
