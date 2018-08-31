/*!
 *	CopyRight Notice:
 *	Copyright 2018 NXP
 *
 *	History :
 *	Date	(y.m.d)		Author			Version			Description
 *	2018-06-10		  Bao Xiahong		0.1				Created
 */

/** Vpu_wrapper_hantro_encoder.c
 *	vpu wrapper file contain all related hantro video decoder api exposed to
 *	application
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "headers/OMX_Video.h"
#include "headers/OMX_VideoExt.h"
#include "ewl.h"
#include "encoder/codec.h"
#include "encoder/encoder.h"
#include "encoder/encoder_h264.h"
#include "encoder/encoder_vp8.h"

#include "utils.h"
#include "vpu_wrapper.h"

static int nVpuLogLevel=0;      //bit 0: api log; bit 1: raw dump; bit 2: yuv dump
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
#define DUMP_ALL_DATA       1
static int g_seek_dump=DUMP_ALL_DATA;   /*0: only dump data after seeking; otherwise: dump all data*/


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


#define VPU_MEM_ALIGN           0x10
#define VPU_BITS_BUF_SIZE       (4*1024*1024)      //bitstream buffer size : big enough for 1080p h264/vp8 with max bitrate

#define VPU_ENC_SEQ_DATA_SEPERATE
#define VPU_ENC_MAX_RETRIES 10000
#define VPU_ENC_DEFAULT (-255)
#define VPU_ENC_MAX_FRAME_INDEX	30
#define VPU_ENC_MIN_BITRTE 10000

#define VIRT_INDEX  0
#define PHY_INDEX   1

#define MAX_WIDTH 1920
#define MIN_WIDTH 132
#define MAX_HEIGHT 1088
#define MIN_HEIGHT 96

#define VPU_ENC_DEFAULT_ALIGNMENT_H 8
#define VPU_ENC_DEFAULT_ALIGNMENT_V 8

#define H264_ENC_MAX_GOP_SIZE 300

#define H264_ENC_MAX_BITRATE (50000*1200)    /* Level 4.1 limit */
#define VP8_ENC_MAX_BITRATE 60000000

#define H264_ENC_QP_DEFAULT 33
#define VP8_ENC_QP_DEFAULT 26

#define ALIGN(ptr,align)       ((align) ? (((unsigned long)(ptr))/(align)*(align)) : ((unsigned long)(ptr)))
#define MemAlign(mem,align) ((((unsigned int)mem)%(align))==0)
#define MemNotAlign(mem,align)  ((((unsigned int)mem)%(align))!=0)

#define VPU_ENC_LOG(...) if(nVpuLogLevel&0x1) {LOG_PRINTF(__VA_ARGS__);}
#define VPU_ENC_TRACE
#define VPU_ENC_API(...) if(nVpuLogLevel&0x1) {LOG_PRINTF(__VA_ARGS__);}
#define VPU_ENC_ERROR(...) if(nVpuLogLevel&0x1) {LOG_PRINTF(__VA_ARGS__);}
#define VPU_ENC_ASSERT(exp) if((!(exp))&&(nVpuLogLevel&0x1)) {LOG_PRINTF("%s: %d : assert condition !!!\r\n",__FUNCTION__,__LINE__);}

/* table for max frame size for each video level */
typedef struct {
    int level;
    int size;    // mbPerFrame, (Width / 16) * (Height / 16)
}EncLevelSizeMap;

/* H264 level size map table, sync with h1 h264 encoder */
static const EncLevelSizeMap H264LevelSizeMapTable[] = {
    {OMX_VIDEO_AVCLevel1,  99},
    {OMX_VIDEO_AVCLevel1b, 99},
    {OMX_VIDEO_AVCLevel11, 396},
    {OMX_VIDEO_AVCLevel12, 396},
    {OMX_VIDEO_AVCLevel13, 396},
    {OMX_VIDEO_AVCLevel2,  396},
    {OMX_VIDEO_AVCLevel21, 792},
    {OMX_VIDEO_AVCLevel22, 1620},
    {OMX_VIDEO_AVCLevel3,  1620},
    {OMX_VIDEO_AVCLevel31, 3600},
    {OMX_VIDEO_AVCLevel32, 5120},
    {OMX_VIDEO_AVCLevel4,  8192},
    {OMX_VIDEO_AVCLevel41, 8192},
    {OMX_VIDEO_AVCLevel42, 8704},
    {OMX_VIDEO_AVCLevel5,  22080},
    {OMX_VIDEO_AVCLevel51, 65025},
};

static int AlignWidth(int width, int align)
{
  if (!align)
    return width;
  else if (width - align < MIN_WIDTH)
    return MIN_WIDTH;
  else
    return ((width) / align * align);
}


static int AlignHeight(int height, int align)
{
  if (!align)
    return height;
  else if (height - align < MIN_HEIGHT)
    return MIN_HEIGHT;
  else
    return (height) / align * align;
}

int VpuEncLogLevelParse(int * pLogLevel)
{
  int level=0;
  FILE* fpVpuLog;
  fpVpuLog=fopen(VPU_LOG_LEVELFILE,"r");
  if (NULL==fpVpuLog){
    //LOG_PRINTF("no vpu log level file: %s \r\n",VPU_LOG_LEVELFILE);
  }
  else  {
    char symbol;
    int readLen = 0;

    readLen = fread(&symbol,1,1,fpVpuLog);
    if(feof(fpVpuLog) != 0){
      //LOG_PRINTF("\n End of file reached.");
    }
    else    {
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

#define OMX_INIT_VERSION_STRUCT(param) \
    do { \
        (param).nSize = sizeof(param); \
        (param).nVersion.s.nVersionMajor = 0x1; \
        (param).nVersion.s.nVersionMinor = 0x1; \
        (param).nVersion.s.nRevision = 0x2; \
        (param).nVersion.s.nStep = 0x0; \
    } while(0);


typedef struct
{
  /* open parameters */
  VpuCodStd CodecFormat;
  int bStreamStarted;
  int bAvcc;

  /* perf calculation*/
  int totalFrameCnt;
  struct timeval tvBegin;
  struct timeval tvEnd;

  const void *pewl;
  /* hantro decoder */
  ENCODER_PROTOTYPE *codec;
  OMX_VIDEO_PARAM_CONFIGTYPE config;
  VIDEO_ENCODER_CONFIG encConfig;

  /* bit stream buffer */
  unsigned char* pBsBufVirt;
  unsigned char* pBsBufPhy;
  int nBsBufLen;
}VpuEncObj;

typedef struct
{
  VpuEncObj obj;
}VpuEncHandleInternal;

static void dumpStream(unsigned char* pBuf, unsigned int len)
{
   FILE * pfile;
   pfile = fopen(VPU_DUMP_RAWFILE,"ab");
   if(pfile){
     fwrite((void*)pBuf, 1, len, pfile);
     fclose(pfile);
   }
}

static void dumpYUV(unsigned char* pBuf, unsigned int len)
{
   FILE * pfile;
   pfile = fopen(VPU_DUMP_YUVFILE,"ab");
   if(pfile){
     fwrite((void*)pBuf, 1, len, pfile);
     fclose(pfile);
   }
}

static OMX_COLOR_FORMATTYPE VPU_EncConvertColorFmtVpu2Omx(VpuColorFormat vpuColorFmt, int chromaInterleave)
{
  OMX_COLOR_FORMATTYPE omxColorFmt = OMX_COLOR_FormatUnused;

  switch(vpuColorFmt) {
    case VPU_COLOR_420:
      omxColorFmt = chromaInterleave ? OMX_COLOR_FormatYUV420SemiPlanar : OMX_COLOR_FormatYUV420Planar;
      break;
    case VPU_COLOR_422YUYV:
      omxColorFmt = OMX_COLOR_FormatYCbYCr;
      break;
    case VPU_COLOR_422UYVY:
      omxColorFmt = OMX_COLOR_FormatCbYCrY;
      break;
    case VPU_COLOR_ARGB8888:
      omxColorFmt = OMX_COLOR_Format32bitBGRA8888;
      break;
    case VPU_COLOR_BGRA8888:
      omxColorFmt = OMX_COLOR_Format32bitARGB8888;
      break;
    case VPU_COLOR_RGB565:
      omxColorFmt = OMX_COLOR_Format16bitRGB565;
      break;
    case VPU_COLOR_RGB555:
      omxColorFmt = OMX_COLOR_Format16bitARGB1555;
      break;
    case VPU_COLOR_BGR565:
      omxColorFmt = OMX_COLOR_Format16bitBGR565;
      break;
    case VPU_COLOR_444:
      // not support yet
      break;
    case VPU_COLOR_400:
      // not support yet
      break;
    default:
      VPU_ENC_ERROR("unknown vpu color format %d\n", vpuColorFmt);
      break;
  }
  return omxColorFmt;
}


static VpuEncRetCode VPU_EncSetAvcDefaults(VpuEncObj* pEncObj)
{
  OMX_VIDEO_PARAM_AVCTYPE* config  =  0;
  config = &pEncObj->encConfig.avc;
  config->nPortIndex = 1;
  config->eProfile = OMX_VIDEO_AVCProfileBaseline;
  config->eLevel = OMX_VIDEO_AVCLevel1;
  config->eLoopFilterMode = OMX_VIDEO_AVCLoopFilterEnable;
  config->nRefFrames = 1;
  config->nAllowedPictureTypes &= OMX_VIDEO_PictureTypeI | OMX_VIDEO_PictureTypeP;
  config->nPFrames = 150;
  config->nSliceHeaderSpacing = 0 ;
  config->nBFrames = 0;
  config->bUseHadamard = OMX_FALSE;
  config->nRefIdx10ActiveMinus1 = 0;
  config->nRefIdx11ActiveMinus1 = 0;
  config->bEnableUEP = OMX_FALSE;
  config->bEnableFMO = OMX_FALSE;
  config->bEnableASO = OMX_FALSE;
  config->bEnableRS = OMX_FALSE;
  config->bFrameMBsOnly = OMX_FALSE;
  config->bMBAFF = OMX_FALSE;
  config->bEntropyCodingCABAC = OMX_FALSE;
  config->bWeightedPPrediction = OMX_FALSE;
  config->nWeightedBipredicitonMode = OMX_FALSE;
  config->bconstIpred = OMX_FALSE;
  config->bDirect8x8Inference = OMX_FALSE;
  config->bDirectSpatialTemporal = OMX_FALSE;
  config->nCabacInitIdc = OMX_FALSE;
  config->eLoopFilterMode = OMX_VIDEO_AVCLoopFilterEnable;
#ifdef CONFORMANCE
  config->eLevel = OMX_VIDEO_AVCLevel1;
  config->bUseHadamard = OMX_TRUE;
#endif

  OMX_VIDEO_CONFIG_AVCINTRAPERIOD* avcIdr;
  avcIdr = &pEncObj->encConfig.avcIdr;
  avcIdr->nPFrames = 150;
  avcIdr->nIDRPeriod = 150;

  OMX_PARAM_DEBLOCKINGTYPE* deb = 0;
  deb = &pEncObj->encConfig.deblocking;
  deb->nPortIndex = 1;
  deb->bDeblocking = OMX_TRUE;

  OMX_VIDEO_PARAM_QUANTIZATIONTYPE* quantization  = 0;
  quantization = &pEncObj->encConfig.videoQuantization;
  quantization->nPortIndex = 1;
  quantization->nQpI = 36;
  quantization->nQpP = 36;
  quantization->nQpB = 0; //Not used

  return VPU_ENC_RET_SUCCESS;
}

static VpuEncRetCode VPU_EncSetVp8Defaults(VpuEncObj* pEncObj)
{
  OMX_VIDEO_PARAM_VP8TYPE* config  =  0;
  config = &pEncObj->encConfig.vp8;
  config->nPortIndex = 1; // output port
  config->eProfile = OMX_VIDEO_VP8ProfileMain;
  config->eLevel = OMX_VIDEO_VP8Level_Version0;
  config->nDCTPartitions = 0;
  config->bErrorResilientMode = OMX_FALSE;

  OMX_VIDEO_VP8REFERENCEFRAMETYPE* vp8Ref = 0;
  vp8Ref = &pEncObj->encConfig.vp8Ref;
  vp8Ref->bPreviousFrameRefresh = OMX_TRUE;
  vp8Ref->bGoldenFrameRefresh = OMX_FALSE;
  vp8Ref->bAlternateFrameRefresh = OMX_FALSE;
  vp8Ref->bUsePreviousFrame = OMX_TRUE;
  vp8Ref->bUseGoldenFrame = OMX_FALSE;
  vp8Ref->bUseAlternateFrame = OMX_FALSE;

  OMX_VIDEO_PARAM_QUANTIZATIONTYPE* quantization  = 0;
  quantization = &pEncObj->encConfig.videoQuantization;
  quantization->nPortIndex = 1;
  quantization->nQpI = 20;
  quantization->nQpP = 20;
  quantization->nQpB = 0; //Not used

  return VPU_ENC_RET_SUCCESS;
}

static VpuEncRetCode VPU_EncSetPreProcessorDefaults(VpuEncObj* pEncObj, unsigned int width, unsigned int height)
{
  OMX_CONFIG_ROTATIONTYPE* rotation = 0;
  rotation = &pEncObj->encConfig.rotation;
  rotation->nPortIndex = 0;
  rotation->nRotation = 0;

  OMX_CONFIG_RECTTYPE* crop = 0;
  crop = &pEncObj->encConfig.crop;
  crop->nPortIndex = 0;
  crop->nLeft = 0;
  crop->nTop = 0;
  crop->nWidth = width;
  crop->nHeight = height;

  return VPU_ENC_RET_SUCCESS;
}


static VpuEncRetCode VPU_EncSetBitrateDefaults(VpuEncObj* pEncObj, unsigned int bitrate)
{
  OMX_VIDEO_PARAM_BITRATETYPE* config = 0;
  config = &pEncObj->encConfig.bitrate;
  config->nPortIndex = 0;
  config->eControlRate = OMX_Video_ControlRateVariable;
  config->nTargetBitrate = bitrate;
#ifdef CONFORMANCE
  config->eControlRate = OMX_Video_ControlRateConstant;
#endif
  return VPU_ENC_RET_SUCCESS;
}

static VpuEncRetCode  VPU_EncSetCommonConfig(
    VpuEncObj* pEncObj,
    ENCODER_COMMON_CONFIG* pCommonCfg,
    RATE_CONTROL_CONFIG* pRateCfg,
    PRE_PROCESSOR_CONFIG* pPpCfg,
    int frameRate,
    int qpMin,
    int qpMax,
    VpuColorFormat colorFmt,
    int chromaInterleave)
{
  int validWidth, validHeight;

  pPpCfg->origWidth = AlignWidth(pEncObj->encConfig.crop.nWidth, VPU_ENC_DEFAULT_ALIGNMENT_H);
  pPpCfg->origHeight = AlignHeight(pEncObj->encConfig.crop.nHeight, VPU_ENC_DEFAULT_ALIGNMENT_V);
  pPpCfg->formatType = VPU_EncConvertColorFmtVpu2Omx(colorFmt, chromaInterleave);
  pPpCfg->angle = pEncObj->encConfig.rotation.nRotation;
  pPpCfg->frameStabilization = OMX_FALSE; // disable stabilization as default ?

  if (pPpCfg->frameStabilization) {
    pPpCfg->xOffset = 0;
    pPpCfg->yOffset = 0;
  } else {
    pPpCfg->xOffset = pEncObj->encConfig.crop.nLeft;
    pPpCfg->yOffset = pEncObj->encConfig.crop.nTop;
  }

  validWidth = pPpCfg->origWidth;
  validHeight = pPpCfg->origHeight;

  if (pEncObj->encConfig.rotation.nRotation == 90 || pEncObj->encConfig.rotation.nRotation == 270) {
    validWidth = pPpCfg->origHeight;
    validHeight = pPpCfg->origWidth;
  }

  pCommonCfg->nInputFramerate = FLOAT_Q16(frameRate);
  pCommonCfg->nOutputWidth = validWidth;
  pCommonCfg->nOutputHeight = validHeight;

  pRateCfg->nQpMin = qpMin; //0
  pRateCfg->nQpMax = qpMax; //51
  pRateCfg->eRateControl = pEncObj->encConfig.bitrate.eControlRate;
  pRateCfg->nTargetBitrate = pEncObj->encConfig.bitrate.nTargetBitrate;

  switch (pRateCfg->eRateControl)
  {
    case OMX_Video_ControlRateDisable:
    {
      pRateCfg->nMbRcEnabled = 0;
      pRateCfg->nHrdEnabled = 0;
      pRateCfg->nPictureRcEnabled = 0;
    }
    break;
    case OMX_Video_ControlRateVariable:
    case OMX_Video_ControlRateVariableSkipFrames:
    {
      pRateCfg->nMbRcEnabled = 1; //0;
      pRateCfg->nHrdEnabled = 0;
      pRateCfg->nPictureRcEnabled = 1;
    }
    break;
    case OMX_Video_ControlRateConstant:
    case OMX_Video_ControlRateConstantSkipFrames:
    {
      pRateCfg->nMbRcEnabled = 1;
      pRateCfg->nHrdEnabled = 1;
      pRateCfg->nPictureRcEnabled = 1;
    }
    break;
    default:
    {
      pRateCfg->eRateControl = OMX_Video_ControlRateDisable;
      pRateCfg->nMbRcEnabled = 0;
      pRateCfg->nHrdEnabled = 0;
      pRateCfg->nPictureRcEnabled = 0;
    }
    break;
  }

  // workaround to set check bitrate if nPictureRc enabled, calculate target bitrate =
  // bitPerFrame * frameRate / compression, so that resolution from max - min can get a approprite bitrate
  if (pRateCfg->nTargetBitrate < VPU_ENC_MIN_BITRTE && pRateCfg->nPictureRcEnabled) {
    int bitPerFrame = pPpCfg->origWidth * pPpCfg->origHeight * 8;
    int compression = 50;
    pRateCfg->nTargetBitrate = bitPerFrame / compression  * frameRate / 1000 * 1000;
    pEncObj->encConfig.bitrate.nTargetBitrate = pRateCfg->nTargetBitrate;
  }

  return VPU_ENC_RET_SUCCESS;
}


static VpuEncRetCode VPU_EncStartEncode(VpuEncObj *pObj, STREAM_BUFFER* pOutputStream)
{
  CODEC_STATE codecState = CODEC_ERROR_UNSPECIFIED;

  codecState = pObj->codec->stream_start(pObj->codec, pOutputStream);

  if (codecState != CODEC_OK) {
    VPU_ENC_ERROR("%s error, codecState %d\n", __FUNCTION__, codecState);
    return VPU_ENC_RET_FAILURE;
  }

  pObj->bStreamStarted = 1;

  return VPU_ENC_RET_SUCCESS;
}

static VpuEncRetCode VPU_EncDoEncode(VpuEncObj *pObj, FRAME* pFrame, STREAM_BUFFER* pStream)
{

  CODEC_STATE codecState = CODEC_ERROR_UNSPECIFIED;

  codecState = pObj->codec->encode(pObj->codec, pFrame, pStream, &pObj->encConfig);

  switch (codecState) {
    case CODEC_OK:
      break;
    case CODEC_CODED_INTRA:
      break;
    case CODEC_CODED_PREDICTED:
      break;
    case CODEC_CODED_SLICE:
      break;
    case CODEC_ERROR_HW_TIMEOUT:
      VPU_ENC_ERROR("codec->encode() returned CODEC_ERROR_HW_TIMEOUT");
      return VPU_ENC_RET_FAILURE_TIMEOUT;
    case CODEC_ERROR_HW_BUS_ERROR:
      VPU_ENC_ERROR("codec->encode() returned CODEC_ERROR_HW_BUS_ERROR");
      return VPU_ENC_RET_FAILURE;
    case CODEC_ERROR_HW_RESET:
      VPU_ENC_ERROR("codec->encode() returned CODEC_ERROR_HW_RESET");
      return VPU_ENC_RET_FAILURE;
    case CODEC_ERROR_SYSTEM:
      VPU_ENC_ERROR("codec->encode() returned CODEC_ERROR_SYSTEM");
      return VPU_ENC_RET_FAILURE;
    case CODEC_ERROR_RESERVED:
      VPU_ENC_ERROR("codec->encode() returned CODEC_ERROR_RESERVED");
      return VPU_ENC_RET_FAILURE;
    case CODEC_ERROR_INVALID_ARGUMENT:
      VPU_ENC_ERROR("codec->encode() returned CODEC_ERROR_INVALID_ARGUMENT");
      return VPU_ENC_RET_INVALID_PARAM;
    case CODEC_ERROR_BUFFER_OVERFLOW:
      VPU_ENC_ERROR("Output buffer size is too small");
      VPU_ENC_ERROR("codec->encode() returned CODEC_ERROR_BUFFER_OVERFLOW");
      return VPU_ENC_RET_INSUFFICIENT_FRAME_BUFFERS;
    default:
      VPU_ENC_ERROR("codec->encode() returned undefined error: %d", codecState);
      return VPU_ENC_RET_FAILURE;
  }

  if (pStream->streamlen > pStream->buf_max_size) {
    VPU_ENC_ERROR("%s: output buffer is too small, need %d but actual is %d\n",
        __FUNCTION__, pStream->streamlen, pStream->buf_max_size);
    return VPU_ENC_RET_INSUFFICIENT_FRAME_BUFFERS;
  }

  return VPU_ENC_RET_SUCCESS;
}

#if 0
static VpuEncRetCode VPU_EncStreamSpecProcess(VpuEncObj *pObj, VpuEncEncParam* pInOutParam)
{
  STREAM_BUFFER stream;
  CODEC_STATE codecState = CODEC_ERROR_UNSPECIFIED;

  memset(&stream, 0, sizeof(STREAM_BUFFER));
  stream.buf_max_size = pInOutParam->nInOutputBufLen;
  stream.bus_data = (OMX_U8*)pInOutParam->nInVirtOutput;
  stream.bus_address = pInOutParam->nInPhyOutput;

  codecState = pObj->codec->stream_end(pObj->codec, &stream);

  if (codecState != CODEC_OK) {
    VPU_ENC_ERROR("%s: stream_end failed, codecState=%d\n", __FUNCTION__, codecState);
    return VPU_ENC_RET_FAILURE;
  }

  pInOutParam->nOutOutputSize = stream.streamlen;
  return VPU_ENC_RET_SUCCESS;
}
#endif

VpuEncRetCode VPU_EncLoad()
{
  VpuEncLogLevelParse(NULL);

  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncUnLoad()
{
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncReset(VpuEncHandle InHandle)
{
  // hantro encoder doesn't have an interface to reset
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncOpen(VpuEncHandle *pOutHandle, VpuMemInfo* pInMemInfo,VpuEncOpenParam* pInParam)
{
  VpuMemSubBlockInfo * pMemPhy;
  VpuMemSubBlockInfo * pMemVirt;
  VpuEncHandleInternal* pVpuObj;
  VpuEncObj* pObj;
  struct EWLInitParam ewlInit;

  pMemVirt = &pInMemInfo->MemSubBlock[VIRT_INDEX];
  pMemPhy = &pInMemInfo->MemSubBlock[PHY_INDEX];
  if ((pMemVirt->pVirtAddr == NULL) || MemNotAlign(pMemVirt->pVirtAddr, VPU_MEM_ALIGN)
      || (pMemVirt->nSize != sizeof(VpuEncHandleInternal)))
  {
    VPU_ENC_ERROR("%s: failure: invalid parameter ! \r\n", __FUNCTION__);
    return VPU_ENC_RET_INVALID_PARAM;
  }

  if ((pMemPhy->pVirtAddr == NULL) || MemNotAlign(pMemPhy->pVirtAddr, VPU_MEM_ALIGN)
      || (pMemPhy->pPhyAddr == NULL) || MemNotAlign(pMemPhy->pPhyAddr, VPU_MEM_ALIGN)
      || (pMemPhy->nSize != (VPU_BITS_BUF_SIZE)))
  {
    VPU_ENC_ERROR("%s: failure: invalid parameter !! \r\n", __FUNCTION__);
    return VPU_ENC_RET_INVALID_PARAM;
  }

  pVpuObj = (VpuEncHandleInternal*)pMemVirt->pVirtAddr;
  pObj = &pVpuObj->obj;

  memset(pObj, 0, sizeof(VpuEncObj));

  pObj->bStreamStarted = 0;
  pObj->bAvcc = ((pInParam->nIsAvcc && VPU_V_AVC == pInParam->eFormat) ? 1 : 0);

  pObj->totalFrameCnt = 0;
  pObj->pBsBufPhy = pMemPhy->pPhyAddr;
  pObj->pBsBufVirt = pMemPhy->pVirtAddr;
  pObj->nBsBufLen = pMemPhy->nSize;

#if 0
  if (pInParam->eFormat == VPU_V_AVC)
  {
    ewlInit.clientType = EWL_CLIENT_TYPE_H264_ENC  ;
  }
  else if (pInParam->eFormat == VPU_V_VP8)
  {
    ewlInit.clientType = EWL_CLIENT_TYPE_VP8_ENC;
  }

  pObj->pewl = (void*)EWLInit(&ewlInit);
  if (!pObj->pewl)
  {
    VPU_ENC_ERROR("%s: EWLInit failed !! \r\n",__FUNCTION__);
    return VPU_ENC_RET_FAILURE;
  }
#endif

  OMX_INIT_VERSION_STRUCT(pObj->encConfig.avc);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.avcIdr);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.deblocking);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.ec);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.bitrate);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.stab);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.videoQuantization);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.rotation);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.crop);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.intraRefreshVop);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.vp8);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.vp8Ref);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.adaptiveRoi);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.temporalLayer);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.intraArea);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.roi1Area);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.roi2Area);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.roi1DeltaQP);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.roi2DeltaQP);
  OMX_INIT_VERSION_STRUCT(pObj->encConfig.intraRefresh);

  pObj->encConfig.intraArea.nTop      = VPU_ENC_DEFAULT;
  pObj->encConfig.intraArea.nLeft     = VPU_ENC_DEFAULT;
  pObj->encConfig.intraArea.nBottom   = VPU_ENC_DEFAULT;
  pObj->encConfig.intraArea.nRight    = VPU_ENC_DEFAULT;

  pObj->encConfig.roi1Area.nTop       = VPU_ENC_DEFAULT;
  pObj->encConfig.roi1Area.nLeft      = VPU_ENC_DEFAULT;
  pObj->encConfig.roi1Area.nBottom    = VPU_ENC_DEFAULT;
  pObj->encConfig.roi1Area.nRight     = VPU_ENC_DEFAULT;

  pObj->encConfig.roi2Area.nTop       = VPU_ENC_DEFAULT;
  pObj->encConfig.roi2Area.nLeft      = VPU_ENC_DEFAULT;
  pObj->encConfig.roi2Area.nBottom    = VPU_ENC_DEFAULT;
  pObj->encConfig.roi2Area.nRight     = VPU_ENC_DEFAULT;

  VPU_EncSetPreProcessorDefaults(pObj, pInParam->nPicWidth, pInParam->nPicHeight);
  VPU_EncSetBitrateDefaults(pObj, pInParam->nBitRate);

  pObj->encConfig.deblocking.bDeblocking = OMX_FALSE;
  pObj->encConfig.rotation.nRotation = pInParam->nRotAngle;
  pObj->encConfig.prependSPSPPSToIDRFrames = OMX_TRUE;

  VPU_ENC_LOG("VPU_EncOpen param w %d h %d bitrate %d gop %d frame rate %d qpmin %d qpmax %d\n",
    pInParam->nPicWidth, pInParam->nPicHeight, pInParam->nBitRate, pInParam->nGOPSize,
    pInParam->nFrameRate, pInParam->nUserQpMin, pInParam->nUserQpMax);

  switch (pInParam->eFormat) {
    case VPU_V_AVC:
    {
      H264_CONFIG config;

      memset(&config, 0, sizeof(H264_CONFIG));

      VPU_EncSetAvcDefaults(pObj);
      VPU_EncSetCommonConfig(pObj, &config.common_config, &config.rate_config, &config.pp_config,
            pInParam->nFrameRate, pInParam->nUserQpMin, pInParam->nUserQpMax,
            pInParam->eColorFormat, pInParam->nChromaInterleave);

      pObj->encConfig.avc.nPFrames = (pInParam->nGOPSize > H264_ENC_MAX_GOP_SIZE ? H264_ENC_MAX_GOP_SIZE : pInParam->nGOPSize);

      // adjust H264 level based on video resolution because h1 encoder will check this.
      int i, mbPerFrame, tableLen;
      mbPerFrame = ((pInParam->nPicWidth + 15) / 16) * ((pInParam->nPicHeight + 15) / 16);
      tableLen = sizeof(H264LevelSizeMapTable)/sizeof(H264LevelSizeMapTable[0]);

      for (i = 0; i < tableLen; i++) {
        if (pObj->encConfig.avc.eLevel == H264LevelSizeMapTable[i].level) {
          if (mbPerFrame <= H264LevelSizeMapTable[i].size)
            break;
          else if (i + 1 < tableLen)
            pObj->encConfig.avc.eLevel = H264LevelSizeMapTable[i + 1].level;
          else
            return VPU_ENC_RET_INVALID_PARAM;
        }
      }

      config.h264_config.eProfile = pObj->encConfig.avc.eProfile;
      config.h264_config.eLevel = pObj->encConfig.avc.eLevel;
      config.bDisableDeblocking = !pObj->encConfig.deblocking.bDeblocking;
      config.bSeiMessages = OMX_FALSE;
      config.nSliceHeight = 0;
      config.nPFrames = pObj->encConfig.avc.nPFrames;
      if (pInParam->eColorFormat == VPU_COLOR_ARGB8888 || pInParam->eColorFormat == VPU_COLOR_BGRA8888 ||
          pInParam->eColorFormat == VPU_COLOR_RGB565 || pInParam->eColorFormat == VPU_COLOR_RGB555 ||
          pInParam->eColorFormat == VPU_COLOR_BGR565)
        config.nVideoFullRange = 1; /* set 1 to imply that 845S H1 HW only support YUV full range when do RGB->YUV CSC(color space convert). */

      if (config.rate_config.nTargetBitrate > H264_ENC_MAX_BITRATE)
        config.rate_config.nTargetBitrate = H264_ENC_MAX_BITRATE;

      config.rate_config.nQpDefault = (pInParam->nRcIntraQp > 0 ? pInParam->nRcIntraQp : H264_ENC_QP_DEFAULT);

      pObj->codec = HantroHwEncOmx_encoder_create_h264(&config);
      VPU_ENC_LOG("open H.264 \r\n");
      break;
    }
    case VPU_V_VP8:
    {
      VP8_CONFIG config;

      VPU_EncSetVp8Defaults(pObj);
      VPU_EncSetCommonConfig(pObj, &config.common_config, &config.rate_config, &config.pp_config,
            pInParam->nFrameRate, pInParam->nUserQpMin, pInParam->nUserQpMax,
            pInParam->eColorFormat, pInParam->nChromaInterleave);

      config.vp8_config.eProfile = pObj->encConfig.vp8.eProfile;
      config.vp8_config.eLevel = pObj->encConfig.vp8.eLevel;
      config.vp8_config.nDCTPartitions = pObj->encConfig.vp8.nDCTPartitions;
      config.vp8_config.bErrorResilientMode = pObj->encConfig.vp8.bErrorResilientMode;

      if (config.rate_config.nTargetBitrate > VP8_ENC_MAX_BITRATE)
        config.rate_config.nTargetBitrate = VP8_ENC_MAX_BITRATE;

      config.rate_config.nQpDefault = (pInParam->nRcIntraQp > 0 ? pInParam->nRcIntraQp : VP8_ENC_QP_DEFAULT);

      pObj->codec = HantroHwEncOmx_encoder_create_vp8(&config);
      VPU_ENC_LOG("open VP8 \r\n");
      break;
    }
    default:
      VPU_ENC_ERROR("%s: failure: invalid format !!! \r\n",__FUNCTION__);
      return VPU_ENC_RET_INVALID_PARAM;
  }

  if (NULL == pObj->codec) {
    VPU_ENC_ERROR("HantroHwEncOmx_encoder_create failed\n");
    return VPU_ENC_RET_FAILURE;
  }

  *pOutHandle=(VpuEncHandle)pVpuObj;
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncOpenSimp(VpuEncHandle *pOutHandle, VpuMemInfo* pInMemInfo,VpuEncOpenParamSimp * pInParam)
{
  VpuEncRetCode ret;
  VpuEncOpenParam sEncOpenParamMore;

  memset(&sEncOpenParamMore,0,sizeof(VpuEncOpenParam));

  sEncOpenParamMore.eFormat = pInParam->eFormat;
  sEncOpenParamMore.nPicWidth = pInParam->nPicWidth;
  sEncOpenParamMore.nPicHeight = pInParam->nPicHeight;
  sEncOpenParamMore.nRotAngle = pInParam->nRotAngle;
  sEncOpenParamMore.nFrameRate = pInParam->nFrameRate;
  sEncOpenParamMore.nBitRate = pInParam->nBitRate * 1000; //kbps->bps
  sEncOpenParamMore.nGOPSize = pInParam->nGOPSize;

  sEncOpenParamMore.nChromaInterleave = pInParam->nChromaInterleave;
  sEncOpenParamMore.sMirror = pInParam->sMirror;

  sEncOpenParamMore.nMapType = pInParam->nMapType;
  sEncOpenParamMore.nLinear2TiledEnable = pInParam->nLinear2TiledEnable;
  sEncOpenParamMore.eColorFormat = pInParam->eColorFormat;

  sEncOpenParamMore.sliceMode.sliceMode = 0;  /* 0: 1 slice per picture; 1: Multiple slices per picture */
  sEncOpenParamMore.sliceMode.sliceSizeMode = 0; /* 0: silceSize defined by bits; 1: sliceSize defined by MB number*/
  sEncOpenParamMore.sliceMode.sliceSize = 4000;  /* Size of a slice in bits or MB numbers */

  sEncOpenParamMore.nInitialDelay = 0;
  sEncOpenParamMore.nVbvBufferSize = 0;

  sEncOpenParamMore.nIntraRefresh = pInParam->nIntraRefresh;

  if(0 == pInParam->nIntraQP) {
    sEncOpenParamMore.nRcIntraQp = -1;
  } else {
    sEncOpenParamMore.nRcIntraQp = pInParam->nIntraQP;
  }

  sEncOpenParamMore.nUserQpMax = 0;
  sEncOpenParamMore.nUserQpMin = 0;
  sEncOpenParamMore.nUserQpMinEnable = 0;
  sEncOpenParamMore.nUserQpMaxEnable = 0;

  sEncOpenParamMore.nUserGamma = (int)(0.75*32768);         /*  (0*32768 <= gamma <= 1*32768) */
  sEncOpenParamMore.nRcIntervalMode = 0;        /* 0:normal, 1:frame_level, 2:slice_level, 3: user defined Mb_level */
  sEncOpenParamMore.nMbInterval = 0;
  sEncOpenParamMore.nAvcIntra16x16OnlyModeEnable = 0;

  //set some default value structure 'VpuEncOpenParamMore'
  switch(pInParam->eFormat)
  {
    case VPU_V_AVC:
      sEncOpenParamMore.VpuEncStdParam.avcParam.avc_constrainedIntraPredFlag = 0;
      sEncOpenParamMore.VpuEncStdParam.avcParam.avc_disableDeblk = 0;
      sEncOpenParamMore.VpuEncStdParam.avcParam.avc_deblkFilterOffsetAlpha = 0;//6;  set 0 to improve quality: ENGR00305955: bottom line flicker issue
      sEncOpenParamMore.VpuEncStdParam.avcParam.avc_deblkFilterOffsetBeta = 0;
      sEncOpenParamMore.VpuEncStdParam.avcParam.avc_chromaQpOffset = 0;
      sEncOpenParamMore.VpuEncStdParam.avcParam.avc_audEnable = 0;
      sEncOpenParamMore.VpuEncStdParam.avcParam.avc_fmoEnable = 0;
      sEncOpenParamMore.VpuEncStdParam.avcParam.avc_fmoType = 0;
      sEncOpenParamMore.VpuEncStdParam.avcParam.avc_fmoSliceNum = 1;
      sEncOpenParamMore.VpuEncStdParam.avcParam.avc_fmoSliceSaveBufSize = 32; /* FMO_SLICE_SAVE_BUF_SIZE */
      break;
    default:
      //unknow format ?
      //return VPU_ENC_RET_INVALID_PARAM;
      break;
  }

  sEncOpenParamMore.nIsAvcc = pInParam->nIsAvcc;
  ret = VPU_EncOpen(pOutHandle, pInMemInfo, &sEncOpenParamMore);
  return ret;
}

VpuEncRetCode VPU_EncClose(VpuEncHandle InHandle)
{
  VpuEncHandleInternal * pVpuObj;
  VpuEncObj* pObj;
  CODEC_STATE codecState = CODEC_ERROR_UNSPECIFIED;
  STREAM_BUFFER stream;

  if(InHandle==NULL)
  {
    VPU_ENC_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);
    return VPU_ENC_RET_INVALID_HANDLE;
  }
  pVpuObj = (VpuEncHandleInternal *)InHandle;
  pObj = &pVpuObj->obj;

  /* record encode end time and calculate fps */
  unsigned long long encodeUs = 0;
  gettimeofday (&pObj->tvEnd, NULL);
  encodeUs = (pObj->tvEnd.tv_sec-pObj->tvBegin.tv_sec)*1000+(pObj->tvEnd.tv_usec-pObj->tvBegin.tv_usec)/1000;
  VPU_ENC_LOG("**** vpu enc: total frame %d encode time %lld fps %f\n",
      pObj->totalFrameCnt, encodeUs, pObj->totalFrameCnt * 1000.0 / encodeUs);

  if (pObj->codec) {
/*
    // not sure if stream_end is needed, its output is 0x00 00 00 01\n
    memcpy(&stream, &(pObj->outputStreamBuf[pObj->outputIndex]), sizeof(STREAM_BUFFER));
    stream.buf_max_size = 115200;
    codecState = pObj->codec->stream_end(pObj->codec, &stream);
    if (codecState != CODEC_OK) {
      VPU_ENC_ERROR("enc stream_end failed, codecState %d\n", codecState)
    }
*/
    pObj->codec->destroy(pObj->codec);
  }

  if (pObj->pewl)
    EWLRelease(pObj->pewl);
  pObj->pewl = NULL;

  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncGetInitialInfo(VpuEncHandle InHandle, VpuEncInitInfo * pOutInitInfo)
{
  pOutInitInfo->nMinFrameBufferCount = 0; // Fixme later, hantro enc has no api to query out init info
  pOutInitInfo->nAddressAlignment = 1;    // Fixme later, hantro enc has no api to query out init info
  pOutInitInfo->eType = VPU_TYPE_HANTRO;
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncGetVersionInfo(VpuVersionInfo * pOutVerInfo)
{
  if(pOutVerInfo == NULL)
  {
    VPU_ENC_ERROR("%s: failure: invalid parameterl \r\n",__FUNCTION__);
    return VPU_ENC_RET_INVALID_PARAM;
  }

  pOutVerInfo->nFwMajor = 1;
  pOutVerInfo->nFwMinor = 1;
  pOutVerInfo->nFwRelease = 1;
  pOutVerInfo->nLibMajor = 1;
  pOutVerInfo->nLibMinor = 1;
  pOutVerInfo->nLibRelease = 1;
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncGetWrapperVersionInfo(VpuWrapperVersionInfo * pOutVerInfo)
{
  pOutVerInfo->nMajor = (VPU_WRAPPER_VERSION_CODE >> (16)) & 0xff;
  pOutVerInfo->nMinor = (VPU_WRAPPER_VERSION_CODE >> (8)) & 0xff;
  pOutVerInfo->nRelease = (VPU_WRAPPER_VERSION_CODE) & 0xff;
  pOutVerInfo->pBinary = (char*)VPUWRAPPER_BINARY_VERSION_STR;
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncRegisterFrameBuffer(VpuEncHandle InHandle,VpuFrameBuffer *pInFrameBufArray, int nNum,int nSrcStride)
{
  // do nothing because h1 encoder don't need register frame buffer
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncQueryMem(VpuMemInfo* pOutMemInfo)
{
  VpuMemSubBlockInfo * pMem;

  if(pOutMemInfo == NULL)
  {
    VPU_ENC_ERROR("%s: failure: invalid parameterl \r\n", __FUNCTION__);
    return VPU_ENC_RET_INVALID_PARAM;
  }
  pMem = &pOutMemInfo->MemSubBlock[VIRT_INDEX];
  pMem->MemType = VPU_MEM_VIRT;
  pMem->nAlignment = VPU_MEM_ALIGN;
  pMem->nSize = sizeof(VpuEncHandleInternal);
  pMem->pVirtAddr = NULL;
  pMem->pPhyAddr = NULL;

  pMem = &pOutMemInfo->MemSubBlock[PHY_INDEX];
  pMem->MemType = VPU_MEM_PHY;
  pMem->nAlignment = VPU_MEM_ALIGN;
  pMem->nSize = VPU_BITS_BUF_SIZE;
  pMem->pVirtAddr = NULL;
  pMem->pPhyAddr = NULL;

  pOutMemInfo->nSubBlockNum = 2;
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncGetMem(VpuMemDesc* pInOutMem)
{
  struct EWLLinearMem info;
  struct EWLInitParam ewlInit;
  const void *pewl = NULL;

  ewlInit.clientType = EWL_CLIENT_TYPE_H264_ENC;
  pewl = (void*)EWLInit(&ewlInit);

  if (!pewl)
  {
    VPU_ENC_ERROR("%s: EWLInit failed !! pewl %p\r\n", __FUNCTION__, pewl);
    return VPU_ENC_RET_FAILURE;
  }

  info.virtualAddress = NULL;

  int ret = EWLMallocLinear(pewl, pInOutMem->nSize, &info);
  if (ret < 0)
  {
    VPU_ENC_ERROR("%s: EWLMallocLinear failed !! ret %d\r\n", __FUNCTION__, ret);
    return VPU_ENC_RET_FAILURE;
  }

  pInOutMem->nPhyAddr = info.busAddress;
  pInOutMem->nVirtAddr = (unsigned long)info.virtualAddress;
  pInOutMem->nCpuAddr = info.ion_fd;
  VPU_ENC_LOG("EWLMallocLinear pewl %p, size %d, virt 0x%x phy 0x%x\n",
    pewl, pInOutMem->nSize, info.virtualAddress, info.busAddress);

  if (pewl)
    EWLRelease(pewl);

  return VPU_ENC_RET_SUCCESS;
}


VpuEncRetCode VPU_EncFreeMem(VpuMemDesc* pInMem)
{
  struct EWLLinearMem info;
  struct EWLInitParam ewlInit;
  const void *pewl = NULL;

  info.size = pInMem->nSize;
  info.virtualAddress = (u32*)pInMem->nVirtAddr;
  info.busAddress = pInMem->nPhyAddr;
  info.ion_fd = pInMem->nCpuAddr;

  ewlInit.clientType = EWL_CLIENT_TYPE_H264_ENC;
  pewl = (void*)EWLInit(&ewlInit);
  if (!pewl)
  {
    VPU_ENC_ERROR("%s: EWLInit failed !! \r\n",__FUNCTION__);
    return VPU_ENC_RET_FAILURE;
  }
  VPU_ENC_LOG("VPU_EncFreeMem fd=%d",info.ion_fd);
  EWLFreeLinear(pewl, &info);

  if (pewl)
    EWLRelease(pewl);

  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncConfig(VpuEncHandle InHandle, VpuEncConfig InEncConf, void* pInParam)
{
  VpuEncHandleInternal * pVpuObj;
  VpuEncObj* pObj;
  int para;

  pVpuObj = (VpuEncHandleInternal *)InHandle;
  if(pVpuObj == NULL)
  {
  	VPU_ENC_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);
  	return VPU_ENC_RET_INVALID_HANDLE;
  }
  pObj = &pVpuObj->obj;

  switch(InEncConf)
  {
  	//case VPU_DEC_CONF_SKIPNONE:
  	//	break;
  	case VPU_ENC_CONF_NONE:
      break;
    case VPU_ENC_CONF_BIT_RATE:
      para = *((int*)pInParam);
      if(para < 0) {
        VPU_ENC_ERROR("%s: invalid bit rate parameter: %d \r\n",__FUNCTION__,para);
        return VPU_ENC_RET_INVALID_PARAM;
      }
      pObj->encConfig.bitrate.nTargetBitrate = para * 1000;  //kbps->bps
      break;
    case VPU_ENC_CONF_INTRA_REFRESH:
      para =* ((int*)pInParam);
      if(para < 0) {
        VPU_ENC_ERROR("%s: invalid intra refresh parameter: %d \r\n",__FUNCTION__,para);
        return VPU_ENC_RET_INVALID_PARAM;
      }
      VPU_ENC_LOG("%s: intra fresh number: %d \r\n",__FUNCTION__,para);
      // fixme later
      //pObj->encConfig.intraRefresh.eRefreshMode = para;
      break;
    case VPU_ENC_CONF_ENA_SPSPPS_IDR:
      /*	nInsertSPSPPSToIDR
        0: sequence header(SPS/PPS) + IDR +P +P +...+ (SPS/PPS)+IDR+....
        1: sequence header(SPS/PPS) + (SPS/PPS)+IDR +P +P +...+ (SPS/PPS)+IDR+....
      */
      VPU_ENC_LOG("%s: enable SPS/PPS for IDR frames %d \r\n",__FUNCTION__);
      pObj->encConfig.prependSPSPPSToIDRFrames = OMX_TRUE;
      break;
    case VPU_ENC_CONF_RC_INTRA_QP: /*avc: 0..51, other 1..31*/
      para = *((int*)pInParam);
      if(para < 0) {
        VPU_ENC_ERROR("%s: invalid intra qp %d \r\n",__FUNCTION__,para);
        return VPU_ENC_RET_INVALID_PARAM;
      }
      VPU_ENC_LOG("%s: intra qp : %d \r\n",__FUNCTION__,para);
      // fixme later
      break;
    case VPU_ENC_CONF_INTRA_REFRESH_MODE:
      para = *((int*)pInParam);
      if(para < 0){
        VPU_ENC_ERROR("%s: invalid intra refresh mode parameter: %d \r\n",__FUNCTION__,para);
        return VPU_ENC_RET_INVALID_PARAM;
      }
      VPU_ENC_LOG("%s: intra fresh mode: %d \r\n",__FUNCTION__,para);
      pObj->encConfig.intraRefresh.eRefreshMode = para;
      break;
    default:
      VPU_ENC_ERROR("%s: failure: invalid setting \r\n",__FUNCTION__);
      return VPU_ENC_RET_INVALID_PARAM;
  }
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncEncodeFrame(VpuEncHandle InHandle, VpuEncEncParam* pInOutParam)
{
  VpuEncHandleInternal * pVpuObj;
  VpuEncObj * pObj;
  VpuEncBufRetCode bufRet=VPU_ENC_INPUT_NOT_USED;
  VpuEncRetCode ret = VPU_ENC_RET_SUCCESS;
  FRAME frame;
  STREAM_BUFFER stream;
  int copy = 0;

  if(InHandle == NULL){
    VPU_ENC_ERROR("%s: failure: handle is null \r\n", __FUNCTION__);
    return VPU_ENC_RET_INVALID_HANDLE;
  }

  pVpuObj = (VpuEncHandleInternal *)InHandle;
  pObj = &(pVpuObj->obj);

  memset(&stream, 0, sizeof(STREAM_BUFFER));

  /* set output buffer */
  copy = 1;
  stream.bus_data = (OMX_U8*)pObj->pBsBufVirt;
  stream.bus_address = (OSAL_BUS_WIDTH)pObj->pBsBufPhy;
  stream.buf_max_size = (OMX_U32)pObj->nBsBufLen;

  /* if the out buffer is DMA buffer, use it instead of pBsBufPhy, this can avoid copy buffer */
  if (pInOutParam->nInPhyOutput) {
    copy = 0;
    stream.bus_data = (OMX_U8*)pInOutParam->nInVirtOutput;
    stream.bus_address = (OSAL_BUS_WIDTH)pInOutParam->nInPhyOutput;
    stream.buf_max_size = pInOutParam->nInOutputBufLen;
  }

  pInOutParam->eOutRetCode |= VPU_ENC_INPUT_NOT_USED;

  /* encoder start stream, output codec data */
  if (!pObj->bStreamStarted) {
    ret = VPU_EncStartEncode(pObj, &stream);
    if (VPU_ENC_RET_SUCCESS == ret && stream.streamlen > 0) {
      if (copy)
        memcpy((void*)pInOutParam->nInVirtOutput, stream.bus_data, stream.streamlen);

      pInOutParam->nOutOutputSize = stream.streamlen; // codec data

      pInOutParam->eOutRetCode |= VPU_ENC_OUTPUT_SEQHEADER;

      if (VPU_DUMP_RAW) {
        dumpStream((unsigned char*)pInOutParam->nInVirtOutput, pInOutParam->nOutOutputSize);
      }
    }
    /* record encode start time */
    gettimeofday (&pObj->tvBegin, NULL);;
    return ret;
  }

  pInOutParam->eOutRetCode &= (~VPU_ENC_OUTPUT_SEQHEADER);

  memset(&frame, 0, sizeof(FRAME));

  if(pInOutParam->pInFrame != NULL) {
    frame.fb_bus_address = (OSAL_BUS_WIDTH)pInOutParam->pInFrame->pbufY;
  } else {
    frame.fb_bus_address = pInOutParam->nInPhyInput;
  }

  frame.bitrate = pObj->encConfig.bitrate.nTargetBitrate;
  frame.bus_lumaStab = 0;  // because stabilize is not enabled
  frame.frame_type = (pInOutParam->nForceIPicture ? INTRA_FRAME : PREDICTED_FRAME);

  if (pObj->encConfig.intraRefreshVop.IntraRefreshVOP == OMX_TRUE) {
    frame.frame_type = INTRA_FRAME;
    pObj->encConfig.intraRefreshVop.IntraRefreshVOP == OMX_FALSE;
  }

  ret = VPU_EncDoEncode(pObj, &frame, &stream);
  VPU_ENC_LOG("VPU_EncDoEncode return %d", ret);

  if (ret != VPU_ENC_RET_SUCCESS) {
    VPU_ENC_ERROR("%s DoEncode return error %d\n", __FUNCTION__, ret);
    return ret;
  }

  if (pInOutParam->eFormat == VPU_V_VP8) {
    // copy data from each partition to the output buffer
    unsigned int offset = 0;

    for (int i = 0; i < 9; i++) {
      if (stream.streamSize[i] + offset <= pInOutParam->nInOutputBufLen) {
        memcpy((void*)(pInOutParam->nInVirtOutput + offset), stream.pOutBuf[i], stream.streamSize[i]);
        offset += stream.streamSize[i];
      } else {
        VPU_ENC_ERROR("%s: output buffer size too small\n", __FUNCTION__);
        return VPU_ENC_RET_INSUFFICIENT_FRAME_BUFFERS;
      }
    }
  } else if (copy){
    memcpy((void*)pInOutParam->nInVirtOutput, stream.bus_data, stream.streamlen);
  }

  pInOutParam->nOutOutputSize = stream.streamlen;

  pInOutParam->eOutRetCode |= (VPU_ENC_OUTPUT_DIS | VPU_ENC_INPUT_USED);
  pObj->totalFrameCnt++;
  VPU_ENC_LOG("Encode out frame cnt %d, size %d type %d\n", pObj->totalFrameCnt, pInOutParam->nOutOutputSize, frame.frame_type);

  if(VPU_DUMP_RAW) {
    dumpStream((unsigned char*)pInOutParam->nInVirtOutput, pInOutParam->nOutOutputSize);
  }

  return ret;
}

/* end of file */
