/*!
 *	Copyright 2017 NXP
 *
 *	History :
 *	Date	(y.m.d)		Author			Version			Description
 *	2017-10-24		Song Bing		0.1				Created
 */

/** Vpu_wrapper_amphion.c
 *	vpu wrapper file contain all related amphion video decoder api exposed to
 *	application
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vpu_wrapper.h"
#include "utils.h"

#ifdef INTERNAL_BUILD
#include "basetype.h"
#include "pal.h"

#include "VPU_types.h"
#include "VPU_api.h"
#else
#include "malone_interface.h"
#endif

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
#define BS_BUFFER_LEN (65*1024)
#define VPU_AMPHION_LWM (5*1024)
#define BS_BUFFER_CNT (5)
#define EOS_PAD_SIZE 128

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

#define TESTVPU_EVENT_QU_SIZE 128
#define TESTVPU_EVENT_QU_MARKFREEENTRY 0xdeadbeef
#define TESTVPU_FIXED_DCP_SIZE_MULTIPLE 0x3000000

#define NXP_MALONE_BASE_ADDRESS 0x0
#define NXP_DPV_BASE_ADDRESS 0x0
#define NXP_PIXIF_BASE_ADDRESS 0x0
#define NXP_FSLCACHE_BASE_ADDRESS 0x0
#define NXP_GIC_BASE_ADDRESS 0x0
#define NXP_UART_BASE_ADDRESS 0x0
#define NXP_TIMER_BASE_ADDRESS 0x0

#define NXP_MALONE_HIF_OFFSET 0x1C000
#define NXP_IRQ_MALONE0_LOW      0x0
#define NXP_IRQ_MALONE0_HI       0x1
#define NXP_IRQ_DPV       0x2

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

typedef struct
{
  u_int32 uStreamID;
  u_int32 uEventID; /* VPU_EVENTTYPE and Local events ? */
  u_int32 uEventData[VPU_EVENT_MAX_DATA_LENGTH+1]; /* Variable length per event type +1 to store the event length*/

} TestVPU_Event_Data;

typedef struct
{
  /* open parameters */
  VpuCodStd CodecFormat;

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
  int nInputCnt;
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

  MEDIA_IP_FORMAT format;
  int nPrivateSeqHeaderInserted;
  int nIsAvcc;	/*for H.264/HEVC format*/
  int nNalSizeLen;
  int nNalNum; /*added for nal_size_length = 1 or 2*/
  bool eosing;
  bool ringbuffer;
  bool bPendingFeed;
  bool needDiscardEvent;
  int nInFrameCount;
  int nMinFrameBufferCount;
  u_int32 uStrIdx;
  u_int32 uPictureStartLocationPre;
  sPALMemDesc sPalMemReqInfo;
}VpuDecObj;

typedef struct 
{
  VpuDecObj obj;
}VpuDecHandleInternal;

static int nVpuLogLevel=0;		//bit 0: api log; bit 1: raw dump; bit 2: yuv dump
static int g_seek_dump=DUMP_ALL_DATA;	/*0: only dump data after seeking; otherwise: dump all data*/

static int nInitCnt = 0;
static int nBadLoad = 0;
static TestVPU_Event_Data gTestVPUEventData[TESTVPU_EVENT_QU_SIZE];
static u_int32 uTestVPUEventQuWrIdx   = 0;
static u_int32 uQuLocalQueuePtr = 0;
static PAL_QUEUE_ID gTestVPUEventQu[VPU_MAX_SUPPORTED_STREAMS];

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

char *event2str[] = {
  "VPU_EventCmdProcessed",         
  "VPU_EventError",               
  "VPU_EventStarted",                
  "VPU_EventStopped", 
  "VPU_EventStreamBufferFeedDone",          
  "VPU_EventStreamBufferLow",   
  "VPU_EventStreamBufferHigh",     
  "VPU_EventStreamBufferOverflow",
  "VPU_EventStreamBufferStarved",
  "VPU_EventSequenceInfo", 
  "VPU_EventPictureHdrFound",
  "VPU_EventPictureDecoded",
  "VPU_EventChunkDecoded",
  "VPU_EventFrameResourceRequest",      
  "VPU_EventFrameResourceReady",
  "VPU_EventFrameResourceRelease",
  "VPU_EventDebugNotify",
  "VPU_EventCodecFormatChange",
  "VPU_EventDebugPing",
  "VPU_EventStreamDecodeError",
  "VPU_EventEndofStreamScodeFound",
  "VPU_EventFlushDone",
  "VPU_EventFrameResourceCheck",
  "VPU_EventStreamBufferResetDone"
};

void TestVPU_DriverEvent_Callback(  u_int32 uStrIdx,
    VPU_EVENTTYPE    tEvent,
    VPU_EVENT_DATA  *ptEventData,
    u_int32 uEventDataLength)
{
  u_int32 i ;
  uint_addr uQuMsg[4];
  u_int32 *puDataPtr;

  VPU_LOG("call back stream index: %d event: %s index: %d \n", uStrIdx,
      event2str[tEvent], uTestVPUEventQuWrIdx);
  /* Store the actual event data into a local array */
  if(gTestVPUEventData[uTestVPUEventQuWrIdx].uStreamID!=TESTVPU_EVENT_QU_MARKFREEENTRY)
  {
    /* the entry we are going to write to is still in-use thus we need to re-size the queue
        this should be an assert to allow us to debug how to fix this */
    VPU_ERROR("error: event queue over flow \n");
    pal_assert_impl((u_int32)TestVPU_DriverEvent_Callback, 1);
  }
  gTestVPUEventData[uTestVPUEventQuWrIdx].uStreamID = uStrIdx;
  gTestVPUEventData[uTestVPUEventQuWrIdx].uEventID = tEvent;
  if(uEventDataLength>VPU_EVENT_MAX_DATA_LENGTH-1)
  {
    /* not enough space for the full message so add error but continue */
    uEventDataLength = VPU_EVENT_MAX_DATA_LENGTH-1;
  }
  gTestVPUEventData[uTestVPUEventQuWrIdx].uEventData[0] = uEventDataLength;
  puDataPtr = (u_int32 *)ptEventData;
  for(i=1;i<(uEventDataLength+1);i++)
  {
    gTestVPUEventData[uTestVPUEventQuWrIdx].uEventData[i] = *puDataPtr++;
  }

  /* pass stream id and position in local array to pal queue */
  uQuMsg[0] = uStrIdx ;
  uQuMsg[1] = uTestVPUEventQuWrIdx ;
  uQuMsg[2] = 0;
  uQuMsg[3] = 0;

  pal_qu_send ( gTestVPUEventQu[uStrIdx], uQuMsg );
  
  uTestVPUEventQuWrIdx +=1;
  if(uTestVPUEventQuWrIdx==TESTVPU_EVENT_QU_SIZE)
  {
    uTestVPUEventQuWrIdx = 0;
  }
}

VpuDecRetCode VPU_DecLoad()
{
  VpuLogLevelParse(NULL);

  sPALConfig tPALCfg = {0};
  VPU_DRIVER_CFG tTestVPUDriverCfg = {0};

  u_int32 iRetCode = 0 ;
  u_int32 iLocalRetCode = 0 ;
  u_int32 i;

  tPALCfg.uNumMalones                     = 1;
  tPALCfg.uMaloneBaseAddr[0x0]            = NXP_MALONE_BASE_ADDRESS;
  tPALCfg.uMaloneBaseAddr[0x1]            = NXP_MALONE_BASE_ADDRESS;
  tPALCfg.uHifOffset[0x0]                 = NXP_MALONE_HIF_OFFSET;
  tPALCfg.uHifOffset[0x1]                 = NXP_MALONE_HIF_OFFSET;
  tPALCfg.uIrqLines[PAL_IRQ_MALONE0_LOW]  = NXP_IRQ_MALONE0_LOW;
  tPALCfg.uIrqLines[PAL_IRQ_MALONE0_HI]   = NXP_IRQ_MALONE0_HI;
  tPALCfg.uIrqLines[PAL_IRQ_MALONE1_LOW]  = NXP_IRQ_MALONE0_LOW;
  tPALCfg.uIrqLines[PAL_IRQ_MALONE1_HI]   = NXP_IRQ_MALONE0_HI;
  tPALCfg.uIrqLines[PAL_IRQ_DPV]          = NXP_IRQ_DPV;

  tPALCfg.uDPVBaseAddr                    = NXP_DPV_BASE_ADDRESS;
  tPALCfg.uPixIfAddr                      = NXP_PIXIF_BASE_ADDRESS;
  tPALCfg.uFSLCacheBaseAddr               = NXP_PIXIF_BASE_ADDRESS;

  /* Timers, Usart and GIC will be totally different for the NXP system and thus the hwlib is different */
  tPALCfg.uNumTimers = 0 ;
  tPALCfg.uGICBaseAddr = NXP_GIC_BASE_ADDRESS;
  tPALCfg.uUartBaseAddr = NXP_UART_BASE_ADDRESS;
  tPALCfg.uTimerBaseAddr = NXP_TIMER_BASE_ADDRESS;

  /* trace level used by Linux PAL?*/
  tPALCfg.pal_trace_level = MEDIAIP_TRACE_LEVEL_ALWAYS;

  /* Needed ? */
  tPALCfg.uPalConfigMagicCookie           = PAL_CONFIG_MAGIC;


  /* Now setup the VPU driver config in a similar manner */
  tTestVPUDriverCfg.uMaloneBase = tPALCfg.uMaloneBaseAddr[0x0];
  tTestVPUDriverCfg.uMaloneIrqID[0] = tPALCfg.uIrqLines[PAL_IRQ_MALONE0_LOW];
  tTestVPUDriverCfg.uMaloneIrqID[1] = tPALCfg.uIrqLines[PAL_IRQ_MALONE0_HI];
  tTestVPUDriverCfg.uDPVBaseAddr = tPALCfg.uDPVBaseAddr;
  tTestVPUDriverCfg.uDPVIrqPin = tPALCfg.uIrqLines[PAL_IRQ_DPV];
  tTestVPUDriverCfg.uPixIfBaseAddr = tPALCfg.uPixIfAddr;
  tTestVPUDriverCfg.uFSLCacheBaseAddr = tPALCfg.uFSLCacheBaseAddr;

  /* The DMA region is populated within the VPU driver as it is specific to the Malone HW requirements*/
  tTestVPUDriverCfg.uMvdDMAMemSize     = 0;
  tTestVPUDriverCfg.uMvdDMAMemPhysAddr = 0;
  tTestVPUDriverCfg.uMvdDMAMemVirtAddr = 0;

  /* Fixed until we have a HW configuration that is otherwise */
  tTestVPUDriverCfg.bFrameBufferCompressionEnabled = FALSE ;

  tTestVPUDriverCfg.eFsCtrlMode = MEDIA_PLAYER_FS_CTRL_MODE_EXTERNAL;
  tTestVPUDriverCfg.eApiMode    = MEDIA_PLAYER_API_MODE_CONTINUOUS;

  VPU_LOG("VPU_DecLoad.\n");

  //if(nBadLoad == 1)
  {
    //if(nInitCnt == 0)
    {
      pal_initialise ( &tPALCfg );

      iRetCode = VPU_Init(&tTestVPUDriverCfg, TestVPU_DriverEvent_Callback, TestVPU_DriverEvent_Callback) ;

      for(i=0;i<TESTVPU_EVENT_QU_SIZE;i++)
          gTestVPUEventData[i].uStreamID = TESTVPU_EVENT_QU_MARKFREEENTRY ; /*mark all entries free to write to */

    }
    nInitCnt++;
  }
  nBadLoad ++;

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
  u_int32 uParamVal[2]; 
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

  pVpuObj=(VpuDecHandleInternal*)pMemVirt->pVirtAddr;
  pObj=&pVpuObj->obj;

  memset(pObj, 0, sizeof(VpuDecObj));

  pObj->uStrIdx = nInitCnt - 1;
  pObj->format = MEDIA_IP_FMT_NULL;

  VPU_LOG("format: %d \r\n",pInParam->CodecFormat);
  switch (pInParam->CodecFormat) {
    case VPU_V_AVC:
      pObj->format = MEDIA_IP_FMT_AVC;
      break;
    case VPU_V_MPEG2: 	 /**< AKA: H.262 */
      pObj->format = MEDIA_IP_FMT_MP2;
      break;
    case VPU_V_H263:		 /**< H.263 */
    case VPU_V_SORENSON: 	 /**< Sorenson */
      pObj->format = MEDIA_IP_FMT_SPK;
      break;
    case VPU_V_MPEG4: 	 /**< MPEG-4 */
    case VPU_V_DIVX4:		/**< DIVX 4 */
    case VPU_V_DIVX56:		/**< DIVX 5/6 */
    case VPU_V_XVID:		/**< XVID */
    case VPU_V_DIVX3:		/**< DIVX 3 */
      pObj->format = MEDIA_IP_FMT_ASP;
      break;
    case VPU_V_RV:		
      pObj->format = MEDIA_IP_FMT_RV;
      break;
    case VPU_V_VC1:		 /**< all versions of Windows Media Video */
    case VPU_V_VC1_AP:
      pObj->format = MEDIA_IP_FMT_VC1;
      break;
    case VPU_V_AVC_MVC:
      pObj->format = MEDIA_IP_FMT_MVC;
      break;
    case VPU_V_MJPG:
      pObj->format = MEDIA_IP_FMT_JPG;
      break;
    case VPU_V_AVS:
      pObj->format = MEDIA_IP_FMT_AVS;
      break;
    case VPU_V_VP6:
      pObj->format = MEDIA_IP_FMT_VP6;
      break;
    case VPU_V_VP8:
      pObj->format = MEDIA_IP_FMT_VP8;
      break;
    case VPU_V_HEVC:
      pObj->format = MEDIA_IP_FMT_HEVC;
      break;
    default:
      VPU_ERROR("%s: failure: invalid format !!! \r\n",__FUNCTION__);
      return VPU_DEC_RET_INVALID_PARAM;
  }

  MEDIAIP_FW_STATUS eRetStatus; 


  /* Queue will receive events from the VPU driver to process */
  eRetStatus = pal_qu_create ( TESTVPU_EVENT_QU_SIZE,
                               NULL,
                               &gTestVPUEventQu[pObj->uStrIdx] );

  if ( eRetStatus != MEDIAIP_FW_STATUS_OK )
  {
      VPU_ERROR("%s: failure: create queue fail !!! \r\n",__FUNCTION__);
      return VPU_DEC_RET_FAILURE;
  }

  VPU_Set_Event_Filter(pObj->uStrIdx, VPU_EventStreamBufferFeedDone);
  VPU_Set_Event_Filter(pObj->uStrIdx, VPU_EventError);
  VPU_Set_Event_Filter(pObj->uStrIdx, VPU_EventCmdProcessed);
  VPU_Set_Event_Filter(pObj->uStrIdx, VPU_EventStreamBufferHigh);

  VPU_Set_Params(pObj->uStrIdx, VPU_ParamId_Format, (u_int32 *)&pObj->format);
  uParamVal[0] = FALSE;
  VPU_Set_Params(pObj->uStrIdx, VPU_ParamId_PESEnable, &uParamVal[0]);

  uParamVal[0] = pMemPhy->pPhyAddr;
  uParamVal[1] = VPU_BITS_BUF_SIZE;
  VPU_Set_Params(pObj->uStrIdx, VPU_ParamId_uSharedStreamBufLoc, &uParamVal[0]);          
  uParamVal[0] = VPU_AMPHION_LWM;
  VPU_Set_Params(pObj->uStrIdx, VPU_ParamId_StreamLwm, &uParamVal[0]);

  VPU_Start(pObj->uStrIdx);

  pObj->ringbuffer = true;
  //record resolution for some special formats (such as VC1,...)
  pObj->picWidth = pInParam->nPicWidth;	
  pObj->picHeight = pInParam->nPicHeight;


  pObj->CodecFormat= pInParam->CodecFormat;
  pObj->pBsBufVirtStart= pMemPhy->pVirtAddr;
  pObj->pBsBufPhyStart= pMemPhy->pPhyAddr;
  pObj->uPictureStartLocationPre = pObj->pBsBufPhyStart;
  pObj->pBsBufPhyEnd=pMemPhy->pPhyAddr+VPU_BITS_BUF_SIZE;
  pObj->bPendingFeed = true;
  pObj->needDiscardEvent = false;
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

static void VpuUpdatePos(VpuDecObj* pObj, unsigned int len)
{
  u_int32 uStreamConsumed;

  if(pObj->nBsBufOffset+pObj->nBsBufLen > VPU_BITS_BUF_SIZE)
  {
    VPU_Stream_FeedBuffer(pObj->uStrIdx, pObj->pBsBufPhyStart + pObj->nBsBufOffset
        + pObj->nBsBufLen - VPU_BITS_BUF_SIZE, len, &uStreamConsumed);
  }
  else
  {
    VPU_Stream_FeedBuffer(pObj->uStrIdx, pObj->pBsBufPhyStart + pObj->nBsBufOffset
        + pObj->nBsBufLen, len, &uStreamConsumed);
  }
  //if(len)
  VPU_LOG("%s: feed len: %d, feeded len: %d\n",__FUNCTION__, len, uStreamConsumed);
  pObj->nBsBufLen += len;
  pObj->nBsBufOffset += uStreamConsumed;
  if(pObj->nBsBufOffset >= VPU_BITS_BUF_SIZE)
    pObj->nBsBufOffset -= VPU_BITS_BUF_SIZE;
  pObj->nBsBufLen -= uStreamConsumed;
}

static void VpuPutInBuf(VpuDecObj* pObj, unsigned char *pIn, unsigned int len)
{
  if(pObj->nBsBufOffset+pObj->nBsBufLen+len > VPU_BITS_BUF_SIZE)
  {
    if(pObj->ringbuffer)
    {
      if(pObj->nBsBufOffset+pObj->nBsBufLen > VPU_BITS_BUF_SIZE)
      {
        memcpy(pObj->pBsBufVirtStart+pObj->nBsBufOffset+pObj->nBsBufLen-VPU_BITS_BUF_SIZE, pIn, len);
      }
      else
      {
        memcpy(pObj->pBsBufVirtStart+pObj->nBsBufOffset+pObj->nBsBufLen, pIn,
            VPU_BITS_BUF_SIZE-pObj->nBsBufOffset-pObj->nBsBufLen);
        memcpy(pObj->pBsBufVirtStart, pIn+VPU_BITS_BUF_SIZE-pObj->nBsBufOffset-pObj->nBsBufLen,
            len+pObj->nBsBufOffset+pObj->nBsBufLen-VPU_BITS_BUF_SIZE);
      }
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

  VpuUpdatePos(pObj, len);

  if(VPU_DUMP_RAW)
    WrapperFileDumpBitstrem(pIn,len);
}

static void VpuFeedEOS(VpuDecObj* pObj)
{
  unsigned char* pHeader=NULL;
  u_int32 uEOSData[2] = {0x000001b0, 0x00000000};
  u_int32 uRequiredEOSFeed;

  /* codec specific EOS and then padding bytes */
  uRequiredEOSFeed = 8 + EOS_PAD_SIZE; 
  switch (pObj->format)
  {
    case MEDIA_IP_FMT_AVC:
      uEOSData[0] = 0x0B010000;
      break;
    case MEDIA_IP_FMT_VC1:
      uEOSData[0] = 0x0a010000;
      break;
    case MEDIA_IP_FMT_MP2:
      uEOSData[0] = 0xb7010000;
      break;
    case MEDIA_IP_FMT_ASP:
      uEOSData[0] = 0xb1010000;
      break;
    case MEDIA_IP_FMT_RV:
    case MEDIA_IP_FMT_VP6:
    case MEDIA_IP_FMT_VP8:
    case MEDIA_IP_FMT_SPK:
      uEOSData[0] = 0x34010000;
      break;
    case MEDIA_IP_FMT_HEVC:
      uEOSData[0]  = 0x4A010000;
      uEOSData[1] = 0x20;
      break;
    default:        
      break;
  }

  pHeader = malloc(uRequiredEOSFeed);
  memset(pHeader, 0, uRequiredEOSFeed);
  memcpy(pHeader, uEOSData, 8);

  VpuPutInBuf(pObj, pHeader, uRequiredEOSFeed);

  free(pHeader);
}

static VpuDecRetCode VPU_DecProcessInBuf(VpuDecObj* pObj, VpuBufferNode* pInData)
{
  unsigned char* pHeader=NULL;
  unsigned int headerLen=0;
  unsigned int headerAllocated=0;
  int pNoErr = 1;

  if(pInData->pVirAddr == (unsigned char *)0x01 && pInData->nSize == 0)
  {
    VPU_LOG("received eos.\n");
    pObj->eosing = true;
    VpuFeedEOS(pObj);
  }

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
      VpuPutInBuf(pObj, pHeader, headerLen);
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
      VPU_LOG("copy codec data: %d\n", headerLen);
      VpuPutInBuf(pObj, pHeader, headerLen);
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
    VpuPutInBuf(pObj, pFrm, nFrmSize);
    if(pFrm!=pInData->pVirAddr){
      free(pFrm);
    }
  } else {
    if(pObj->CodecFormat==VPU_V_VC1_AP) {
      unsigned char aVC1Head[VC1_MAX_FRM_HEADER_SIZE];
      pHeader=aVC1Head;
      VC1CreateNalFrameHeader(pHeader,(int*)(&headerLen),(unsigned int*)(pInData->pVirAddr));
      VpuPutInBuf(pObj, pHeader, headerLen);
    } else if(pObj->CodecFormat==VPU_V_MJPG) {
      pObj->nBsBufOffset = 0;
    }

    VpuPutInBuf(pObj, pInData->pVirAddr, pInData->nSize);
  }
  pInData->nSize = 0;

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

static VpuDecRetCode TestVPU_Event_Thread(VpuDecObj* pObj, VpuBufferNode* pInData, int* pOutBufRetCode)
{ 
  uint_addr uQuMsg[4]; 
  u_int32 uQuStrId;
  TestVPU_Event_Data eEventQData;
  u_int32 uEventDataLen;
  u_int32 i;

  while ( 1 )
  {
    VPU_LOG("read queue\n");
    pal_qu_receive ( gTestVPUEventQu[pObj->uStrIdx],
             PAL_WAIT_FOREVER,
             uQuMsg);

    /* From the pal_q stored message get the actual event data to process */
    uQuStrId = (u_int32)uQuMsg[0];
    uQuLocalQueuePtr = (u_int32)uQuMsg[1];
    if(uQuLocalQueuePtr>TESTVPU_EVENT_QU_SIZE)
    {
      /* error out of bound queue pointer so a mess up with event posting */
      pal_assert_impl((u_int32)TestVPU_Event_Thread, 2);
    }
    if(gTestVPUEventData[uQuLocalQueuePtr].uStreamID != uQuStrId)
    {
      /* error since the event queue and the local queue if working correctly should have the same str id */
      pal_assert_impl((u_int32)TestVPU_Event_Thread, 3);
    }

    eEventQData.uStreamID= uQuStrId;
    uEventDataLen = gTestVPUEventData[uQuLocalQueuePtr].uEventData[0];
    eEventQData.uEventID =gTestVPUEventData[uQuLocalQueuePtr].uEventID;

    for(i=1;i<(uEventDataLen+1);i++)
    {
      eEventQData.uEventData[i-1]= gTestVPUEventData[uQuLocalQueuePtr].uEventData[i]; // Pos 0 is length of the event
    }

    VPU_LOG("process stream index: %d event: %s index: %d \n", uQuMsg[0],
        event2str[eEventQData.uEventID], uQuMsg[1]);

    /*Mark that we have concluded using this entry of information */
    gTestVPUEventData[uQuLocalQueuePtr].uStreamID = TESTVPU_EVENT_QU_MARKFREEENTRY;


    TestVPU_Event_Data *peEventData = &eEventQData;
    if(pObj->needDiscardEvent)
    {
      switch(peEventData->uEventID)
      {
        case VPU_EventStreamBufferResetDone:
          VPU_LOG("Stream buffer reset done\n");
          pObj->needDiscardEvent = false;
          return VPU_DEC_RET_SUCCESS;
        case VPU_EventFrameResourceRelease:
          {
            u_int32 uFsId = 0;

            uFsId = (u_int32)peEventData->uEventData[0];
            VPU_LOG("Process frame resource release event, id: %d\n", uFsId);
            if (uFsId == MEDIA_PLAYER_SKIPPED_FRAME_ID)
              break;

            if(pObj->needDiscardEvent)
            {
              if(pObj->frameBufState[uFsId] == VPU_FRAME_STATE_FREE)
                pObj->frameBufState[uFsId] = VPU_FRAME_STATE_DEC;
              break;
            }
          }
        default:
          break;
      }
    }

    switch(peEventData->uEventID)
    {
      case VPU_EventStarted:
        VPU_LOG("Process started event\n");
        break;
      case VPU_EventStreamDecodeError:
        VPU_LOG("stream error.\n");
        return VPU_DEC_RET_SUCCESS;
      case VPU_EventSequenceInfo:
        VPU_LOG("Process sequence info event\n");
        if(pObj->state<VPU_DEC_STATE_INITOK)
          *pOutBufRetCode |= VPU_DEC_INIT_OK;
        //else
          //*pOutBufRetCode |= VPU_DEC_RESOLUTION_CHANGED;
        pObj->state=VPU_DEC_STATE_INITOK;
        return VPU_DEC_RET_SUCCESS;
      case VPU_EventStreamBufferStarved:
      case VPU_EventStreamBufferLow:
        {
          VPU_LOG("Process stream buffer low event\n");
          if(pInData->pVirAddr == NULL || pInData->nSize == 0)
          {
            pObj->bPendingFeed = true;
            return VPU_DEC_RET_SUCCESS;
          }

          VPU_DecProcessInBuf(pObj, pInData);
          *pOutBufRetCode |= VPU_DEC_INPUT_USED;
          break;
        }
      case VPU_EventFrameResourceRequest:
        {
          MEDIAIP_FW_STATUS eRetCode = MEDIAIP_FW_STATUS_RESOURCE_ERROR;
          u_int32 uFSIdx;
          u_int32 uReqNum;
          pVPU_FS_TYPE pVpuFsType = ( pVPU_FS_TYPE )peEventData->uEventData; 

          uReqNum = pVpuFsType->uFSNum;

          VPU_LOG("Process frame resource request event. buffer type: %d\n", pVpuFsType->eType);
          if ( pVpuFsType->eType == VPU_FS_DCP_REQ )
          {
            /* Currently setting this to 48MB each! */
            u_int32 uBufferSize = TESTVPU_FIXED_DCP_SIZE_MULTIPLE;

            pObj->sPalMemReqInfo.size = uBufferSize;
            // FIXME: need release it.
            if(MEDIAIP_FW_STATUS_OK!=pal_get_phy_buf(&pObj->sPalMemReqInfo))
            {
              VPU_ERROR("%s: failure: allocate memory fail \r\n",__FUNCTION__);
              return VPU_DEC_RET_FAILURE;
            }

            VPU_FS_DESC sVPUFsParams;

            sVPUFsParams.ulFsId            = uFSIdx;
            sVPUFsParams.ulFsLumaBase[0]   = pObj->sPalMemReqInfo.phy_addr;
            sVPUFsParams.ulFsLumaBase[1]   = uBufferSize;
            sVPUFsParams.ulFsType          = pVpuFsType->eType;                                                                 

            VPU_Frame_Alloc ( pObj->uStrIdx, &sVPUFsParams );
            break;
          }
          else if ( pVpuFsType->eType == VPU_FS_MBI_REQ )
            break;

          if(!pObj->needDiscardEvent)
          {
            int i;

            for (i=0;i<pObj->frameNum;i++){
              if (pObj->frameBufState[i] == VPU_FRAME_STATE_DEC){
                VPU_FS_DESC sVPUFsParams;
                VpuFrameBuffer* pInFrameBuf = &pObj->frameBuf[i];
                sVPUFsParams.ulFsId            = i;
                sVPUFsParams.ulFsLumaBase[0]   = pInFrameBuf->pbufY;
                sVPUFsParams.ulFsLumaBase[1]   = pInFrameBuf->pbufY + pInFrameBuf->nStrideY * pObj->picHeight / 2;
                sVPUFsParams.ulFsChromaBase[0] = pInFrameBuf->pbufCb;
                sVPUFsParams.ulFsChromaBase[1] = pInFrameBuf->pbufCb + pInFrameBuf->nStrideY * pObj->picHeight / 4;
                sVPUFsParams.ulFsStride        = pInFrameBuf->nStrideY;
                sVPUFsParams.ulFsType          = VPU_FS_FRAME_REQ;

                VPU_LOG("Alloc from reserve buffer: %d\n", i);
                VPU_DQ_Release_Frame(pObj->uStrIdx, i);
                VPU_Frame_Alloc ( pObj->uStrIdx, &sVPUFsParams );

                pObj->frameBufState[i] = VPU_FRAME_STATE_FREE;
                break;
              }
            }
            if(i<pObj->frameNum)
              break;
          }

          pObj->state=VPU_DEC_STATE_DEC;
          *pOutBufRetCode |= VPU_DEC_NO_ENOUGH_BUF;
          VPU_LOG("output status: %d\n", *pOutBufRetCode);
          return VPU_DEC_RET_SUCCESS;
        }
      case VPU_EventPictureDecoded:
        {
          u_int32 uFsId= peEventData->uEventData[10];
          u_int32 uActiveDpbmcCrc;
          u_int32 uStreamBytesConsumed;
          u_int32 uPictureStartLocation;
          MediaIPFW_Video_PicPerfInfo      *psPerfInfo;
          MediaIPFW_Video_PicPerfDcpInfo   *psPerfDcpInfo;
          MediaIPFW_Video_PicInfo          *psDecPicInfo;

          if (uFsId == MEDIA_PLAYER_SKIPPED_FRAME_ID || pObj->needDiscardEvent)
            break;

          if(pObj->format==MEDIA_IP_FMT_HEVC)
          {
            if(VPU_Get_Decode_Status( pObj->uStrIdx, VPU_STATUS_TYPE_PIC_PERFDCP_INFO, uFsId, (void ** )&psPerfDcpInfo ))
            {
              VPU_ERROR("%s: failure: can not get consumed info \r\n",__FUNCTION__);
              return VPU_DEC_RET_INVALID_PARAM;
            }
            uActiveDpbmcCrc = psPerfDcpInfo->uDBEDpbCRC[0];

            uStreamBytesConsumed = psPerfDcpInfo->uDFENumBytes;
          }
          else
          {
            if(VPU_Get_Decode_Status( pObj->uStrIdx, VPU_STATUS_TYPE_PIC_PERF_INFO, uFsId, (void ** )&psPerfInfo ))
            {
              VPU_ERROR("%s: failure: can not get consumed info \r\n",__FUNCTION__);
              return VPU_DEC_RET_INVALID_PARAM;
            }
            uActiveDpbmcCrc = psPerfInfo->uDpbmcCrc;
            uStreamBytesConsumed = psPerfInfo->uRbspBytesCount;
          }   

          if(VPU_Get_Decode_Status( pObj->uStrIdx, VPU_STATUS_TYPE_PIC_DECODE_INFO, uFsId, (void ** )&psDecPicInfo ))
          {
            VPU_ERROR("%s: failure: can not get consumed info \r\n",__FUNCTION__);
            return VPU_DEC_RET_INVALID_PARAM;
          }

          uPictureStartLocation = psDecPicInfo->uPicStAddr;
          uStreamBytesConsumed = uPictureStartLocation>pObj->uPictureStartLocationPre
            ?uPictureStartLocation-pObj->uPictureStartLocationPre:VPU_BITS_BUF_SIZE
            +uPictureStartLocation-pObj->uPictureStartLocationPre;
          pObj->uPictureStartLocationPre = uPictureStartLocation;

          pObj->nAccumulatedConsumedFrmBytes += uStreamBytesConsumed;
          VPU_LOG("vpu report consumed len: %d\n", uStreamBytesConsumed);

          pObj->bPendingFeed = true;
          pObj->nInputCnt --;
        }
        *pOutBufRetCode |= VPU_DEC_ONE_FRM_CONSUMED;
        return VPU_DEC_RET_SUCCESS;
      case VPU_EventFrameResourceReady:
        {
          u_int32 uFsId = 0;
          MediaIPFW_Video_PicInfo *psPicInfo;

          uFsId = (u_int32)peEventData->uEventData[0];
          VPU_LOG("Process frame resource ready event, id: %d\n", uFsId);
          if (uFsId == MEDIA_PLAYER_SKIPPED_FRAME_ID)
            break;

          if(pObj->needDiscardEvent)
          {
            if(pObj->frameBufState[uFsId] == VPU_FRAME_STATE_FREE)
              pObj->frameBufState[uFsId] = VPU_FRAME_STATE_DEC;
            break;
          }
          if(VPU_Get_Decode_Status( pObj->uStrIdx, VPU_STATUS_TYPE_PIC_DECODE_INFO, uFsId, (void ** )&psPicInfo))
          {
            VPU_ERROR("%s: failure: can not find picture info \r\n",__FUNCTION__);
            return VPU_DEC_RET_INVALID_PARAM;
          }
          pObj->frameBufState[uFsId] = VPU_FRAME_STATE_DISP;
          pObj->frameInfo.pDisplayFrameBuf = &pObj->frameBuf[uFsId];
          pObj->frameInfo.pExtInfo=&pObj->frmExtInfo;
          pObj->frameInfo.pExtInfo->FrmCropRect.nRight=pObj->picWidth;//<=1920?pObj->picWidth:1920;//psPicInfo->DispInfo.uDispVerRes;
          pObj->frameInfo.pExtInfo->FrmCropRect.nBottom=pObj->picHeight;//<=1080?pObj->picHeight:1080;//psPicInfo->DispInfo.uDispHorRes;
          VPU_LOG("fs id: %d crop: %d %d\n", uFsId, pObj->frameInfo.pExtInfo->FrmCropRect.nRight, pObj->frameInfo.pExtInfo->FrmCropRect.nBottom);

          *pOutBufRetCode |= VPU_DEC_OUTPUT_DIS;
          pObj->state=VPU_DEC_STATE_OUTOK;
          pObj->nInFrameCount --;
          VPU_LOG("output buffer, buffer in vpu: %d\n", pObj->nInFrameCount);

          if(VPU_DUMP_YUV)
            WrapperFileDumpYUV(pObj,pObj->frameInfo.pDisplayFrameBuf);

          VPU_LOG("output status: %d\n", *pOutBufRetCode);
          return VPU_DEC_RET_SUCCESS;
        }
      case VPU_EventEndofStreamScodeFound:
        VPU_LOG("Got EOS from video decoder.\n");
        *pOutBufRetCode |= VPU_DEC_OUTPUT_EOS;
        VPU_LOG("output status: %d\n", *pOutBufRetCode);
        return VPU_DEC_RET_SUCCESS;
      default:
        break;
    }
  }
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

  VPU_LOG("input length: %d\n", pInData->nSize);
  pVpuObj=(VpuDecHandleInternal *)InHandle;
  pObj=&pVpuObj->obj;

  *pOutBufRetCode = 0;

  if(!pObj->needDiscardEvent)
  {
    VpuUpdatePos(pObj, 0);
#if 0
    if(pObj->bPendingFeed)
    {
      if(pInData->pVirAddr != NULL || pInData->nSize != 0)
        pObj->bPendingFeed = false;
      VPU_DecProcessInBuf(pObj, pInData);
      *pOutBufRetCode |= VPU_DEC_INPUT_USED;

      if(pObj->nBsBufLen < BS_BUFFER_LEN && pObj->eosing == false)
      {
        pObj->bPendingFeed = true;
        *pOutBufRetCode |= VPU_DEC_NO_ENOUGH_INBUF;
        return VPU_DEC_RET_SUCCESS;
      }
    }
#else
    if((pObj->nBsBufLen < BS_BUFFER_LEN || pObj->nInputCnt < BS_BUFFER_CNT) && pObj->eosing == false)
    {
      VPU_DecProcessInBuf(pObj, pInData);
      *pOutBufRetCode |= VPU_DEC_INPUT_USED;
      if(pObj->eosing == false)
        *pOutBufRetCode |= VPU_DEC_NO_ENOUGH_INBUF;
      pObj->nInputCnt ++;
      VPU_LOG("output status: %d\n", *pOutBufRetCode);
      return VPU_DEC_RET_SUCCESS;
    }
#endif
#if 0
    if(pObj->state>=VPU_DEC_STATE_DEC)
    {
      if(pObj->nInFrameCount < pObj->nMinFrameBufferCount)
      {
        *pOutBufRetCode |= VPU_DEC_NO_ENOUGH_BUF;
        VPU_LOG("output status: %d\n", *pOutBufRetCode);
        return VPU_DEC_RET_SUCCESS;
      }
    }
#endif
  }

  return TestVPU_Event_Thread(pObj, pInData, pOutBufRetCode);
}

VpuDecRetCode VPU_DecGetInitialInfo(VpuDecHandle InHandle, VpuDecInitInfo * pOutInitInfo)
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

  if(pVpuObj->obj.state!=VPU_DEC_STATE_INITOK)
  {
    VPU_ERROR("%s: failure: error state %d \r\n",__FUNCTION__,pVpuObj->obj.state);
    return VPU_DEC_RET_WRONG_CALL_SEQUENCE;
  }

  MediaIPFW_Video_SeqInfo *psSeqInfo;
  if(VPU_Get_Decode_Status( pObj->uStrIdx, VPU_STATUS_TYPE_SEQ_INFO, 0, (void ** )&psSeqInfo))
  {
    VPU_ERROR("%s: failure: getinfo fail\n",__FUNCTION__);
    return VPU_DEC_RET_FAILURE;
  }

  pOutInitInfo->nPicWidth = psSeqInfo->uHorRes;
  pObj->picWidth = psSeqInfo->uHorRes;
  pOutInitInfo->nPicHeight = (psSeqInfo->uVerRes + 255) / 256 * 256;
  pObj->picHeight = psSeqInfo->uVerRes;
  pOutInitInfo->nMinFrameBufferCount = psSeqInfo->uNumDPBFrms;
  pOutInitInfo->nBitDepth = psSeqInfo->uBitDepthLuma;
  pObj->nMinFrameBufferCount = psSeqInfo->uNumDPBFrms;
  VPU_LOG("%s: min frame count: %d \r\n",__FUNCTION__, pOutInitInfo->nMinFrameBufferCount);
  //update state
  pVpuObj->obj.state=VPU_DEC_STATE_REGFRMOK;

  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecRegisterFrameBuffer(VpuDecHandle InHandle,VpuFrameBuffer *pInFrameBufArray, int nNum)
{
  VPU_ERROR("%s: Amphion needn't register frame buffer. \r\n",__FUNCTION__);
  return VPU_DEC_RET_WRONG_CALL_SEQUENCE;
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
  VPU_LOG("output Y address: %x\n", pVpuObj->obj.frameInfo.pDisplayFrameBuf->pbufY);

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
  VPU_LOG("%s: Consumed bytes: %d + %d\n",__FUNCTION__, pOutFrameLengthInfo->nStuffLength, pOutFrameLengthInfo->nFrameLength);

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
  VPU_FS_DESC sVPUFsParams;
  VpuDecObj* pObj;
  int index;
  bool bNewFrame = false;

  if(InHandle==NULL)
  {
    VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);
    return VPU_DEC_RET_INVALID_HANDLE;
  }
  pVpuObj=(VpuDecHandleInternal *)InHandle;
  pObj=&pVpuObj->obj;

  index=VpuSearchFrameIndex(pObj, pInFrameBuf->pbufY);
  if(index < 0)
  {
    pVpuObj->obj.frameBuf[pObj->frameNum]=*pInFrameBuf;
    index = pObj->frameNum;
    pObj->frameNum ++;
    bNewFrame = true;
  }

  sVPUFsParams.ulFsId            = index;
  sVPUFsParams.ulFsLumaBase[0]   = pInFrameBuf->pbufY;
  sVPUFsParams.ulFsLumaBase[1]   = pInFrameBuf->pbufY + pInFrameBuf->nStrideY * pObj->picHeight / 2;
  sVPUFsParams.ulFsChromaBase[0] = pInFrameBuf->pbufCb;
  sVPUFsParams.ulFsChromaBase[1] = pInFrameBuf->pbufCb + pInFrameBuf->nStrideY * pObj->picHeight / 4;
  sVPUFsParams.ulFsStride        = pInFrameBuf->nStrideY;
  sVPUFsParams.ulFsType          = VPU_FS_FRAME_REQ;                                                                 

  if(!bNewFrame)
  {
    VPU_DQ_Release_Frame(pObj->uStrIdx, index);
  }
  VPU_Frame_Alloc ( pObj->uStrIdx, &sVPUFsParams );

  if(bNewFrame && (index<0x11))  //FIXME: 0x11 is one constant in malone decoder for MBI buffers
  {
    sVPUFsParams.ulFsId            = index;
    sVPUFsParams.ulFsLumaBase[0]   = pInFrameBuf->pbufMvCol;
    sVPUFsParams.ulFsType          = VPU_FS_MBI_REQ;                                                                 

    VPU_Frame_Alloc ( pObj->uStrIdx, &sVPUFsParams );
  }

  pObj->frameBufState[index] = VPU_FRAME_STATE_FREE;
  pObj->nInFrameCount ++;
  VPU_LOG("Feed frame buffer, id: %d buffer in vpu: %d\n", index, pObj->nInFrameCount);

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

  VPU_LOG("%s: flush \r\n",__FUNCTION__);
  pObj->needDiscardEvent = true;
  VPU_Flush(pObj->uStrIdx, TRUE);     
  VPU_LOG("%s: flush done \r\n",__FUNCTION__);

  pObj->nBsBufLen=0;
  pObj->nBsBufOffset=0;
  pObj->nPrivateSeqHeaderInserted=0;

  pObj->nInputCnt=0;
  pObj->uPictureStartLocationPre = pObj->pBsBufPhyStart;
  pObj->nAccumulatedConsumedStufferBytes=0;
  pObj->nAccumulatedConsumedFrmBytes=0;
  pObj->nAccumulatedConsumedBytes=0;
  pObj->pLastDecodedFrm=NULL;
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

  VPU_LOG("%s\n",__FUNCTION__);
  if(InHandle==NULL) 
  {
    VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);
    return VPU_DEC_RET_INVALID_HANDLE;
  }
  pVpuObj=(VpuDecHandleInternal *)InHandle;
  pObj=&pVpuObj->obj;

  VPU_Stop(pObj->uStrIdx);
  pal_qu_destroy(gTestVPUEventQu[pObj->uStrIdx]);
  pal_free_phy_buf(&pObj->sPalMemReqInfo);

  return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecUnLoad()
{
  VPU_LOG("VPU_DecUnLoad.\n");
  //if(nBadLoad == 2)
  {
    nInitCnt--;
    //if(nInitCnt == 0)
    {
      VPU_LOG("VPU_Term.\n");
      VPU_Term();
      VPU_LOG("VPU_Term done.\n");
    }
  }

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

  VPU_Flush(pObj->uStrIdx, TRUE);     

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
  sPALMemDesc sPalMemReqInfo;

  sPalMemReqInfo.size = pInOutMem->nSize;

  if(MEDIAIP_FW_STATUS_OK!=pal_get_phy_buf(&sPalMemReqInfo))
  {
    VPU_ERROR("%s: failure: allocate memory fail \r\n",__FUNCTION__);
    return VPU_DEC_RET_FAILURE;
  }

  pInOutMem->nPhyAddr = sPalMemReqInfo.phy_addr;
  pInOutMem->nVirtAddr = sPalMemReqInfo.virt_addr;
  pInOutMem->nCpuAddr = sPalMemReqInfo.ion_buf_fd;

  return VPU_DEC_RET_SUCCESS;

}

VpuDecRetCode VPU_DecFreeMem(VpuMemDesc* pInMem)
{
  sPALMemDesc sPalMemReqInfo;

  sPalMemReqInfo.size = pInMem->nSize;
  sPalMemReqInfo.virt_addr = pInMem->nVirtAddr;
  sPalMemReqInfo.phy_addr = pInMem->nPhyAddr;
  sPalMemReqInfo.ion_buf_fd = pInMem->nCpuAddr;

  if(MEDIAIP_FW_STATUS_OK!=pal_free_phy_buf(&sPalMemReqInfo))
  {
    VPU_ERROR("%s: failure: free memory fail \r\n",__FUNCTION__);
    return VPU_DEC_RET_FAILURE;
  }

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
