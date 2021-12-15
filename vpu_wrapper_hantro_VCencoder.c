/**
 *	Copyright 2019-2020 NXP
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 *
 *	History :
 *	Date	(y.m.d)		Author			Version			Description
 *	2019-07-10		  Hou Qi		    0.1		    Created
 */

/** Vpu_wrapper_hantro_VCencoder.c
 *	vpu wrapper file contain all related hantro video encoder api exposed to
 *	application
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <fcntl.h>

#include "hantro_VC8000E_enc/hevcencapi.h"
#include "hantro_VC8000E_enc/enccommon.h"
#include "hantro_VC8000E_enc/base_type.h"
#include "hantro_VC8000E_enc/ewl.h"

#include "utils.h"
#include "vpu_wrapper.h"

static int nVpuLogLevel=0;      //bit 0: api log; bit 1: raw dump; bit 2: yuv dump
#ifdef ANDROID
//#define LOG_NDEBUG 0
#define LOG_TAG "VpuWrapper"
#include <utils/Log.h>
#define LOG_PRINTF ALOGD
#define VPU_LOG_LEVELFILE "/data/vpu_log_level"
#define VPU_DUMP_RAWFILE "/data/temp_wrapper.bit"
#define VPU_DUMP_YUVFILE "/data/temp_wrapper.yuv"
#else
#define LOG_PRINTF printf
#define VPU_LOG_LEVELFILE "/etc/vpu_log_level"
#define VPU_DUMP_RAWFILE "temp_wrapper.bit"
#define VPU_DUMP_YUVFILE "temp_wrapper.yuv"
#endif

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
#define VPU_BITS_BUF_SIZE       (10*1024*1024)      //bitstream buffer size

#define VPU_ENC_SEQ_DATA_SEPERATE
#define VPU_ENC_MAX_RETRIES 10000
#define VPU_ENC_DEFAULT (-255)
#define VPU_ENC_MAX_FRAME_INDEX	30

#define VPU_ENC_MIN_BITRATE 10000
#define VPU_ENC_MAX_BITRATE 60000000

#define VIRT_INDEX  0
#define PHY_INDEX   1

#define FALSE 0
#define TRUE 1

#define MIN_WIDTH 132
#define MIN_HEIGHT 96

#define VPU_ENC_DEFAULT_ALIGNMENT_H 4
#define VPU_ENC_DEFAULT_ALIGNMENT_V 4

#define ENC_QP_DEFAULT     26
#define ENC_MIN_QP_DEFAULT 0
#define ENC_MAX_QP_DEFAULT 51

#define MAX_GOPCONFIG_SIZE 8
#define ENC_MAX_GOP_SIZE 300

#define MOVING_AVERAGE_FRAMES    120
#define LEAST_MONITOR_FRAME       3
#define MAX_GOP_PIC_CONFIG_NUM   48
#define MAX_GOP_SPIC_CONFIG_NUM  16

#define ALIGN(ptr,align)       ((align) ? (((unsigned long)(ptr))/(align)*(align)) : ((unsigned long)(ptr)))
#define MemAlign(mem,align) ((((unsigned int)mem)%(align))==0)
#define MemNotAlign(mem,align)  ((((unsigned int)mem)%(align))!=0)

#define VPU_ENC_LOG(...) if(nVpuLogLevel&0x1) {LOG_PRINTF(__VA_ARGS__);}
#define VPU_ENC_TRACE
#define VPU_ENC_API(...) if(nVpuLogLevel&0x1) {LOG_PRINTF(__VA_ARGS__);}
#define VPU_ENC_ERROR(...) if(nVpuLogLevel&0x1) {LOG_PRINTF(__VA_ARGS__);}
#define VPU_ENC_ASSERT(exp) if((!(exp))&&(nVpuLogLevel&0x1)) {LOG_PRINTF("%s: %d : assert condition !!!\r\n",__FUNCTION__,__LINE__);}

#define MEMORY_SENTINEL 0xACDCACDC;

typedef struct FrameString
{
  int    num;
  char   type;
  int    poc;
  int    qpOffset;
  double qpFactor;
  int    temporalId_factor;
  int    num_ref_pics;
  int    ref_pics[MAX_GOPCONFIG_SIZE];
  int    used_by_cur[MAX_GOPCONFIG_SIZE];
}FrameString;

FrameString RpsDefault_GOPSize_1[1] =
{
  {1,'P',1,0,0.578,0,1,{-1},{1}},
};

FrameString RpsDefault_H264_GOPSize_1[1] =
{
  {1,'P',1,0,0.4,0,1,{-1},{1}},
};

FrameString RpsDefault_GOPSize_2[2] =
{
  {1,'P',2,0,0.6,0,1,{-2},{1}},
  {2,'B',1,0,0.68,0,2,{-1,1},{1,1}},
};

FrameString RpsDefault_GOPSize_3[3] =
{
  {1,'P',3,0,0.5,0,1,{-3},{1}},
  {2,'B',1,0,0.5,0,2,{-1,2},{1,1}},
  {3,'B',2,0,0.68,0,2,{-1,1},{1,1}},
};

FrameString RpsDefault_GOPSize_4[4] =
{
  {1,'P',4,0,0.5,0,1,{-4},{1}},
  {2,'B',2,0,0.3536,0,2,{-2,2},{1,1}},
  {3,'B',1,0,0.5,0,3,{-1,1,3},{1,1,0}},
  {4,'B',3,0,0.5,0,2,{-1,1},{1,1}},
};

FrameString RpsDefault_GOPSize_5[5] =
{
  {1,'P',5,0,0.442,0,1,{-5},{1}},
  {2,'B',2,0,0.3536,0,2,{-2,3},{1,1}},
  {3,'B',1,0,0.68,0,3,{-1,1,4},{1,1,0}},
  {4,'B',3,0,0.3536,0,2,{-1,2},{1,1}},
  {5,'B',4,0,0.68,0,2,{-1,1},{1,1}},
};

FrameString RpsDefault_GOPSize_6[6] =
{
  {1,'P',6,0,0.442,0,1,{-6},{1}},
  {2,'B',3,0,0.3536,0,2,{-3,3},{1,1}},
  {3,'B',1,0,0.3536,0,3,{-1,2,5},{1,1,0}},
  {4,'B',2,0,0.68,0,3,{-1,1,4},{1,1,0}},
  {5,'B',4,0,0.3536,0,2,{-1,2},{1,1}},
  {6,'B',5,0,0.68,0,2,{-1,1},{1,1}},
};

FrameString RpsDefault_GOPSize_7[7] =
{
  {1,'P',7,0,0.442,0,1,{-7},{1}},
  {2,'B',3,0,0.3536,0,2,{-3,4},{1,1}},
  {3,'B',1,0,0.3536,0,3,{-1,2,6},{1,1,0}},
  {4,'B',2,0,0.68,0,3,{-1,1,5},{1,1,0}},
  {5,'B',5,0,0.3536,0,2,{-2,2},{1,1}},
  {6,'B',4,0,0.68,0,3,{-1,1,3},{1,1,0}},
  {7,'B',6,0,0.68,0,2,{-1,1},{1,1}},
};

FrameString RpsDefault_GOPSize_8[8] =
{
  {1,'P',8,0,0.442,0,1,{-8},{1}},
  {2,'B',4,0,0.3536,0,2,{-4,4},{1,1}},
  {3,'B',2,0,0.3536,0,3,{-2,2,6},{1,1,0}},
  {4,'B',1,0,0.68,0,4,{-1,1,3,7},{1,1,0,0}},
  {5,'B',3,0,0.68,0,3,{-1,1,5},{1,1,0}},
  {6,'B',6,0,0.3536,0,2,{-2,2},{1,1}},
  {7,'B',5,0,0.68,0,3,{-1,1,3},{1,1,0}},
  {8,'B',7,0,0.68,0,2,{-1,1},{1,1}},
};

typedef enum FRAME_TYPE
{
    INTRA_FRAME,
    PREDICTED_FRAME,
    NONINTRA_FRAME
}FRAME_TYPE;

typedef struct FRAME
{
    unsigned char* fb_bus_data;
    unsigned long fb_bus_address_Luma;
    unsigned long fb_bus_address_ChromaU;
    unsigned long fb_bus_address_ChromaV;
    u32 fb_frameSize;
    u32 fb_bufferSize;
    FRAME_TYPE frame_type;
    u32 bitrate;
}FRAME;

typedef struct STREAM_BUFFER
{
    unsigned char* bus_data;           // set by common
    unsigned long bus_address; // set by common
    u32 buf_max_size;       // set by common
    u32 streamlen;          // set by codec
}STREAM_BUFFER;

typedef struct
{
  VCEncConfig cfg;
  VCEncCodingCtrl codingCfg;
  VCEncRateCtrl rcCfg;
  VCEncPreProcessingCfg preProcCfg;
  i32 gopSize;   //set 1: no B frame
  u32 nPFrames;  //gop size between two key frames
}CONFIG;

// internal ENCODER interface, which wraps up Hantro API
typedef struct ENCODER_PROTOTYPE ENCODER_PROTOTYPE;
struct ENCODER_PROTOTYPE
{
    void (*destroy)(ENCODER_PROTOTYPE*);
    VpuEncRetCode (*stream_start)(ENCODER_PROTOTYPE*, CONFIG*, STREAM_BUFFER*);
    VpuEncRetCode (*stream_end)(ENCODER_PROTOTYPE*, STREAM_BUFFER*);
    VpuEncRetCode (*encode)(ENCODER_PROTOTYPE*, FRAME*, STREAM_BUFFER*, CONFIG*);
};

typedef struct
{
  VCEncInst inst;
  VCEncIn encIn;
  ENCODER_PROTOTYPE base;
  u32 nIFrameCounter;
  u32 nTotalFrames;
  VCEncPictureCodingType nextCodingType;
}ENCODER;

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
  /* hantro encoder */
  ENCODER_PROTOTYPE *codec;
  CONFIG config;

  /* bit stream buffer */
  unsigned char* pBsBufVirt;
  unsigned char* pBsBufPhy;
  int nBsBufLen;
  int max_cu_size;
}VpuEncObj;

typedef struct
{
  VpuEncObj obj;
}VpuEncHandleInternal;

i32 CheckArea(VCEncPictureArea* area, VpuEncObj* pObj)
{
  i32 w = (pObj->config.cfg.width + pObj->max_cu_size - 1) / pObj->max_cu_size;
  i32 h = (pObj->config.cfg.height + pObj->max_cu_size - 1) / pObj->max_cu_size;

  if ((area->left < (u32)w) && (area->right < (u32)w) &&
      (area->top < (u32)h) && (area->bottom < (u32)h))
    return 1;

  return 0;
}

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

static int VpuEncLogLevelParse(int * pLogLevel)
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

/* table for max frame size for each video level */
typedef struct {
    int level;
    int size;    // mbPerFrame, (Width / 16) * (Height / 16)
}EncLevelSizeMap;

/* H264 level size map table, sync with h1 h264 encoder */
static const EncLevelSizeMap H264LevelSizeMapTable[] = {
    {VCENC_H264_LEVEL_1,  99},
    {VCENC_H264_LEVEL_1_b, 99},
    {VCENC_H264_LEVEL_1_1, 396},
    {VCENC_H264_LEVEL_1_2, 396},
    {VCENC_H264_LEVEL_1_3, 396},
    {VCENC_H264_LEVEL_2,  396},
    {VCENC_H264_LEVEL_2_1, 792},
    {VCENC_H264_LEVEL_2_2, 1620},
    {VCENC_H264_LEVEL_3,  1620},
    {VCENC_H264_LEVEL_3_1, 3600},
    {VCENC_H264_LEVEL_3_2, 5120},
    {VCENC_H264_LEVEL_4,  8192},
    {VCENC_H264_LEVEL_4_1, 8192},
    {VCENC_H264_LEVEL_4_2, 8704},
    {VCENC_H264_LEVEL_5,  22080},
    {VCENC_H264_LEVEL_5_1, 65025},
};

static int calculateH264Level(int width, int height)
{
  // adjust H264 level based on video resolution because encoder will check this.
  int i, mbPerFrame, tableLen;
  mbPerFrame = ((width + 15) / 16) * ((height + 15) / 16);
  tableLen = sizeof(H264LevelSizeMapTable)/sizeof(H264LevelSizeMapTable[0]);
  int level = VCENC_H264_LEVEL_1;

  for (i = 0; i < tableLen; i++) {
    if (level == H264LevelSizeMapTable[i].level) {
      if (mbPerFrame <= H264LevelSizeMapTable[i].size)
        break;
      else if (i + 1 < tableLen)
        level = H264LevelSizeMapTable[i + 1].level;
      else
        return VCENC_H264_LEVEL_5_1; // mbPerFrame too big, return the highest level
    }
  }

  return level;
}


static void VPU_EncInitConfigParams(VCEncIn *pEncIn, VCEncConfig* cfg, CONFIG* params)
{
  int idx;
  u32 maxRefPics = 0;
  u32 maxTemporalId = 0;
  cfg->codecFormat = params->cfg.codecFormat;
  if (IS_H264(cfg->codecFormat))
  {
    switch (params->cfg.profile)
    {
      case VCENC_H264_BASE_PROFILE:
      case VCENC_H264_MAIN_PROFILE:
      case VCENC_H264_HIGH_PROFILE:
      case VCENC_H264_HIGH_10_PROFILE:
        cfg->profile = params->cfg.profile;
        break;
      default:
        VPU_ENC_ERROR("Unsupported H264 encoding profile: %d", params->cfg.profile);
        return;
        break;
    }
    switch (params->cfg.level)
    {
      case VCENC_H264_LEVEL_1:
      case VCENC_H264_LEVEL_1_b:
      case VCENC_H264_LEVEL_1_1:
      case VCENC_H264_LEVEL_1_2:
      case VCENC_H264_LEVEL_1_3:
      case VCENC_H264_LEVEL_2:
      case VCENC_H264_LEVEL_2_1:
      case VCENC_H264_LEVEL_2_2:
      case VCENC_H264_LEVEL_3:
      case VCENC_H264_LEVEL_3_1:
      case VCENC_H264_LEVEL_3_2:
      case VCENC_H264_LEVEL_4:
      case VCENC_H264_LEVEL_4_1:
      case VCENC_H264_LEVEL_4_2:
      case VCENC_H264_LEVEL_5:
      case VCENC_H264_LEVEL_5_1:
      case VCENC_H264_LEVEL_5_2:
      case VCENC_H264_LEVEL_6:
      case VCENC_H264_LEVEL_6_1:
      case VCENC_H264_LEVEL_6_2:
        cfg->level = params->cfg.level;
        break;
      default:
        VPU_ENC_ERROR("Unsupported H264 encoding level: %d", params->cfg.level);
        return;
        break;
    }
  }
  else
  {
    switch (params->cfg.profile)
    {
      case VCENC_HEVC_MAIN_PROFILE:
      case VCENC_HEVC_MAIN_STILL_PICTURE_PROFILE:
      case VCENC_HEVC_MAIN_10_PROFILE:
        cfg->profile = params->cfg.profile;
        break;
      default:
        VPU_ENC_ERROR("Unsupported HEVC encoding profile: %d", params->cfg.profile);
        return;
        break;
    }
    switch (params->cfg.level)
    {
      case VCENC_HEVC_LEVEL_1:
      case VCENC_HEVC_LEVEL_2:
      case VCENC_HEVC_LEVEL_2_1:
      case VCENC_HEVC_LEVEL_3:
      case VCENC_HEVC_LEVEL_3_1:
      case VCENC_HEVC_LEVEL_4:
      case VCENC_HEVC_LEVEL_4_1:
      case VCENC_HEVC_LEVEL_5:
      case VCENC_HEVC_LEVEL_5_1:
      case VCENC_HEVC_LEVEL_5_2:
      case VCENC_HEVC_LEVEL_6:
      case VCENC_HEVC_LEVEL_6_1:
      case VCENC_HEVC_LEVEL_6_2:
        cfg->level = params->cfg.level;
        break;
      default:
        VPU_ENC_ERROR("Unsupported HEVC encoding level: %d", params->cfg.level);
        return;
        break;
    }
  }

  cfg->tier = params->cfg.tier;
  cfg->streamType = (params->cfg.streamType == VCENC_BYTE_STREAM) ? VCENC_BYTE_STREAM : VCENC_NAL_UNIT_STREAM;
  cfg->width = params->cfg.width;
  cfg->height = params->cfg.height;
  cfg->frameRateNum = params->cfg.frameRateNum;
  cfg->frameRateDenom = params->cfg.frameRateDenom;
  cfg->strongIntraSmoothing = params->cfg.strongIntraSmoothing;
  cfg->bitDepthLuma = params->cfg.bitDepthLuma;
  cfg->bitDepthChroma = params->cfg.bitDepthChroma;
  cfg->compressor = params->cfg.compressor;
  cfg->interlacedFrame = params->cfg.interlacedFrame;

  //specify the amount of reference frame buffers that will be allocated
  for (idx = 0; idx < pEncIn->gopConfig.size; idx ++)
  {
    VCEncGopPicConfig *gopPiccfg = &(pEncIn->gopConfig.pGopPicCfg[idx]);
    if (gopPiccfg->codingType != VCENC_INTRA_FRAME)
    {
      if (maxRefPics < gopPiccfg->numRefPics)
        maxRefPics = gopPiccfg->numRefPics;

      if (maxTemporalId < gopPiccfg->temporalId)
        maxTemporalId = gopPiccfg->temporalId;
    }
  }
  cfg->refFrameAmount = maxRefPics + cfg->interlacedFrame + pEncIn->gopConfig.ltrcnt;
  cfg->maxTLayers = maxTemporalId +1;

  cfg->enableOutputCuInfo = params->cfg.enableOutputCuInfo;
  cfg->rdoLevel = params->cfg.rdoLevel;
  cfg->verbose = params->cfg.verbose;
  cfg->exp_of_input_alignment = params->cfg.exp_of_input_alignment;
  cfg->exp_of_ref_alignment = params-> cfg.exp_of_ref_alignment;
  cfg->exp_of_ref_ch_alignment = params->cfg.exp_of_ref_ch_alignment;
  cfg->P010RefEnable = params->cfg.P010RefEnable;
  cfg->enableSsim = params->cfg.enableSsim;
  cfg->ctbRcMode = params->cfg.ctbRcMode;
  cfg->parallelCoreNum = params->cfg.parallelCoreNum;
  cfg->bPass1AdaptiveGop = (params->gopSize == 0);
  cfg->picOrderCntType = params->cfg.picOrderCntType;
  cfg->dumpRegister = params->cfg.dumpRegister;
  cfg->rasterscan = params->cfg.rasterscan;
  cfg->log2MaxPicOrderCntLsb = params->cfg.log2MaxPicOrderCntLsb;
  cfg->log2MaxFrameNum = params->cfg.log2MaxFrameNum;
  cfg->lookaheadDepth = params->cfg.lookaheadDepth;
  cfg->cuInfoVersion = params->cfg.cuInfoVersion;
  cfg->codedChromaIdc = params->cfg.codedChromaIdc;

  if (params->cfg.parallelCoreNum > 1 && cfg->width * cfg->height < 256 * 256){
    VPU_ENC_LOG("Disable multicore for small resolution (< 255*255)");
    cfg->parallelCoreNum = params->cfg.parallelCoreNum = 1;
  }
}

static void VPU_EncInitCodingCtrlParams(VCEncCodingCtrl* codingCfg, CONFIG* params)
{
  int i;
  codingCfg->sliceSize = params->codingCfg.sliceSize;
  codingCfg->enableCabac = params->codingCfg.enableCabac;
  codingCfg->cabacInitFlag = params->codingCfg.cabacInitFlag;
  codingCfg->videoFullRange = params->codingCfg.videoFullRange;
  codingCfg->disableDeblockingFilter = params->codingCfg.disableDeblockingFilter;
  codingCfg->tc_Offset = params->codingCfg.tc_Offset;
  codingCfg->beta_Offset = params->codingCfg.beta_Offset;
  codingCfg->enableSao = params->codingCfg.enableSao;
  codingCfg->enableDeblockOverride = params->codingCfg.enableDeblockOverride;
  codingCfg->deblockOverride = params->codingCfg.deblockOverride;
  codingCfg->seiMessages = params->codingCfg.seiMessages;
  codingCfg->gdrDuration = params->codingCfg.gdrDuration;
  codingCfg->roiMapDeltaQpEnable = params->codingCfg.roiMapDeltaQpEnable;
  codingCfg->roiMapDeltaQpBlockUnit = params->codingCfg.roiMapDeltaQpBlockUnit;
  codingCfg->RoimapCuCtrl_index_enable = params->codingCfg.RoimapCuCtrl_index_enable;
  codingCfg->RoimapCuCtrl_enable       = params->codingCfg.RoimapCuCtrl_enable;
  codingCfg->roiMapDeltaQpBinEnable    = params->codingCfg.roiMapDeltaQpBinEnable;
  codingCfg->RoimapCuCtrl_ver          = params->codingCfg.RoimapCuCtrl_ver;
  codingCfg->RoiQpDelta_ver           = params->codingCfg.RoiQpDelta_ver;
  /* SKIP map */
  codingCfg->skipMapEnable = params->codingCfg.skipMapEnable;
  codingCfg->enableScalingList = params->codingCfg.enableScalingList;
  codingCfg->chroma_qp_offset = params->codingCfg.chroma_qp_offset;

  /*stream multi-segment*/
  codingCfg->streamMultiSegmentMode = params->codingCfg.streamMultiSegmentMode;
  codingCfg->streamMultiSegmentAmount = params->codingCfg.streamMultiSegmentAmount;
  /* denoise */
  codingCfg->noiseReductionEnable = params->codingCfg.noiseReductionEnable;
  codingCfg->noiseLow = params->codingCfg.noiseLow;
  codingCfg->firstFrameSigma = params->codingCfg.firstFrameSigma;
  /* smart */
  codingCfg->smartModeEnable = params->codingCfg.smartModeEnable;
  codingCfg->smartH264LumDcTh = params->codingCfg.smartH264LumDcTh;
  codingCfg->smartH264CbDcTh = params->codingCfg.smartH264CbDcTh;
  codingCfg->smartH264CrDcTh = params->codingCfg.smartH264CrDcTh;
  for(i = 0; i < 3; i++) {
    codingCfg->smartHevcLumDcTh[i] = params->codingCfg.smartHevcLumDcTh[i];
    codingCfg->smartHevcChrDcTh[i] = params->codingCfg.smartHevcChrDcTh[i];
    codingCfg->smartHevcLumAcNumTh[i] = params->codingCfg.smartHevcLumAcNumTh[i];
    codingCfg->smartHevcChrAcNumTh[i] = params->codingCfg.smartHevcChrAcNumTh[i];
  }
  codingCfg->smartH264Qp = params->codingCfg.smartH264Qp;
  codingCfg->smartHevcLumQp = params->codingCfg.smartHevcLumQp;
  codingCfg->smartHevcChrQp = params->codingCfg.smartHevcChrQp;
  for(i = 0; i < 4; i++)
    codingCfg->smartMeanTh[i] = params->codingCfg.smartMeanTh[i];
  codingCfg->smartPixNumCntTh = params->codingCfg.smartPixNumCntTh;

  /* tile */
  codingCfg->tiles_enabled_flag = params->codingCfg.tiles_enabled_flag && !IS_H264(params->cfg.codecFormat);
  codingCfg->num_tile_columns = params->codingCfg.num_tile_columns;
  codingCfg->num_tile_rows       = params->codingCfg.num_tile_rows;
  codingCfg->loop_filter_across_tiles_enabled_flag = params->codingCfg.loop_filter_across_tiles_enabled_flag;

  codingCfg->intraArea.top    = params->codingCfg.intraArea.top;
  codingCfg->intraArea.left   = params->codingCfg.intraArea.left;
  codingCfg->intraArea.bottom = params->codingCfg.intraArea.bottom;
  codingCfg->intraArea.right  = params->codingCfg.intraArea.right;
  codingCfg->intraArea.enable = params->codingCfg.intraArea.enable;

  codingCfg->ipcm1Area.top    = params->codingCfg.ipcm1Area.top;
  codingCfg->ipcm1Area.left   = params->codingCfg.ipcm1Area.left;
  codingCfg->ipcm1Area.bottom = params->codingCfg.ipcm1Area.bottom;
  codingCfg->ipcm1Area.right  = params->codingCfg.ipcm1Area.right;
  codingCfg->ipcm1Area.enable = params->codingCfg.ipcm1Area.enable;

  codingCfg->ipcm2Area.top    = params->codingCfg.ipcm2Area.top;
  codingCfg->ipcm2Area.left   = params->codingCfg.ipcm2Area.left;
  codingCfg->ipcm2Area.bottom = params->codingCfg.ipcm2Area.bottom;
  codingCfg->ipcm2Area.right  = params->codingCfg.ipcm2Area.right;
  codingCfg->ipcm2Area.enable = params->codingCfg.ipcm2Area.enable;

  codingCfg->roi1Area.top    = params->codingCfg.roi1Area.top;
  codingCfg->roi1Area.left   = params->codingCfg.roi1Area.left;
  codingCfg->roi1Area.bottom = params->codingCfg.roi1Area.bottom;
  codingCfg->roi1Area.right  = params->codingCfg.roi1Area.right;
  codingCfg->roi1Area.enable = params->codingCfg.roi1Area.enable;

  codingCfg->roi2Area.top    = params->codingCfg.roi2Area.top;
  codingCfg->roi2Area.left   = params->codingCfg.roi2Area.left;
  codingCfg->roi2Area.bottom = params->codingCfg.roi2Area.bottom;
  codingCfg->roi2Area.right  = params->codingCfg.roi2Area.right;
  codingCfg->roi2Area.enable = params->codingCfg.roi2Area.enable;

  codingCfg->roi3Area.top    = params->codingCfg.roi3Area.top;
  codingCfg->roi3Area.left   = params->codingCfg.roi3Area.left;
  codingCfg->roi3Area.bottom = params->codingCfg.roi3Area.bottom;
  codingCfg->roi3Area.right  = params->codingCfg.roi3Area.right;
  codingCfg->roi3Area.enable = params->codingCfg.roi3Area.enable;

  codingCfg->roi4Area.top    = params->codingCfg.roi4Area.top;
  codingCfg->roi4Area.left   = params->codingCfg.roi4Area.left;
  codingCfg->roi4Area.bottom = params->codingCfg.roi4Area.bottom;
  codingCfg->roi4Area.right  = params->codingCfg.roi4Area.right;
  codingCfg->roi4Area.enable = params->codingCfg.roi4Area.enable;

  codingCfg->roi5Area.top    = params->codingCfg.roi5Area.top;
  codingCfg->roi5Area.left   = params->codingCfg.roi5Area.left;
  codingCfg->roi5Area.bottom = params->codingCfg.roi5Area.bottom;
  codingCfg->roi5Area.right  = params->codingCfg.roi5Area.right;
  codingCfg->roi5Area.enable = params->codingCfg.roi5Area.enable;

  codingCfg->roi6Area.top    = params->codingCfg.roi6Area.top;
  codingCfg->roi6Area.left   = params->codingCfg.roi6Area.left;
  codingCfg->roi6Area.bottom = params->codingCfg.roi6Area.bottom;
  codingCfg->roi6Area.right  = params->codingCfg.roi6Area.right;
  codingCfg->roi6Area.enable = params->codingCfg.roi6Area.enable;

  codingCfg->roi7Area.top    = params->codingCfg.roi7Area.top;
  codingCfg->roi7Area.left   = params->codingCfg.roi7Area.left;
  codingCfg->roi7Area.bottom = params->codingCfg.roi7Area.bottom;
  codingCfg->roi7Area.right  = params->codingCfg.roi7Area.right;
  codingCfg->roi7Area.enable = params->codingCfg.roi7Area.enable;

  codingCfg->roi8Area.top    = params->codingCfg.roi8Area.top;
  codingCfg->roi8Area.left   = params->codingCfg.roi8Area.left;
  codingCfg->roi8Area.bottom = params->codingCfg.roi8Area.bottom;
  codingCfg->roi8Area.right  = params->codingCfg.roi8Area.right;
  codingCfg->roi8Area.enable = params->codingCfg.roi8Area.enable;

  codingCfg->roi1Qp = params->codingCfg.roi1Qp;
  codingCfg->roi2Qp = params->codingCfg.roi2Qp;
  codingCfg->roi3Qp = params->codingCfg.roi3Qp;
  codingCfg->roi4Qp = params->codingCfg.roi4Qp;
  codingCfg->roi5Qp = params->codingCfg.roi5Qp;
  codingCfg->roi6Qp = params->codingCfg.roi6Qp;
  codingCfg->roi7Qp = params->codingCfg.roi7Qp;
  codingCfg->roi8Qp = params->codingCfg.roi8Qp;
}

static void VPU_EncInitRateCtrlParams(VCEncRateCtrl* rcCfg, CONFIG* params)
{
  rcCfg->qpHdr = params->rcCfg.qpHdr;
  if (params->rcCfg.qpMinPB >= 0)
  {
    rcCfg->qpMinPB = params->rcCfg.qpMinPB;
    if (rcCfg->qpHdr != -1 && rcCfg->qpHdr < rcCfg->qpMinPB)
      rcCfg->qpHdr = rcCfg->qpMinPB;
  }
  if (params->rcCfg.qpMaxPB >= 0)
  {
    rcCfg->qpMaxPB = params->rcCfg.qpMaxPB;
    if (rcCfg->qpHdr != -1 && rcCfg->qpHdr > rcCfg->qpMaxPB)
      rcCfg->qpHdr = rcCfg->qpMaxPB;
  }
  if (params->rcCfg.qpMinI >= 0)
  {
    rcCfg->qpMinI = params->rcCfg.qpMinI;
    if (rcCfg->qpHdr != -1 && rcCfg->qpHdr < rcCfg->qpMinI)
      rcCfg->qpHdr = rcCfg->qpMinPB;
  }
  if (params->rcCfg.qpMaxI >= 0)
  {
    rcCfg->qpMaxI = params->rcCfg.qpMaxI;
    if (rcCfg->qpHdr != -1 && rcCfg->qpHdr > rcCfg->qpMaxI)
      rcCfg->qpHdr = rcCfg->qpMaxI;
  }
  rcCfg->pictureSkip = params->rcCfg.pictureSkip;
  rcCfg->pictureRc = params->rcCfg.pictureRc;
  rcCfg->ctbRc = params->rcCfg.ctbRc;
  rcCfg->blockRCSize = params->rcCfg.blockRCSize;
  rcCfg->rcQpDeltaRange = params->rcCfg.rcQpDeltaRange;
  rcCfg->rcBaseMBComplexity = params->rcCfg.rcBaseMBComplexity;

  rcCfg->bitPerSecond = params->rcCfg.bitPerSecond;
  rcCfg->bitVarRangeI = params->rcCfg.bitVarRangeI;
  rcCfg->bitVarRangeP = params->rcCfg.bitVarRangeP;
  rcCfg->bitVarRangeB = params->rcCfg.bitVarRangeB;
  rcCfg->tolMovingBitRate = params->rcCfg.tolMovingBitRate;
  rcCfg->longTermQpDelta = params->rcCfg.longTermQpDelta;
  rcCfg->monitorFrames = (params->cfg.frameRateNum + params->cfg.frameRateDenom - 1) / params->cfg.frameRateDenom;
  params->rcCfg.monitorFrames = rcCfg->monitorFrames;

  if(rcCfg->monitorFrames > MOVING_AVERAGE_FRAMES)
    rcCfg->monitorFrames = MOVING_AVERAGE_FRAMES;
  if (rcCfg->monitorFrames < 10)
  {
    rcCfg->monitorFrames = (params->cfg.frameRateNum > params->cfg.frameRateDenom) ? 10 : LEAST_MONITOR_FRAME;
  }
  rcCfg->hrd = params->rcCfg.hrd;
  rcCfg->hrdCpbSize = params->rcCfg.hrdCpbSize;

  if (params->rcCfg.bitrateWindow != 0)
    rcCfg->bitrateWindow = MIN (params->rcCfg.bitrateWindow, ENC_MAX_GOP_SIZE);

  if (params->rcCfg.intraQpDelta != 0)
    rcCfg->intraQpDelta = params->rcCfg.intraQpDelta;
  rcCfg->vbr = params->rcCfg.vbr;
  rcCfg->fixedIntraQp = params->rcCfg.fixedIntraQp;
  rcCfg->smoothPsnrInGOP = params->rcCfg.smoothPsnrInGOP;
  rcCfg->u32StaticSceneIbitPercent = params->rcCfg.u32StaticSceneIbitPercent;
}

static void VPU_EncInitPreProcessorParams(VCEncPreProcessingCfg* preProcCfg, CONFIG* params)
{
  preProcCfg->origWidth = params->preProcCfg.origWidth;
  preProcCfg->origHeight = params->preProcCfg.origHeight;
  preProcCfg->xOffset = params->preProcCfg.xOffset;
  preProcCfg->yOffset = params->preProcCfg.yOffset;
  preProcCfg->inputType = (VCEncPictureType)params->preProcCfg.inputType;
  preProcCfg->rotation = (VCEncPictureRotation)params->preProcCfg.rotation;
  preProcCfg->mirror = (VCEncPictureMirror)params->preProcCfg.mirror;
  if (params->cfg.interlacedFrame)
    preProcCfg->origHeight /= 2;

  preProcCfg->colorConversion.type = (VCEncColorConversionType)params->preProcCfg.colorConversion.type;
  if (preProcCfg->colorConversion.type == VCENC_RGBTOYUV_USER_DEFINED)
  {
    preProcCfg->colorConversion.coeffA = 20000;
    preProcCfg->colorConversion.coeffB = 44000;
    preProcCfg->colorConversion.coeffC = 5000;
    preProcCfg->colorConversion.coeffE = 35000;
    preProcCfg->colorConversion.coeffF = 38000;
    preProcCfg->colorConversion.coeffG = 35000;
    preProcCfg->colorConversion.coeffH = 38000;
    preProcCfg->colorConversion.LumaOffset = 0;
  }

  if (preProcCfg->rotation && preProcCfg->rotation != 3)
  {
    preProcCfg->scaledWidth = params->preProcCfg.scaledHeight;
    preProcCfg->scaledHeight = params->preProcCfg.scaledWidth;
  }
  else
  {
    preProcCfg->scaledWidth = params->preProcCfg.scaledWidth;
    preProcCfg->scaledHeight = params->preProcCfg.scaledHeight;
  }
  preProcCfg->input_alignment = params->preProcCfg.input_alignment;
  preProcCfg->constChromaEn = params->preProcCfg.constChromaEn;
  if (params->preProcCfg.constCb != 0)
    preProcCfg->constCb = params->preProcCfg.constCb;
  if (params->preProcCfg.constCr != 0)
    preProcCfg->constCr = params->preProcCfg.constCr;
}

static int VPU_EncParseGopConfigString(FrameString *line, VCEncGopConfig *gopCfg, int frame_idx, int gopSize)
{
  if (!line)
    return -1;

  int frameN, poc, num_ref_pics, i;
  char type;
  VCEncGopPicConfig* gopPicCfg = NULL;

  //frame idx
  frameN = line->num;
  if ((frameN != (frame_idx + 1)) && (frameN != 0)) return -1;

  if (frameN > gopSize)
    return 0;

  gopPicCfg = &(gopCfg->pGopPicCfg[gopCfg->size++]);

  //frame type
  type = line->type;
  if (type == 'P' || type == 'p')
    gopPicCfg->codingType = VCENC_PREDICTED_FRAME;
  else if (type == 'B' || type == 'b')
    gopPicCfg->codingType = VCENC_BIDIR_PREDICTED_FRAME;
  else
    return -1;

  poc = line->poc;
  if (poc < 1 || poc > gopSize) return -1;
  gopPicCfg->poc = poc;
  gopPicCfg->QpOffset = line->qpOffset;
  gopPicCfg->QpFactor = line->qpFactor;
  // sqrt(QpFactor) is used in calculating lambda
  gopPicCfg->QpFactor = sqrt(gopPicCfg->QpFactor);
  gopPicCfg->temporalId = line->temporalId_factor;

  //num_ref_pics
  num_ref_pics = line->num_ref_pics;
  if (num_ref_pics < 0 || num_ref_pics > VCENC_MAX_REF_FRAMES)
  {
    VPU_ENC_ERROR("GOP Config: Error, num_ref_pic can not be more than %d", VCENC_MAX_REF_FRAMES);
    return -1;
  }

  //ref_pics
  for (i = 0; i < num_ref_pics; i++)
  {
    gopPicCfg->refPics[i].ref_pic = line->ref_pics[i];
  }

  //used_by_cur
  for (i = 0; i < num_ref_pics; i++)
  {
    gopPicCfg->refPics[i].used_by_cur = line->used_by_cur[i];
  }

  gopPicCfg->numRefPics = num_ref_pics;

  return 0;
}

static int VPU_EncReadGopConfig(FrameString *config, VCEncGopConfig *gopCfg, int gopSize, u8 *gopCfgOffset)
{
  int ret = -1;
  if (gopCfg->size >= MAX_GOP_PIC_CONFIG_NUM)
    return -1;

  if (gopCfgOffset)
    gopCfgOffset[gopSize] = gopCfg->size;
  if(config)
  {
    int id = 0;
    while (config[id].num)
    {
      VPU_EncParseGopConfigString (&config[id], gopCfg, id, gopSize);
      id ++;
    }
    ret = 0;
  }
  return ret;
}

static int VPU_EncInitGopConfigs(int gopSize, VCEncGopConfig *gopCfg, CONFIG* params)
{
  //initialize this->encIn.gopConfig.pGopPicCfg
  VCEncGopPicConfig gopPicCfg[MAX_GOP_PIC_CONFIG_NUM];
  VCEncGopPicSpecialConfig gopPicSpecialCfg[MAX_GOP_SPIC_CONFIG_NUM];
  memset(gopPicCfg, 0, sizeof(gopPicCfg));
  memset(gopPicSpecialCfg, 0, sizeof(gopPicSpecialCfg));
  gopCfg->pGopPicCfg = gopPicCfg;
  gopCfg->pGopPicSpecialCfg = gopPicSpecialCfg;
  gopCfg->size = 0;

  int i, pre_load_num;
  FrameString *default_configs[8] = {
              (IS_H264(params->cfg.codecFormat) ? RpsDefault_H264_GOPSize_1 : RpsDefault_GOPSize_1),
              RpsDefault_GOPSize_2,
              RpsDefault_GOPSize_3,
              RpsDefault_GOPSize_4,
              RpsDefault_GOPSize_5,
              RpsDefault_GOPSize_6,
              RpsDefault_GOPSize_7,
              RpsDefault_GOPSize_8 };
  if (gopSize < 0 || gopSize > MAX_GOPCONFIG_SIZE)
  {
    VPU_ENC_ERROR("GOP Config: Error, Invalid GOP Size");
    return -1;
  }

  // GOP size in rps array for gopSize=N
  // N<=4:      GOP1, ..., GOPN
  // 4<N<=8:   GOP1, GOP2, GOP3, GOP4, GOPN
  // N > 8:       GOP1, GOPN
  // Adaptive:  GOP1, GOP2, GOP3, GOP4, GOP6, GOP8
  if (gopSize > MAX_GOPCONFIG_SIZE)
    pre_load_num = 1;
  else if (gopSize >= 4 || gopSize == 0)
    pre_load_num = 4;
  else
    pre_load_num = gopSize;

  gopCfg->special_size = 0;
  gopCfg->ltrcnt       = 0;

  for (i = 1; i <= pre_load_num; i++)
  {
    if (VPU_EncReadGopConfig (default_configs[i-1], gopCfg, i, gopCfg->gopCfgOffset))
      return -1;
  }

  if (gopSize == 0)
  {
    //gop6
    if (VPU_EncReadGopConfig (default_configs[5], gopCfg, 6, gopCfg->gopCfgOffset))
      return -1;
    //gop8
    if (VPU_EncReadGopConfig (default_configs[7], gopCfg, 8, gopCfg->gopCfgOffset))
      return -1;
  }
  else if (gopSize > 4)
  {
    //gopSize
    if (VPU_EncReadGopConfig (default_configs[gopSize-1], gopCfg, gopSize, gopCfg->gopCfgOffset))
      return -1;
  }

  return 0;
}

static void VPU_EncInitEncInParamsCreate(VCEncIn *pEncIn, CONFIG* params)
{
  int k;
  // initialize pic reference configure, before VCEncStrmStart called
  pEncIn->gopCurrPicConfig.codingType = FRAME_TYPE_RESERVED;
  pEncIn->gopCurrPicConfig.numRefPics = NUMREFPICS_RESERVED;
  pEncIn->gopCurrPicConfig.poc = -1;
  pEncIn->gopCurrPicConfig.QpFactor = QPFACTOR_RESERVED;
  pEncIn->gopCurrPicConfig.QpOffset = QPOFFSET_RESERVED;
  pEncIn->gopCurrPicConfig.temporalId = TEMPORALID_RESERVED;
  pEncIn->i8SpecialRpsIdx = -1;
  for (k = 0; k < VCENC_MAX_REF_FRAMES; k++)
  {
    pEncIn->gopCurrPicConfig.refPics[k].ref_pic     = INVALITED_POC;
    pEncIn->gopCurrPicConfig.refPics[k].used_by_cur = 0;
  }

  for (k = 0; k < VCENC_MAX_LT_REF_FRAMES; k++)
    pEncIn->long_term_ref_pic[k] = INVALITED_POC;

  pEncIn->bIsPeriodUsingLTR = HANTRO_FALSE;
  pEncIn->bIsPeriodUpdateLTR = HANTRO_FALSE;
  memset(pEncIn->bLTR_need_update, 0, sizeof(bool)*VCENC_MAX_LT_REF_FRAMES);
  pEncIn->bIsIDR = HANTRO_TRUE;
  pEncIn->u8IdxEncodedAsLTR = 0;
  pEncIn->gopSize = params->gopSize;
  pEncIn->vui_timing_info_enable = 1;
  pEncIn->hashType = 0;
  pEncIn->poc = 0;
  pEncIn->last_idr_picture_cnt = pEncIn->picture_cnt = 0;

  // initialize this->encIn.gopConfig
  pEncIn->gopConfig.idr_interval = params->nPFrames;
  pEncIn->gopConfig.gdrDuration = 0;
  pEncIn->gopConfig.firstPic = 0;
  pEncIn->gopConfig.lastPic = 100;
  pEncIn->gopConfig.outputRateNumer = params->cfg.frameRateNum;
  pEncIn->gopConfig.outputRateDenom = params->cfg.frameRateDenom;
  pEncIn->gopConfig.inputRateNumer = params->cfg.frameRateNum;
  pEncIn->gopConfig.inputRateDenom = params->cfg.frameRateDenom;
  pEncIn->gopConfig.gopLowdelay = 0;
  pEncIn->gopConfig.interlacedFrame = 0;

  VPU_EncInitGopConfigs(pEncIn->gopSize, &pEncIn->gopConfig, params);
}

static VCEncPictureType VPU_EncConvertColorFmt(VpuColorFormat vpuColorFmt, int chromaInterleave)
{
  VCEncPictureType inputColorFmt = VCENC_FORMAT_MAX;

  switch(vpuColorFmt) {
    case VPU_COLOR_420:
      inputColorFmt = chromaInterleave ? VCENC_YUV420_SEMIPLANAR : VCENC_YUV420_PLANAR;
      break;
    // case VPU_COLOR_420_VU:
    //   inputColorFmt = VCENC_YUV420_SEMIPLANAR_VU;
    //   break;
    case VPU_COLOR_422YUYV:
      inputColorFmt = VCENC_YUV422_INTERLEAVED_YUYV;
      break;
    case VPU_COLOR_422UYVY:
      inputColorFmt = VCENC_YUV422_INTERLEAVED_UYVY;
      break;
    case VPU_COLOR_ARGB8888:
      inputColorFmt = VCENC_BGR888;
      break;
    case VPU_COLOR_BGRA8888:
      inputColorFmt = VCENC_RGB888;
      break;
    case VPU_COLOR_RGB565:
      inputColorFmt = VCENC_RGB565;
      break;
    case VPU_COLOR_BGR565:
      inputColorFmt = VCENC_BGR565;
      break;
    case VPU_COLOR_RGB555:
      inputColorFmt = VCENC_RGB555;
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
  return inputColorFmt;
}

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

static VpuEncRetCode VPU_EncSetPreProcessorDefaults(
  VpuEncObj* pObj,
  unsigned int width,
  unsigned int height,
  int angle,
  VpuColorFormat colorFmt,
  int chromaInterleave)
{
  VCEncPreProcessingCfg* preProcCfg = 0;
  int stride_align;
  preProcCfg = &pObj->config.preProcCfg;
  preProcCfg->inputType = VPU_EncConvertColorFmt(colorFmt, chromaInterleave);
  if (preProcCfg->inputType == VCENC_YUV420_PLANAR)
    stride_align = 32;
  else
    stride_align = 16;
  preProcCfg->origWidth = (width + stride_align - 1) & (~(stride_align - 1));
  preProcCfg->origHeight = height;

  if (angle == 0)
    preProcCfg->rotation = 0;
  if (angle == 90 || angle == -270)
    preProcCfg->rotation = 1;
  if (angle == -90 || angle == 270)
    preProcCfg->rotation = 2;
  if (angle == 180 || angle == -180)
    preProcCfg->rotation = 3;

  preProcCfg->constChromaEn = 0;
  preProcCfg->input_alignment = 1<<4;

  return VPU_ENC_RET_SUCCESS;
}

static VpuEncRetCode VPU_EncSetRateCtrlDefaults(
  VpuEncObj* pObj,
  unsigned int bitrate,
  VpuCodStd format,
  int nRcIntraQp,
  int qpMin,
  int qpMax)
{
  VCEncRateCtrl* rcCfg = 0;
  rcCfg = &pObj->config.rcCfg;
  if (format == VPU_V_AVC)
  {
    rcCfg->blockRCSize = 2;
    rcCfg->ctbRcRowQpStep = 4;
    pObj->max_cu_size = 16;
  }
  if (format == VPU_V_HEVC)
  {
    rcCfg->blockRCSize = 0;
    rcCfg->ctbRcRowQpStep = 16;
    pObj->max_cu_size = 64;
  }
  rcCfg->hrd = 0;
  /* vbr and rate control are conflict */
  rcCfg->vbr = (bitrate == 0 ? 1 : 0);
  rcCfg->pictureRc = (rcCfg->vbr == 0 ? 1: 0);
  rcCfg->pictureSkip = 0;
  rcCfg->qpMinPB = rcCfg->qpMinI = (qpMin > 0 ? qpMin : ENC_MIN_QP_DEFAULT);
  rcCfg->qpMaxPB = rcCfg->qpMaxI = (qpMax > 0 ? qpMax : ENC_MAX_QP_DEFAULT);
  rcCfg->qpHdr = ((nRcIntraQp >= rcCfg->qpMinPB && nRcIntraQp <= rcCfg->qpMaxPB) ? nRcIntraQp : ENC_QP_DEFAULT);
  rcCfg->bitPerSecond = bitrate;
  rcCfg->hrdCpbSize = 1000000;
  rcCfg->bitVarRangeI = 10000;
  rcCfg->bitVarRangeP = 10000;
  rcCfg->bitVarRangeB = 10000;
  rcCfg->tolMovingBitRate = 2000;
  rcCfg->monitorFrames = VPU_ENC_DEFAULT;
  rcCfg->u32StaticSceneIbitPercent = 80;
  rcCfg->rcQpDeltaRange = 10;
  rcCfg->rcBaseMBComplexity = 15;
  rcCfg->picQpDeltaMax = VPU_ENC_DEFAULT;
  rcCfg->picQpDeltaMin = VPU_ENC_DEFAULT;
  rcCfg->tolCtbRcIntra = -1;

  return VPU_ENC_RET_SUCCESS;
}

static VpuEncRetCode VPU_EncSetCodingCtrlDefaults(VpuEncObj* pObj)
{
  VCEncCodingCtrl* codingCfg = 0;
  VCEncConfig* cfg = &pObj->config.cfg;

  codingCfg = &pObj->config.codingCfg;
  codingCfg->tc_Offset = -2;
  codingCfg->beta_Offset = 5;
  codingCfg->enableSao = 1;
  codingCfg->enableCabac = 1;

  if (cfg->codecFormat == VCENC_VIDEO_CODEC_H264 && cfg->profile == VCENC_H264_BASE_PROFILE)
  {
    codingCfg->enableCabac = 0;
  }

  /* 0 means none full range, 1 means full range when do RGB->YUV CSC */
  codingCfg->videoFullRange = 0;
  codingCfg->noiseLow = 10;
  codingCfg->firstFrameSigma = 11;
  codingCfg->smartH264Qp = 30;
  codingCfg->smartHevcLumQp = 30;
  codingCfg->smartHevcChrQp = 30;
  codingCfg->smartH264LumDcTh = 5;
  codingCfg->smartH264CbDcTh = 1;
  codingCfg->smartH264CrDcTh = 1;
  codingCfg->smartHevcChrAcNumTh[0] = 3;
  codingCfg->smartHevcChrAcNumTh[1] = 12;
  codingCfg->smartHevcChrAcNumTh[2] = 51;
  codingCfg->smartHevcLumAcNumTh[0] = 12;
  codingCfg->smartHevcLumAcNumTh[1] = 51;
  codingCfg->smartHevcLumAcNumTh[2] = 204;
  codingCfg->smartHevcChrDcTh[0] = 2;
  codingCfg->smartHevcChrDcTh[1] = 2;
  codingCfg->smartHevcChrDcTh[2] = 2;
  codingCfg->smartHevcLumDcTh[0] = 2;
  codingCfg->smartHevcLumDcTh[1] = 2;
  codingCfg->smartHevcLumDcTh[2] = 2;
  codingCfg->smartMeanTh[0] = 5;
  codingCfg->smartMeanTh[1] = 5;
  codingCfg->smartMeanTh[2] = 5;
  codingCfg->smartMeanTh[3] = 5;
  codingCfg->num_tile_columns = 1;
  codingCfg->num_tile_rows = 1;
  codingCfg->loop_filter_across_tiles_enabled_flag = 1;
  codingCfg->RoiQpDelta_ver = 1;

  codingCfg->streamMultiSegmentAmount = 4;

  codingCfg->roi1Qp = VPU_ENC_DEFAULT;
  codingCfg->roi2Qp = VPU_ENC_DEFAULT;
  codingCfg->roi3Qp = VPU_ENC_DEFAULT;
  codingCfg->roi4Qp = VPU_ENC_DEFAULT;
  codingCfg->roi5Qp = VPU_ENC_DEFAULT;
  codingCfg->roi6Qp = VPU_ENC_DEFAULT;
  codingCfg->roi7Qp = VPU_ENC_DEFAULT;
  codingCfg->roi8Qp = VPU_ENC_DEFAULT;

  codingCfg->intraArea.top    = -1;
  codingCfg->intraArea.left   = -1;
  codingCfg->intraArea.bottom = -1;
  codingCfg->intraArea.right  = -1;
  codingCfg->intraArea.enable = CheckArea(&codingCfg->intraArea, pObj);

  codingCfg->ipcm1Area.top     = -1;
  codingCfg->ipcm1Area.left    = -1;
  codingCfg->ipcm1Area.bottom  = -1;
  codingCfg->ipcm1Area.right   = -1;
  codingCfg->ipcm1Area.enable  = CheckArea(&codingCfg->ipcm1Area, pObj);

  codingCfg->ipcm2Area.top     = -1;
  codingCfg->ipcm2Area.left    = -1;
  codingCfg->ipcm2Area.bottom  = -1;
  codingCfg->ipcm2Area.right   = -1;
  codingCfg->ipcm2Area.enable  = CheckArea(&codingCfg->ipcm2Area, pObj);

  codingCfg->roi1Area.enable  = (CheckArea(&codingCfg->roi1Area, pObj) && (codingCfg->roi1DeltaQp || (codingCfg->roi1Qp >= 0)));
  codingCfg->roi2Area.enable  = (CheckArea(&codingCfg->roi2Area, pObj) && (codingCfg->roi2DeltaQp || (codingCfg->roi2Qp >= 0)));
  codingCfg->roi3Area.enable  = (CheckArea(&codingCfg->roi3Area, pObj) && (codingCfg->roi3DeltaQp || (codingCfg->roi3Qp >= 0)));
  codingCfg->roi4Area.enable  = (CheckArea(&codingCfg->roi4Area, pObj) && (codingCfg->roi4DeltaQp || (codingCfg->roi4Qp >= 0)));
  codingCfg->roi5Area.enable  = (CheckArea(&codingCfg->roi5Area, pObj) && (codingCfg->roi5DeltaQp || (codingCfg->roi5Qp >= 0)));
  codingCfg->roi6Area.enable  = (CheckArea(&codingCfg->roi6Area, pObj) && (codingCfg->roi6DeltaQp || (codingCfg->roi6Qp >= 0)));
  codingCfg->roi7Area.enable  = (CheckArea(&codingCfg->roi7Area, pObj) && (codingCfg->roi7DeltaQp || (codingCfg->roi7Qp >= 0)));
  codingCfg->roi8Area.enable  = (CheckArea(&codingCfg->roi8Area, pObj) && (codingCfg->roi8DeltaQp || (codingCfg->roi8Qp >= 0)));

  return VPU_ENC_RET_SUCCESS;
}

static VpuEncRetCode VPU_EncSetConfigDefaults(VpuEncObj* pObj, int frameRate, VpuCodStd format, int width, int height)
{
  VCEncConfig* cfg = 0;
  cfg = &pObj->config.cfg;
  cfg->streamType = VCENC_BYTE_STREAM;
  if (format == VPU_V_AVC)
    cfg->codecFormat = VCENC_VIDEO_CODEC_H264;
  if (format == VPU_V_HEVC)
    cfg->codecFormat = VCENC_VIDEO_CODEC_HEVC;
  cfg->profile = (IS_H264(cfg->codecFormat) ? VCENC_H264_BASE_PROFILE : VCENC_HEVC_MAIN_PROFILE);
  cfg->tier = VCENC_HEVC_MAIN_TIER;

  if (pObj->config.preProcCfg.rotation && pObj->config.preProcCfg.rotation != 3)
  {
    cfg->width = AlignHeight(height, VPU_ENC_DEFAULT_ALIGNMENT_V);
    cfg->height = AlignWidth(width, VPU_ENC_DEFAULT_ALIGNMENT_H);
  }
  else
  {
    cfg->width = AlignWidth(width, VPU_ENC_DEFAULT_ALIGNMENT_H);
    cfg->height = AlignHeight(height, VPU_ENC_DEFAULT_ALIGNMENT_V);
  }
  cfg->level = (IS_H264(cfg->codecFormat) ? calculateH264Level(cfg->width, cfg->height) : VCENC_HEVC_LEVEL_5_1);
  cfg->frameRateNum = frameRate;
  cfg->frameRateDenom = 1;
  cfg->maxTLayers = 1;         /*will be recalculated after InitGopConfigs*/
  cfg->refFrameAmount = 1;     /*will be recalculated after InitGopConfigs*/
  cfg->bitDepthLuma = 8;
  cfg->bitDepthChroma = 8;
  cfg->enableSsim = 1;
  cfg->rdoLevel = 1;
  cfg->exp_of_input_alignment = 4;
  cfg->parallelCoreNum = 1;
  cfg->log2MaxPicOrderCntLsb = 16;
  cfg->log2MaxFrameNum = 12;
  cfg->cuInfoVersion = -1;
  cfg->codedChromaIdc = VCENC_CHROMA_IDC_420;

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
    VPU_ENC_ERROR("%s: failure: invalid parameter \r\n",__FUNCTION__);
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
  // do nothing because VC8000E encoder don't need register frame buffer
  return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPU_EncQueryMem(VpuMemInfo* pOutMemInfo)
{
  VpuMemSubBlockInfo * pMem;

  if(pOutMemInfo == NULL)
  {
    VPU_ENC_ERROR("%s: failure: invalid parameter \r\n", __FUNCTION__);
    return VPU_ENC_RET_INVALID_PARAM;
  }
  pMem = &pOutMemInfo->MemSubBlock[VIRT_INDEX];
  pMem->MemType = VPU_MEM_VIRT;
  pMem->nAlignment = VPU_MEM_ALIGN;
  pMem->nSize = sizeof(VpuEncHandleInternal);

  pMem = &pOutMemInfo->MemSubBlock[PHY_INDEX];
  pMem->MemType = VPU_MEM_PHY;
  pMem->nAlignment = VPU_MEM_ALIGN;
  pMem->nSize = VPU_BITS_BUF_SIZE;

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

  int ret = EWLMallocLinear(pewl, pInOutMem->nSize, VPU_MEM_ALIGN, &info);
  if (ret < 0)
  {
    VPU_ENC_ERROR("%s: EWLMallocLinear failed !! ret %d\r\n", __FUNCTION__, ret);
    return VPU_ENC_RET_FAILURE;
  }

  pInOutMem->nPhyAddr = info.busAddress;
  pInOutMem->nVirtAddr = (unsigned long)info.virtualAddress;
  pInOutMem->nSize = info.size;
  pInOutMem->nCpuAddr = info.ion_fd;

  VPU_ENC_LOG("EWLMallocLinear pewl %p, size %d, virt %p phy %p\n",
    pewl, pInOutMem->nSize, info.virtualAddress, (void*)info.busAddress);

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
  info.allocVirtualAddr = (u32*)pInMem->nVirtAddr;
  info.busAddress = pInMem->nPhyAddr;
  info.ion_fd = pInMem->nCpuAddr;

  ewlInit.clientType = EWL_CLIENT_TYPE_H264_ENC;
  pewl = (void*)EWLInit(&ewlInit);
  if (!pewl)
  {
    VPU_ENC_ERROR("%s: EWLInit failed !! \r\n",__FUNCTION__);
    return VPU_ENC_RET_FAILURE;
  }
  EWLFreeLinear(pewl, &info);

  if (pewl)
    EWLRelease(pewl);

  return VPU_ENC_RET_SUCCESS;
}

static void AlignedPicSizeGotbyFormat (VCEncPictureType type, u32 width, u32 height, u32 alignment,
                                       u32 *luma_Size, u32 *chroma_Size)
{
  u32 luma_stride=0, chroma_stride = 0;
  u32 lumaSize = 0, chromaSize = 0;
  VCEncGetAlignedStride(width, type, &luma_stride, &chroma_stride, alignment);
  switch (type)
  {
    case VCENC_YUV420_PLANAR:
      lumaSize = luma_stride * height;
      chromaSize = chroma_stride * height / 2 * 2;
      break;
    case VCENC_YUV420_SEMIPLANAR:
    case VCENC_YUV420_SEMIPLANAR_VU:
      lumaSize = luma_stride * height;
      chromaSize = chroma_stride * height / 2;
      break;
    case VCENC_YUV422_INTERLEAVED_YUYV:
    case VCENC_YUV422_INTERLEAVED_UYVY:
    case VCENC_RGB565:
    case VCENC_BGR565:
    case VCENC_RGB555:
    case VCENC_BGR555:
    case VCENC_RGB444:
    case VCENC_BGR444:
    case VCENC_RGB888:
    case VCENC_BGR888:
    case VCENC_RGB101010:
    case VCENC_BGR101010:
      lumaSize = luma_stride * height;
      chromaSize = 0;
      break;
    case VCENC_YUV420_PLANAR_10BIT_I010:
      lumaSize = luma_stride * height;
      chromaSize = chroma_stride * height / 2 * 2;
      break;
    case VCENC_YUV420_PLANAR_10BIT_P010:
      lumaSize = luma_stride * height;
      chromaSize = chroma_stride * height / 2;
      break;
    case VCENC_YUV420_PLANAR_10BIT_PACKED_PLANAR:
      lumaSize = luma_stride * 10 / 8 * height;
      chromaSize = chroma_stride * 10 / 8 * height / 2 * 2;
      break;
    case VCENC_YUV420_10BIT_PACKED_Y0L2:
      lumaSize = luma_stride * 2 * 2 * height / 2;
      chromaSize = 0;
      break;
    case VCENC_YUV420_PLANAR_8BIT_DAHUA_HEVC:
      lumaSize = luma_stride * ((height + 32 - 1) & (~(32 - 1)));
      chromaSize = lumaSize / 2;
      break;
    case VCENC_YUV420_PLANAR_8BIT_DAHUA_H264:
      lumaSize = luma_stride * height * 2 * 12 / 8;
      chromaSize = 0;
      break;
    case VCENC_YUV420_SEMIPLANAR_8BIT_FB:
    case VCENC_YUV420_SEMIPLANAR_VU_8BIT_FB:
      lumaSize = luma_stride * ((height + 3) / 4);
      chromaSize = chroma_stride * (((height / 2) + 3) / 4);
      break;
    case VCENC_YUV420_PLANAR_10BIT_P010_FB:
      lumaSize = luma_stride * ((height + 3) / 4);
      chromaSize = chroma_stride * (((height / 2) + 3) / 4);
      break;
    case VCENC_YUV420_SEMIPLANAR_101010:
      lumaSize = luma_stride * height;
      chromaSize = chroma_stride * height / 2;
      break;
    case VCENC_YUV420_8BIT_TILE_64_4:
    case VCENC_YUV420_UV_8BIT_TILE_64_4:
      lumaSize = luma_stride *  ((height + 3) / 4);
      chromaSize = chroma_stride * (((height / 2) + 3) / 4);
      break;
    case VCENC_YUV420_10BIT_TILE_32_4:
      lumaSize = luma_stride * ((height+ 3) / 4);
      chromaSize = chroma_stride * (((height / 2) + 3) / 4);
      break;
    case VCENC_YUV420_10BIT_TILE_48_4:
    case VCENC_YUV420_VU_10BIT_TILE_48_4:
      lumaSize = luma_stride * ((height + 3) / 4);
      chromaSize = chroma_stride * (((height / 2) + 3) / 4);
      break;
    case VCENC_YUV420_8BIT_TILE_128_2:
    case VCENC_YUV420_UV_8BIT_TILE_128_2:
      lumaSize = luma_stride * ((height + 1) / 2);
      chromaSize = chroma_stride * (((height / 2) + 1) / 2);
      break;
    case VCENC_YUV420_10BIT_TILE_96_2:
    case VCENC_YUV420_VU_10BIT_TILE_96_2:
      lumaSize = luma_stride * ((height + 1) / 2);
      chromaSize = chroma_stride * (((height / 2) + 1) / 2);
      break;
    default:
      VPU_ENC_ERROR("not support this format\n");
      chromaSize = lumaSize = 0;
      break;
  }

  if (luma_Size != NULL)
    *luma_Size = lumaSize;
  if (chroma_Size != NULL)
    *chroma_Size = chromaSize;
}

static VpuEncRetCode VCEnc_encoder_stream_start(ENCODER_PROTOTYPE* arg, CONFIG* params, STREAM_BUFFER* stream)
{
  if(arg == NULL || stream == NULL)
  {
    VPU_ENC_ERROR("%s: failure: invalid parameter \r\n",__FUNCTION__);
    return VPU_ENC_RET_INVALID_PARAM;
  }
  VpuEncRetCode stat;
  VCEncOut encOut;
  ENCODER* this = (ENCODER*)arg;

  VPU_EncInitGopConfigs(this->encIn.gopSize, &this->encIn.gopConfig, params);

  this->encIn.pOutBuf[0] = (u32 *) stream->bus_data;
  this->encIn.outBufSize[0] = stream->buf_max_size;
  this->encIn.busOutBuf[0] = (ptr_t)stream->bus_address;
  this->nTotalFrames = 0;

  VCEncRet ret = VCEncStrmStart(this->inst, &this->encIn, &encOut);

  switch (ret)
  {
    case VCENC_OK:
      stream->streamlen = encOut.streamSize;
      stat = VPU_ENC_RET_SUCCESS;
      break;
    case VCENC_NULL_ARGUMENT:
      stat = VPU_ENC_RET_INVALID_PARAM;
      VPU_ENC_ERROR("VCEncStrmStart returned VCENC_NULL_ARGUMENT");
      break;
    case VCENC_INSTANCE_ERROR:
      stat = VCENC_ERROR;
      VPU_ENC_ERROR("VCEncStrmStart returned VCENC_INSTANCE_ERROR");
      break;
    case VCENC_INVALID_ARGUMENT:
      stat = VPU_ENC_RET_INVALID_PARAM;
      VPU_ENC_ERROR("VCEncStrmStart returned VCENC_INVALID_ARGUMENT");
      break;
    case VCENC_INVALID_STATUS:
      stat = VCENC_ERROR;
      VPU_ENC_ERROR("VCEncStrmStart returned VCENC_INVALID_STATUS");
      break;
    default:
      stat = VCENC_ERROR;
      VPU_ENC_ERROR("VCEncStrmStart returned unspecified error: %d", ret);
      break;
  }
  return stat;
}

static void VPU_EncSetEncInParamsEncode (VCEncIn *pEncIn, FRAME* frame, STREAM_BUFFER* stream, CONFIG* params)
{
  u32 luma_Size = 0, chroma_Size = 0;

  AlignedPicSizeGotbyFormat (params->preProcCfg.inputType, params->preProcCfg.origWidth, params->preProcCfg.origHeight,
                                        params->preProcCfg.input_alignment, &luma_Size, &chroma_Size);
  // for ANDROID, frame->fb_bus_address_ChromaU and frame->fb_bus_address_ChromaV are null.
  // consider input buffer's Y,U,V physical address to be continous.
  #ifdef ANDROID
  pEncIn->busLuma = frame->fb_bus_address_Luma;
  pEncIn->busChromaU = pEncIn->busLuma + luma_Size;
  pEncIn->busChromaV = pEncIn->busChromaU + chroma_Size / 2;
  #else
  pEncIn->busLuma = frame->fb_bus_address_Luma;
  pEncIn->busChromaU = frame->fb_bus_address_ChromaU;
  pEncIn->busChromaV = frame->fb_bus_address_ChromaV;
  #endif
  pEncIn->busLumaOrig = pEncIn->busChromaUOrig = pEncIn->busChromaVOrig = (ptr_t)NULL;

  pEncIn->timeIncrement = params->cfg.frameRateDenom;
  pEncIn->resendSPS = 0;
  pEncIn->resendPPS = 0;
  pEncIn->resendVPS = 0;
  if (pEncIn->codingType == VCENC_INTRA_FRAME && pEncIn->picture_cnt != 0) {
    pEncIn->poc = 0;
    pEncIn->resendSPS = 1;
    pEncIn->resendPPS = 1;
    pEncIn->resendVPS = 1;
  }

  pEncIn->pOutBuf[0] = (u32 *) stream->bus_data;
  pEncIn->outBufSize[0] = stream->buf_max_size;
  pEncIn->busOutBuf[0] = (ptr_t)stream->bus_address;
  }

VpuEncRetCode VCEnc_encoder_encode(ENCODER_PROTOTYPE* arg, FRAME* frame, STREAM_BUFFER* stream, CONFIG* params)
{
  if(arg == NULL || stream == NULL || frame == NULL)
  {
    VPU_ENC_ERROR("%s: failure: invalid parameter \r\n",__FUNCTION__);
    return VPU_ENC_RET_INVALID_PARAM;
  }
  VpuEncRetCode stat;
  ENCODER* this = (ENCODER*)arg;

  VCEncRet ret;
  VCEncOut encOut;
  VCEncRateCtrl rcCfg;
  VCEncCodingCtrl codingCfg;

  memset(&codingCfg, 0, sizeof(VCEncCodingCtrl));
  memset(&rcCfg, 0, sizeof(VCEncRateCtrl));

  // set the first frame
  if (this->nTotalFrames == 0)
  {
    this->encIn.timeIncrement = 0;
  }
  if (frame->frame_type == INTRA_FRAME)
  {
    this->encIn.codingType = VCENC_INTRA_FRAME;
    this->encIn.last_idr_picture_cnt = this->nTotalFrames;
  }
  else
  {
    this->encIn.codingType = VCENC_PREDICTED_FRAME;
  }
  this->encIn.picture_cnt = this->nTotalFrames;
  VPU_EncSetEncInParamsEncode(&this->encIn, frame, stream, params);

  VPU_ENC_LOG("Frame type %s, Frame counter (%d)\n", (this->encIn.codingType == VCENC_INTRA_FRAME) ?
        "I": ((this->encIn.codingType == VCENC_PREDICTED_FRAME) ? "P" : "B"),
        (int)this->nTotalFrames);

  ret = VCEncGetRateCtrl(this->inst, &rcCfg);

  if (ret == VCENC_OK)
  {
    if (rcCfg.bitPerSecond != frame->bitrate) {
      rcCfg.bitPerSecond = frame->bitrate;
      ret = VCEncSetRateCtrl(this->inst, &rcCfg);
    }
  }
  else
  {
    VPU_ENC_ERROR("VCEncGetRateCtrl failed! (%d)", ret);
    return VPU_ENC_RET_FAILURE;
  }
  ret = VCEncGetCodingCtrl(this->inst, &codingCfg);

  VPU_ENC_LOG("rcCfg.qpHdr %d", rcCfg.qpHdr);
  VPU_ENC_LOG("rcCfg.bitPerSecond %d\n", rcCfg.bitPerSecond);

  if (ret == VCENC_OK)
  {
    VPU_EncInitGopConfigs(this->encIn.gopSize, &this->encIn.gopConfig, params);
    ret = VCEncStrmEncode(this->inst, &this->encIn, &encOut, NULL, NULL);
    VPU_EncInitGopConfigs(this->encIn.gopSize, &this->encIn.gopConfig, params);
  }
  else
  {
    VPU_ENC_ERROR("VCEncGetCodingCtrl failed! (%d)", ret);
    return VPU_ENC_RET_FAILURE;
  }

  switch (ret)
  {
    case VCENC_FRAME_ENQUEUE:
      stat = VPU_ENC_RET_SUCCESS;
      this->nextCodingType = VCEncFindNextPic (this->inst, &this->encIn, this->encIn.gopSize, this->encIn.gopConfig.gopCfgOffset, FALSE);
      VPU_ENC_LOG("VCENC_FRAME_ENQUEUE: nextCodingType %d", this->nextCodingType);
      break;
    case VCENC_FRAME_READY:
      stat = VPU_ENC_RET_SUCCESS;
      this->nTotalFrames++;
      stream->streamlen = encOut.streamSize;
      if (encOut.codingType == VCENC_INTRA_FRAME) {
        VPU_ENC_LOG("encOut coded frame: VCENC_INTRA_FRAME ");
      }
      else if (encOut.codingType == VCENC_PREDICTED_FRAME) {
        VPU_ENC_LOG("encOut coded frame: VCENC_PREDICTED_FRAME ");
      }
      else if (encOut.codingType == VCENC_BIDIR_PREDICTED_FRAME) {
        VPU_ENC_LOG("encOut coded frame: VCENC_BIDIR_PREDICTED_FRAME ");
      }
      else  {
        VPU_ENC_LOG("encOut: Not coded frame ");
      }
      this->nextCodingType = VCEncFindNextPic (this->inst, &this->encIn, this->encIn.gopSize, this->encIn.gopConfig.gopCfgOffset, FALSE);
      VPU_ENC_LOG("VCENC_FRAME_READY: nextCodingType %d", this->nextCodingType);
      break;
    case VCENC_NULL_ARGUMENT:
      stat = VPU_ENC_RET_INVALID_PARAM;
      VPU_ENC_ERROR("VCEncStrmEncode returned VCENC_NULL_ARGUMENT");
      break;
    case VCENC_INSTANCE_ERROR:
      stat = VPU_ENC_RET_FAILURE;
      VPU_ENC_ERROR("VCEncStrmEncode returned VCENC_INSTANCE_ERROR");
      break;
    case VCENC_INVALID_ARGUMENT:
      stat = VPU_ENC_RET_INVALID_PARAM;
      VPU_ENC_ERROR("VCEncStrmEncode returned VCENC_INVALID_ARGUMENT");
      break;
    case VCENC_INVALID_STATUS:
      stat = VPU_ENC_RET_FAILURE;
      VPU_ENC_ERROR("VCEncStrmEncode returned VCENC_INVALID_STATUS");
      break;
    case VCENC_HW_RESERVED:
      stat = VPU_ENC_RET_FAILURE;
      VPU_ENC_ERROR("VCEncStrmEncode returned VCENC_HW_RESERVED");
      break;
    case VCENC_HW_RESET:
      stat = VPU_ENC_RET_FAILURE;
      VPU_ENC_ERROR("VCEncStrmEncode returned VCENC_HW_RESET");
      break;
    case VCENC_HW_TIMEOUT:
      stat = VPU_ENC_RET_FAILURE_TIMEOUT;
      VPU_ENC_ERROR("VCEncStrmEncode returned VCENC_HW_TIMEOUT：");
      break;
    case VCENC_SYSTEM_ERROR:
      stat = VPU_ENC_RET_FAILURE;
      VPU_ENC_ERROR("VCEncStrmEncode returned VCENC_SYSTEM_ERROR");
      break;
    case VCENC_OUTPUT_BUFFER_OVERFLOW:
      stat = VPU_ENC_RET_INSUFFICIENT_FRAME_BUFFERS;
      VPU_ENC_ERROR("VCEncStrmEncode returned VCENC_OUTPUT_BUFFER_OVERFLOW");
      break;
    default:
      stat = VPU_ENC_RET_FAILURE;
      VPU_ENC_ERROR("VCEncStrmEncode returned unspecified error: %d", ret);
  }

  return stat;
}

static VpuEncRetCode VPU_EncStartEncode(VpuEncObj *pObj, STREAM_BUFFER* pOutputStream)
{
  VpuEncRetCode stat = VPU_ENC_RET_FAILURE;

  stat = VCEnc_encoder_stream_start(pObj->codec, &pObj->config, pOutputStream);

  if (stat != VPU_ENC_RET_SUCCESS) {
    VPU_ENC_ERROR("%s error, stat %d\n", __FUNCTION__, stat);
    return VPU_ENC_RET_FAILURE;
  }

  pObj->bStreamStarted = 1;

  return VPU_ENC_RET_SUCCESS;
}

static VpuEncRetCode VPU_EncDoEncode(VpuEncObj *pObj, FRAME* pFrame, STREAM_BUFFER* pStream)
{
  VpuEncRetCode stat = VPU_ENC_RET_FAILURE;

  stat = VCEnc_encoder_encode(pObj->codec, pFrame, pStream, &pObj->config);

  if (stat == VPU_ENC_RET_SUCCESS) {
    if (pStream->streamlen > pStream->buf_max_size) {
      VPU_ENC_ERROR("%s: output buffer is too small, need %d but actual is %d\n",
          __FUNCTION__, pStream->streamlen, pStream->buf_max_size);
      return VPU_ENC_RET_INSUFFICIENT_FRAME_BUFFERS;
    }
  }

  return stat;
}

VpuEncRetCode VPU_EncEncodeFrame(VpuEncHandle InHandle, VpuEncEncParam* pInOutParam)
{
  VpuEncHandleInternal * pVpuObj;
  VpuEncObj * pObj;
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

#if 1
  /* set output buffer */
  copy = 1;
  stream.bus_data = (unsigned char*)pObj->pBsBufVirt;
  stream.bus_address = (u32)pObj->pBsBufPhy;
  stream.buf_max_size = (u32)pObj->nBsBufLen;

  /* if the out buffer is DMA buffer, use it instead of pBsBufPhy, this can avoid copy buffer */
  if (pInOutParam->nInPhyOutput) {
    copy = 0;
    stream.bus_data = (unsigned char*)pInOutParam->nInVirtOutput;
    stream.bus_address = (u32)pInOutParam->nInPhyOutput;
    stream.buf_max_size = (u32)pInOutParam->nInOutputBufLen;
  }
#endif

  pInOutParam->eOutRetCode |= VPU_ENC_INPUT_NOT_USED;

  /* encoder start stream, output codec data */
  if (!pObj->bStreamStarted) {
    ret = VPU_EncStartEncode(pObj, &stream);
#if 0
    printf ("\n");
    {
      char *tmp = stream.bus_data;
      for (int i=0; i<200; i++)
        printf ("%02x ", tmp[i]);
    }
    printf ("\n");
#endif
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
    gettimeofday (&pObj->tvBegin, NULL);
    return ret;
  }

  pInOutParam->eOutRetCode &= (~VPU_ENC_OUTPUT_SEQHEADER);

  memset(&frame, 0, sizeof(FRAME));

  if(pInOutParam->pInFrame != NULL) {
    frame.fb_bus_address_Luma = (unsigned long)pInOutParam->pInFrame->pbufY;
    frame.fb_bus_address_ChromaU = (unsigned long)pInOutParam->pInFrame->pbufCb;
    frame.fb_bus_address_ChromaV = (unsigned long)pInOutParam->pInFrame->pbufCr;
  } else {
    frame.fb_bus_address_Luma = pInOutParam->nInPhyInput;
  }

  frame.bitrate = pObj->config.rcCfg.bitPerSecond;
  frame.frame_type = (pInOutParam->nForceIPicture ? INTRA_FRAME : NONINTRA_FRAME);

  ret = VPU_EncDoEncode(pObj, &frame, &stream);
#if 0
    printf ("\n");
    {
      char *tmp = stream.bus_data;
      for (int i=0; i<200; i++)
        printf ("%02x ", tmp[i]);
    }
    printf ("\n");
#endif
  VPU_ENC_LOG("VPU_EncDoEncode return %d\n", ret);

  if (ret != VPU_ENC_RET_SUCCESS) {
    VPU_ENC_ERROR("%s DoEncode return error %d\n", __FUNCTION__, ret);
    return ret;
  }

  if (copy) {
    memcpy((void*)pInOutParam->nInVirtOutput, stream.bus_data, stream.streamlen);
  }

  pInOutParam->nOutOutputSize = stream.streamlen;

  pInOutParam->eOutRetCode |= (VPU_ENC_OUTPUT_DIS | VPU_ENC_INPUT_USED);
  pObj->totalFrameCnt++;
  VPU_ENC_LOG("Encode out frame cnt %d, size %d type %d\n\n", pObj->totalFrameCnt, pInOutParam->nOutOutputSize, frame.frame_type);

  if(VPU_DUMP_RAW) {
    dumpStream((unsigned char*)pInOutParam->nInVirtOutput, pInOutParam->nOutOutputSize);
  }

  return ret;
}

static void VCEnc_Free(void* pData)
{
  unsigned char* block = ((unsigned char*)pData) -sizeof(unsigned long);
  unsigned long sentinel = MEMORY_SENTINEL;
  unsigned long size = *((unsigned long*)block);
  memcmp(&block[size+sizeof(size)], &sentinel, sizeof(sentinel));
  free(block);
}

static void VCEnc_encoder_destroy(ENCODER_PROTOTYPE* arg)
{
  ENCODER* this = (ENCODER*)arg;
  if (this)
  {
    this->base.stream_start = 0;
    this->base.stream_end = 0;
    this->base.encode = 0;
    this->base.destroy = 0;

    if (this->inst)
    {
      VCEncRelease (this->inst);
      this->inst = 0;
    }

    VCEnc_Free(this);
  }
}

VpuEncRetCode VPU_EncClose(VpuEncHandle InHandle)
{
  VpuEncHandleInternal * pVpuObj;
  VpuEncObj* pObj;

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

  if (pObj->codec)
  {
    VCEnc_encoder_destroy(pObj->codec);
  }

  if (pObj->pewl)
    EWLRelease(pObj->pewl);
  pObj->pewl = NULL;

  return VPU_ENC_RET_SUCCESS;
}

static void * VCEnc_Malloc(unsigned long size)
{
  unsigned long extra = sizeof(unsigned long) * 2;
  unsigned char* data = (unsigned char*)malloc(size + extra);
  unsigned long sentinel = MEMORY_SENTINEL;

  if (!data) {
    return 0;
  }

  memcpy(data, &size, sizeof(size));
  memcpy(&data[size + sizeof(size)], &sentinel, sizeof(sentinel));
  return data + sizeof(size);
}

// create codec instance and initialize it
static ENCODER_PROTOTYPE* VCEnc_encoder_create(const CONFIG* params, VpuIsoColorAspects * pColorAspects)
{
  VCEncConfig cfg;
  memset(&cfg, 0, sizeof(VCEncConfig));

  ENCODER *this = VCEnc_Malloc(sizeof(ENCODER));

  this->inst = 0;
  memset(&this->encIn, 0, sizeof(VCEncIn));
  this->base.stream_start = VCEnc_encoder_stream_start;
  this->base.encode = VCEnc_encoder_encode;
  this->base.destroy = VCEnc_encoder_destroy;
  this->nIFrameCounter = 0;
  this->nextCodingType = VCENC_INTRA_FRAME;

  VPU_EncInitEncInParamsCreate(&this->encIn, (CONFIG*)params);
  VPU_EncInitConfigParams(&this->encIn, &cfg, (CONFIG*)params);

  VCEncRet ret = VCEncInit(&cfg, &this->inst);

  // Setup coding control
  if (ret == VCENC_OK)
  {
    VCEncCodingCtrl codingCfg;
    memset(&codingCfg, 0, sizeof(VCEncCodingCtrl));
    ret = VCEncGetCodingCtrl(this->inst, &codingCfg);

    if (ret == VCENC_OK)
    {
      VPU_EncInitCodingCtrlParams(&codingCfg, (CONFIG*)params);
      ret = VCEncSetCodingCtrl(this->inst, &codingCfg);
    }
    else
    {
      VPU_ENC_ERROR("VCEncGetCodingCtrl failed! (%d)", ret);
      return NULL;
    }
  }
  else
  {
    VPU_ENC_ERROR ("VCEncInit failed ! (%d)", ret);
    return NULL;
  }

  // Setup rate control
  if (ret == VCENC_OK)
  {
    VCEncRateCtrl rcCfg;
    memset(&rcCfg, 0, sizeof(VCEncRateCtrl));
    ret = VCEncGetRateCtrl(this->inst, &rcCfg);
    if (ret == VCENC_OK)
    {
      VPU_EncInitRateCtrlParams(&rcCfg, (CONFIG*)params);
      ret = VCEncSetRateCtrl(this->inst, &rcCfg);
    }
    else
    {
      VPU_ENC_ERROR("VCEncGetRateCtrl failed! (%d)", ret);
      return NULL;
    }
  }
  else
  {
    VPU_ENC_ERROR ("VCEncSetCodingCtrl failed ! (%d)", ret);
    return NULL;
  }

  // Setup preprocessing
  if (ret == VCENC_OK)
  {
    VCEncPreProcessingCfg preProcCfg;
    memset(&preProcCfg, 0, sizeof(VCEncPreProcessingCfg));
    ret = VCEncGetPreProcessing(this->inst, &preProcCfg);
    if (ret == VCENC_OK)
    {
      VPU_EncInitPreProcessorParams(&preProcCfg, (CONFIG*)params);
      ret = VCEncSetPreProcessing(this->inst, &preProcCfg);
    }
    else
    {
      VPU_ENC_ERROR("VCEncGetPreProcessing failed! (%d)", ret);
      return NULL;
    }
  }
  else
  {
    VPU_ENC_ERROR ("VCEncSetRateCtrl failed ! (%d)", ret);
    return NULL;
  }

  if (ret != VCENC_OK)
  {
      VPU_ENC_ERROR("H264EncSetPreProcessing failed! (%d)", ret);
      return NULL;
  }

  // Setup VUI color aspects
  ret = VCEncSetVuiColorDescription(this->inst,
            pColorAspects->nVideoSignalPresentFlag, 5 /*Unspecified video format*/,
            pColorAspects->nColourDescPresentFlag, pColorAspects->nPrimaries,
            pColorAspects->nTransfer, pColorAspects->nMatrixCoeffs);

  if (ret != VCENC_OK)
  {
    VPU_ENC_ERROR("VCEncSetVuiColorDescription failed! (%d)", ret);
    return NULL;
  }

  return (ENCODER_PROTOTYPE*) this;
}

VpuEncRetCode VPU_EncOpen(VpuEncHandle *pOutHandle, VpuMemInfo* pInMemInfo,VpuEncOpenParam* pInParam)
{
  VpuMemSubBlockInfo * pMemPhy;
  VpuMemSubBlockInfo * pMemVirt;
  VpuEncHandleInternal* pVpuObj;
  VpuEncObj* pObj;

  pMemVirt = &pInMemInfo->MemSubBlock[VIRT_INDEX];
  pMemPhy = &pInMemInfo->MemSubBlock[PHY_INDEX];
  if ((pMemVirt->pVirtAddr == NULL) || MemNotAlign(pMemVirt->pVirtAddr, VPU_MEM_ALIGN)
      || (pMemVirt->nSize < sizeof(VpuEncHandleInternal)))
  {
    VPU_ENC_ERROR("%s: failure: invalid parameter ! \r\n", __FUNCTION__);
    return VPU_ENC_RET_INVALID_PARAM;
  }

  if ((pMemPhy->pVirtAddr == NULL) || MemNotAlign(pMemPhy->pVirtAddr, VPU_MEM_ALIGN)
      || (pMemPhy->pPhyAddr == NULL) || MemNotAlign(pMemPhy->pPhyAddr, VPU_MEM_ALIGN)
      || (pMemPhy->nSize < (VPU_BITS_BUF_SIZE)))
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
  pObj->pBsBufPhy = pMemPhy->pPhyAddr;    //this is physical address for output buffer, used in HW
  pObj->pBsBufVirt = pMemPhy->pVirtAddr;
  pObj->nBsBufLen = pMemPhy->nSize;

  VPU_ENC_LOG("VPU_EncOpen param w %d h %d bitrate %d gop %d frame rate %d qpmin %d qpmax %d\n",
    pInParam->nPicWidth, pInParam->nPicHeight, pInParam->nBitRate, pInParam->nGOPSize,
    pInParam->nFrameRate, pInParam->nUserQpMin, pInParam->nUserQpMax);

  //set default parameters for h264 and hevc format
  switch (pInParam->eFormat) {
    case VPU_V_AVC:
    case VPU_V_HEVC:
      VPU_EncSetPreProcessorDefaults(pObj, pInParam->nOrigWidth, pInParam->nOrigHeight,
        pInParam->nRotAngle, pInParam->eColorFormat, pInParam->nChromaInterleave);
      VPU_EncSetRateCtrlDefaults(pObj, pInParam->nBitRate, pInParam->eFormat,
        pInParam->nRcIntraQp, pInParam->nUserQpMin, pInParam->nUserQpMax);
      VPU_EncSetConfigDefaults(pObj, pInParam->nFrameRate, pInParam->eFormat, pInParam->nPicWidth, pInParam->nPicHeight);
      VPU_EncSetCodingCtrlDefaults(pObj);

      pObj->config.codingCfg.videoFullRange = pInParam->sColorAspects.nFullRange;

      if (pInParam->eColorFormat == VPU_COLOR_ARGB8888 || pInParam->eColorFormat == VPU_COLOR_BGRA8888 ||
          pInParam->eColorFormat == VPU_COLOR_RGB565 || pInParam->eColorFormat == VPU_COLOR_RGB555 ||
          pInParam->eColorFormat == VPU_COLOR_BGR565) {
            if (pObj->config.codingCfg.videoFullRange) {        /* full range */
              if (pInParam->nColorConversionType == 4) {
                pObj->config.preProcCfg.colorConversion.type = 0;  /* full range BT.601 */
              } else if (pInParam->nColorConversionType == 3) {
                pObj->config.preProcCfg.colorConversion.type = 1;  /* full range BT.709 */
              }
              else {
                pObj->config.preProcCfg.colorConversion.type = 0;
              }
            }
            else {          /* none full range */
              if (pInParam->nColorConversionType == 4) {
                pObj->config.preProcCfg.colorConversion.type = 4;  /* none full range BT.601 */
              } else if (pInParam->nColorConversionType == 3) {
                pObj->config.preProcCfg.colorConversion.type = 6;  /* none full range BT.709 */
              }
              else {
                pObj->config.preProcCfg.colorConversion.type = 4;
              }
            }
      }

      if (pInParam->nStreamSliceCount <= 1) {
        pObj->config.codingCfg.streamMultiSegmentMode = 0;
        pObj->config.codingCfg.streamMultiSegmentAmount = 1;
      } else {
        pObj->config.codingCfg.streamMultiSegmentMode = 1;
        pObj->config.codingCfg.streamMultiSegmentAmount = pInParam->nStreamSliceCount;
      }

      pObj->config.gopSize = 1;    /* make sure no B frame */
      pObj->config.nPFrames = (pInParam->nGOPSize > ENC_MAX_GOP_SIZE ? ENC_MAX_GOP_SIZE : pInParam->nGOPSize);

      if (pObj->config.rcCfg.bitPerSecond > VPU_ENC_MAX_BITRATE)
        pObj->config.rcCfg.bitPerSecond = VPU_ENC_MAX_BITRATE;
      if (pObj->config.rcCfg.bitPerSecond < VPU_ENC_MIN_BITRATE)
      {
        // workaround to set check bitrate, calculate bitPerSecond = bitPerFrame * pInParam->nFrameRate / compression,
        // so that resolution from max - min can get a approprite bitrate
        int bitPerFrame = pObj->config.cfg.width * pObj->config.cfg.height * 8;
        int compression = 50;
        pObj->config.rcCfg.bitPerSecond = bitPerFrame / compression  * pInParam->nFrameRate / 1000 * 1000;
      }

      pObj->codec = VCEnc_encoder_create(&pObj->config, &pInParam->sColorAspects);
      if (IS_H264(pObj->config.cfg.codecFormat)) {
        VPU_ENC_LOG("open H264 \r\n");
      }
      else  {
        VPU_ENC_LOG("open HEVC \r\n");
      }
      break;
    default:
      VPU_ENC_ERROR("%s: failure: invalid format !!! \r\n",__FUNCTION__);
      return VPU_ENC_RET_INVALID_PARAM;
  }

  if (NULL == pObj->codec) {
    VPU_ENC_ERROR("VCEnc_encoder_create failed\n");
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

  /* temporarily, Android will not set this parameter, add protect here */
  if (pInParam->nOrigWidth == 0 || pInParam->nOrigHeight == 0) {
    sEncOpenParamMore.nOrigWidth = pInParam->nPicWidth;
    sEncOpenParamMore.nOrigHeight = pInParam->nPicHeight;
  } else {
    sEncOpenParamMore.nOrigWidth = pInParam->nOrigWidth;
    sEncOpenParamMore.nOrigHeight = pInParam->nOrigHeight;
  }

  sEncOpenParamMore.nRotAngle = pInParam->nRotAngle;
  sEncOpenParamMore.nFrameRate = pInParam->nFrameRate;
  sEncOpenParamMore.nBitRate = pInParam->nBitRate * 1000; //kbps->bps
  sEncOpenParamMore.nGOPSize = pInParam->nGOPSize;
  sEncOpenParamMore.nColorConversionType = pInParam->nColorConversionType;
  sEncOpenParamMore.nStreamSliceCount = pInParam->nStreamSliceCount;

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

  memcpy(&sEncOpenParamMore.sColorAspects, &pInParam->sColorAspects, sizeof(VpuIsoColorAspects));

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

VpuEncRetCode VPU_EncConfig(VpuEncHandle InHandle, VpuEncConfig InEncConf, void* pInParam)
{
  VpuEncHandleInternal * pVpuObj;
  VpuEncObj* pObj;
  int para;

	if(InHandle == NULL)
	{
		VPU_ENC_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);
		return VPU_ENC_RET_INVALID_HANDLE;
	}
	pVpuObj = (VpuEncHandleInternal *)InHandle;
	pObj = &pVpuObj->obj;

  switch(InEncConf)
  {
	  // case VPU_DEC_CONF_SKIPNONE:
		//   break;
  	case VPU_ENC_CONF_NONE:
      break;
    case VPU_ENC_CONF_BIT_RATE:
      para = *((int*)pInParam);
      if(para < 0) {
        VPU_ENC_ERROR("%s: invalid bit rate parameter: %d \r\n",__FUNCTION__,para);
        return VPU_ENC_RET_INVALID_PARAM;
      }
      pObj->config.rcCfg.bitPerSecond = para * 1000;  //kbps->bps
      break;
    case VPU_ENC_CONF_INTRA_REFRESH:
      para = *((int*)pInParam);
      if(para < 0) {
        VPU_ENC_ERROR("%s: invalid intra refresh parameter: %d \r\n",__FUNCTION__,para);
        return VPU_ENC_RET_INVALID_PARAM;
      }
      VPU_ENC_LOG("%s: intra fresh number: %d \r\n",__FUNCTION__,para);
      break;
    case VPU_ENC_CONF_ENA_SPSPPS_IDR:
      /*	nInsertSPSPPSToIDR
        0: sequence header(SPS/PPS) + IDR +P +P +...+ (SPS/PPS)+IDR+....
        1: sequence header(SPS/PPS) + (SPS/PPS)+IDR +P +P +...+ (SPS/PPS)+IDR+....
      */
      VPU_ENC_LOG("%s: enable SPS/PPS for IDR frames \r\n",__FUNCTION__);
      //pObj->encConfig.prependSPSPPSToIDRFrames = OMX_TRUE;
      break;
    case VPU_ENC_CONF_RC_INTRA_QP: /*avc: 0..51, other 1..31*/
      para = *((int*)pInParam);
      if(para < 0) {
        VPU_ENC_ERROR("%s: invalid intra qp %d \r\n",__FUNCTION__,para);
        return VPU_ENC_RET_INVALID_PARAM;
      }
      VPU_ENC_LOG("%s: intra qp : %d \r\n",__FUNCTION__,para);
      break;
    case VPU_ENC_CONF_INTRA_REFRESH_MODE:
      para = *((int*)pInParam);
      if(para < 0){
        VPU_ENC_ERROR("%s: invalid intra refresh mode parameter: %d \r\n",__FUNCTION__,para);
        return VPU_ENC_RET_INVALID_PARAM;
      }
      VPU_ENC_LOG("%s: intra fresh mode: %d \r\n",__FUNCTION__,para);
      //pObj->encConfig.intraRefresh.eRefreshMode = para;
      break;
    default:
      VPU_ENC_ERROR("%s: failure: invalid setting \r\n",__FUNCTION__);
      return VPU_ENC_RET_INVALID_PARAM;
  }
  return VPU_ENC_RET_SUCCESS;
}
