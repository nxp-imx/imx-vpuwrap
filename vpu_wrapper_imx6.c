/*!
 *	CopyRight Notice:
 *	The following programs are the sole property of Freescale Semiconductor Inc.,
 *	and contain its proprietary and confidential information.
 *	Copyright (c) 2010-2011, Freescale Semiconductor Inc.,
 *	All Rights Reserved
 *
 *	History :
 *	Date	(y.m.d)		Author			Version			Description
 *	2010-09-07		eagle zhou		0.1				Created
 */

/** Vpu_wrapper.c
 *	vpu wrapper file contain all related vpu api exposed to application
 */

#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#include "vpu_lib.h"
#include "vpu_io.h"
#include "vpu_wrapper.h"

//#define ANDROID_DEGUG

#ifdef USE_VPU_WRAPPER_TIMER
#include "vpu_wrapper_timer.h"
#define TIMER_INIT				timer_init()
#define TIMER_MARK(id)			timer_mark(id)
#define TIMER_START(id)			timer_start(id)
#define TIMER_STOP(id)			timer_stop(id)
#define TIMER_MARK_REPORT(id)	timer_mark_report(id)
#define TIMER_REPORT(id)			timer_report(id)
#else
#define TIMER_INIT				
#define TIMER_MARK(id)			
#define TIMER_START(id)			
#define TIMER_STOP(id)			
#define TIMER_MARK_REPORT(id)	
#define TIMER_REPORT(id)			
#endif

#define TIMER_MARK_GETOUTPUT_ID		(0)
#define TIMER_CLEARDISP_ID				(0)


//#define VPU_DEC_PIPELINE
#define VPU_DEC_CHECK_INIT_LENGTH		//VPU limitation of minimum size 512 before seq init ?
#define VPU_ONE_EOS
#define VPU_SUPPORT_UNCLOSED_GOP		// for unclosed gop case:(1) drop B frames whose reference are missing (2) drop non-I frame after flushing
#define VPU_PROTECT_MULTI_INSTANCE	// for stream mode, we need to add protection for (startoneframe()--check busy/wait interrupt--getoutput()) to avoid hangup
//#define VPU_DEBUG_BS
//#define VPU_DEC_FILE_MODE				// default, using file mode for all codecs
#define VPU_IFRAME_SEARCH				// for file mode, we need to enable iframesearch to clear buffer: to implement seek(flush) feature: (3) skipping decoding until key frame
										//In fact, (2)=(1)+(3)
#define VPU_FILEMODE_QUICK_EXIT		// for vc1 complexity, vpu_DecSetEscSeqInit() will cost some time, so we return error directly for file mode, but not check the loop count
										//for example: wmv9_CP_240x180_26fps_263kbps_vc1.cmplx.wmv
#define VPU_AVOID_DEAD_LOOP			// avoid dead loop, cover: seqinit step; decode step
										//for example: mp4v_mp3_mp3audio_4567_50_100_10_20.mp4
#define VPU_FILEMODE_WORKAROUND		//add some work around for file mode: H.264/H.263/Mpeg2/Mpeg4/DivX456/XVID
#define VPU_SUPPORT_NO_ENOUGH_FRAME	//avoid hang when no enough frame buffer
#define VPU_VC1_AP_SKIP_WORKAROUND	//work around for skip frame(VC1), vpu may output one frame which has not been released, now, we simply drop this frame; clip: Test_1440x576_WVC1_6Mbps.wmv
#define VPU_INIT_FREE_SIZE_LIMITATION	//after the first init step, rd may exceed wr (can no occur at decode state), for example, WR+8, but RD+512, As result, free size is not enough when try the second init. But in fact, we can ignore the info
										//for example: Divx5_640x480_23.976_1013_a_mp3_48_158_2_1st-key-frame-is-the-2nd-frame_11.Search-for-the-Full-Moon-02.avi
#define VPU_FILEMODE_INTERLACE_WORKAROUND		//for interlaced clips: If user feed two fields seperately, and vpu action is: return valid for the first field, and return invalid for the second field. So we need to drop the second field and notify user get one timestamp
#define VPU_FILEMODE_CORRUPT_WORKAROUND	//for some corrupt clips(h264_P_B1.3_25.0fps_730k_320x240_aac_48KHz_128Kbps_c2_3min3s_Tomsk_iPod.mp4)
											//vpu return dexindex=-2, dispindex=-3 even data length !=0
#define VPU_SUPPORT_NO_INBUF		//no enough input buffer: to avoid null run
#define VPU_FAKE_FLUSH				//for debug flush mode
#define VPU_FILEMODE_PBCHUNK_FLUSH_WORKAROUND	//if flush is called between PB chunk(need to feed to vpu twice), the pb state is not cleared by vpu.
													//as result, the following key frame will be regarded as B frame and the output is wrong until next key frame.
													//video may be freeze after seek when VPU_SUPPORT_UNCLOSED_GOP is enabled:Xvid_SP1_640x480_23.98_655_aaclc_44_2_test.mkv(the first interval of key frame is about 6s)
#define VPU_FLUSH_BEFORE_DEC_WORKAROUND		//for vpu, below case need to be avoided, eg. should not update 0 after register frame immediately
													//register frame -> update 0 -> get EOS -> update non-0 -> always return EOS even data is valid
//#define VPU_SEEK_ANYPOINT_WORKAROUND	//unrecoverable mosaic may be introduced by random seek point(mainly for H.264??), so we need to call some related flush operation.
//#define VPU_FILEMODE_SUPPORT_INTERLACED_SKIPMODE	//for interlaced clips: we should make sure the skipmode for two field are the same, to avoid mosaic and hangup issues
											//it is mainly for skip B  strategy (performance issue): so we only consider skipframeMode
//#define VPU_FILEMODE_MERGE_INTERLACE_DEBUG		//in file mode, it is unstable that feeding two fields seperately. So we make some effort to merge two fields into one frame
#define VPU_FILEMODE_INTERLACE_TIMESTAMP_ENHANCE	//for field decoding: move "pop of timestampe" from decode order to display order
													//for some interlaced clips with deep dpb, original design may introduce much bigger timestamp(about 0.5 seconds): technicolor/332_dec.ts

#define VPU_ENC_OUTFRAME_ALIGN	//vpu limitation: 4(or 8?) bytes alignment for output frame address
#define VPU_ENC_GUESS_OUTLENGTH	//no size in SetOutputBuffer(), so we guess one value
#define VPU_ENC_ALIGN_LIMITATION	//vpu encoder has 16 pixels alignment limitation: vpu will cut down to 16-aligned pixels automatically
#define VPU_ENC_SEQ_DATA_SEPERATE	//output sequence header and data seperately, otherwise, only our vpu decoder can play it

#define VPU_FILEMODE_MERGE_FLAG	1

//#define VPU_WRAPPER_DEBUG
#ifdef VPU_WRAPPER_DEBUG
#ifdef ANDROID_DEGUG
#include "Log.h"
#define LOG_PRINTF LogOutput
#else
#define LOG_PRINTF printf
#endif
#define VPU_LOG(...) //LOG_PRINTF
//#define VPU_TRACE	LOG_PRINTF("%s: %d \r\n",__FUNCTION__,__LINE__)
#define VPU_TRACE
#define VPU_API  LOG_PRINTF 
#define VPU_ERROR LOG_PRINTF
#define VPU_ENC_API	LOG_PRINTF
#define VPU_ENC_LOG(...)// LOG_PRINTF
#define VPU_ENC_ERROR	LOG_PRINTF
#define ASSERT(exp)	if(!(exp)) {LOG_PRINTF("%s: %d : assert condition !!!\r\n",__FUNCTION__,__LINE__);}
//#define VPU_WRAPPER_DUMP
#define MAX_YUV_FRAME  (0)
typedef unsigned int UINT32;
#define DUMP_ALL_DATA		1
static int g_seek_dump=DUMP_ALL_DATA;	/*0: only dump data after seeking; otherwise: dump all data*/
#else
#define VPU_LOG(...)
#define VPU_TRACE
#define VPU_API(...) 
#define VPU_ERROR(...)
#define VPU_ENC_API(...)
#define VPU_ENC_LOG(...)
#define VPU_ENC_ERROR(...)
#define ASSERT(exp)
#endif

//#define VPU_RESET_TEST		//avoid reset board for every changing to FW

#ifdef VPU_SUPPORT_UNCLOSED_GOP
#define MIN_REF_CNT			1			// ref number before B frame: it is display order, but not decode order !!!
#define MAX_DROPB_CNT		5			// maxium continuous dropping B frames number: avoid freeze for closed gop (I B B B B B B B B... B EOS) 
#define MIN_KEY_CNT			1			// key number before the first non-I frame: display order
#define DIS_DROP_FRAME					// to avoid seek timeout , decoder need to output one valid buffer, but not drop frame automaticaly
#endif

#define vpu_memset	memset
#define vpu_memcpy	memcpy

#ifdef NULL
#undef NULL
#define NULL 0
#endif

#define USE_NEW_VPU_API	//api change for vp8

#if 1	//for iMX6 
//#define IMX6_MULTI_FORMATS_WORKAROUND	//need to reset to decoder different formats: such VC1 followed by Mpeg2
#define IMX6_SKIPMODE_WORKAROUND_FILL_DUMMY
#define IMX6_RANGEMAP_WORKAROUND_IGNORE
#define IMX6_LD_BUG_WORKAROUND	 //1 for iMX6 compiler : ld (2.20.1-system.20100303) bug ??	
#define IMX6_PIC_ORDER_WORKAROUND	//fixed for 6_Gee_HD.avi
#define IMX6_BUFNOTENOUGH_WORKAROUND	//when buffer is not enough, vpu may return dispIndex=-1(EOS) directly, but not decIndex=-1
//#define IMX6_INTER_DEBUG_RD_WR	//internal debug: rd wr register
//#define IMX6_INTER_DEBUG	//internal debug
#endif
/****************************** decoder part **************************************/

#define VPU_MEM_ALIGN			0x8
#if 1 //for iMX6 stream mode
#define VPU_BITS_BUF_SIZE		(2*1024*1024)		//bitstream buffer size : big enough contain two big frames
#else
#define VPU_BITS_BUF_SIZE		(1024*1024)
#endif
#define VPU_SLICE_SAVE_SIZE	0x2D800
#define VPU_PS_SAVE_SIZE		0x80000
#define VPU_VP8_MBPARA_SIZE	0x87780			//68 * (1920 * 1088 / 256)=0x87780;

#define VPU_MIN_INIT_SIZE		(512)			//min required data size for seq init
#define VPU_MIN_DEC_SIZE		(64*1024)		//min required data size for decode

#ifdef VPU_AVOID_DEAD_LOOP
#define VPU_MAX_INIT_SIZE		(1024*1024)		//avoid dead loop for unsupported clips
#define VPU_MAX_INIT_LOOP		(50)				//avoid dead loop for crashed files, including null file

#define VPU_MAX_DEC_SIZE		(8*1024*1024)	//avoid dead loop in decode state for corrupted clips
#define VPU_MAX_DEC_LOOP		(4000)			//avoid dead loop in decode state for corrupted clips
#endif

#define VPU_TIME_OUT			(200)			//used for flush operation: wait time
#define VPU_MAX_TIME_OUT_CNT	(10)				//used for flush operation: max counts
#define VPU_MAX_EOS_DEAD_LOOP_CNT	(20)			//used for flush operation
#define VPU_MAX_FRAME_INDEX	30

#define VPU_MIN_UINT_SIZE		(512)			//min required data size for vpu_DecStartOneFrame()

#define VIRT_INDEX	0
#define PHY_INDEX	1

#define VPU_POLLING_TIME_OUT			(10)		//used for normal decode
#define VPU_POLLING_MIN_TIME_OUT		(1)		//used for normal decode: use it when vpu is not busy
#define VPU_MAX_POLLING_BUSY_CNT		(200)	//used for normal decode: max counts 

#define VPU_POLLING_PRESCAN_TIME_OUT			(500)	//used for prescan mode
#define VPU_MAX_POLLING_PRESCAN_BUSY_CNT	(4)		//used for prescan mode: max counts 

#define VPU_OUT_DEC_INDEX_NOMEANING	-4	//unmeaning value: it is not defined by vpu
#define VPU_OUT_DEC_INDEX_UNDEFINE	-3
#define VPU_OUT_DEC_INDEX_UNDEC		-2
#define VPU_OUT_DEC_INDEX_EOS			-1
#define VPU_OUT_DIS_INDEX_NODIS		-3
#define VPU_OUT_DIS_INDEX_NODIS_SKIP	-2
#define VPU_OUT_DIS_INDEX_EOS			-1

#define VPU_FRAME_STATE_FREE			0	//clear them by memset() at init step
#define VPU_FRAME_STATE_DEC			1	//decoded by vpu, but not send out
#define VPU_FRAME_STATE_DISP			2	//send out by vpu for display

#define MemAlign(mem,align)	((((unsigned int)mem)%(align))==0)
#define MemNotAlign(mem,align)	((((unsigned int)mem)%(align))!=0)

#define NotEnoughInitData(free)	(((VPU_BITS_BUF_SIZE)-(free))<(VPU_MIN_INIT_SIZE))
#define NotEnoughDecData(free)	(((VPU_BITS_BUF_SIZE)-(free))<(VPU_MIN_DEC_SIZE))


#define VC1_MAX_SEQ_HEADER_SIZE	256		//for clip: WVC1_stress_a0_stress06.wmv, its header length = 176 (>128)
#define VC1_MAX_FRM_HEADER_SIZE	32
#define VP8_SEQ_HEADER_SIZE	32
#define VP8_FRM_HEADER_SIZE	12
#define RCV_HEADER_LEN			24
#define RCV_CODEC_VERSION		(0x5 << 24) //FOURCC_WMV3_WMV
#define RCV_NUM_FRAMES			0xFFFFFF
#define RCV_SET_HDR_EXT		0x80000000
#define VC1_IS_NOT_NAL(id)		(( id & 0x00FFFFFF) != 0x00010000)

#define AVC_IS_IDR(type)		(0==((type)&0x1))	//bit[0]==0
#define AVC_IS_ISLICE(type)	((1==((type)&0x1))&&(0==((type)&0x6))) //bit[0]==1 && bit[2:1]==0
#define AVC_IS_PSLICE(type)	((1==((type)&0x1))&&(2==((type)&0x6))) //bit[0]==1 && bit[2:1]==1
#define AVC_IS_BSLICE(type)	((1==((type)&0x1))&&((4==((type)&0x6))||(6==((type)&0x6)))) //bit[0]==1 && bit[2:1]==2 or 3

#define FRAME_IS_REF(type)	((type==VPU_IDR_PIC)||(type==VPU_I_PIC)||(type==VPU_P_PIC)||(type==VPU_UNKNOWN_PIC))
#define FRAME_IS_B(type)	((type==VPU_B_PIC))
#define FRAME_IS_KEY(type)	((type==VPU_IDR_PIC)||(type==VPU_I_PIC))
#define FRAME_ISNOT_KEY(type)	((type!=VPU_IDR_PIC)&&(type!=VPU_I_PIC))

/*
for stream: WVC1_stress_NoAudio_intensitycomp.wmv, the first frame(two fields) is defined as I/P
here, we loose the rule and regard the frame as I frame for those P/I and I/P.
VC1-AP frame type:(it is not totally defined by specifiction, refer to table 105)
	Bot	0	1	2	3	4	5	6	7
Top		I	P	BI	B	SKIP	*	*	*
0	I	I	I	*	*	*	*	*	*
1	P	I	P	*	*	*	*	*	*
2	BI	*	*	BI	B	*	*	*	*
3	B	*	*	B	B	*	*	*	*
4	SKIP*	*	*	*	SKIP*	*	*
5	*	*	*	*	*	*	*	*	*
6	*	*	*	*	*	*	*	*	*
7	*	*	*	*	*	*	*	*	*
*/

static VpuPicType g_VC1APPicType[8][8]={
{VPU_I_PIC,VPU_I_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC},
{VPU_I_PIC,VPU_P_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC},
{VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_BI_PIC,VPU_B_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC},
{VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_B_PIC,VPU_BI_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC},

{VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_SKIP_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC},
{VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC},
{VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC},
{VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC,VPU_UNKNOWN_PIC}
};

typedef enum
{
	VPU_DEC_STATE_OPEN=0,
	VPU_DEC_STATE_INITOK,
	VPU_DEC_STATE_REGFRMOK,
	VPU_DEC_STATE_DEC,
	VPU_DEC_STATE_STARTFRAMEOK,	/*it is used for non-block mode*/
	VPU_DEC_STATE_OUTOK,
	VPU_DEC_STATE_EOS,
	VPU_DEC_STATE_CORRUPT,
}VpuDecState;

typedef struct
{
	int picType;
	int idrFlag;
	int topFieldFirst;
	int repeatFirstField;
	int pFrameInPBPacket;	/*P frame in [P,B] chunk*/
	VpuFieldType	eFieldType;
	int viewID;	/*MVC: view id*/	
}VpuFrameBufInfo;

typedef struct
{
	/* open parameters */
	VpuCodStd CodecFormat;
	//int blockmode;

	/* decode parameters */
	int iframeSearchEnable;
	int skipFrameMode;
	int skipFrameNum;

	/* init info */
	VpuDecInitInfo initInfo;
	
	/* out frame info */
	VpuDecOutFrameInfo frameInfo;

	/* frame buffer management */
	int frameNum;
	VpuFrameBuffer frameBuf[VPU_MAX_FRAME_INDEX];	 /*buffer node*/
	VpuFrameBufInfo   frameBufInfo[VPU_MAX_FRAME_INDEX];  /*info required by user*/
	int frameBufState[VPU_MAX_FRAME_INDEX];  /*record frame state for clearing display frame(if user forgot to clear them)*/

	/* bitstream buffer pointer info */
	unsigned char* pBsBufVirtStart;
	unsigned char* pBsBufPhyStart;
	unsigned char* pBsBufPhyEnd;
	//unsigned char* pBsBufPhyWritePtr;

	/* avc slice/ps buffer*/
	unsigned char* pAvcSlicePhyBuf;
	unsigned char* pAvcSPSPhyBuf;	/*vpu may write sps/pps info into this buffer */

	/* */

	/* state */
	VpuDecState state;

	/* historical info */
	VpuFrameBuffer * pPreDisplayFrameBuf;
	//VpuFrameBuffer * pPreDecodedFrameBuf;

	int nPrivateSeqHeaderInserted;

	/*resolution for some special formats, such as package VC1 header,...*/
	int picWidth;
	int picHeight;

#ifdef VPU_SUPPORT_UNCLOSED_GOP
	//(1) drop B frame
	int refCnt;	//IDR/I/P
	int dropBCnt;
	//(2) drop non-I frame
	int keyCnt;	//IDR/I
#endif
#ifdef VPU_IFRAME_SEARCH
	int keyDecCnt;	//IDR/I: it is in decode order, not like keyCnt
#endif

#ifdef VPU_SEEK_ANYPOINT_WORKAROUND
	int seekKeyLoc;	/*I/IDR location: it is decode order, similar with keyDecCnt*/
	int recommendFlush;	/*recommend user call flush to clear some related internal state in vpu*/
#endif

#ifdef VPU_SUPPORT_NO_ENOUGH_FRAME
	int dataUsedInFileMode;	//in file mode, avoid copy data repeatedly
	int lastDatLenInFileMode;	//record the last data length
	int lastConfigMode;		//protection for PB: use same skipmode for PB chunk
#endif
	int filemode;	/*now, for DivX3, VC1, RV*/
	int firstData;	/*for file mode: we need to send the first data to seqinit and startoneframe seperately */
	int firstDataSize; /*data length for the first data*/
#ifdef VPU_PROTECT_MULTI_INSTANCE
	int filledEOS;	/* have vpu_DecUpdateBitstreamBuffer(handle,0) */
#endif	
	int pbPacket;/*divx PB chunk*/
	int pbClips;	/*for PB clips, skipmode will introduce problem*/

#ifdef VPU_FLUSH_BEFORE_DEC_WORKAROUND
	int realWork;	/*will be set 1 if only vpu_DecGetOutputInfo() is called*/
#endif

#ifdef VPU_FILEMODE_SUPPORT_INTERLACED_SKIPMODE
	int firstFrameMode;	/*record the skip frame mode for first field*/
	int fieldCnt;			/*first field decoded: 1; second field decoded: 0;*/						
#endif

#ifdef VPU_FILEMODE_MERGE_INTERLACE_DEBUG
	int needMergeFields;	/*1: need merge; 0: needn't merge*/
	int lastFieldOffset;	/*record the length of first field*/
#endif

#ifdef VPU_FILEMODE_INTERLACE_TIMESTAMP_ENHANCE
	int fieldDecoding;	/*two fields are feed to vpu seperately*/
	int oweFieldTS;		/*the number of fields whose timestamp still not be popped*/
#endif

	int mjpg_frmidx;	/*for mjpg output frame*/
	FrameBuffer vpu_regframebuf[VPU_MAX_FRAME_INDEX];	/*we need to record it for mjpg's frame management*/
	int mjpg_linebuffmode;  /*for iMX6: 1: line buffer mode; 0: non-line buffer mode*/
}VpuDecObj;

typedef struct 
{
	DecHandle handle;
	VpuDecObj obj;	
}VpuDecHandleInternal;


#ifdef VPU_WRAPPER_DEBUG
void printf_memory(unsigned char* addr, int width, int height, int stride)
{
	int i,j;
	unsigned char* ptr;

	ptr=addr;
	VPU_LOG("addr: 0x%X \r\n",(unsigned int)addr);
	for(i=0;i<height;i++)
	{
		for(j=0;j<width;j++)
		{
			VPU_LOG("%2X ",ptr[j]);         
		}
		VPU_LOG("\r\n");
		ptr+=stride;
	}
	VPU_LOG("\r\n");	
	return;
}

void WrapperFileDumpBitstrem(FILE** ppFp, unsigned char* pBits, unsigned int nSize)
{
	if(nSize==0)
	{
		return;
	}

	if (0==g_seek_dump) return;
	
	if(*ppFp==NULL)
	{
		*ppFp=fopen("temp_wrapper.bit","wb");
		if(*ppFp==NULL)
		{
			VPU_LOG("open temp_wrapper.bit failure \r\n");
			return;
		}
		VPU_LOG("open temp_wrapper.bit OK \r\n");
	}

	fwrite(pBits,1,nSize,*ppFp);
	fflush(*ppFp);
	return;
}

void WrapperFileDumpYUV(FILE** ppFp, unsigned char*  pY,unsigned char*  pU,unsigned char*  pV, unsigned int nYSize,unsigned int nCSize,int nColorfmt)
{
	static unsigned int cnt=0;
	int nCScale=1;
	
	switch(nColorfmt)
	{
		case 0:	//4:2:0
			nCScale=1;
			break;
		case 1:	//4:2:2 hor
		case 2:	//4:2:2 ver
			nCScale=2;
			break;
		case 3:	//4:4:4
			nCScale=4;
			break;
		case 4:	//4:0:0
			nCScale=0;
			break;
		default:	//4:2:0
			break;			
	}	

	if (0==g_seek_dump) return;
	
	if(*ppFp==NULL)
	{
		*ppFp=fopen("temp_wrapper.yuv","wb");
		if(*ppFp==NULL)
		{
			VPU_LOG("open temp_wrapper.yuv failure \r\n");
			return;
		}
		VPU_LOG("open temp_wrapper.yuv OK \r\n");
	}

	if(cnt<MAX_YUV_FRAME)
	{
		fwrite(pY,1,nYSize,*ppFp);
		fwrite(pU,1,nCSize*nCScale,*ppFp);
		fwrite(pV,1,nCSize*nCScale,*ppFp);
		fflush(*ppFp);
		cnt++;
	}
	
	return;
}
#endif


int VC1CreateNALSeqHeader(unsigned char* pHeader, int* pHeaderLen, 
	unsigned char* pCodecPri,int nCodecSize, unsigned int* pData, int nMaxHeader)
{
	int nHeaderLen;
	unsigned char temp[4]={0x00,0x00,0x01,0x0D};
	nHeaderLen =nCodecSize -1;
	if((4+nHeaderLen)>nMaxHeader)
	{
		//for case: WVC1_stress_a0_stress06.wmv: header size is 176, In fact, it is also OK if we only copy 128 bytes
		nHeaderLen=nMaxHeader-4;
		VPU_ERROR("error: header length %d overrun !!! \r\n",nCodecSize);
	}
	vpu_memcpy(pHeader, pCodecPri+1, nHeaderLen);

	if(VC1_IS_NOT_NAL(pData[0]))
	{
		//insert 0x0000010D at the end of header 
		vpu_memcpy(pHeader+nHeaderLen, temp, 4);
		nHeaderLen += 4;
	}

	*pHeaderLen=nHeaderLen;

	return 1;
}

int VC1CreateRCVSeqHeader(unsigned char* pHeader, int* pHeaderLen, 
	unsigned char* pCodecPri,unsigned int nFrameSize,int nWidth,int nHeight)
{
	int nHeaderLen;

	unsigned int nValue;
	unsigned int HdrExtDataLen;
	int i=0;
	int profile;

	nHeaderLen = RCV_HEADER_LEN;

	//Number of Frames, Header Extension Bit, Codec Version
	nValue = RCV_NUM_FRAMES | RCV_SET_HDR_EXT | RCV_CODEC_VERSION;
	pHeader[i++] = (unsigned char)nValue;
	pHeader[i++] = (unsigned char)(nValue >> 8);
	pHeader[i++] = (unsigned char)(nValue >> 16);
#if 0 //1 ???	
	pHeader[i++] = 0xC5;
#else
	pHeader[i++] = (unsigned char)(nValue >> 24);
#endif

	//Header Extension Size
	//ASF Parser gives 5 bytes whereas the VPU expects only 4 bytes, so limiting it
	HdrExtDataLen = 4;
	pHeader[i++] = (unsigned char)HdrExtDataLen;
	pHeader[i++] = (unsigned char)(HdrExtDataLen >> 8);
	pHeader[i++] = (unsigned char)(HdrExtDataLen >> 16);
	pHeader[i++] = (unsigned char)(HdrExtDataLen >> 24);

	profile=(*pCodecPri)>>4;
	if((profile!=0)&&(profile!=4)&&(profile!=12))
	{
		VPU_ERROR("unsuport profile: %d, private: 0x%X \r\n",profile,*((unsigned int*)pCodecPri));
	}
	vpu_memcpy(pHeader+i, pCodecPri, HdrExtDataLen);
	i += HdrExtDataLen;

	//Height
	pHeader[i++] = (unsigned char)nHeight;
	pHeader[i++] = (unsigned char)(((nHeight >> 8) & 0xff));
	pHeader[i++] = (unsigned char)(((nHeight >> 16) & 0xff));
	pHeader[i++] = (unsigned char)(((nHeight >> 24) & 0xff));
	//Width
	pHeader[i++] = (unsigned char)nWidth;
	pHeader[i++] = (unsigned char)(((nWidth >> 8) & 0xff));
	pHeader[i++] = (unsigned char)(((nWidth >> 16) & 0xff)); 
	pHeader[i++] = (unsigned char)(((nWidth >> 24) & 0xff));

	//RCV2 ???
	//nHeaderLen+=16;
	//...
	
	//Frame Size
	pHeader[i++] = (unsigned char)nFrameSize;
	pHeader[i++] = (unsigned char)(nFrameSize >> 8);
	pHeader[i++] = (unsigned char)(nFrameSize >> 16);
#if 0	//1 ???
	pHeader[i++] = (unsigned char)((nFrameSize >> 24));
#else
	pHeader[i++] = (unsigned char)((nFrameSize >> 24) | 0x80);
#endif

	*pHeaderLen=nHeaderLen;

	return 1;
}


int VC1CreateNalFrameHeader(unsigned char* pHeader, int* pHeaderLen,unsigned int*pInData )
{
	unsigned int VC1Id;
	VC1Id=*pInData;
	if(VC1_IS_NOT_NAL(VC1Id))	
	{
		//need insert header : special ID
		pHeader[0]=0x0;
		pHeader[1]=0x0;
		pHeader[2]=0x01;
		pHeader[3]=0x0D;
		*pHeaderLen=4;	
	}
	else
	{
		//need not insert header
		//do nothing
		*pHeaderLen=0;
	}

	return 1;
}


int VC1CreateRCVFrameHeader(unsigned char* pHeader, int* pHeaderLen,unsigned int nInSize )
{
	pHeader[0] = (unsigned char)nInSize;
	pHeader[1] = (unsigned char)(nInSize >> 8);
	pHeader[2] = (unsigned char)(nInSize >> 16);
	pHeader[3] = (unsigned char)(nInSize >> 24);
	*pHeaderLen=4;

	return 1;
}

int VP8CreateSeqHeader(unsigned char* pHeader, int* pHeaderLen, 
	unsigned int nTimeBaseDen,unsigned int nTimeBaseNum,unsigned int nFrameCnt,int nWidth,int nHeight)
{
	int i=0;

	pHeader[i++] = 'D';
	pHeader[i++] = 'K';
	pHeader[i++] = 'I';
	pHeader[i++] = 'F';
	/*version*/
	pHeader[i++]=0;
	pHeader[i++]=0;
	/*headersize*/
	pHeader[i++]=VP8_SEQ_HEADER_SIZE;
	pHeader[i++]=0;
	/*fourcc*/
	pHeader[i++]='V';
	pHeader[i++]='P';
	pHeader[i++]='8';
	pHeader[i++]='0';
	/*width*/
	pHeader[i++]=(unsigned char)nWidth;
	pHeader[i++]=(unsigned char)(((nWidth >> 8) & 0xff));
	/*height*/
	pHeader[i++]=(unsigned char)nHeight;
	pHeader[i++]=(unsigned char)(((nHeight >> 8) & 0xff));
	/*rate*/
	pHeader[i++] = (unsigned char)nTimeBaseDen;
	pHeader[i++] = (unsigned char)(((nTimeBaseDen >> 8) & 0xff));
	pHeader[i++] = (unsigned char)(((nTimeBaseDen >> 16) & 0xff));
	pHeader[i++] = (unsigned char)(((nTimeBaseDen >> 24) & 0xff));
	/*scale*/
	pHeader[i++] = (unsigned char)nTimeBaseNum;
	pHeader[i++] = (unsigned char)(((nTimeBaseNum >> 8) & 0xff));
	pHeader[i++] = (unsigned char)(((nTimeBaseNum >> 16) & 0xff));
	pHeader[i++] = (unsigned char)(((nTimeBaseNum >> 24) & 0xff));
	/*frame cnt*/
	pHeader[i++] = (unsigned char)nFrameCnt;
	pHeader[i++] = (unsigned char)(((nFrameCnt >> 8) & 0xff));
	pHeader[i++] = (unsigned char)(((nFrameCnt >> 16) & 0xff));
	pHeader[i++] = (unsigned char)(((nFrameCnt >> 24) & 0xff));
	/*unused*/
	pHeader[i++]=0;
	pHeader[i++]=0;
	pHeader[i++]=0;
	pHeader[i++]=0;

	ASSERT(i==VP8_SEQ_HEADER_SIZE);
	*pHeaderLen=VP8_SEQ_HEADER_SIZE;

	return 1;
}

int VP8CreateFrameHeader(unsigned char* pHeader, int* pHeaderLen,unsigned int nInSize,unsigned nPTSLow32,unsigned int nPTSHig32)
{
	int i=0;
	/*frame size*/
	pHeader[i++] = (unsigned char)nInSize;
	pHeader[i++] = (unsigned char)(nInSize >> 8);
	pHeader[i++] = (unsigned char)(nInSize >> 16);
	pHeader[i++] = (unsigned char)(nInSize >> 24);
	/*PTS[31:0]*/
	pHeader[i++] = (unsigned char)nPTSLow32;
	pHeader[i++] = (unsigned char)(nPTSLow32 >> 8);
	pHeader[i++] = (unsigned char)(nPTSLow32 >> 16);
	pHeader[i++] = (unsigned char)(nPTSLow32 >> 24);
	/*PTS[63:32]*/
	pHeader[i++] = (unsigned char)nPTSHig32;
	pHeader[i++] = (unsigned char)(nPTSHig32 >> 8);
	pHeader[i++] = (unsigned char)(nPTSHig32 >> 16);
	pHeader[i++] = (unsigned char)(nPTSHig32 >> 24);

	ASSERT(i==VP8_FRM_HEADER_SIZE);
	*pHeaderLen=VP8_FRM_HEADER_SIZE;

	return 1;
}

unsigned int VpuConvertAspectRatio(VpuCodStd eInFormat,unsigned int InRatio,int InWidth,int InHeight, int profile, int level)
{
#define FIXED_POINTED_1	(0x10000)	//(Q16_SHIFT)
	unsigned int tmp;
	//set default value: no scale
	unsigned int OutWidth=FIXED_POINTED_1;	
	unsigned int OutHeight=FIXED_POINTED_1;
	unsigned int Q16Ratio=FIXED_POINTED_1;
	VPU_LOG("aspect ratio: format: %d, ratio: 0x%X, InWidth: %d, InHeight: %d \r\n",eInFormat,InRatio,InWidth,InHeight);
	switch(eInFormat)
	{
		case VPU_V_MPEG2:
			//FIXME: we have no other better info to identify mpeg1 or mpeg2 except profile/level
			if((profile==0)&&(level==0))
			{
				//Mpeg1
/*
CODE	HEIGHT/WIDTH	COMMENT
0000	undefined		Forbidden
0001	1.0				square pels
0010	0.6735	
0011	0.7031			16:9 625-line
0100	0.7615	
0101	0.8055	
0110	0.8437			16:9 525-line
0111	0.8935	
1000	0.9157			702x575 at 4:3 = 0.9157
1001	0.9815	
1010	1.0255	
1011	1.0695	
1100	1.0950			711x487 at 4:3 = 1.0950
1101	1.1575	
1110	1.2015	
1111	undefined		reserved
*/				switch(InRatio)
				{
					case 0x1:	// 1.0 (SAR)
						//no scale(use default value)
						break;
					case 0x2:	// 1:0.6735 (SAR)
						OutWidth=(double)FIXED_POINTED_1*10000;
						OutHeight=FIXED_POINTED_1*6735;
						break;						
					case 0x3:	// 1:0.7031 (SAR)
						OutWidth=(double)FIXED_POINTED_1*10000;
						OutHeight=FIXED_POINTED_1*7031;
						break;	
					case 0x4:	// 1:0.7615 (SAR)
						OutWidth=(double)FIXED_POINTED_1*10000;
						OutHeight=FIXED_POINTED_1*7615;
						break;	
					case 0x5:	// 1:0.8055 (SAR)
						OutWidth=(double)FIXED_POINTED_1*10000;
						OutHeight=FIXED_POINTED_1*8055;
						break;	
					case 0x6:	// 1:0.8437 (SAR)
						OutWidth=(double)FIXED_POINTED_1*10000;
						OutHeight=FIXED_POINTED_1*8437;
						break;	
					case 0x7:	// 1:0.8935 (SAR)
						OutWidth=(double)FIXED_POINTED_1*10000;
						OutHeight=FIXED_POINTED_1*8935;
						break;	
					case 0x8:	// 1:0.9157 (SAR)
						OutWidth=(double)FIXED_POINTED_1*10000;
						OutHeight=FIXED_POINTED_1*9157;
						break;	
					case 0x9:	// 1:0.9815 (SAR)
						OutWidth=(double)FIXED_POINTED_1*10000;
						OutHeight=FIXED_POINTED_1*9815;
						break;	
					case 0xA:	// 1:1.0255 (SAR)
						OutWidth=(double)FIXED_POINTED_1*10000;
						OutHeight=FIXED_POINTED_1*10255;
						break;	
					case 0xB:	// 1:1.0695 (SAR)
						OutWidth=(double)FIXED_POINTED_1*10000;
						OutHeight=FIXED_POINTED_1*10695;
						break;	
					case 0xC:	// 1:1.0950 (SAR)
						OutWidth=(double)FIXED_POINTED_1*10000;
						OutHeight=FIXED_POINTED_1*10950;
						break;		
					case 0xD:	// 1:1.1575 (SAR)
						OutWidth=(double)FIXED_POINTED_1*10000;
						OutHeight=FIXED_POINTED_1*11575;
						break;	
					case 0xE:	// 1:1.2015 (SAR)
						OutWidth=(double)FIXED_POINTED_1*10000;
						OutHeight=FIXED_POINTED_1*12015;
						break;				
					default:
						VPU_ERROR("unsupported ration: 0x%X \r\n",InRatio);
						break;
				}
			}
			else
			{
				//Mpeg2
/*	
		aspect_ratio_information 	Sample Aspect Ratio 		DAR
		0000 					Forbidden 				Forbidden			
		0001					1.0 (Square Sample) 		每
		0010 					每 						3 ¯ 4
		0011 					每 						9 ¯ 16
		0100 					每 						1 ¯ 2.21		
		0101 					每 						Reserved
		＃ 												＃
		1111 					每 						Reserved		
*/
				switch(InRatio)
				{
					case 0x1:	// 1.0 (SAR)
						//no scale(use default value)
						break;
					case 0x2:	// 4:3 (DAR)
						OutWidth=(double)FIXED_POINTED_1*InHeight*4/(3*InWidth);
						OutHeight=FIXED_POINTED_1;
						break;
					case 0x3:	// 16:9 (DAR)
						OutWidth=(double)FIXED_POINTED_1*InHeight*16/(9*InWidth);
						OutHeight=FIXED_POINTED_1;
						break;
					case 0x4:	// 2.21 : 1 (DAR)
						OutWidth=(double)FIXED_POINTED_1*InHeight*221/(100*InWidth);
						OutHeight=FIXED_POINTED_1;
						break;
					default:
						VPU_ERROR("unsupported ration: 0x%X \r\n",InRatio);
						break;
				}	
			}
			break;
		case VPU_V_AVC:
/*
	if aspectRateInfo [31:16] is 0, aspectRateInfo [7:0] means
	aspect_ratio_idc. Otherwise, AspectRatio means Extended_SAR.
	sar_width = aspectRateInfo [31:16],
	sar_height = aspectRateInfo [15:0]
		aspect_ratio_idc 	Sample aspect ratio 		(informative)Examples of use
		0 				Unspecified
		1 				1:1(※square§)		1280x720 16:9 frame without overscan
												1920x1080 16:9 frame without overscan (cropped from 1920x1088)
												640x480 4:3 frame without overscan
		2 				12:11 					720x576 4:3 frame with horizontal overscan
												352x288 4:3 frame without overscan
		3 				10:11 					720x480 4:3 frame with horizontal overscan
												352x240 4:3 frame without overscan
		4 				16:11 					720x576 16:9 frame with horizontal overscan
												540x576 4:3 frame with horizontal overscan
		5 				40:33 					720x480 16:9 frame with horizontal overscan
												540x480 4:3 frame with horizontal overscan
		6 				24:11 					352x576 4:3 frame without overscan
												480x576 16:9 frame with horizontal overscan
		7 				20:11 					352x480 4:3 frame without overscan
												480x480 16:9 frame with horizontal overscan
		8 				32:11 					352x576 16:9 frame without overscan
		9 				80:33 					352x480 	16:9 frame without overscan
		10 				18:11 					480x576 4:3 frame with horizontal overscan
		11 				15:11 					480x480 4:3 frame with horizontal overscan
		12 				64:33 					540x576 16:9 frame with horizontal overscan
		13 				160:99 					540x480 16:9 frame with horizontal overscan
		14..254 			Reserved
		255 				Extended_SAR	
*/			
			tmp=(InRatio>>16)&0xFFFF;	//[31:16]
			if(tmp==0)
			{
				tmp=InRatio&0xFF;		//[7:0]
				switch(tmp)
				{
					case 0x1:	// 1:1
						//no scale(use default value)
						break;
					case 0x2:	// 12:11
						OutWidth=FIXED_POINTED_1*12;
						OutHeight=FIXED_POINTED_1*11;	
						break;
					case 0x3:	// 10:11
						OutWidth=FIXED_POINTED_1*10;
						OutHeight=FIXED_POINTED_1*11;	
						break;
					case 0x4:	// 16:11
						OutWidth=FIXED_POINTED_1*16;
						OutHeight=FIXED_POINTED_1*11;	
						break;
					case 0x5:	// 40:33
						OutWidth=FIXED_POINTED_1*40;
						OutHeight=FIXED_POINTED_1*33;	
						break;
					case 0x6:	// 24:11
						OutWidth=FIXED_POINTED_1*24;
						OutHeight=FIXED_POINTED_1*11;	
						break;
					case 0x7:	// 20:11
						OutWidth=FIXED_POINTED_1*20;
						OutHeight=FIXED_POINTED_1*11;	
						break;
					case 0x8:	// 32:11
						OutWidth=FIXED_POINTED_1*32;
						OutHeight=FIXED_POINTED_1*11;	
						break;
					case 0x9:	// 80:33
						OutWidth=FIXED_POINTED_1*80;
						OutHeight=FIXED_POINTED_1*33;	
						break;

					case 0xA:	// 18:11
						OutWidth=FIXED_POINTED_1*18;
						OutHeight=FIXED_POINTED_1*11;	
						break;
					case 0xB:	// 15:11
						OutWidth=FIXED_POINTED_1*15;
						OutHeight=FIXED_POINTED_1*11;	
						break;
					case 0xC:	// 64:33
						OutWidth=FIXED_POINTED_1*64;
						OutHeight=FIXED_POINTED_1*33;	
						break;
					case 0xD:	// 160:99
						OutWidth=FIXED_POINTED_1*160;
						OutHeight=FIXED_POINTED_1*99;	
						break;
					default:
						VPU_ERROR("unsupported ration: 0x%X \r\n",InRatio);
						break;
				}
			}
			else
			{
				//extended SAR: => sar_width: sar_height ??
				//sar_width = aspectRateInfo [31:16],
				//sar_height = aspectRateInfo [15:0]	
				tmp=(InRatio>>16)&0xFFFF;	//width=[31:16]
				//OutWidth=FIXED_POINTED_1*tmp/InWidth;
				OutWidth=FIXED_POINTED_1*tmp;
				tmp=InRatio&0xFFFF;			//height=[15:0]
				//OutHeight=FIXED_POINTED_1*tmp/InHeight;
				OutHeight=FIXED_POINTED_1*tmp;
			}
			break;
		case VPU_V_DIVX3:	//??
		case VPU_V_DIVX4:
		case VPU_V_DIVX56:
		case VPU_V_XVID:
		case VPU_V_MPEG4:
/*
		aspect_ratio_info 	pixel aspect ratios
		0000 									Forbidden
		0001 									1:1 (Square)
		0010 									12:11 (625-type for 4:3 picture)
		0011 									10:11 (525-type for 4:3 picture)
		0100 									16:11 (625-type stretched for 16:9 picture)
		0101 									40:33 (525-type stretched for 16:9 picture)
		0110-1110 								Reserved
		1111 									extended PAR
*/			
			switch(InRatio)
			{
				case 0x1:	// 1:1 (SAR)
					OutWidth=FIXED_POINTED_1;
					OutHeight=FIXED_POINTED_1;
					break;
				case 0x2:	// 12:11 (SAR)
					OutWidth=FIXED_POINTED_1*12;
					OutHeight=FIXED_POINTED_1*11;
					break;
				case 0x3:	// 10:11 (SAR)
					OutWidth=FIXED_POINTED_1*10;
					OutHeight=FIXED_POINTED_1*11;				
					break;
				case 0x4:	// 16:11 (SAR)
					OutWidth=FIXED_POINTED_1*16;
					OutHeight=FIXED_POINTED_1*11;
					break;
				case 0x5:	// 40:33 (SAR)
					OutWidth=FIXED_POINTED_1*40;
					OutHeight=FIXED_POINTED_1*33;
					break;
				default:
					VPU_ERROR("unsupported ration: 0x%X \r\n",InRatio);
					break;
			}
			break;
		case VPU_V_VC1:
		case VPU_V_VC1_AP:	
			//Aspect Width = aspectRateInfo [31:16],
			//Aspect Height = aspectRateInfo [15:0]
			tmp=(InRatio>>16)&0xFFFF;	//width=[31:16]
			//OutWidth=FIXED_POINTED_1*tmp/InWidth;
			OutWidth=FIXED_POINTED_1*tmp;
			tmp=InRatio&0xFFFF;			//height=[15:0]
			//OutHeight=FIXED_POINTED_1*tmp/InHeight;
			OutHeight=FIXED_POINTED_1*tmp;
			break;
		case VPU_V_MJPG:
		case VPU_V_AVC_MVC:
		case VPU_V_AVS:
		case VPU_V_VP8:
			//ignore ratio
			break;
		default:
			//ignore ratio
			VPU_ERROR("unsupported ration: 0x%X \r\n",InRatio);
			break;
	}

	if((OutWidth==0)||(OutHeight==0))
	{
		Q16Ratio=FIXED_POINTED_1; //no valid ratio
	}
	else
	{
		Q16Ratio=(double)OutWidth*FIXED_POINTED_1/OutHeight;
	}
	VPU_LOG("return Q16Ratio: 0x%X, [OutWidth,OutHeight]=[%d, %d]  \r\n",Q16Ratio,OutWidth,OutHeight);
	return Q16Ratio;
}
#if 1  //1 for iMX6
VpuPicType VpuConvertPicType(VpuCodStd InCodec,int InPicType,int InIdrFlag)
{
	VpuPicType eOutPicType=VPU_UNKNOWN_PIC;

	/*
	InPicType:
	interlaced: top field: [5:3]; bot field: [2:0]
	progressive: [2:0]
	now, we only check the bot field(or frame) bits [2:0]
	*/
	switch (InCodec)
	{
		case VPU_V_AVC:
			/*
			InIdrFlag:
			[0]: second field or frame
			[1]: first field
			now, we only check the second field bit [0]
			*/
			if((InIdrFlag)&0x1)
			{
				eOutPicType=VPU_IDR_PIC;
				VPU_LOG("frame : (IDR) \r\n");
			}
			else
			{
				switch(InPicType)
				{
					case 0:
						eOutPicType=VPU_I_PIC;
						VPU_LOG("frame : (I) \r\n");
						break;
					case 1:
						eOutPicType=VPU_P_PIC;
						VPU_LOG("frame : (P) \r\n");
						break;
					case 2:
						eOutPicType=VPU_B_PIC;
						VPU_LOG("frame : (B) \r\n");
						break;			
					default:
						VPU_LOG("frame : (*) \r\n");
						break;
				}
			}
			break;			
		case VPU_V_VC1:
			/*
			0 - I picture
			1 - P picture
			2 - BI picture
			3 - B picture
			4 - P_SKIP picture
			*/
			switch(InPicType&0x7)
			{
				case 0:
					eOutPicType=VPU_I_PIC;
					VPU_LOG("frame : (I) \r\n");
					break;
				case 1:
					eOutPicType=VPU_P_PIC;
					VPU_LOG("frame : (P) \r\n");
					break;
				case 2:	
					eOutPicType=VPU_BI_PIC;
					VPU_LOG("frame : (BI) \r\n");
					break;
				case 3:	
					eOutPicType=VPU_B_PIC;
					VPU_LOG("frame : (B) \r\n");
					break;
				case 4:	
					eOutPicType=VPU_SKIP_PIC;
					VPU_LOG("frame : (SKIP) \r\n");
					break;
				default:
					VPU_LOG("frame : (*) \r\n");
					break;
			}
			break;
		case VPU_V_VC1_AP:
			//need to check [2:0](second field) and [5:3](first field)
			eOutPicType=g_VC1APPicType[(InPicType>>3)&0x7][InPicType&0x7];
			VPU_LOG("VC1-AP: pictype: %d \r\n",eOutPicType);
			break;
		default:	
			/*
			0 - I picture
			1 - P picture
			2 - B picture
			3 - D picture in MPEG2, S picture in MPEG4		
			*/
			switch(InPicType)
			{
				case 0:
					eOutPicType=VPU_I_PIC;
					VPU_LOG("frame : (I) \r\n");
					break;
				case 1:
					eOutPicType=VPU_P_PIC;
					VPU_LOG("frame : (P) \r\n");
					break;
				case 2:
					eOutPicType=VPU_B_PIC;
					VPU_LOG("frame : (B) \r\n");
					break;			
				default:
					VPU_LOG("frame : (*) \r\n");
					break;
			}
			break;			
	}	
	return eOutPicType;
}
#else
VpuPicType VpuConvertPicType(VpuCodStd InCodec,int InPicType)
{
	VpuPicType eOutPicType=VPU_UNKNOWN_PIC;

	switch (InCodec)
	{
		case VPU_V_AVC:
			if(AVC_IS_IDR(InPicType))
			{
				eOutPicType=VPU_IDR_PIC;
				VPU_LOG("frame : (I) \r\n");
			}
			else if(AVC_IS_ISLICE(InPicType))
			{
				eOutPicType=VPU_I_PIC;
				VPU_LOG("frame : (IS) \r\n");
			}
			else if(AVC_IS_PSLICE(InPicType))
			{
				eOutPicType=VPU_P_PIC;		
				VPU_LOG("frame : (PS) \r\n");
			}
			else if(AVC_IS_BSLICE(InPicType))
			{
				eOutPicType=VPU_B_PIC;		
				VPU_LOG("frame : (BS) \r\n");
			}
			else
			{
				VPU_LOG("frame :  (*) \r\n");
			}		
			break;
		case VPU_V_VC1:
			//only check [2:0]
			switch(InPicType&0x7)
			{
				case 0:
					eOutPicType=VPU_I_PIC;
					VPU_LOG("frame : (I) \r\n");
					break;
				case 1:
					eOutPicType=VPU_P_PIC;
					VPU_LOG("frame : (P) \r\n");
					break;
				case 2:	
					eOutPicType=VPU_BI_PIC;
					VPU_LOG("frame : (BI) \r\n");
					break;
				case 3:	
					eOutPicType=VPU_B_PIC;
					VPU_LOG("frame : (B) \r\n");
					break;
				case 4:	
					eOutPicType=VPU_SKIP_PIC;
					VPU_LOG("frame : (SKIP) \r\n");
					break;
				default:
					VPU_LOG("frame : (*) \r\n");
					break;
			}
			break;
		case VPU_V_VC1_AP:
			//need to check [2:0](second field) and [5:3](first field)
			eOutPicType=g_VC1APPicType[(InPicType>>3)&0x7][InPicType&0x7];
			VPU_LOG("VC1-AP: pictype: %d \r\n",eOutPicType);
			break;
		default:	
			switch(InPicType)
			{
				case 0:
					eOutPicType=VPU_I_PIC;
					VPU_LOG("frame : (I) \r\n");
					break;
				case 1:
					eOutPicType=VPU_P_PIC;
					VPU_LOG("frame : (P) \r\n");
					break;
				case 2:
					eOutPicType=VPU_B_PIC;
					VPU_LOG("frame : (B) \r\n");
					break;			
				default:
					VPU_LOG("frame : (*) \r\n");
					break;
			}
			break;			
	}	
	return eOutPicType;
}
#endif

VpuFieldType VpuConvertFieldType(VpuCodStd InCodec,DecOutputInfo * pCurDecFrameInfo)
{
	VpuFieldType eField=VPU_FIELD_NONE;

	switch (InCodec)
	{
		case VPU_V_AVC:
			if(pCurDecFrameInfo->interlacedFrame)
			{
				if (pCurDecFrameInfo->topFieldFirst) eField = VPU_FIELD_TB;
				else eField = VPU_FIELD_BT;
			}
			break;
		case VPU_V_VC1:
		case VPU_V_VC1_AP:
			if (pCurDecFrameInfo->pictureStructure==2)
			{
				VPU_LOG("frame interlaced \r\n");
			}
			else if (pCurDecFrameInfo->pictureStructure==3)
			{
				if (pCurDecFrameInfo->topFieldFirst) eField = VPU_FIELD_TB;
				else 	eField= VPU_FIELD_BT;
			}			
			break;
		case VPU_V_MPEG2:
		case VPU_V_H263:
		case VPU_V_DIVX3:
			if (pCurDecFrameInfo->interlacedFrame
				|| !pCurDecFrameInfo->progressiveFrame)
			{
				if (pCurDecFrameInfo->pictureStructure == 1) eField= VPU_FIELD_TOP;
				else if (pCurDecFrameInfo->pictureStructure == 2) eField= VPU_FIELD_BOTTOM;
				else if (pCurDecFrameInfo->pictureStructure == 3)
				{
					if (pCurDecFrameInfo->topFieldFirst) eField = VPU_FIELD_TB;
					else eField = VPU_FIELD_BT;
				}
			}
			break;
		case VPU_V_MPEG4:
		case VPU_V_DIVX4:
		case VPU_V_DIVX56:
		case VPU_V_XVID:
		case VPU_V_RV:
		case VPU_V_MJPG:
			//none ??
			break;
		default:	
			break;			
	}
	
	return eField;
}

int VpuSaveDecodedFrameInfo(VpuDecObj* pObj, int index,DecOutputInfo * pCurDecFrameInfo)
{
	VpuFrameBufInfo * pDstInfo;
	
	if(index>=pObj->frameNum)
	{
		//overflow !!!		
		return 0;
	}	

	//VPU_LOG("save index %d: pictype = %d \r\n",index,pCurDecFrameInfo->picType);
	pDstInfo=&pObj->frameBufInfo[index];
	pDstInfo->picType=pCurDecFrameInfo->picType;
	pDstInfo->idrFlag=pCurDecFrameInfo->idrFlg;
	//pDstInfo->topFieldFirst=pCurDecFrameInfo->topFieldFirst;
	//pDstInfo->repeatFirstField=pCurDecFrameInfo->repeatFirstField;
	pDstInfo->pFrameInPBPacket=pObj->pbPacket;
	pDstInfo->eFieldType=VpuConvertFieldType(pObj->CodecFormat,pCurDecFrameInfo);
	pDstInfo->viewID=pCurDecFrameInfo->mvcPicInfo.viewIdxDecoded;

	return 1;
}

int VpuLoadDispFrameInfo(VpuDecObj* pObj, int index,VpuDecOutFrameInfo* pDispFrameInfo)
{
	VpuFrameBufInfo * pSrcInfo;
	
	if(index>=pObj->frameNum)
	{
		//overflow !!!		
		return 0;
	}		

	pSrcInfo=&pObj->frameBufInfo[index];
	//pDispFrameInfo->ePicType=pSrcInfo->picType;
	pDispFrameInfo->ePicType=VpuConvertPicType(pObj->CodecFormat,pSrcInfo->picType,pSrcInfo->idrFlag);
	//pDispFrameInfo->nTopFieldFirst=pSrcInfo->topFieldFirst;
	//pDispFrameInfo->nRepeatFirstField=pSrcInfo->repeatFirstField;
	pDispFrameInfo->eFieldType=pSrcInfo->eFieldType;
	pDispFrameInfo->nMVCViewID=pSrcInfo->viewID;	

	//VPU_LOG("load index %d: pictype = %d \r\n",index,pSrcInfo->picType);	
	return 1;
}

int VpuSearchFrameIndex(VpuDecObj* pObj,VpuFrameBuffer * pInFrameBuf)
{
	int index;
	int i;

	for(i=0;i<pObj->frameNum;i++)
	{
		if((&pObj->frameBuf[i]) == pInFrameBuf)
		{
			index=i;
			break;
		}
	}
	
	if (i>=pObj->frameNum)
	{
		//not find !!
		VPU_LOG("%s: error: can not find frame index \r\n",__FUNCTION__);
		index=-1;
	}
	return index;
}

int  VpuSearchFrameBuf(VpuDecObj* pObj,int index,VpuFrameBuffer ** ppOutFrameBuf)
{
	if((index>=pObj->frameNum)||(index<0))
	{
		//overflow !!!
		*ppOutFrameBuf=NULL;
		return 0;
	}
	else
	{
		*ppOutFrameBuf=&pObj->frameBuf[index];
		return 1;
	}
}

int  VpuSearchFreeFrameBuf(VpuDecObj* pObj,int* pIndex)
{
	int i;

	for(i=0;i<pObj->frameNum;i++)
	{
		if(pObj->frameBufState[i] == VPU_FRAME_STATE_FREE)
		{			
			break;
		}
	}
	
	if (i>=pObj->frameNum)
	{
		//not find !!
		VPU_LOG("%s: can not find frame index \r\n",__FUNCTION__);
		*pIndex=-1;
		return 0;
	}
	*pIndex=i;
	return 1;
}

int VpuSetDispFrameState(int index, int* pFrameState,int state)
{
	pFrameState[index]=state;
	return 1;
}

int VpuGetDispFrameState(int index, int* pFrameState)
{
	return pFrameState[index];
}

int VpuClearDispFrame(int index, int* pFrameState)
{
	pFrameState[index]=VPU_FRAME_STATE_FREE;
	return 1;
}

int VpuDispFrameIsNotCleared(int index, int* pFrameState)
{
	if(pFrameState[index]!=VPU_FRAME_STATE_FREE)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}


int VpuFreeAllDispFrame(DecHandle InVpuHandle,int Num,int* pFrameState)
{
	//TODO: it is already useless !!!
	int i;
	RetCode ret=RETCODE_SUCCESS;
	for(i=0;i<Num;i++)
	{
		if (VpuDispFrameIsNotCleared(i, pFrameState))
		{
			VpuClearDispFrame(i, pFrameState);
			VPU_API("%s: calling vpu_DecClrDispFlag(): %d \r\n",__FUNCTION__,i);
			ret=vpu_DecClrDispFlag(InVpuHandle,i);
			if(RETCODE_SUCCESS!=ret)
			{
				VPU_ERROR("%s: vpu clear display frame failure, index=0x%X, ret=%d \r\n",__FUNCTION__,i,ret);
			}
		}
	}
	return ((ret==RETCODE_SUCCESS)?1:0);
}

int VpuClearAllDispFrame(int Num,int* pFrameState)
{
	int i;
	for(i=0;i<Num;i++)
	{
		VpuClearDispFrame(i, pFrameState);
	}
	return 1;
}

int VpuClearAllDispFrameFlag(DecHandle InVpuHandle,int Num)
{
	int i;
	RetCode ret=RETCODE_SUCCESS;
	for(i=0;i<Num;i++)
	{
		VPU_API("%s: calling vpu_DecClrDispFlag(): %d \r\n",__FUNCTION__,i);
		ret=vpu_DecClrDispFlag(InVpuHandle,i);
		if(RETCODE_SUCCESS!=ret)
		{
			VPU_ERROR("%s: vpu clear display frame failure, index=0x%X, ret=%d \r\n",__FUNCTION__,i,ret);
		}
	}
	return ((ret==RETCODE_SUCCESS)?1:0);
}

#ifdef IMX6_BUFNOTENOUGH_WORKAROUND 
int VpuQueryVpuHoldBufNum(VpuDecObj* pObj)
{
	//occupied by vpu: state = free or dec
	int i;
	int num=0;
	for(i=0;i<pObj->frameNum;i++)
	{
		if((pObj->frameBufState[i] == VPU_FRAME_STATE_DEC) ||(pObj->frameBufState[i] == VPU_FRAME_STATE_FREE))
		{			
			num++;
		}
	}
	return num;
}
#endif
	
int VpuBitsBufIsEnough(DecHandle InVpuHandle,int nFillSize)
{
	PhysicalAddress Rd;
	PhysicalAddress Wr;
	unsigned long nSpace;
	RetCode ret;

	VPU_TRACE;
#ifndef IMX6_LD_BUG_WORKAROUND
	VPU_API("calling vpu_DecGetBitstreamBuffer() \r\n");
#endif
	ret=vpu_DecGetBitstreamBuffer(InVpuHandle, &Rd, &Wr, &nSpace);
	VPU_TRACE;

	//check ret ??
	
	//check free space
	if(nSpace<nFillSize)
	{
		//VPU_LOG("vpu bistream is too full, free=%d, required=%d \r\n",(unsigned int)nSpace,nFillSize);
		return 0;
	}
	else
	{
		return 1;
	}
}

int VpuBitsBufValidDataLength(DecHandle InVpuHandle,VpuDecObj* pObj,unsigned int* pOutValidSize,unsigned int* pOutFreeSize)
{
	PhysicalAddress Rd;
	PhysicalAddress Wr;
	unsigned long nSpace;
	RetCode ret;

	VPU_TRACE;
	//VPU_API("calling vpu_DecGetBitstreamBuffer() \r\n");
	ret=vpu_DecGetBitstreamBuffer(InVpuHandle, &Rd, &Wr, &nSpace);
	VPU_TRACE;

	//check ret ??

	*pOutValidSize=((unsigned int)Wr-(unsigned int)pObj->pBsBufPhyStart);
	*pOutFreeSize=nSpace;
	return 1;
}

int VpuFillData(DecHandle InVpuHandle,VpuDecObj* pObj,unsigned char* pInVirt,unsigned int nSize, int InIsEnough,int nFileModeOffset)
{

#ifdef VPU_ONE_EOS
	static int eos_flag=0;
#endif

	PhysicalAddress Rd;
	PhysicalAddress Wr;
	unsigned long nSpace;
	unsigned int nFillSize,nFillUnit;	
	RetCode ret;
	unsigned char* pFill;
	unsigned char* pSrc;

#ifdef VPU_DEBUG_BS
	static int totalSize=0;
#endif

#ifdef VPU_WRAPPER_DUMP
	static FILE* fpBitstream=NULL;
	//static int nDumpSize=0;
#endif

	//EOS:  pInVirt!=NULL && nSize==0
	if(pInVirt==NULL)
	{
		return 1; //0
	}

#ifdef VPU_ONE_EOS
	if((1==eos_flag))		
	{
		if (0==nSize)
		{
			// avoid repeated send EOS flag
			return 1; 
		}
		else
		{
			//reset for repeat playing
			eos_flag=0;
		}
	}
#endif
	

	nFillSize=nSize;
	pSrc=pInVirt;

	//get bits buff info
	VPU_TRACE;
	VPU_API("calling vpu_DecGetBitstreamBuffer() \r\n");
	ret=vpu_DecGetBitstreamBuffer(InVpuHandle, &Rd, &Wr, &nSpace);
	VPU_API("Wr: 0x%X, Rd: 0x%X, space: %d \r\n",Wr,Rd,nSpace);
	VPU_TRACE;

#ifdef IMX6_INTER_DEBUG_RD_WR
{
	unsigned int rd,wr;
//#define BIT_RD_PTR_0			0x120
//#define BIT_WR_PTR_0			0x124
	IOClkGateSet(1);
	rd=VpuReadReg(0x120);
	wr=VpuReadReg(0x124);
	IOClkGateSet(0);
	printf("vpu register: wr: 0x%X, rd: 0x%X \r\n", wr, rd);
}
#endif

	if(0==InIsEnough)		//TODO: should remove it after wrapper is stable
	{
		//check free space
		if(nSpace<nFillSize)
		{
			//VPU_LOG("vpu bistream is too full, free=%d, required=%d \r\n",(unsigned int)nSpace,nFillSize);
			//1 need to check and update pObj->state from DEC to FRAMEOK ????
			return 0;
		}
	}

#ifdef VPU_DEBUG_BS
	totalSize+=nFillSize;
	VPU_LOG("total filled data size = %d \r\n",totalSize);
#endif

	//in file mode, we may not get correct value from vpu_DecGetBitstreamBuffer(), so we need to add different branch for filemode.
	if(0==pObj->filemode)
	{
		//check ring buffer's bottom
		if((unsigned int)pObj->pBsBufPhyEnd < (unsigned int)Wr + nFillSize)
		{
			//need to split data into two segments
			ASSERT((unsigned int)(pObj->pBsBufPhyEnd) != (unsigned int)Wr);
			nFillUnit=(unsigned int)pObj->pBsBufPhyEnd-(unsigned int)Wr;
			pFill=pObj->pBsBufVirtStart+((unsigned int)Wr-(unsigned int)pObj->pBsBufPhyStart);
			vpu_memcpy(pFill,pSrc,nFillUnit);
			VPU_API("calling vpu_DecUpdateBitstreamBuffer(): %d \r\n",nFillUnit);
			ret = vpu_DecUpdateBitstreamBuffer(InVpuHandle, nFillUnit);

			//update nFillUnit for next writing
			pSrc+=nFillUnit;
			nFillUnit=nFillSize- nFillUnit;
			VPU_API("calling vpu_DecGetBitstreamBuffer() \r\n");
			ret = vpu_DecGetBitstreamBuffer(InVpuHandle, &Rd, &Wr, &nSpace);
			VPU_LOG("nSpace: %d \r\n", nSpace);
		}
		else
		{
			nFillUnit=nFillSize;
		}

		//write the left data
		pFill=pObj->pBsBufVirtStart+((unsigned int)Wr-(unsigned int)pObj->pBsBufPhyStart);
		vpu_memcpy(pFill,pSrc,nFillUnit);
		VPU_TRACE;
		VPU_API("calling vpu_DecUpdateBitstreamBuffer(): %d \r\n",nFillUnit);
		ret = vpu_DecUpdateBitstreamBuffer(InVpuHandle, nFillUnit);
		VPU_TRACE;

		//VPU_API("calling vpu_DecGetBitstreamBuffer() \r\n");
		//ret = vpu_DecGetBitstreamBuffer(InVpuHandle, &Rd, &Wr, &nSpace);
		//VPU_LOG("nSpace: %d \r\n", nSpace);		
	}
	else
	{
		//file mode: always write data from start address+offset.
		nFillUnit=nFillSize;
		ASSERT(nFillUnit<(unsigned int)(pObj->pBsBufPhyEnd-pObj->pBsBufPhyStart-nFileModeOffset));
		pFill=pObj->pBsBufVirtStart+nFileModeOffset;
		vpu_memcpy(pFill,pSrc,nFillUnit);
		VPU_API("calling vpu_DecUpdateBitstreamBuffer(): %d \r\n",nFillUnit);
		ret = vpu_DecUpdateBitstreamBuffer(InVpuHandle, nFillUnit);
	}

	//check ret ??

#ifdef VPU_ONE_EOS
	//update eos flag
	if(nFillUnit==0)
	{
		eos_flag=1;
	}
	else
	{
		eos_flag=0;
	}
#endif

#ifdef VPU_PROTECT_MULTI_INSTANCE
	//TODO: In fact, we can merge two variables (eos_flag and filledEOS) into one 
	if(nFillUnit==0)
	{
		pObj->filledEOS=1;
	}
	else
	{
		pObj->filledEOS=0;
	}
#endif

#ifdef VPU_WRAPPER_DUMP
	WrapperFileDumpBitstrem(&fpBitstream,pInVirt,nSize);
	//nDumpSize+=nSize;
	//LOG_PRINTF("dump size: %d \r\n",nDumpSize);
#endif
	
	return 1;
}

int VpuSeqInit(DecHandle InVpuHandle, VpuDecObj* pObj ,VpuBufferNode* pInData,VpuDecBufRetCode* pOutRetCode,int * pNoErr) 
{
	RetCode ret;
	DecInitialInfo initInfo;

	unsigned char* pHeader=NULL;
	unsigned int headerLen=0;

	unsigned char aVC1Header[VC1_MAX_SEQ_HEADER_SIZE];
	unsigned char aVP8Header[VP8_SEQ_HEADER_SIZE+VP8_FRM_HEADER_SIZE];
	int bufIsEnough=1;

	//FIXME: total_* is only for internal debug now after we adding macro VPU_AVOID_DEAD_LOOP 
	static int total_size=0;		// avoid dead loop for unsupported clips
	static int total_loop=0;		// avoid dead loop for crashed file
	*pNoErr=1;	//set default: no error

	//for special formats, we need to re-organize data
	switch(pObj->CodecFormat)
	{
		case VPU_V_VC1:
		case VPU_V_VC1_AP:
			pHeader=aVC1Header;
			if (0==pInData->sCodecData.nSize)
			{
				//raw file: .rcv/.vc1
				//do nothing
				//1 for identify raw data ( .rcv/.vc1), user should not clear sCodecData.nSize before seqinit finished !!!
			}
			else if(pObj->nPrivateSeqHeaderInserted==0)
			{
				//insert private data
				if((pObj->CodecFormat==VPU_V_VC1_AP))
				{
					if((pInData->pVirAddr==NULL) ||(pInData->nSize<4))
					{
						//we need pInData->pVirAddr to create correct VC1 header
						//TODO: or define one default value when pInData->pVirAddr is NULL
						VPU_LOG("%s: no input buffer, return and do nothing \r\n",__FUNCTION__);	
						*pOutRetCode=VPU_DEC_INPUT_NOT_USED;
						return 0;
					}
					VC1CreateNALSeqHeader(pHeader, (int*)(&headerLen),pInData->sCodecData.pData, (int)pInData->sCodecData.nSize, (unsigned int*)pInData->pVirAddr,VC1_MAX_SEQ_HEADER_SIZE);
				}
				else
				{
					//1 nSize must == frame size ??? 
					VPU_LOG("%s: [width x height]=[%d x %d] , frame size =%d \r\n",__FUNCTION__,pObj->picWidth,pObj->picHeight,pInData->nSize);
					VC1CreateRCVSeqHeader(pHeader, (int*)(&headerLen),pInData->sCodecData.pData, pInData->nSize,pObj->picWidth,pObj->picHeight);
				}

#ifdef VPU_WRAPPER_DEBUG
				printf_memory(pHeader, headerLen, 1, headerLen);
#endif				

			}
			else
			{
				//private data have already been inserted before
				//so we need to insert frame header !!! when we enable macro VPU_DEC_CHECK_INIT_LENGTH

				if((pInData->pVirAddr==NULL)/* ||(pInData->nSize<4)*/)
				{
					VPU_LOG("%s: no input buffer, return and do nothing \r\n",__FUNCTION__);	
					*pOutRetCode=VPU_DEC_INPUT_NOT_USED;
					return 0;
				}

				if((pObj->CodecFormat==VPU_V_VC1_AP))
				{
					ASSERT(pInData->nSize>=4);
					VC1CreateNalFrameHeader(pHeader,(int*)(&headerLen),(unsigned int*)(pInData->pVirAddr));
				}
				else
				{
					//need to insert header : frame size
					VC1CreateRCVFrameHeader(pHeader,(int*)(&headerLen),pInData->nSize);
				}					
			}	
			break;
		case VPU_V_VP8:
			pHeader=aVP8Header;
			if (pInData->sCodecData.nSize==0xFFFFFFFF)	
			{
				//raw data
				//do nothing
				//1 for identify raw data , user should set 0xFFFFFFF to sCodecData.nSize before seqinit finished !!!
			}
			else
			{
				if(pObj->nPrivateSeqHeaderInserted==0)
				{
					unsigned int frmHdrLen=0;
					if(pInData->sCodecData.nSize!=0)
					{
						VPU_ERROR("Warning: VP8 CodecData is not NULL, and it will be ignored by wrapper !\r\n");
					}
					//insert private data: seq+frm header
					VPU_LOG("%s: [width x height]=[%d x %d] , frame size =%d \r\n",__FUNCTION__,pObj->picWidth,pObj->picHeight,pInData->nSize);
					VP8CreateSeqHeader(pHeader, (int*)(&headerLen),1,1,0,pObj->picWidth,pObj->picHeight);
					VP8CreateFrameHeader(pHeader+headerLen,(int*)(&frmHdrLen),pInData->nSize,0,0);
					headerLen+=frmHdrLen;
#ifdef VPU_WRAPPER_DEBUG
					printf_memory(pHeader, headerLen, 1, headerLen);
#endif
				}
				else
				{
					//seq header have already been inserted before
					//so we need to insert frame header !!! when we enable macro VPU_DEC_CHECK_INIT_LENGTH
					if((pInData->pVirAddr==NULL)/* ||(pInData->nSize<4)*/)
					{
						VPU_LOG("%s: no input buffer, return and do nothing \r\n",__FUNCTION__);	
						*pOutRetCode=VPU_DEC_INPUT_NOT_USED;
						return 0;
					}
					//need to insert header : frame size
					VP8CreateFrameHeader(pHeader,(int*)(&headerLen),pInData->nSize,0,0);
				}	
			}
			break;
		default:
			//other formats
			if (0==pInData->sCodecData.nSize)
			{
				//raw data
				//do nothing
			}
			else if(pObj->nPrivateSeqHeaderInserted==0)
			{
				//insert private data
				pHeader=pInData->sCodecData.pData;
				headerLen=pInData->sCodecData.nSize;				
			}			
			else
			{
				//private data have already been inserted before
				//do nothing
			}
			break;
	}


	//check free space and fill data into vpu
	bufIsEnough=VpuBitsBufIsEnough(InVpuHandle,headerLen+pInData->nSize);

#ifdef VPU_INIT_FREE_SIZE_LIMITATION
	{
		#define VPU_INIT_UNIT_SIZE	512
		unsigned int validSize;
		unsigned int freeSize;
		VpuBitsBufValidDataLength(InVpuHandle, pObj, &validSize,&freeSize);
		//if(1==pObj->filemode) //only in file mode ???, not sure
		if((freeSize<VPU_INIT_UNIT_SIZE)&&(validSize<(VPU_BITS_BUF_SIZE/2)))
		{
			VPU_LOG("fake info: freeSize: %d, validSize: %d, we should continue feed data \r\n",freeSize,validSize);
			bufIsEnough=1;
		}
	}
#endif

	if(0==bufIsEnough)
	{
		//write failure: buffer is full
		*pOutRetCode=VPU_DEC_INPUT_NOT_USED;
		//TODO: add flush bitstream buffer to avoid dead loop in application ??
		//Here: we think the clip can not be identified by vpu if bitstream buffer is full
		VPU_ERROR("seq init failure: buffer is full: total_size: %d, total_loop: %d \r\n",total_size,total_loop);
		*pNoErr=0;
		total_size=0;	//clear 0
		total_loop=0;
		return 0;
	}
	else
	{
		//it is enough to fill
		int fill_ret;
		if(0!=headerLen)
		{
			fill_ret=VpuFillData(InVpuHandle,pObj,pHeader,headerLen,1,0);
			if(0!=pInData->sCodecData.nSize)
			{
				//not raw data : .rcv/.vc1
				pObj->nPrivateSeqHeaderInserted=1; // we need to re-open vpu wrapper if user want to re-seqinit
			}
			else if(pObj->CodecFormat==VPU_V_VP8)
			{
				//for VP8, regard it as non-raw data as long as headerLen!=0
				pObj->nPrivateSeqHeaderInserted=1; 
			}
		}
		//allow pInData->nSize==0 ???
		fill_ret=VpuFillData(InVpuHandle,pObj,pInData->pVirAddr,pInData->nSize,1,headerLen);
		ASSERT(fill_ret==1);
		total_size+=headerLen+pInData->nSize;
		total_loop++;
	}

	if(0==pObj->filemode)
	{
#ifdef VPU_DEC_CHECK_INIT_LENGTH
		PhysicalAddress Rd;
		PhysicalAddress Wr;
		unsigned long nSpace;
		//check init size only for stream mode
		VPU_API("calling vpu_DecGetBitstreamBuffer() \r\n");
		vpu_DecGetBitstreamBuffer(InVpuHandle, &Rd, &Wr, &nSpace);
		if(NotEnoughInitData(nSpace))
		{
			//not inited
			*pOutRetCode=VPU_DEC_INPUT_USED;
			return 0;
		}
		VPU_LOG("have collect %d bytes data to start seq init \r\n",VPU_MIN_INIT_SIZE);
#endif	
	}
	else
	{
		//here, we need to consider header length
		//ASSERT(headerLen==0);
		pObj->firstDataSize=headerLen+pInData->nSize;
	}
	VPU_LOG("===================seqinit : length: %d \r\n",pObj->firstDataSize);

	VPU_TRACE;
	VPU_API("calling vpu_DecSetEscSeqInit(): 1 \r\n");
	vpu_DecSetEscSeqInit(InVpuHandle, 1);
	VPU_TRACE;	
	VPU_API("calling vpu_DecGetInitialInfo() \r\n");
	ret = vpu_DecGetInitialInfo(InVpuHandle, &initInfo);
	VPU_TRACE;
	VPU_API("calling vpu_DecSetEscSeqInit(): 0, interlace: %d , errcode: 0x%X \r\n",initInfo.interlace,initInfo.errorcode);
	vpu_DecSetEscSeqInit(InVpuHandle, 0);
	VPU_TRACE;
	if (ret != RETCODE_SUCCESS)
	{
		*pOutRetCode=VPU_DEC_INPUT_USED;

		if(1==pObj->filemode)
		{
			//if((0!=headerLen)&&(0==pInData->nSize)&&(NULL==pInData->pVirAddr))
			{
				VPU_LOG("header is valid, but data is unvalid ! \r\n");
				//FIX case: user has not set input before decoding; (clip: 720P10M30FPS.mpg, command(p s p))
				//shouldn't return error
				pObj->nPrivateSeqHeaderInserted=0; //need to re-fill the header next round
			}
		}
		return 0;
	}
	else
	{
		int cropWidth,cropHeight;
		*pOutRetCode=VPU_DEC_INIT_OK|VPU_DEC_INPUT_USED;

		//update state
		pObj->state=VPU_DEC_STATE_INITOK;
#ifdef IMX6_PIC_ORDER_WORKAROUND		
		VPU_API("codec: %d(AVC=6),profile: %d,  minum bufcount: %d, buf delay: %d \r\n",pObj->CodecFormat,initInfo.profile,initInfo.minFrameBufferCount, initInfo.frameBufDelay);
		if((3==initInfo.minFrameBufferCount)/*&&(0==initInfo.frameBufDelay)*/)
		{
			initInfo.minFrameBufferCount=10;
		}
#endif
		//record init output info
		pObj->initInfo.nMinFrameBufferCount=initInfo.minFrameBufferCount;
		pObj->initInfo.nPicHeight=initInfo.picHeight;
		pObj->initInfo.nPicWidth=initInfo.picWidth;
		pObj->initInfo.nInterlace=initInfo.interlace;
		if(VPU_V_MJPG==pObj->CodecFormat)
		{
			pObj->initInfo.nMjpgSourceFormat=initInfo.mjpg_sourceFormat;
		}

		//record crop info
		if(((0==initInfo.picCropRect.bottom)&&(0==initInfo.picCropRect.right))
			||(initInfo.picCropRect.right<=initInfo.picCropRect.left)
			||(initInfo.picCropRect.bottom<=initInfo.picCropRect.top))
		{
			//Init info is invalid
			pObj->initInfo.PicCropRect.nLeft= 0;
			pObj->initInfo.PicCropRect.nRight=initInfo.picWidth;
			pObj->initInfo.PicCropRect.nTop = 0;
			pObj->initInfo.PicCropRect.nBottom=initInfo.picHeight;
		}
		else
		{
			pObj->initInfo.PicCropRect.nLeft = initInfo.picCropRect.left;
			pObj->initInfo.PicCropRect.nRight=initInfo.picCropRect.right;
			pObj->initInfo.PicCropRect.nTop = initInfo.picCropRect.top;
			pObj->initInfo.PicCropRect.nBottom=initInfo.picCropRect.bottom;
		}

		//convert aspect ratio info
		cropWidth=pObj->initInfo.PicCropRect.nRight-pObj->initInfo.PicCropRect.nLeft;
		cropHeight=pObj->initInfo.PicCropRect.nBottom-pObj->initInfo.PicCropRect.nTop;
		pObj->initInfo.nQ16ShiftWidthDivHeightRatio=VpuConvertAspectRatio(pObj->CodecFormat,(unsigned int)initInfo.aspectRateInfo,cropWidth,cropHeight, initInfo.profile, initInfo.level);

		//clear 0
		total_size=0;
		total_loop=0;

		VPU_LOG("%s:vpu init OK: [width x heigh]=[%d x %d] \r\n",__FUNCTION__,initInfo.picWidth,initInfo.picHeight);
#ifdef VPU_FILEMODE_INTERLACE_TIMESTAMP_ENHANCE
		if((1==initInfo.interlace)&&(VPU_V_AVC==pObj->CodecFormat))	//FIXME: now only process for H.264 ???
		{
			pObj->fieldDecoding=1;
		}
#endif
		return 1;
	}

}

int VpuGetOutput(DecHandle InVpuHandle, VpuDecObj* pObj,VpuDecBufRetCode* pOutRetCode,int InSkipMode,int* pOutInStreamModeEnough,int InFilemodeChunkSize)
{
	RetCode ret;
	DecOutputInfo outInfo;
	VpuFrameBuffer * pFrameDisp;
	VpuFrameBuffer * pFrameDecode=NULL;	
#ifdef VPU_WRAPPER_DUMP
	static FILE* fpYUV=NULL;
#endif
#ifdef VPU_FILEMODE_WORKAROUND
	int disOrderOutput=0;		// the state of output buffer is error
#endif
	vpu_memset(&outInfo,0,sizeof(DecOutputInfo));	//clear 0: it is useful for some error case debug
	VPU_TRACE;
	VPU_API("calling vpu_DecGetOutputInfo() \r\n");
	ret = vpu_DecGetOutputInfo(InVpuHandle, &outInfo);
	VPU_API("calling vpu_DecGetOutputInfo(), indexFrameDecoded: %d, return indexFrameDisplay: %d, type: %d, success: 0x%X  \r\n",outInfo.indexFrameDecoded,outInfo.indexFrameDisplay,outInfo.picType,outInfo.decodingSuccess);
	VPU_LOG("fieldSequence: %d, vc1_repeatFrame: %d,interlacedFrame: %d, indexFrameRangemap: %d, progressiveFrame: %d, topFieldFirst: %d \r\n",outInfo.fieldSequence,outInfo.vc1_repeatFrame,outInfo.interlacedFrame,outInfo.indexFrameRangemap,outInfo.progressiveFrame,outInfo.topFieldFirst);
#ifdef IMX6_INTER_DEBUG
{
	static int valid_deccnt=0;
	static int valid_discnt=0;
	if(outInfo.indexFrameDisplay>=0)
	{
		valid_discnt++;
	}
	if(outInfo.indexFrameDecoded>=0)	
	{
		valid_deccnt++;
	}	
	printf("dec cnt: %d , dis cnt: %d \r\n",valid_deccnt,valid_discnt);
}
#endif

#ifdef IMX6_RANGEMAP_WORKAROUND_IGNORE //for iMX6
//if(0==outInfo.indexFrameRangemap) 	//for WVC1_APL1_720x480_30fps_1000kbps_NoAudio_MA10055.WMV: always is zero
	outInfo.indexFrameRangemap=-1;	
#endif

	pObj->pbPacket=outInfo.mp4PackedPBframe;
	if((pObj->pbClips==0) && (0!=pObj->pbPacket)) 
	{
		pObj->pbClips=1;
	}
#ifdef VPU_FILEMODE_SUPPORT_INTERLACED_SKIPMODE	
#if 0	// it is difficult to check since "skipframeMode!=0" will affect the result of "interlacedFrame" and "topFieldFirst"
	if((VPU_V_AVC==pObj->CodecFormat)&&(0!=pObj->initInfo.nInterlace))
	{
		if((1==outInfo.interlacedFrame)&&(0==outInfo.topFieldFirst) &&(outInfo.indexFrameDecoded>=0))
		{
			pObj->mediumFrame=1; //the first field is decoded
		}
		else if ((1==outInfo.interlacedFrame)&&(1==outInfo.topFieldFirst) &&(VPU_OUT_DEC_INDEX_UNDEC==outInfo.indexFrameDecoded))
		{
			pObj->mediumFrame=0; //the second field is decoded
		}
		else
		{
			pObj->mediumFrame=0; //???
		}
	}
#endif
	//FIXME: Now, only support H.264 interlaced in file mode
	if((VPU_V_AVC==pObj->CodecFormat)&&(0!=pObj->initInfo.nInterlace)&&(1==pObj->filemode))		
	{
		if((outInfo.indexFrameDecoded!=VPU_OUT_DEC_INDEX_EOS)&&(0!=InFilemodeChunkSize))
		{
			//one valid field is decoded
			pObj->fieldCnt=(pObj->fieldCnt==0)?1:0;
		}
	}
#endif
	VPU_TRACE;
	*pOutInStreamModeEnough=1;
	if (ret != RETCODE_SUCCESS)
	{
		VPU_LOG("%s:vpu get output info failure: ret=%d \r\n",__FUNCTION__,ret);
		//pObj->state= ???
		return 0;
	}
	else
	{
		int search_ret=1;	//default is no error
		//check err MB
		//VPU_LOG("err MB: %d \r\n",outInfo.numOfErrMBs);

		//VPU_LOG("pic type: %d \r\n",outInfo.picType);		
#ifdef VPU_FLUSH_BEFORE_DEC_WORKAROUND
		//if(outInfo.indexFrameDecoded>=0)	//need to check decindex or prescanresult ???
		{
			pObj->realWork=1;
		}
#endif

#ifdef VPU_PROTECT_MULTI_INSTANCE
#if 1 //for iMX6 rollback
		//if(0)	//for normal mode ??
		if(outInfo.decodingSuccess & 0x10)
		{
			//current frame is not integrated, rollback to frame header
			ASSERT(outInfo.indexFrameDisplay<0);
#else
		if((outInfo.prescanresult==0)&&(pObj->filemode==0))
		{
#endif
			VPU_LOG("not completed frame \r\n");
			//stream mode, and incomplete picture stream
			*pOutRetCode=VPU_DEC_OUTPUT_NODIS;
			/*workaround for decindex==-1: pepsi-p-diddy.mp4
			   we need to carefully when enable: VPU_SUPPORT_NO_ENOUGH_FRAME
			*/
			outInfo.indexFrameDecoded=VPU_OUT_DEC_INDEX_NOMEANING;	//skip below some special process, such as VPU_DEC_NO_ENOUGH_BUF/VPU_DEC_OUTPUT_DROPPED 
			*pOutInStreamModeEnough=0;
		}
		else
#endif		
		{
			if(VPU_V_MJPG==pObj->CodecFormat)
			{				
				//for MJPG, output frame isn't related with indexFrameDisplay
				//1 how to judge error output ??
				if(outInfo.indexFrameDecoded>=0)		//???
				{
					outInfo.indexFrameDisplay=pObj->mjpg_frmidx;
					outInfo.indexFrameDecoded=pObj->mjpg_frmidx;
					VPU_API("MJPG: change index manually: indexFrameDecoded: %d, return indexFrameDisplay: %d \r\n",outInfo.indexFrameDecoded,outInfo.indexFrameDisplay);
				}
				//pObj->mjpg_frmidx=(pObj->mjpg_frmidx+1)%pObj->frameNum;
			}

			//update state
			//pObj->state=VPU_DEC_STATE_OUTOK;

			//set return code
			if(VPU_OUT_DEC_INDEX_EOS==outInfo.indexFrameDecoded)
			{	
				// decode EOS, skip, no enough frame...?
				//pFrameDecode==NULL;
			}
			else if(VPU_OUT_DEC_INDEX_UNDEC==outInfo.indexFrameDecoded)
			{
				// not decoded
				//pFrameDecode==NULL;
			}
			else if(VPU_OUT_DEC_INDEX_UNDEFINE==outInfo.indexFrameDecoded)
			{
				ASSERT(0);
				//pFrameDecode==NULL;
			}
			else
			{
				search_ret=VpuSearchFrameBuf(pObj,outInfo.indexFrameDecoded,&pFrameDecode);
				if(search_ret)
				{
					//backup current decoded frame info
					//VpuSaveDecodedFrameInfo(pObj,outInfo.indexFrameDecoded,&outInfo);
#ifdef VPU_VC1_AP_SKIP_WORKAROUND	
					if((pObj->CodecFormat==VPU_V_VC1_AP)
						&&(VpuConvertPicType(pObj->CodecFormat,outInfo.picType,outInfo.idrFlg)==VPU_SKIP_PIC))
					{
						//ENGR00157397:we should not call VpuSaveDecodedFrameInfo(), avoid no display at the first seconds (mosaic type)
						//don't change the state: 
						//for skip frame, sometimes,may only have two states: free and display ??
						VPU_ERROR("Caution: VC1 AP: SKIP frame, skip setting decode state(it may be in decode/display state) \r\n");
					}
					else
#endif
					{
						int state;
						//backup current decoded frame info
						VpuSaveDecodedFrameInfo(pObj,outInfo.indexFrameDecoded,&outInfo);
						state=VpuGetDispFrameState(outInfo.indexFrameDecoded, pObj->frameBufState);
						if(VPU_FRAME_STATE_FREE!=state)
						{
							//FIX some error clips: WVC1_stress_a0_stress06.wmv
							VPU_ERROR("error: decoded into one unreleased buffer(disp state): don't set decode state, and then it will be skipped !!!\r\n");
							//outInfo.picType=0x24;
						}
						else
						{
							VpuSetDispFrameState(outInfo.indexFrameDecoded, pObj->frameBufState,VPU_FRAME_STATE_DEC);
						}
					}
				}
#ifdef VPU_IFRAME_SEARCH
				{
					//VpuPicType pictype=VpuConvertPicType(pObj->CodecFormat,outInfo.picType);
					//if(FRAME_IS_KEY(pictype))
					{
						if(pObj->keyDecCnt<MIN_KEY_CNT)
						{
							pObj->keyDecCnt++;
						}
					}
				}
#endif				
#ifdef VPU_SEEK_ANYPOINT_WORKAROUND
				//only consider case: (H.264, filemode,non-interlaced)
				if(0==pObj->seekKeyLoc)
				{
					if(pObj->CodecFormat==VPU_V_AVC)
					{
						VPU_LOG("pictype: %d \r\n",outInfo.picType);
						VpuPicType pictype=VpuConvertPicType(pObj->CodecFormat,outInfo.picType,outInfo.idrFlg);
						if((0==pObj->initInfo.nInterlace)&&(1==pObj->filemode))
						{
							if(FRAME_IS_KEY(pictype))
							{
								pObj->seekKeyLoc=1;
								pObj->recommendFlush=0;
							}
							else
							{
								//FIXME: It is dangeous call vpu_DecBitBufferFlush() directly !!!!!!
								//We should return one type to notify componet to call flushfilter() ?????
								//VPU_API("calling vpu_DecBitBufferFlush() : non-key frame seek point \r\n");
								//ret=vpu_DecBitBufferFlush(InVpuHandle);
								//if(RETCODE_SUCCESS!=ret)
								//{
								//	VPU_ERROR("%s: vpu flush bit failure (in while loop), ret=%d \r\n",__FUNCTION__,ret);
								//	//return 0;
								//}	
								pObj->recommendFlush=1;
							}
						}
						else
						{
							//FIXME: It is complex to process interlace !!!
							//FIXME: It is difficult to process stream mode !!! how to ???
						}
					}
				}
#endif
			}

#ifdef VPU_FILEMODE_WORKAROUND
			if(1==VpuSearchFrameBuf(pObj,outInfo.indexFrameDisplay,&pFrameDisp))
			{	
				int state;
				state=VpuGetDispFrameState(outInfo.indexFrameDisplay, pObj->frameBufState);
				if((pObj->CodecFormat==VPU_V_VC1_AP) && (outInfo.indexFrameRangemap>=0))
				{
					state=VpuGetDispFrameState(outInfo.indexFrameRangemap, pObj->frameBufState);
					if(outInfo.indexFrameRangemap!=outInfo.indexFrameDisplay)
					{
						//clear original decode buffer to avoid check error above
						VpuSetDispFrameState(outInfo.indexFrameRangemap,pObj->frameBufState,VPU_FRAME_STATE_FREE);
						//update state for the new disp buffer ??
						//FIXME: still not sure !!!!
						if(state==VPU_FRAME_STATE_DEC)
						{
							if(VPU_FRAME_STATE_FREE==VpuGetDispFrameState(outInfo.indexFrameDisplay, pObj->frameBufState))
							{
								VpuSetDispFrameState(outInfo.indexFrameDisplay,pObj->frameBufState,VPU_FRAME_STATE_DEC );	
							}
							else
							{
								//do nothing ??
								//WVC1_APL1_720x480_30fps_1000kbps_NoAudio_MA10055.WMV
							}
						}
						else
						{
							//output one frame not decoded before, drop it in below checking
						}
					}
				}
				if(VPU_FRAME_STATE_DEC!=state)
				{
					//FIXME: we should set dropped, but not discard it internally. otherwise, the timestamp is not matched!!!!
					VPU_API("%s: calling vpu_DecClrDispFlag(): %d (invalid output) \r\n",__FUNCTION__,outInfo.indexFrameDisplay);				
					ret=vpu_DecClrDispFlag(InVpuHandle,outInfo.indexFrameDisplay);
					ASSERT(RETCODE_SUCCESS==ret);

					if(pObj->CodecFormat==VPU_V_VC1_AP)
					{
						//Test_1440x576_WVC1_6Mbps.wmv: skip frame, already is set to disp state ??
						//do nothing, only output error log
						VPU_ERROR("error: output one frame not decoded at all !!!!!(may be in disp/free state) \r\n");
					}
					else if((pObj->CodecFormat==VPU_V_MPEG4)
						||(pObj->CodecFormat==VPU_V_DIVX4)
						||(pObj->CodecFormat==VPU_V_DIVX56)
						||(pObj->CodecFormat==VPU_V_XVID)
						||(pObj->CodecFormat==VPU_V_H263))
					{
						//now, we can't call vpu_DecBitBufferFlush, so the frame buffers may be not cleared enough at flush step 
						outInfo.indexFrameDisplay=VPU_OUT_DIS_INDEX_NODIS;
						VPU_ERROR("error:  output one frame not decoded at all !!!!!, we will discard it !!!!! \r\n");
						disOrderOutput=1;
					}
					else
					{
						//fields + skipmode: buffer may be disorder: ch100-mpeg4sd-dec.ts
						//discarding the frame is useful when enable VPU_FILEMODE_SUPPORT_INTERLACED_SKIPMODE
						outInfo.indexFrameDisplay=VPU_OUT_DIS_INDEX_NODIS;
						VPU_ERROR("error: output one frame not decoded at all !!!!! \r\n");
						disOrderOutput=1;
					}
				}
			}
#endif

			if(VPU_OUT_DIS_INDEX_NODIS==outInfo.indexFrameDisplay)
			{	
				// not display
				pFrameDisp=NULL;
				*pOutRetCode=VPU_DEC_OUTPUT_NODIS;
			}
			else if(VPU_OUT_DIS_INDEX_NODIS_SKIP==outInfo.indexFrameDisplay)
			{
				// not display, related to skip option
				pFrameDisp=NULL;
				*pOutRetCode=VPU_DEC_OUTPUT_NODIS;
			}
			else if(VPU_OUT_DIS_INDEX_EOS==outInfo.indexFrameDisplay)
			{
				// stream EOS: no more ouput for display
				pFrameDisp=NULL;
				*pOutRetCode=VPU_DEC_OUTPUT_EOS;
			}
			else
			{
				// normal for display
#ifdef VPU_SUPPORT_UNCLOSED_GOP
				int dropFlag=0;
#endif
				VpuPicType picType;
				int index=outInfo.indexFrameDisplay;
				*pOutRetCode=VPU_DEC_OUTPUT_DIS;
				VPU_LOG("indexFrameDisplay=%d \r\n",index);
				search_ret=VpuSearchFrameBuf(pObj,index,&pFrameDisp);
				if(search_ret)
				{				
					pObj->frameInfo.pDisplayFrameBuf=pFrameDisp;
					//load current display frame info
					ASSERT(pObj->frameBufInfo[index].viewID==outInfo.mvcPicInfo.viewIdxDisplay);
					VpuLoadDispFrameInfo(pObj,index,&pObj->frameInfo);
				}
				picType=pObj->frameInfo.ePicType;
				if((pObj->CodecFormat==VPU_V_VC1_AP) && (outInfo.indexFrameRangemap>=0))
				{
					picType=VpuConvertPicType(pObj->CodecFormat,pObj->frameBufInfo[outInfo.indexFrameRangemap].picType,pObj->frameBufInfo[outInfo.indexFrameRangemap].idrFlag);
				}

#ifdef VPU_SUPPORT_UNCLOSED_GOP
				//(1) check drop B frame case
				if(FRAME_IS_REF(picType))
				{
					VPU_LOG("%s: Ref frame %d \r\n",__FUNCTION__,picType);
					if(pObj->refCnt<MIN_REF_CNT)
					{
						pObj->refCnt++;
					}
				}

				if(FRAME_IS_B(picType)&&(pObj->refCnt<MIN_REF_CNT))
				{
					//drop B frame
					dropFlag=1;
					pObj->dropBCnt++;
					VPU_LOG("%s: change B frame to dropped frame : %d \r\n",__FUNCTION__,pObj->dropBCnt);
					if(pObj->dropBCnt>=MAX_DROPB_CNT)
					{
						//avoid freeze for real closed gop (I B B B B ... B B)
						pObj->dropBCnt=0;	//clear cnt to 0	
						pObj->refCnt=MIN_REF_CNT;
					}
				}

				//(2) check drop non-I frame case
#if 0			//FIXME: for some .ts clips, I Field + P Field, no KEY frame return, So we have to loose the condition
				if(FRAME_IS_KEY(picType))
#else			
				if(((1==pObj->initInfo.nInterlace)&&FRAME_IS_REF(picType))
					|| ((0==pObj->initInfo.nInterlace)&&FRAME_IS_KEY(picType)))
				/*if(FRAME_IS_REF(picType))*/
#endif					
				{
					VPU_LOG("%s: Key frame %d \r\n",__FUNCTION__,picType);
					if(pObj->keyCnt<MIN_KEY_CNT)
					{
						pObj->keyCnt++;
					}
				}

				if(FRAME_ISNOT_KEY(picType)&&(pObj->keyCnt<MIN_KEY_CNT))
				{
					//drop non-I frame
					//here: we have not max drop number, not like drop B 
					dropFlag=1;
					VPU_LOG("%s: change non-I frame to dropped frame : \r\n",__FUNCTION__);
				}
#ifdef DIS_DROP_FRAME								
				if(1==dropFlag)
				{
					VPU_LOG("unclosed gop: set it mosaic \r\n");
					*pOutRetCode=VPU_DEC_OUTPUT_MOSAIC_DIS;
				}
#else			//to avoid seek timeout 
				if(1==dropFlag)
				{
					//drop current frame
					pFrameDisp=NULL;				
					//add one type ( dropped ), but not VPU_DEC_OUTPUT_NODIS, since we need to notify user get one timestamp !!!
					//*pOutRetCode=VPU_DEC_OUTPUT_NODIS;
					*pOutRetCode=VPU_DEC_OUTPUT_DROPPED;

					//we need to clear current frame, since user will not fetch/clear the frame buffer
					VPU_API("%s: calling vpu_DecClrDispFlag(): %d \r\n",__FUNCTION__,outInfo.indexFrameDisplay);				
					ret=vpu_DecClrDispFlag(InVpuHandle,outInfo.indexFrameDisplay);
					if(RETCODE_SUCCESS!=ret)
					{
						VPU_ERROR("%s: vpu clear display frame failure, index=0x%X, ret=%d \r\n",__FUNCTION__,outInfo.indexFrameDisplay,ret);
						search_ret=0; //return fail
					}
				}
				else
#endif
#endif
				{
//#ifdef VPU_VC1_AP_SKIP_WORKAROUND	
					int disIndex=outInfo.indexFrameDisplay;
					//for case: display order: I B P(skip) 
					//FIXME: we only simply drop P(skip) to avoid conflict logic
					//if(pObj->CodecFormat==VPU_V_VC1_AP)
					{
						int state;
						//if((pObj->CodecFormat==VPU_V_VC1_AP) && (-1!=outInfo.indexFrameRangemap))
						//{
						//	disIndex=outInfo.indexFrameRangemap;
						//}
						state=VpuGetDispFrameState(disIndex, pObj->frameBufState);
						if(VPU_FRAME_STATE_DISP==state)	//still not released by user
						{
							//pFrameDisp=???
							*pOutRetCode=VPU_DEC_OUTPUT_REPEAT;
							VPU_ERROR("error: not released by user, drop this repeated frame \r\n");
						}
					}
//#endif
					//update frame state
					VpuSetDispFrameState(disIndex,pObj->frameBufState,VPU_FRAME_STATE_DISP);

					//check repeated frame (mainly for VC1 ?)

					if((pFrameDisp) && (pObj->pPreDisplayFrameBuf==pFrameDisp))
					{
						*pOutRetCode=VPU_DEC_OUTPUT_REPEAT;
						//previous frame(same address) will be cleared
						//so we needn't clear current frame, not like VPU_DEC_OUTPUT_DROPPED
					}

					//record historical info
					pObj->pPreDisplayFrameBuf=pFrameDisp;
					//pObj->pPreDecodedFrameBuf=pFrameDecode;
				}
			}	
		}

		//update state
		if((*pOutRetCode==VPU_DEC_OUTPUT_DIS)||(*pOutRetCode==VPU_DEC_OUTPUT_MOSAIC_DIS))
		{
			TIMER_MARK(TIMER_MARK_GETOUTPUT_ID);
			pObj->state=VPU_DEC_STATE_OUTOK;	// user should call get output
#ifdef VPU_WRAPPER_DUMP
			{
				int colorformat=0;
				if(VPU_V_MJPG==pObj->CodecFormat)
				{
					colorformat=pObj->initInfo.nMjpgSourceFormat;
				}
				WrapperFileDumpYUV(&fpYUV,pObj->frameInfo.pDisplayFrameBuf->pbufVirtY,
					pObj->frameInfo.pDisplayFrameBuf->pbufVirtCb,
					pObj->frameInfo.pDisplayFrameBuf->pbufVirtCr,
					pObj->picWidth*pObj->picHeight,pObj->picWidth*pObj->picHeight/4,colorformat);
			}			
#endif
#if 1	//fix (-2,>0) case: 
		//(1) for interlace/corrupt case: there is one valid output. eg. user will get two time stamps
		//(2) for not codec case: [P B] chunk + not coded: only need to get one time stamps
			if(0!=InSkipMode)
			{
				if(outInfo.indexFrameDecoded==VPU_OUT_DEC_INDEX_UNDEC/*eg. ==-2 ???*/)
				{
					if(1==pObj->frameBufInfo[outInfo.indexFrameDisplay].pFrameInPBPacket)	//needn't check range map(for VC1AP) since PB only occur in MPEG4
					{
						VPU_LOG("get only one timestamp for not coded \r\n");
					}
					else
					{
#ifdef VPU_FILEMODE_INTERLACE_TIMESTAMP_ENHANCE
						if(pObj->fieldDecoding)
						{
							pObj->oweFieldTS++;
						}
						else
						{
							*pOutRetCode=(*pOutRetCode)|VPU_DEC_SKIP;							
						}
#else
						*pOutRetCode=(*pOutRetCode)|VPU_DEC_SKIP;
#endif
						//needn't clear it , only notify user to get one timestamp						
					}
				}
			}
#endif
		}
		else if (*pOutRetCode==VPU_DEC_OUTPUT_EOS)
		{
			pObj->state=VPU_DEC_STATE_EOS;	// user should feed valid data for next play
		}
		else	
		{
			pObj->state=VPU_DEC_STATE_DEC;	//user need not call get output again
#ifdef VPU_IFRAME_SEARCH
			//we suppose: display index is also unvalid when decode index is unvalid, eg. when skipping, (decIndex<0 && disIndex<0)
			if((pObj->keyDecCnt==0)
				&& (outInfo.indexFrameDecoded==VPU_OUT_DEC_INDEX_UNDEC/*eg. ==-2 ???*/)
				&& (outInfo.indexFrameDisplay<0))
			{
				//FIXME: need to check VPU_FILEMODE_INTERLACE_TIMESTAMP_ENHANCE ???
				*pOutRetCode=VPU_DEC_OUTPUT_DROPPED;
				//needn't clear it , only notify user to get one timestamp
			}
#endif			

#if 1 //skip mode ??
			if(0!=InSkipMode)
			{
#if 1 //for iMX6
				if((outInfo.indexFrameDecoded==VPU_OUT_DEC_INDEX_UNDEC/*eg. ==-2 ???*/)
					&& (outInfo.indexFrameDisplay==VPU_OUT_DIS_INDEX_NODIS_SKIP/*eg. ==-2 ??*/))
#else
				if((outInfo.indexFrameDecoded==VPU_OUT_DEC_INDEX_UNDEC/*eg. ==-2 ???*/)
					&& (outInfo.indexFrameDisplay==VPU_OUT_DIS_INDEX_NODIS/*eg. ==-3 ??*/))
#endif					
				{
#ifdef VPU_FILEMODE_INTERLACE_TIMESTAMP_ENHANCE
					if(1==pObj->fieldDecoding)
					{
						pObj->oweFieldTS++;
					}
					else
					{
						*pOutRetCode=VPU_DEC_OUTPUT_DROPPED;					
					}
#else
					*pOutRetCode=VPU_DEC_OUTPUT_DROPPED;
#endif
					//needn't clear it , only notify user to get one timestamp
				}
			}
#endif
#ifdef VPU_FILEMODE_WORKAROUND
			if(1==disOrderOutput)
			{
				//notify user to get one timestamp
				*pOutRetCode=VPU_DEC_OUTPUT_DROPPED;
			}
#endif
		}

#ifdef VPU_FILEMODE_INTERLACE_TIMESTAMP_ENHANCE
		if((1==pObj->fieldDecoding)&&(pObj->oweFieldTS>0))
		{
			if((*pOutRetCode==VPU_DEC_OUTPUT_DIS)||
				(*pOutRetCode==VPU_DEC_OUTPUT_MOSAIC_DIS)||
				(*pOutRetCode==VPU_DEC_OUTPUT_DROPPED))
			{
				*pOutRetCode=(*pOutRetCode)|VPU_DEC_SKIP;
				pObj->oweFieldTS--;
			}
		}
#endif


#ifdef VPU_SUPPORT_NO_ENOUGH_FRAME
		if(outInfo.indexFrameDecoded==VPU_OUT_DEC_INDEX_EOS)
		{
			//no enough frame buffer and return one output frame
			*pOutRetCode=(*pOutRetCode)|VPU_DEC_NO_ENOUGH_BUF;
		}
#endif

#ifdef VPU_SEEK_ANYPOINT_WORKAROUND
		if(1==pObj->recommendFlush)
		{
			*pOutRetCode=(*pOutRetCode)|VPU_DEC_FLUSH;
		}
#endif

		return search_ret;		//0 or 1
	}	
}

int VpuBitFlush(VpuDecHandleInternal * pVpuObj, int location)
{
	RetCode ret;		
	int flush=0;

	if(0==pVpuObj->obj.filemode)		
	{
		flush=1;
#if 1//for iMX6		
		switch (pVpuObj->obj.CodecFormat)
		{
			////case VPU_V_H263:
			case VPU_V_RV: // for RV9_1080x720_30_9590_NoAudio.mkv
			//case VPU_V_VC1:	// for WMV9_MPML_360x240_30fps_1500K_4sec_10min.wmv
			//case VPU_V_VC1_AP:	// for Test_1440x576_WVC1_6Mbps.wmv
				flush=0;	
				break;
			default:
				//flush=0;
				break;
		}
#endif		
	}
#ifdef VPU_FILEMODE_WORKAROUND
	else
	{
		switch (pVpuObj->obj.CodecFormat)
		{
			case VPU_V_MPEG4:
			case VPU_V_DIVX4:
			case VPU_V_DIVX56:
			case VPU_V_XVID:
			case VPU_V_H263:
				//flush=1;	//seek failed for clips: H.263_mp3_352x288.avi;10s-vga-senchiro.avi
				break;			
			case VPU_V_AVC:
				//flush=1;	//mosaic issue: DongxieXidu.ultra.mkv(e 0 80, or e 0 3), --now it is fixed, so we still can set flush=1
				flush=1;		//technicolor/h264.mp4
#ifdef VPU_SEEK_ANYPOINT_WORKAROUND
				flush=1;		//philips: zdfhd.ts
#endif
				break;
			case VPU_V_MPEG2:
				flush=1;
				break;
			default:
				//
				break;
		}
	}
#endif

	if(1==flush)
	{	
#ifdef IMX6_INTER_DEBUG_RD_WR
{
	unsigned int rd,wr;
//#define BIT_RD_PTR_0			0x120
//#define BIT_WR_PTR_0			0x124
	IOClkGateSet(1);
	rd=VpuReadReg(0x120);
	wr=VpuReadReg(0x124);
	IOClkGateSet(0);
	printf("vpu register: wr: 0x%X, rd: 0x%X \r\n", wr, rd);
}
#endif
	
		//we skip bit bufferflush operation for file mode
		VPU_API("calling vpu_DecBitBufferFlush() : %d \r\n",location);
		ret=vpu_DecBitBufferFlush(pVpuObj->handle);
		if(RETCODE_SUCCESS!=ret)
		{
			VPU_ERROR("%s: vpu flush bit failure (in while loop), ret=%d \r\n",__FUNCTION__,ret);
			return 0;
		}	
	}	
	return 1;
}

int VpuDecClearOperationEOStoDEC(VpuDecHandle InHandle)
{
	VpuDecHandleInternal * pVpuObj;
		
	if(InHandle==NULL) 
	{
		return 0;
	}
	pVpuObj=(VpuDecHandleInternal *)InHandle;

#if 0 //for iMX6
	if(pVpuObj->obj.CodecFormat==VPU_V_H263)
	{
		return 1;
	}
#endif
	//FIXED: wrapper should not clear buffer itself, user should be responsible to clear it !!!otherwise, vpu may overwrite one output frame which still is hold by user.
	//clear frame whose clear operations are missing by user
	//VpuFreeAllDispFrame(pVpuObj->handle, pVpuObj->obj.frameNum, pVpuObj->obj.frameBufState);

#ifdef VPU_FILEMODE_WORKAROUND
	VpuClearAllDispFrameFlag(pVpuObj->handle, pVpuObj->obj.frameNum);
#endif

	//!!!: In fact, vpu will auto clear all buffers at vpu_DecBitBufferFlush() !!!
	//So, user need to add additional logic(at user end if user care this case) to make protection
	VpuClearAllDispFrame(pVpuObj->obj.frameNum, pVpuObj->obj.frameBufState);

	//reset historical info
	//pVpuObj->obj.pPreDisplayFrameBuf=NULL;

	//if(1==pVpuObj->obj.filemode)
	//{
	//	//FIXED seek issue: 320x240-44-16s.avi (can not seek to 0 or 10 seconds)
	//	return 1;
	//}


	//we skip bit bufferflush operation for file mode	
	// In EOS, the bistream buffer may become disorder, such as RD pointer will overflow WR pointer
	// So, we must flush bitstream here again, otherwise following data may not be filled since buffer is full (may have only 511 bytes).
	if (0==VpuBitFlush(pVpuObj,2/*at the eos*/))
	{
		return 0;
	}

#ifdef IMX6_SKIPMODE_WORKAROUND_FILL_DUMMY//for iMX6 testing
{
#define DUMY_LEN	512
	unsigned char* tmp=NULL;
	switch(pVpuObj->obj.CodecFormat)
	{
		case VPU_V_MPEG4:
		//case VPU_V_DIVX3:
		case VPU_V_DIVX4:
		case VPU_V_DIVX56:
		case VPU_V_XVID:
		case VPU_V_H263:
		case VPU_V_AVC:
		//case VPU_V_VC1:	
		case VPU_V_VC1_AP:
		case VPU_V_MPEG2:
		//case VPU_V_RV:
			tmp=malloc(DUMY_LEN);	
			if(tmp)
			{
				memset(tmp,0,DUMY_LEN);
				VpuFillData(pVpuObj->handle, &pVpuObj->obj, tmp, DUMY_LEN, 1, 0);
				free(tmp);		
			}
			else
			{
				VPU_ERROR("LEVEL: 1: malloc %d bytes failure \r\n",DUMY_LEN);
			}
			break;
		default:
			break;
	}
}
#endif
	
	return 1;

}

#if 1 //1  for iMX6   : stream mode
int VpuWaitBusy(int needWait)
{
	static int busy_cnt=0;
#if 0	//for rollback mode, we should not return 0(busy)
	int ret=0;
	if(needWait)
	{	
		VPU_API("while: calling vpu_WaitForInt(%d) \r\n",VPU_POLLING_TIME_OUT);
		ret=vpu_WaitForInt(VPU_POLLING_TIME_OUT);
	}
	else
	{
		VPU_API("while: calling vpu_WaitForInt(%d) \r\n",VPU_POLLING_MIN_TIME_OUT);
		ret=vpu_WaitForInt(VPU_POLLING_MIN_TIME_OUT);	// polling
	}
	if(ret!=0)
	{
		busy_cnt++;
		if(busy_cnt> VPU_MAX_POLLING_BUSY_CNT)
		{
			VPU_ERROR("wait busy : time out : count: %d \r\n",busy_cnt);
			busy_cnt=0;		//need to clear it ??
			return -1;             //time out for some corrupt clips
		}
		return 0;	//busy
	}
	busy_cnt=0;
#else
	busy_cnt=0;
	VPU_API("while: calling vpu_WaitForInt(%d) \r\n",VPU_POLLING_PRESCAN_TIME_OUT);
	while(0!=vpu_WaitForInt(VPU_POLLING_PRESCAN_TIME_OUT))
	{
		busy_cnt++;
		if(busy_cnt> VPU_MAX_POLLING_PRESCAN_BUSY_CNT)
		{
			VPU_ERROR("while: wait busy : time out : count: %d \r\n",busy_cnt);
			return -1;             //time out for some corrupt clips
		}
	}
#endif
	return 1;	//not busy
}	
#else
int VpuWaitBusy(int needWait)
{
	static int busy_cnt=0;

#ifdef VPU_PROTECT_MULTI_INSTANCE	
	busy_cnt=0;
	VPU_API("while: calling vpu_WaitForInt(%d) \r\n",VPU_POLLING_PRESCAN_TIME_OUT);
	while(0!=vpu_WaitForInt(VPU_POLLING_PRESCAN_TIME_OUT))
	{
		busy_cnt++;
		if(busy_cnt> VPU_MAX_POLLING_PRESCAN_BUSY_CNT)
		{
			VPU_ERROR("while: wait busy : time out : count: %d \r\n",busy_cnt);
			return -1;             //time out for some corrupt clips
		}
	}
#else //#ifdef VPU_PROTECT_MULTI_INSTANCE
	VPU_API("calling vpu_IsBusy() \r\n");
	if(vpu_IsBusy())
	{
		if(needWait)
		{
			VPU_API("busy: calling vpu_WaitForInt(%d) \r\n",VPU_POLLING_TIME_OUT);
			if(0!=vpu_WaitForInt(VPU_POLLING_TIME_OUT))
			{
				busy_cnt++;
				if(busy_cnt> VPU_MAX_POLLING_BUSY_CNT)
				{
					VPU_ERROR("wait busy : time out : count: %d \r\n",busy_cnt);
					busy_cnt=0;		//need to clear it ??
					return -1;             //time out for some corrupt clips
				}
				return 0;
			}
			else
			{
				busy_cnt=0;
			}
		}
		else
		{
			return 0;
		}
	}
	else
	{
		busy_cnt=0;
		VPU_API("not busy: calling vpu_WaitForInt(%d) \r\n",VPU_POLLING_TIME_OUT);
		vpu_WaitForInt(VPU_POLLING_MIN_TIME_OUT); //supposed return immediately
	}
#endif	//#ifdef VPU_PROTECT_MULTI_INSTANCE
	return 1;
}
#endif

int VpuDecBuf(DecHandle InVpuHandle, VpuDecObj* pObj ,VpuBufferNode* pInData,VpuDecBufRetCode* pOutRetCode,int* pNoErr,int* pOutInStreamModeEnough) 
{
	RetCode ret;
	VpuDecBufRetCode bufUseState=VPU_DEC_INPUT_USED;
	DecParam decParam;
	unsigned char* pHeader=NULL;
	unsigned int headerLen=0;

	unsigned char aVC1Head[VC1_MAX_FRM_HEADER_SIZE];
	unsigned char aVP8Head[VP8_FRM_HEADER_SIZE];
	int busyState;
	int needWait;
	static int skipframeMode=0;


	*pOutRetCode=(VpuDecBufRetCode)0x0;
	*pNoErr=1;	// set OK

	//VPU_LOG("%s: pObj->state: %d \r\n",__FUNCTION__,pObj->state);

	switch(pObj->CodecFormat)
	{
		case VPU_V_VC1:
		case VPU_V_VC1_AP:
			//for VC1, special header info may need to be inserted
			pHeader=aVC1Head;
			if(0==pObj->nPrivateSeqHeaderInserted)
			{
				//for raw file : .rcv/.vc1
				//do nothing
			}
			else if (pInData->nSize==0) //(NULL==pInData->pVirAddr)
			{
				//eos
			}
			else
			{
				//insert frame header
				if((pObj->CodecFormat==VPU_V_VC1_AP))
				{
					VC1CreateNalFrameHeader(pHeader,(int*)(&headerLen),(unsigned int*)(pInData->pVirAddr));
				}
				else
				{
					//need to insert header : frame size
					VC1CreateRCVFrameHeader(pHeader,(int*)(&headerLen),pInData->nSize);
				}	
			}
			break;
		case VPU_V_VP8:
			//for VP8, special header info may need to be inserted
			pHeader=aVP8Head;
			if(0==pObj->nPrivateSeqHeaderInserted)
			{
				//for raw file 
				//do nothing
			}
			else if (pInData->nSize==0) //(NULL==pInData->pVirAddr)
			{
				//eos
			}
			else
			{
				//insert frame header
				//need to insert header : frame size
				VP8CreateFrameHeader(pHeader,(int*)(&headerLen),pInData->nSize,0,0);
			}
			break;
		case  VPU_V_MJPG:
			//for MJPG, need to user appoint the output frame
			if(0==VpuSearchFreeFrameBuf(pObj, &pObj->mjpg_frmidx))
			{
				//no frame buffer
				*pOutRetCode=VPU_DEC_OUTPUT_NODIS|VPU_DEC_NO_ENOUGH_BUF;
				return 1;
			}
			break;
		default:
			//do nothing for other formats
			break;
	}

	if(0==pObj->filemode)
	{
		//check free space and fill data into vpu
		if(0==VpuBitsBufIsEnough(InVpuHandle,headerLen+pInData->nSize))
		{
			//buffer is full	
			bufUseState=VPU_DEC_INPUT_NOT_USED;	
		}
		else
		{
			//it is enough to fill
			int fill_ret;
			if(0!=headerLen)
			{
				fill_ret=VpuFillData(InVpuHandle,pObj,pHeader,headerLen,1,0);
			}

			//allow pInData->nSize==0 for EOS
			fill_ret=VpuFillData(InVpuHandle,pObj,pInData->pVirAddr,pInData->nSize,1,headerLen);
			ASSERT(fill_ret==1);
		}
	}
	else
	{
		//file mode: only fill data at decode state
		bufUseState=VPU_DEC_INPUT_NOT_USED;	
	}


	if(VPU_DEC_STATE_DEC==pObj->state)
	{
#ifdef VPU_PROTECT_MULTI_INSTANCE //#ifdef VPU_DEC_PIPELINE
		if((0==pObj->filemode))
		{
			//only for stream mode
			//when prescan is enabled, we need to make sure enough possible data before calling vpu_DecStartOneFrame() to improve performance.
			PhysicalAddress Rd;
			PhysicalAddress Wr;
			unsigned long nSpace;
			VPU_TRACE;
			VPU_API("calling vpu_DecGetBitstreamBuffer() \r\n");
			vpu_DecGetBitstreamBuffer(InVpuHandle, &Rd, &Wr, &nSpace);
			//case ((VPU_BITS_BUF_SIZE-nSpace)>=VPU_MIN_UINT_SIZE): avoid wait timeout
			//case (0!=pInData->nSize): avoid delay including no normal eos output at end of stream.
			//case (0==pObj->filledEOS): avoid no normal eos output
			if((((VPU_BITS_BUF_SIZE-nSpace)<VPU_MIN_UINT_SIZE)&&(0==pObj->filledEOS))
				||((NotEnoughDecData(nSpace))&&(0!=pInData->nSize)))
			{
				//return directly without decoding
				VPU_LOG("nSpace: %d, filled : %d \r\n",(int)nSpace, (int)(VPU_BITS_BUF_SIZE-nSpace));
				*pOutRetCode=bufUseState;				
				return 0;
			}	
			VPU_LOG("nSpace: %d, filled : %d \r\n",(int)nSpace, (int)(VPU_BITS_BUF_SIZE-nSpace));
		}
#endif

		//set dec parameters
		//clear 0 firstly	
		vpu_memset(&decParam,0,sizeof(DecParam));
		decParam.skipframeMode=pObj->skipFrameMode;
		decParam.skipframeNum=pObj->skipFrameNum;
		decParam.iframeSearchEnable=pObj->iframeSearchEnable;
		//VPU_LOG("before start one frame, skip mode: %d, num: %d, isearch: %d \r\n",pObj->skipFrameMode,pObj->skipFrameNum,pObj->iframeSearchEnable);
#ifdef VPU_IFRAME_SEARCH
		if(0==pObj->keyDecCnt)
		{
			if(1)	//we need to consider timestamp, so we enable skipframeMode, but not iframeSearchEnable
			{
				decParam.iframeSearchEnable=0;
				decParam.skipframeMode=1;	//skip non-I frame
				decParam.skipframeNum=1;		//only skip one non-I frame every time
			}
			else
			{
				decParam.iframeSearchEnable=1;
				decParam.skipframeMode=0;
				decParam.skipframeNum=0;		//skip all P/B frames until next I(IDR for H.264)
			}
		}
#if 1 //for iMX6		
		switch (pObj->CodecFormat)
		{
			case VPU_V_H263: // for H263_BP3_352x288_25_AACLC_48Khz_190kbps_pixar-ice_age_extra.avi
			//case VPU_V_VC1:	//for WMV9_MPML_360x240_30fps_1500K_4sec_10min.wmv
			//case VPU_V_VC1_AP:  // for WVC1_APL1_720x480_30fps_1000kbps_NoAudio_MA10055.WMV: the first frame is I/P field pair
				decParam.iframeSearchEnable=0;
				decParam.skipframeMode=0;
				decParam.skipframeNum=0;	
				break;
			default:
				break;
		}	
#endif		
#endif

#ifdef VPU_DEC_DIRECT_INPUT
		decParam.chunkSize=...
		decParam.picStreamBufferAddr=...
		decParam.picStartByteOffset=...
#endif
		if(1==pObj->filemode)
		{
			//decParam.picStreamBufferAddr=	// don't need to set it, since we don't enable dynamic buffer allocation.
			if(1==pObj->firstData)
			{
				//re-use data at seqinit step
				decParam.chunkSize=pObj->firstDataSize;
				pObj->firstData=0;	//need not set it again later, since we only do seqinit once.
				bufUseState=VPU_DEC_INPUT_NOT_USED;	
#ifdef VPU_FILEMODE_MERGE_INTERLACE_DEBUG
				//just filled the first field, record the offset and return directly
				pObj->lastFieldOffset=decParam.chunkSize;
				*pOutRetCode=VPU_DEC_INPUT_NOT_USED|VPU_DEC_SKIP;
				return 1;
#endif				
			}
#ifdef VPU_SUPPORT_NO_ENOUGH_FRAME
			else if (1==pObj->dataUsedInFileMode)
			{
				decParam.chunkSize=pObj->lastDatLenInFileMode;
				decParam.skipframeMode=pObj->lastConfigMode;
				bufUseState=VPU_DEC_INPUT_NOT_USED;	//use last data
			}
#endif
#ifdef VPU_FILEMODE_MERGE_INTERLACE_DEBUG
			else if((0!=pObj->needMergeFields)&&(0!=pObj->lastFieldOffset))
			{
				// feed the second field
				int fill_ret;
				fill_ret=VpuFillData(InVpuHandle,pObj,pInData->pVirAddr,pInData->nSize,1,pObj->lastFieldOffset);
				ASSERT(fill_ret==1);	//always enough to fill data
				decParam.chunkSize=pObj->lastFieldOffset+pInData->nSize;
				pObj->lastFieldOffset=0;	//clear 0
				bufUseState=VPU_DEC_INPUT_USED;	
			}
#endif
			else
			{			
				//Here, we need to check headerLen !
				int fill_ret;
				if(0!=headerLen)
				{
					fill_ret=VpuFillData(InVpuHandle,pObj,pHeader,headerLen,1,0);
				}
				fill_ret=VpuFillData(InVpuHandle,pObj,pInData->pVirAddr,pInData->nSize,1,headerLen);
				ASSERT(fill_ret==1);	//always enough to fill data
				decParam.chunkSize=pInData->nSize+headerLen;
				bufUseState=VPU_DEC_INPUT_USED;	
#ifdef VPU_FILEMODE_MERGE_INTERLACE_DEBUG
				//just filled the first field, record the offset and return directly
				pObj->lastFieldOffset=decParam.chunkSize;
				*pOutRetCode=VPU_DEC_INPUT_USED|VPU_DEC_SKIP;
				return 1;
#endif
			}

#ifdef VPU_FILEMODE_WORKAROUND
			//IOClkGateSet(true);
			//VpuWriteReg(0x124, pObj->pBsBufPhyStart+decParam.chunkSize);	//how to get instance number ?
			//IOClkGateSet(false);			
#endif
#if 0		// can not enable this check !!!
			if(0==decParam.chunkSize)
			{
				*pOutRetCode=(*pOutRetCode)|bufUseState;
				return 1;
			}
#else
			//Here: decParam.chunkSize==0 is allowed and required ???.
#endif			
			VPU_LOG("file mode: data size: %d \r\n",decParam.chunkSize);
#if 0 // file mode debug
			{
				static FILE* fpBitstream=NULL;
				LOG_PRINTF("0x%X \r\n",*((unsigned int*)pObj->pBsBufVirtStart));
				WrapperFileDumpBitstrem(&fpBitstream,(unsigned char*)pObj->pBsBufVirtStart,decParam.chunkSize);
			}			
#endif

#ifdef VPU_FILEMODE_SUPPORT_INTERLACED_SKIPMODE
			if(0!=pObj->initInfo.nInterlace)
			{
				if(pObj->fieldCnt==1)
				{
					decParam.skipframeMode=pObj->firstFrameMode;	//will decode the second field
				}
				else
				{
					pObj->firstFrameMode=decParam.skipframeMode;	//will decode the first field
				}
				////if(VPU_V_AVC==pObj->CodecFormat)	//FIXME: now only process H.264 !!!
				//if(VPU_V_MPEG2!=pObj->CodecFormat)	//mpeg2: two fields will be mergee into one frame
				//{
				//	if(decParam.chunkSize>0)
				//	{
				//		pObj->fieldCnt=(pObj->fieldCnt==0)?1:0;
				//	}
				//}
			}
#else
#ifdef VPU_FILEMODE_INTERLACE_WORKAROUND
			if(0!=pObj->initInfo.nInterlace)
			{
				//for interlace, it is unstable if enable skipmode: FHD025.Sanyo(1min).1920x1080.MPEG2.AAC.1536Kbps.48KHz.mp4
#ifndef VPU_IFRAME_SEARCH
				decParam.skipframeMode=0;
#endif
			}
#endif
#endif
			if(pObj->pbClips==1)
			{
				//for pb chunk + file mode(stream mode seem be OK): both PB chunk and not_coded frame may be affected by skipmode.
				//So, disable skip mode.
				decParam.skipframeMode=0;
			}
			
		}
		else
		{
			//stream mode
#ifdef VPU_PROTECT_MULTI_INSTANCE
			decParam.prescanEnable=0; //1 for iMX6x
			decParam.prescanMode=0; //0 = Start decoding, 1 = Returns without decoding
#endif
		}

		if(VPU_V_MJPG==pObj->CodecFormat)
		{
			//for mjpg, we shouldn't set skipmode???
			decParam.skipframeMode=0;
			//for MJPG, need to user appoint the output frame
			VPU_API("vpu_DecGiveCommand: SET_ROTATOR_OUTPUT: %d \r\n",pObj->mjpg_frmidx);
			vpu_DecGiveCommand(InVpuHandle, SET_ROTATOR_OUTPUT, (void *)(&pObj->vpu_regframebuf[pObj->mjpg_frmidx]));
#if 1 //for iMX6
			if(pObj->mjpg_linebuffmode==1)
			{
				decParam.phyJpgChunkBase=(PhysicalAddress)pObj->pBsBufPhyStart;
				decParam.virtJpgChunkBase=pObj->pBsBufVirtStart;
				//decParam.chunkSize=;  //already been set 
			}
#endif
		}

		// start decode frame
		VPU_LOG("===================vpu_DecStartOneFrame: chunkSize: %d, search: %d \r\n",decParam.chunkSize,decParam.iframeSearchEnable);		
		VPU_TRACE;
		VPU_API("calling vpu_DecStartOneFrame(): %d, skipmode: %d \r\n",decParam.chunkSize,decParam.skipframeMode);
		ret = vpu_DecStartOneFrame(InVpuHandle, &decParam);
		VPU_TRACE;
		if (ret != RETCODE_SUCCESS)
		{
			*pOutRetCode=bufUseState;
			*pNoErr=0;
			return 0;
		}

		//update state
		pObj->state=VPU_DEC_STATE_STARTFRAMEOK;

		skipframeMode=decParam.skipframeMode;
#ifdef VPU_FILEMODE_INTERLACE_WORKAROUND
		if((0==skipframeMode)&&(0!=pObj->initInfo.nInterlace)&&(1==pObj->filemode)&&(0!=decParam.chunkSize))
		{
			//for interlace clips: 
			//if user feed two fields seperately, we should drop the second field and get one timestamp
			//clips: FHD025.Sanyo(1min).1920x1080.MPEG2.AAC.1536Kbps.48KHz.mp4
			skipframeMode=1;
		}
		else
		{
			//here: we only consider filemode, for streammode, the timestamp will be not matched and timestamp queue may overflow.
		}
#endif
#ifdef VPU_FILEMODE_CORRUPT_WORKAROUND
		if((0==skipframeMode)&&(1==pObj->filemode)&&(0!=decParam.chunkSize))
		{
			//for corrupt clips: 
			//if user feed one valid frame(size!=0), and vpu may return decIndex,disIndex=(-2,-3), in this case, we should drop it and get one timestamp
			//clips: h264_P_B1.3_25.0fps_730k_320x240_aac_48KHz_128Kbps_c2_3min3s_Tomsk_iPod.mp4
			skipframeMode=1;
		}
		else
		{
			//here: we only consider filemode, for streammode, the timestamp will be not matched and timestamp queue may overflow.
		}
#endif
		
	}

	ASSERT(pObj->state==VPU_DEC_STATE_STARTFRAMEOK);

	//set wait mode
	if((0==pInData->nSize)||(VPU_DEC_INPUT_NOT_USED==bufUseState))
	{
		//strategy: wait only when : (1) no invalid input data (2) or bitstream buffer is full
		needWait=1;
	}
	else
	{
		needWait=0;
	}
	busyState=VpuWaitBusy(needWait);
	//0: busy, -1: timeout; 1: not busy
	if(busyState<=0)
	{
		*pOutRetCode=bufUseState;
		return busyState;
	}
	
	//Be careful: We must make sure: one and only one successful vpu_WaitForInt() is called before calling VpuGetOutput()
	*pNoErr=VpuGetOutput(InVpuHandle, pObj, pOutRetCode,skipframeMode,pOutInStreamModeEnough,decParam.chunkSize);

#ifdef VPU_SUPPORT_NO_ENOUGH_FRAME
	if((VPU_DEC_NO_ENOUGH_BUF&(*pOutRetCode))
		||(0!=pObj->pbPacket))
	{
		//record data length to avoid repeat copy next time, only for file mode
		//we need not consider this issue in stream mode
		//Here, we shouldn't check whether (VPU_DEC_INPUT_USED==bufUseState) !!!
		pObj->dataUsedInFileMode=1;
		pObj->lastDatLenInFileMode=decParam.chunkSize;
		pObj->lastConfigMode=decParam.skipframeMode;
	}
	else
	{
		pObj->dataUsedInFileMode=0;
		pObj->lastDatLenInFileMode=0;
		pObj->lastConfigMode=0;
	}

#endif
	
	*pOutRetCode=(*pOutRetCode)|bufUseState;

	return 1;	
}

#ifdef VPU_FILEMODE_PBCHUNK_FLUSH_WORKAROUND
int VpuPBChunkFlush(VpuDecHandleInternal * pVpuObj)
{
	RetCode ret;
	int cnt=0;
	VpuDecObj* pObj;
	DecParam decParam;
	DecOutputInfo outInfo;

	pObj=&pVpuObj->obj;
	
	vpu_memset(&decParam,0,sizeof(DecParam));
	decParam.skipframeMode=3;  // skip all
	decParam.skipframeNum=1;
	decParam.iframeSearchEnable=0;
	decParam.chunkSize=pObj->lastDatLenInFileMode;	
	decParam.skipframeMode=pObj->lastConfigMode;
	VPU_API("calling vpu_DecStartOneFrame(): PB chunk: %d \r\n",decParam.chunkSize);
	ret = vpu_DecStartOneFrame(pVpuObj->handle, &decParam);
	if (ret != RETCODE_SUCCESS)
	{
		VPU_ERROR("%s: vpu start one frame PB chunk failure: ret = 0x%X \r\n",__FUNCTION__,ret);
		return 0;//return VPU_DEC_RET_FAILURE;
	}		
	VPU_API("calling PB chunk: vpu_WaitForInt(%d) \r\n",VPU_TIME_OUT);
	while(0!=vpu_WaitForInt(VPU_TIME_OUT))
	{
		cnt++;
		if(cnt >VPU_MAX_TIME_OUT_CNT)
		{
			VPU_ERROR("%s: flush PB chunk time out \r\n",__FUNCTION__);	
			pObj->state=VPU_DEC_STATE_CORRUPT;
			return 0;//return VPU_DEC_RET_FAILURE_TIMEOUT;
		}
	}
	VPU_API("calling PB chunk: vpu_DecGetOutputInfo() \r\n");
	ret = vpu_DecGetOutputInfo(pVpuObj->handle, &outInfo);
	VPU_API("calling PB chunk: vpu_DecGetOutputInfo(), indexFrameDecoded: %d, return indexFrameDisplay: %d  \r\n",outInfo.indexFrameDecoded,outInfo.indexFrameDisplay);
	
	if (ret != RETCODE_SUCCESS)
	{
		VPU_ERROR("%s: vpu get output info failure: ret = 0x%X \r\n",__FUNCTION__,ret);
		return 0;//return VPU_DEC_RET_FAILURE;
	}
	if(outInfo.indexFrameDisplay>=0)
	{
		VPU_API("%s: calling vpu_DecClrDispFlag(): %d \r\n",__FUNCTION__,outInfo.indexFrameDisplay);
		ret=vpu_DecClrDispFlag(pVpuObj->handle,outInfo.indexFrameDisplay);
		if(RETCODE_SUCCESS!=ret)
		{
			VPU_ERROR("%s: vpu clear display frame failure, index=0x%X, ret=%d \r\n",__FUNCTION__,outInfo.indexFrameDisplay,ret);
			return 0;//return VPU_DEC_RET_FAILURE;
		}
	}

#ifdef VPU_FLUSH_BEFORE_DEC_WORKAROUND
	//if(outInfo.indexFrameDecoded>=0)	//need to check decindex ???
	{
		pObj->realWork=1;
	}
#endif
	
	return 1;	//OK
}
#endif

int VpuCheckDeadLoop(VpuDecObj* pObj ,VpuBufferNode* pInData,VpuDecBufRetCode* pOutRetCode,int* pNoErr) 
{
	//*pNoErr=1;		//don't reset it !!!!: the *pNoErr already has one valid value.
#ifdef VPU_AVOID_DEAD_LOOP
	static int total_init_size=0;		// avoid dead loop at init step
	static int total_init_loop=0;	// avoid dead loop at init step 
	static int total_dec_size=0;	// avoid dead loop at decode step
	static int total_dec_loop=0;	// avoid dead loop at decode step
	int size;
	int cnt;
	int noerr;

	size=0;
	cnt=0;
	noerr=1;
	//here, we don't consider the pInData->sCodecData.nSize
	if(VPU_DEC_INPUT_USED&(*pOutRetCode))
	{
		size=pInData->nSize;		
		if(size>0)	//it is important !!!
		{
			cnt=1;
		}
	}

	if((size==0) && (NULL!=pInData->pVirAddr))
	{
		cnt=1;	//avoit deadloop for smaller clip:WVC1_APL4_16x16_30fps_46kbps_NoAudio_MA40263
	}


	VPU_LOG("%s: total_dec_size: %d, total_dec_loop: %d \r\n",__FUNCTION__,total_dec_size,total_dec_loop);
	switch (pObj->state)
	{
		case VPU_DEC_STATE_OPEN:
			total_init_size+=size;
			total_init_loop+=cnt;
			break;
		case VPU_DEC_STATE_DEC:
		case VPU_DEC_STATE_STARTFRAMEOK:
			total_dec_size+=size;
			total_dec_loop+=cnt;
			break;	
		default:
			//clear 0
			total_init_size=0;
			total_init_loop=0;
			total_dec_size=0;
			total_dec_loop=0;
			break;
	}
#if 0	//dangerous !!
	if((total_dec_size>VPU_MAX_DEC_SIZE)||(total_dec_loop>VPU_MAX_DEC_LOOP))
	//for some clips: MPEG1SS_MP2_720x480_29.97fps_a_32khz_224_welcometoBJ.mpg
	//will timeout even set VPU_MAX_DEC_LOOP=4000, such as CMD_PLAY_STOP test
#else
	if((total_dec_size>VPU_MAX_DEC_SIZE))	
	// for clip:H264_BP40_640x480_15_15107_MP3_48_192_2.avi, it will cache about 15 frames before the first ouput
	// as result, VPU_MAX_DEC_SIZE == 1M isn't enough too. Now, we set bigger value to VPU_MAX_DEC_SIZE
#endif		
	{
		//dead loop at decode step
		noerr=0;
		VPU_ERROR("decode dead loop: total_size: %d, total_cnt: %d \r\n",total_dec_size,total_dec_loop);
	}
#if 1	//dangerous !!
	if((total_init_size>VPU_MAX_INIT_SIZE)||(total_init_loop>VPU_MAX_INIT_LOOP))
#else
	if((total_init_size>VPU_MAX_INIT_SIZE))
#endif
	{
		//dead loop at seq init step
		if((0==pInData->nSize)&&(NULL!=pInData->pVirAddr))
		{
			//EOS: (addr!=NULL && size==0)
			//here: we should not sent error event for eos case !!!!
			*pOutRetCode=VPU_DEC_INPUT_NOT_USED|VPU_DEC_OUTPUT_EOS;		
			pObj->state=VPU_DEC_STATE_EOS;
		}
		else
		{
			noerr=0;
		}
		VPU_ERROR("seq init dead loop: total_size: %d, total_cnt: %d \r\n",total_init_size,total_init_loop);
	}

#ifdef VPU_FILEMODE_QUICK_EXIT
#define VPU_MAX_INIT_FILEMODE_LOOP	(100)	
	if((1==pObj->filemode) )
	{
		if(total_init_loop>VPU_MAX_INIT_FILEMODE_LOOP)
		{
			if((0==/*headerLen+*/pInData->nSize)&&(NULL!=pInData->pVirAddr))
			{
				//EOS: (addr!=NULL && size==0)
				//here: we should not sent error event for eos case !!!!
				*pOutRetCode=VPU_DEC_INPUT_NOT_USED|VPU_DEC_OUTPUT_EOS;		
				pObj->state=VPU_DEC_STATE_EOS;
			}
			else
			{
				//will send error event
				noerr=0;
			}
			VPU_ERROR("seq init dead loop (file mode): total_size: %d, total_cnt: %d \r\n",total_init_size,total_init_loop);			
		}
		else
		{
			if((pObj->state==VPU_DEC_STATE_OPEN)&&(pInData->nSize>0))
			{
				//for corrupt clips: we need to notify user to get one timestamp
				*pOutRetCode=(*pOutRetCode)|VPU_DEC_OUTPUT_DROPPED;
			}
		}
	}
#endif

	if(0==noerr)
	{
		//will send error event
		*pNoErr=0;
		//clear 0 for all count
		total_init_size=0;
		total_init_loop=0;
		total_dec_size=0;
		total_dec_loop=0;
	}
#else
	//do nothing
#endif
	return 1;
}

VpuDecRetCode VPU_Load()
{
	RetCode ret;
	VPU_TRACE;
	VPU_API("calling vpu_Init() \r\n");	
	ret=vpu_Init(NULL);
	VPU_TRACE;
	if(RETCODE_SUCCESS !=ret)
	{
		VPU_ERROR("%s: vpu init failure \r\n",__FUNCTION__);	
		return VPU_DEC_RET_FAILURE;
	}

	TIMER_INIT;
	
	//TODO: add protection for exception exist ?

	return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecGetVersionInfo(VpuVersionInfo * pOutVerInfo)
{
#ifdef  __WINCE
	unsigned int ver;
	unsigned short pn;
	unsigned short version;
	unsigned char  ipprjnum;
	RetCode ret;
	if(pOutVerInfo==NULL)
	{
		VPU_ERROR("%s: failure: invalid parameterl \r\n",__FUNCTION__);	
		return VPU_DEC_RET_INVALID_PARAM;
	}
	VPU_API("calling vpu_GetVersionInfo() \r\n");
	ret=vpu_GetVersionInfo(&ver);
	
	if(RETCODE_SUCCESS!=ret)
	{
		VPU_ERROR("%s: get vpu version failure: ret=%d \r\n",__FUNCTION__,ret);
		return VPU_DEC_RET_FAILURE;
	}
	else
	{
		pn = (unsigned short)(ver>>16);
		version = (unsigned short)ver;
		ipprjnum = (unsigned char)(pn);
		VPU_API("%s: VPU(num:%d) version: %04d.%04d.%08d",__FUNCTION__,ipprjnum,(version>>(12))&0x0f, (version>>(8))&0x0f, (version)&0xff );
	}
	pOutVerInfo->nPrjnum=ipprjnum;
	pOutVerInfo->nFwMajor=(version>>(12))&0x0f;
	pOutVerInfo->nFwMinor=(version>>(8))&0x0f	;
	pOutVerInfo->nFwRelease=(version)&0xff;
	pOutVerInfo->nLibMajor=(version>>(12))&0x0f;
	pOutVerInfo->nLibMinor=(version>>(8))&0x0f	;
	pOutVerInfo->nLibRelease=(version)&0xff;
#else
	vpu_versioninfo ver;
	RetCode ret;

	if(pOutVerInfo==NULL)
	{
		VPU_ERROR("%s: failure: invalid parameterl \r\n",__FUNCTION__);	
		return VPU_DEC_RET_INVALID_PARAM;
	}
	VPU_TRACE;
	VPU_API("calling vpu_GetVersionInfo() \r\n");
	ret=vpu_GetVersionInfo(&ver);
	VPU_TRACE;
	if(RETCODE_SUCCESS!=ret)
	{
		VPU_ERROR("%s: get vpu version failure, ret=%d \r\n",__FUNCTION__,ret);
		return VPU_DEC_RET_FAILURE;
	}
	else
	{
		VPU_API("%s: VPU FW: [major.minor.release]=[%d.%d.%d] \r\n",__FUNCTION__,ver.fw_major,ver.fw_minor,ver.fw_release);
		VPU_API("%s: VPU LIB: [major.minor.release]=[%d.%d.%d] \r\n",__FUNCTION__,ver.lib_major,ver.lib_minor,ver.lib_release);
	}
	pOutVerInfo->nPrjnum=1;
	pOutVerInfo->nFwMajor=ver.fw_major;
	pOutVerInfo->nFwMinor=ver.fw_minor;
	pOutVerInfo->nFwRelease=ver.fw_release;
	pOutVerInfo->nLibMajor=ver.lib_major;
	pOutVerInfo->nLibMinor=ver.lib_minor;
	pOutVerInfo->nLibRelease=ver.lib_release;
#endif	 	

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

#if 1 //for iMX6: VP8
	ASSERT(VPU_VP8_MBPARA_SIZE<=(VPU_SLICE_SAVE_SIZE+VPU_PS_SAVE_SIZE));
	//for vp8, use the same memory with avc
#endif
	/*add slice/ps buffer support for avc */	
	pMem->nSize+=VPU_SLICE_SAVE_SIZE+VPU_PS_SAVE_SIZE;

	pOutMemInfo->nSubBlockNum=2;
	
	return VPU_DEC_RET_SUCCESS;
}


VpuDecRetCode VPU_DecOpen(VpuDecHandle *pOutHandle, VpuDecOpenParam * pInParam,VpuMemInfo* pInMemInfo)
{
	VpuMemSubBlockInfo * pMemPhy;
	VpuMemSubBlockInfo * pMemVirt;
	VpuDecHandleInternal* pVpuObj;
	VpuDecObj* pObj;

	RetCode ret;
	DecOpenParam sDecOpenParam;
	
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
		||(pMemPhy->nSize!=(VPU_BITS_BUF_SIZE+VPU_SLICE_SAVE_SIZE+VPU_PS_SAVE_SIZE)))
	{
		VPU_ERROR("%s: failure: invalid parameter !! \r\n",__FUNCTION__);	
		return VPU_DEC_RET_INVALID_PARAM;
	}

	pVpuObj=(VpuDecHandleInternal*)pMemVirt->pVirtAddr;
	pObj=&pVpuObj->obj;

	// clear vpu obj 
	vpu_memset(pObj, 0, sizeof(VpuDecObj));
	//clear 0 firstly
	vpu_memset(&sDecOpenParam, 0, sizeof(DecOpenParam));

#if 1	//for iMX6
	pInParam->nEnableFileMode=0;
	sDecOpenParam.bitstreamMode=1; //0-normal mode, 1-rollback mode
#endif

	if(pInParam->nEnableFileMode)
	{
		sDecOpenParam.filePlayEnable = 1; /* always using file mode */
	}
	else
	{
		sDecOpenParam.filePlayEnable = 0; /* always using stream mode */
	}

	//sDecOpenParam.dynamicAllocEnable = 0;
	VPU_LOG("format: %d \r\n",pInParam->CodecFormat);
	switch (pInParam->CodecFormat) {
		case VPU_V_MPEG2: 	 /**< AKA: H.262 */
			sDecOpenParam.bitstreamFormat = STD_MPEG2;
			//sDecOpenParam.filePlayEnable = 0;
			VPU_LOG("open Mpeg2 \r\n");
			break;
		case VPU_V_H263:		 /**< H.263 */
			sDecOpenParam.bitstreamFormat = STD_H263;
			VPU_LOG("open H263 \r\n");
			break;
		case VPU_V_MPEG4: 	 /**< MPEG-4 */
			sDecOpenParam.bitstreamFormat = STD_MPEG4;
			sDecOpenParam.mp4Class = 0;
			VPU_LOG("open Mpeg4 \r\n");
			break;	
		case VPU_V_DIVX56:		/**< DIVX 5/6 */
			sDecOpenParam.bitstreamFormat = STD_MPEG4;
			sDecOpenParam.mp4Class = 1;
			VPU_LOG("open DIVX 56 \r\n");
			break;
		case VPU_V_XVID:		/**< XVID */
			sDecOpenParam.bitstreamFormat = STD_MPEG4;
			sDecOpenParam.mp4Class = 2;
			VPU_LOG("open XVID \r\n");
			break;			
		case VPU_V_DIVX4:		/**< DIVX 4 */
			sDecOpenParam.bitstreamFormat = STD_MPEG4;
			sDecOpenParam.mp4Class = 5;
			VPU_LOG("open DIVX 4 \r\n");
			break;	
		case VPU_V_DIVX3:		/**< DIVX 3 */ 
			sDecOpenParam.bitstreamFormat = STD_DIV3;
			sDecOpenParam.reorderEnable = 1;
			//sDecOpenParam.filePlayEnable = 1; 
			VPU_LOG("open DIVX 3 \r\n");	
			break;		
		case VPU_V_RV:		
			sDecOpenParam.bitstreamFormat = STD_RV;
			sDecOpenParam.reorderEnable = 1;
			//sDecOpenParam.filePlayEnable = 1; 
			VPU_LOG("open RV \r\n");
			break;		
		case VPU_V_VC1:		 /**< all versions of Windows Media Video */
		case VPU_V_VC1_AP:
			sDecOpenParam.bitstreamFormat = STD_VC1;
			//sDecOpenParam.filePlayEnable = 1; 
			sDecOpenParam.reorderEnable = 1;
			VPU_LOG("open VC1 \r\n");
			break;
		case VPU_V_AVC_MVC:
			sDecOpenParam.avcExtension=1;
		case VPU_V_AVC:
			sDecOpenParam.bitstreamFormat = STD_AVC;
			//pCodecPriv->sPsSaveBuffer.size = PS_SAVE_SIZE;
			//GET_PHY_MEM(&(pCodecPriv->sPsSaveBuffer));
			//pCodecPriv->sSliceBuffer.size = SLICE_SAVE_SIZE;
			//GET_PHY_MEM(&(pCodecPriv->sSliceBuffer));
			sDecOpenParam.reorderEnable = pInParam->nReorderEnable;
			//sDecOpenParam.filePlayEnable = 0;
			VPU_LOG("open H.264 \r\n");
			break;
		case VPU_V_MJPG:
			sDecOpenParam.bitstreamFormat = STD_MJPG;
			sDecOpenParam.mjpg_thumbNailDecEnable=0;	//no thumbnail ??
#if 1 //for iMX6
			sDecOpenParam.pBitStream=pMemPhy->pVirtAddr;
			sDecOpenParam.jpgLineBufferMode=1;	/*need to enable it*/
#endif
			VPU_LOG("open MJPEG \r\n");
			break;
		case VPU_V_AVS:
			sDecOpenParam.bitstreamFormat = STD_AVS;
			sDecOpenParam.reorderEnable = 1;
			VPU_LOG("open AVS \r\n");
			break;
		case VPU_V_VP8:
			sDecOpenParam.bitstreamFormat = STD_VP8;
			sDecOpenParam.reorderEnable = 1;
			VPU_LOG("open VP8 \r\n");
			break;
		default:
			VPU_ERROR("%s: failure: invalid format !!! \r\n",__FUNCTION__);	
			return VPU_DEC_RET_INVALID_PARAM;
	}

	sDecOpenParam.bitstreamBuffer = (PhysicalAddress)pMemPhy->pPhyAddr;
	sDecOpenParam.bitstreamBufferSize = VPU_BITS_BUF_SIZE;//pMemPhy->nSize;
	//sDecOpenParam.psSaveBuffer = NULL;
	//sDecOpenParam.psSaveBufferSize = 0;
	sDecOpenParam.chromaInterleave = pInParam->nChromaInterleave;
#if 1	//needed for divx3
	sDecOpenParam.picWidth = pInParam->nPicWidth;	
	sDecOpenParam.picHeight = pInParam->nPicHeight;
#endif

	/*record avc slice/ps buffer*/
	pObj->pAvcSlicePhyBuf=pMemPhy->pPhyAddr+VPU_BITS_BUF_SIZE;
	pObj->pAvcSPSPhyBuf=pMemPhy->pPhyAddr+VPU_BITS_BUF_SIZE+VPU_SLICE_SAVE_SIZE;
	sDecOpenParam.psSaveBuffer=(PhysicalAddress)pObj->pAvcSPSPhyBuf;
	sDecOpenParam.psSaveBufferSize=VPU_PS_SAVE_SIZE;

	VPU_TRACE;
	VPU_API("calling vpu_DecOpen() : filePlayEnable: %d , format: %d \r\n",sDecOpenParam.filePlayEnable,sDecOpenParam.bitstreamFormat);
	ret= vpu_DecOpen(&pVpuObj->handle, &sDecOpenParam);
	VPU_TRACE;
	if(ret!=RETCODE_SUCCESS)
	{
		VPU_ERROR("%s: vpu open failure: ret=%d \r\n",__FUNCTION__,ret);
		return VPU_DEC_RET_FAILURE;
	}

	// record open params info
	pObj->CodecFormat  = pInParam->CodecFormat;
	//pObj->blockmode=pInParam->block;

	//record resolution for some special formats (such as VC1,...)
	pObj->picWidth = pInParam->nPicWidth;	
	pObj->picHeight = pInParam->nPicHeight;
	
	// init bitstream buf info
	pObj->pBsBufVirtStart= pMemPhy->pVirtAddr;
	pObj->pBsBufPhyStart= pMemPhy->pPhyAddr;
	//pObj->pBsBufPhyWritePtr= pVpuObj->obj.pBsBufPhyStart;
	pObj->pBsBufPhyEnd=pMemPhy->pPhyAddr+VPU_BITS_BUF_SIZE;//+pMemPhy->nSize;

	// init state
	pObj->state=VPU_DEC_STATE_OPEN;

#ifdef VPU_SUPPORT_UNCLOSED_GOP
	//pObj->refCnt=MIN_REF_CNT;	//we only consider flush operation(eg. seek), so init it with valid value, but not 0
	pObj->refCnt=0;				//for some .ts clips, we need to skip the first corrupt frames
	pObj->dropBCnt=0;
	//pObj->keyCnt=MIN_KEY_CNT;	//we only consider flush operation(eg. seek) ???
	pObj->keyCnt=0;				//for some .ts clips, we need to skip the first corrupt frames
#endif	
#ifdef VPU_IFRAME_SEARCH
	//pObj->keyDecCnt=MIN_KEY_CNT;//we only consider flush operation(eg.seek)
	pObj->keyDecCnt=0;
#endif
#ifdef VPU_SEEK_ANYPOINT_WORKAROUND
	pObj->seekKeyLoc=0;			//we consider the normal play (for .ts clips)
	pObj->recommendFlush=0;
#endif
#ifdef VPU_SUPPORT_NO_ENOUGH_FRAME
	pObj->dataUsedInFileMode=0;
	pObj->lastDatLenInFileMode=0;
	pObj->lastConfigMode=0;
#endif
	// setting related with file mode
	pObj->filemode=sDecOpenParam.filePlayEnable;
	pObj->firstData=1;
	pObj->firstDataSize=0;
#ifdef VPU_PROTECT_MULTI_INSTANCE
	pObj->filledEOS=0;
#endif
	pObj->pbPacket=0;
	pObj->pbClips=0;

#ifdef VPU_FLUSH_BEFORE_DEC_WORKAROUND
	pObj->realWork=0;
#endif

#ifdef VPU_FILEMODE_SUPPORT_INTERLACED_SKIPMODE
	pObj->firstFrameMode=0;
	pObj->fieldCnt=0;
#endif

#ifdef VPU_FILEMODE_MERGE_INTERLACE_DEBUG
	pObj->needMergeFields=VPU_FILEMODE_MERGE_FLAG;
	pObj->lastFieldOffset=0;
#endif

#ifdef VPU_FILEMODE_INTERLACE_TIMESTAMP_ENHANCE
	pObj->fieldDecoding=0;
	pObj->oweFieldTS=0;
#endif

	pObj->mjpg_frmidx=0;
#if 1 //for iMX6 MJPEG
	if((VPU_V_MJPG==pInParam->CodecFormat)&&(1==sDecOpenParam.jpgLineBufferMode))
	{
		//caution: in this case, vpu must be open with stream mode on iMX6.
		pObj->filemode=1; //unify the logic: for filemode and linebuffermode
		pObj->mjpg_linebuffmode=1;
	}
#endif	
	*pOutHandle=(VpuDecHandle)pVpuObj;	

	return VPU_DEC_RET_SUCCESS;
}


VpuDecRetCode VPU_DecConfig(VpuDecHandle InHandle, VpuDecConfig InDecConf, void* pInParam)
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
	
	switch(InDecConf)
	{
		case VPU_DEC_CONF_SKIPNONE:
			pObj->skipFrameMode=0;
			pObj->skipFrameNum=0;	
			pObj->iframeSearchEnable=0;
			break;
		case VPU_DEC_CONF_SKIPPB:
			pObj->skipFrameMode=1;
			pObj->skipFrameNum=*((int*)pInParam);	
			pObj->iframeSearchEnable=0;
			break;
		case VPU_DEC_CONF_SKIPB:
			pObj->skipFrameMode=2;
			pObj->skipFrameNum=*((int*)pInParam);	
			pObj->iframeSearchEnable=0;
			break;
		case VPU_DEC_CONF_SKIPALL:
			pObj->skipFrameMode=3;
			pObj->skipFrameNum=*((int*)pInParam);	
			pObj->iframeSearchEnable=0;
			break;
		case VPU_DEC_CONF_ISEARCH:
			pObj->skipFrameMode=0;
			pObj->skipFrameNum=*((int*)pInParam);	
			pObj->iframeSearchEnable=1;
			break;
		//case VPU_DEC_CONF_BLOCK:
		//	pObj->blockmode=1;
		//	break;
		//case VPU_DEC_CONF_NONEBLOCK:
		//	pObj->blockmode=0;
		//	break;
		default:
			VPU_ERROR("%s: failure: invalid setting \r\n",__FUNCTION__);	
			return VPU_DEC_RET_INVALID_PARAM;
	}
	
	return VPU_DEC_RET_SUCCESS;
}


VpuDecRetCode VPU_DecDecodeBuf(VpuDecHandle InHandle, VpuBufferNode* pInData,VpuDecBufRetCode* pOutRetCode)
{
	VpuDecHandleInternal * pVpuObj;
	VpuDecObj* pObj;
	int noerr=1;
	int streamModeEnough=1;
	int seqOk=0;

	if(InHandle==NULL) 
	{
		VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);		
		return VPU_DEC_RET_INVALID_HANDLE;
	}

	pVpuObj=(VpuDecHandleInternal *)InHandle;
	pObj=&pVpuObj->obj;

	//check NULL data, do nothing ? now, we need to NULL data to send EOS ?
	if(1==pObj->filemode)
	{
		//improve performance: CT: 38701782: transformer 2 1080p_mpeg4.mkv
		if((NULL==pInData->pVirAddr)&&(0==pInData->nSize))
		{
			*pOutRetCode=VPU_DEC_INPUT_USED;
#ifdef VPU_SUPPORT_NO_INBUF		
			*pOutRetCode=(*pOutRetCode)|VPU_DEC_NO_ENOUGH_INBUF;
#endif			
			return VPU_DEC_RET_SUCCESS;
		}
	}

	//check MJPG
	
	if(VPU_V_MJPG==pObj->CodecFormat)
	{
		ASSERT(1==pObj->filemode);
		if(0==pInData->nSize)
		{
			if(NULL==pInData->pVirAddr)
			{
				*pOutRetCode=VPU_DEC_INPUT_USED;
#ifdef VPU_SUPPORT_NO_INBUF		
				*pOutRetCode=(*pOutRetCode)|VPU_DEC_NO_ENOUGH_INBUF;
#endif
				return VPU_DEC_RET_SUCCESS;
			}
			else
			{
				//for mjpg: in file mode, vpu seem don't return -1 after update 0.
				//so, here we need to return eos manually !!!
				int index;
				if(0==VpuSearchFreeFrameBuf(pObj, &index))
				{
					//no frame buffer, we need to reserved one frame for the last output even it doesn't contain valid data
					VPU_API("MJPG: need to reserve on frame for the last output, return no output \r\n");
					*pOutRetCode=VPU_DEC_OUTPUT_NODIS|VPU_DEC_NO_ENOUGH_BUF;
				}
				else
				{
					VPU_API("MJPG: return EOS manually \r\n");
					*pOutRetCode=VPU_DEC_OUTPUT_EOS;
					pObj->state=VPU_DEC_STATE_EOS;	// user should feed valid data for next play					
				}
				return VPU_DEC_RET_SUCCESS;
			}			
		}
	}

RepeatDec:
	switch (pObj->state)
	{
		case VPU_DEC_STATE_OPEN:
			//need to check (pInData->nSize==0) ?? we should not send 0 bytes at seqinit step 
			seqOk=VpuSeqInit(pVpuObj->handle,pObj,pInData,pOutRetCode,&noerr);
#ifdef VPU_SUPPORT_NO_INBUF
			if((seqOk==0)&&((*pOutRetCode)&VPU_DEC_INPUT_USED))
			{
				if((NULL!=pInData->pVirAddr)&&(0==pInData->nSize))	//for iMX6X(stream mode)
				{
					//do nothing if meeting eos: stream mode: WVC1_APL4_16x16_30fps_46kbps_NoAudio_MA40263
				}
				else
				{
					*pOutRetCode=(*pOutRetCode)|VPU_DEC_NO_ENOUGH_INBUF;
				}
			}
#endif
			break;
		case VPU_DEC_STATE_INITOK:
			VPU_ERROR("%s: failure: missing VPU_DecGetInitialInfo() \r\n",__FUNCTION__);
			return VPU_DEC_RET_WRONG_CALL_SEQUENCE;
		case VPU_DEC_STATE_REGFRMOK:
			VPU_ERROR("%s: failure: missing VPU_DecRegisterFrameBuffer() \r\n",__FUNCTION__);
			return VPU_DEC_RET_WRONG_CALL_SEQUENCE;			
		case VPU_DEC_STATE_DEC:
		case VPU_DEC_STATE_STARTFRAMEOK:
#ifdef IMX6_BUFNOTENOUGH_WORKAROUND
			{
				int used_num=0;
				used_num=VpuQueryVpuHoldBufNum(pObj);
				//if(used_num<pVpuObj->obj.initInfo.nMinFrameBufferCount)
				if(used_num<pVpuObj->obj.initInfo.nMinFrameBufferCount-1)
				{
					VPU_LOG("buf may not enough, %d may been used by vpu , mini cnt: %d \r\n",used_num,pVpuObj->obj.initInfo.nMinFrameBufferCount);
					*pOutRetCode=VPU_DEC_NO_ENOUGH_BUF|VPU_DEC_OUTPUT_NODIS;	
					return VPU_DEC_RET_SUCCESS;
				}
			}
#endif
			if(-1==VpuDecBuf(pVpuObj->handle,pObj,pInData,pOutRetCode,&noerr,&streamModeEnough))
			{
				VPU_ERROR("%s: time out \r\n",__FUNCTION__);	
				pObj->state=VPU_DEC_STATE_CORRUPT;
				return VPU_DEC_RET_FAILURE_TIMEOUT;
			}
#ifdef VPU_SUPPORT_NO_INBUF
			if((*pOutRetCode)&VPU_DEC_INPUT_USED)
			{
				if((pInData->pVirAddr!=NULL)&&(pInData->nSize==0))
				{
					//in eos: shouldn't set no_enough_input
				}
				else
				{
					//if(1==pObj->filemode)
					//{
					//	*pOutRetCode=(*pOutRetCode)|VPU_DEC_NO_ENOUGH_INBUF;					
					//}
					//else
					{
						if(0==streamModeEnough)
						{
							*pOutRetCode=(*pOutRetCode)|VPU_DEC_NO_ENOUGH_INBUF;	
						}
					}
				}
			}
#endif
			
			break;
		case VPU_DEC_STATE_OUTOK:
			VPU_ERROR("%s: failure: missing VPU_DecGetOutputFrame() \r\n",__FUNCTION__);
			return VPU_DEC_RET_WRONG_CALL_SEQUENCE;
		case VPU_DEC_STATE_EOS:
			if(pInData->nSize>0)
			{
				if(0==VpuDecClearOperationEOStoDEC(InHandle))
				{
					VPU_ERROR("%s: trans eos to dec state failure ! \r\n",__FUNCTION__);
					return VPU_DEC_RET_FAILURE;
				}
				pObj->state=VPU_DEC_STATE_DEC;  //repeat play
				goto RepeatDec;
			}
			else if((pInData->pVirAddr!=NULL)&&(pInData->nSize==0))
			{
				//fix case: special EOS flag with 0 bytes
				if(0==VpuDecClearOperationEOStoDEC(InHandle))
				{
					VPU_ERROR("%s: trans eos to dec state failure !!! \r\n",__FUNCTION__);
					return VPU_DEC_RET_FAILURE;
				}
				pObj->state=VPU_DEC_STATE_DEC;  //repeat play
				goto RepeatDec;				
			}
			else
			{
				*pOutRetCode=VPU_DEC_INPUT_USED;   //do nothing and return
#ifdef VPU_SUPPORT_NO_INBUF
				*pOutRetCode=(*pOutRetCode)|VPU_DEC_NO_ENOUGH_INBUF;
#endif				
				break;
			}	
		case VPU_DEC_STATE_CORRUPT:
			// do nothing, wait calling VPU_Reset(), and then reload vpu again
			*pOutRetCode=VPU_DEC_INPUT_NOT_USED;
			break;
		default:
			VPU_ERROR("%s: failure: error state: %d \r\n",__FUNCTION__,pObj->state);
			return VPU_DEC_RET_INVALID_PARAM;
	}

	VpuCheckDeadLoop(pObj,pInData,pOutRetCode,&noerr);
	
	if(noerr)
	{
		return VPU_DEC_RET_SUCCESS;
	}
	else
	{
		VPU_ERROR("%s: return failure \r\n",__FUNCTION__);	
		return VPU_DEC_RET_FAILURE;
	}
}


VpuDecRetCode VPU_DecGetInitialInfo(VpuDecHandle InHandle, VpuDecInitInfo * pOutInitInfo)
{
	VpuDecHandleInternal * pVpuObj;

	if(InHandle==NULL) 
	{
		VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);	
		return VPU_DEC_RET_INVALID_HANDLE;
	}

	pVpuObj=(VpuDecHandleInternal *)InHandle;
	if(pVpuObj->obj.state!=VPU_DEC_STATE_INITOK)
	{
		VPU_ERROR("%s: failure: error state %d \r\n",__FUNCTION__,pVpuObj->obj.state);	
		return VPU_DEC_RET_WRONG_CALL_SEQUENCE;
	}

	//update state
	pVpuObj->obj.state=VPU_DEC_STATE_REGFRMOK;
	VPU_TRACE;
	*pOutInitInfo=pVpuObj->obj.initInfo;
	VPU_TRACE;
	
	return VPU_DEC_RET_SUCCESS;
}


VpuDecRetCode VPU_DecRegisterFrameBuffer(VpuDecHandle InHandle,VpuFrameBuffer *pInFrameBufArray, int nNum)
{
	VpuDecHandleInternal * pVpuObj;
	RetCode ret;
	//FrameBuffer vpu_regframebuf[VPU_MAX_FRAME_INDEX];
	DecBufInfo sBufInfo;
	int i;
	
	if(InHandle==NULL) 
	{
		VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);	
		return VPU_DEC_RET_INVALID_HANDLE;
	}

	pVpuObj=(VpuDecHandleInternal *)InHandle;
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
		//record frame buf info
		pVpuObj->obj.frameBuf[i]=*pInFrameBufArray;

		//re-map frame buf info for vpu register
		pVpuObj->obj.vpu_regframebuf[i].myIndex=i;
		pVpuObj->obj.vpu_regframebuf[i].strideY=(unsigned long)pInFrameBufArray->nStrideY;
		pVpuObj->obj.vpu_regframebuf[i].strideC=(unsigned long)pInFrameBufArray->nStrideC;
#ifdef USE_NEW_VPU_API			
		pVpuObj->obj.vpu_regframebuf[i].myIndex=i;
#endif
		pVpuObj->obj.vpu_regframebuf[i].bufY=(PhysicalAddress)pInFrameBufArray->pbufY;
		pVpuObj->obj.vpu_regframebuf[i].bufCb=(PhysicalAddress)pInFrameBufArray->pbufCb;
		pVpuObj->obj.vpu_regframebuf[i].bufCr=(PhysicalAddress)pInFrameBufArray->pbufCr;
		pVpuObj->obj.vpu_regframebuf[i].bufMvCol=(PhysicalAddress)pInFrameBufArray->pbufMvCol;

		VPU_LOG("register frame %d: (phy)	Y:0x%X, U:0x%X, V:0x%X \r\n",i,(unsigned int)pVpuObj->obj.vpu_regframebuf[i].bufY,(unsigned int)pVpuObj->obj.vpu_regframebuf[i].bufCb,(unsigned int)pVpuObj->obj.vpu_regframebuf[i].bufCr);
		VPU_LOG("register frame %d: (virt)	Y:0x%X, U:0x%X, V:0x%X \r\n",i,(unsigned int)pInFrameBufArray->pbufVirtY,(unsigned int)pInFrameBufArray->pbufVirtCb,(unsigned int)pInFrameBufArray->pbufVirtCr);
		VPU_LOG("register mv    %d: (phy)	0x%X,    (virt)    0x%X \r\n",i,(unsigned int)pVpuObj->obj.vpu_regframebuf[i].bufMvCol,(unsigned int)pInFrameBufArray->pbufVirtMvCol);		
		pInFrameBufArray++;
	}
	pVpuObj->obj.frameNum=nNum;

	//1 not used sBufInfo again, only clear it
	vpu_memset(&sBufInfo, 0, sizeof(DecBufInfo));

	/*set slice save buf*/
#ifdef USE_NEW_VPU_API		
	sBufInfo.avcSliceBufInfo.bufferBase =(PhysicalAddress)pVpuObj->obj.pAvcSlicePhyBuf;
	sBufInfo.avcSliceBufInfo.bufferSize =VPU_SLICE_SAVE_SIZE;	
#else
	sBufInfo.avcSliceBufInfo.sliceSaveBuffer=(PhysicalAddress)pVpuObj->obj.pAvcSlicePhyBuf;
	sBufInfo.avcSliceBufInfo.sliceSaveBufferSize=VPU_SLICE_SAVE_SIZE;
#endif	
#if 1 //for iMX6: Vp8
	sBufInfo.maxDecFrmInfo.maxMbX=(pVpuObj->obj.initInfo.nPicWidth+15)/16;
	sBufInfo.maxDecFrmInfo.maxMbY=pVpuObj->obj.initInfo.nPicHeight/16;
	sBufInfo.maxDecFrmInfo.maxMbNum=sBufInfo.maxDecFrmInfo.maxMbX*sBufInfo.maxDecFrmInfo.maxMbY;
	if(VPU_V_VP8==pVpuObj->obj.CodecFormat)
	{
		//use the same memory with avc
		ASSERT((17*4*sBufInfo.maxDecFrmInfo.maxMbNum)<=VPU_VP8_MBPARA_SIZE);
		ASSERT(VPU_VP8_MBPARA_SIZE<=(VPU_SLICE_SAVE_SIZE+VPU_PS_SAVE_SIZE));
		sBufInfo.vp8MbDataBufInfo.bufferBase=(PhysicalAddress)pVpuObj->obj.pAvcSlicePhyBuf;
		sBufInfo.vp8MbDataBufInfo.bufferSize=VPU_VP8_MBPARA_SIZE;
	}
#endif		
	VPU_TRACE;
	VPU_API("calling vpu_DecRegisterFrameBuffer() \r\n");
	ret = vpu_DecRegisterFrameBuffer(pVpuObj->handle,
			pVpuObj->obj.vpu_regframebuf,
			nNum,
			pVpuObj->obj.vpu_regframebuf[0].strideY, /* necessary ? */
			&sBufInfo);
	VPU_TRACE;
	if(RETCODE_SUCCESS!=ret)
	{
		VPU_ERROR("%s: vpu register frame failure, ret=%d \r\n",__FUNCTION__,ret);
		return VPU_DEC_RET_FAILURE;
	}	

	if(VPU_V_MJPG==pVpuObj->obj.CodecFormat)
	{
		unsigned int rot_angle = 0;
		unsigned int mirror = 0;
		//rot_angle=90;
		VPU_API("vpu_DecGiveCommand: SET_ROTATION_ANGLE: %d \r\n",rot_angle);
		vpu_DecGiveCommand(pVpuObj->handle, SET_ROTATION_ANGLE,(void*)(&rot_angle));
		VPU_API("vpu_DecGiveCommand: SET_MIRROR_DIRECTION: %d \r\n",mirror);
		vpu_DecGiveCommand(pVpuObj->handle, SET_MIRROR_DIRECTION,(void*)(&mirror));
		VPU_API("vpu_DecGiveCommand: SET_ROTATOR_STRIDE: %d \r\n",(int)pVpuObj->obj.vpu_regframebuf[0].strideY);
		vpu_DecGiveCommand(pVpuObj->handle, SET_ROTATOR_STRIDE,(void*)(&pVpuObj->obj.vpu_regframebuf[0].strideY));
	}

	//update state
	pVpuObj->obj.state=VPU_DEC_STATE_DEC;	

	return VPU_DEC_RET_SUCCESS;
}



VpuDecRetCode VPU_DecGetOutputFrame(VpuDecHandle InHandle, VpuDecOutFrameInfo * pOutFrameInfo)
{
	VpuDecHandleInternal * pVpuObj;

	if(InHandle==NULL) 
	{
		VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);	
		return VPU_DEC_RET_INVALID_HANDLE;
	}

	pVpuObj=(VpuDecHandleInternal *)InHandle;
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


VpuDecRetCode VPU_DecOutFrameDisplayed(VpuDecHandle InHandle, VpuFrameBuffer* pInFrameBuf)
{
	VpuDecHandleInternal * pVpuObj;
	RetCode ret;
	int index;
	
	if(InHandle==NULL) 
	{
		VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);	
		return VPU_DEC_RET_INVALID_HANDLE;
	}
	pVpuObj=(VpuDecHandleInternal *)InHandle;

	switch(pVpuObj->obj.state)
	{
		case VPU_DEC_STATE_CORRUPT:
			//skip calling vpu api
			return VPU_DEC_RET_SUCCESS;
		default:
			break;
	}

	//search frame buffer index
	index=VpuSearchFrameIndex(&pVpuObj->obj, pInFrameBuf);
	if (-1==index)
	{
		VPU_ERROR("%s: failure: vpu can not find the frame buf, pInFrameBuf=0x%X \r\n",__FUNCTION__,(unsigned int)pInFrameBuf);
		return VPU_DEC_RET_INVALID_PARAM;		
	}


	//clear disp flag: need to check since it may be cleared in flushall operation
	if (VpuDispFrameIsNotCleared(index, pVpuObj->obj.frameBufState))
	{
		//clear frame state
		VpuClearDispFrame( index, pVpuObj->obj.frameBufState);

		VPU_API("%s: calling vpu_DecClrDispFlag(): %d \r\n",__FUNCTION__,index);
		//TIMER_START(TIMER_CLEARDISP_ID);
		ret=vpu_DecClrDispFlag(pVpuObj->handle,index);
		//TIMER_STOP(TIMER_CLEARDISP_ID);
		if(RETCODE_SUCCESS!=ret)
		{
			VPU_ERROR("%s: vpu clear display frame failure, index=0x%X, ret=%d \r\n",__FUNCTION__,index,ret);
			return VPU_DEC_RET_FAILURE;			
		}

		//reset historical info
		if(pInFrameBuf==pVpuObj->obj.pPreDisplayFrameBuf)
		{
			pVpuObj->obj.pPreDisplayFrameBuf=NULL;
		}
	}

	return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecFlushAll(VpuDecHandle InHandle)
{
	VpuDecHandleInternal * pVpuObj;
	VpuDecObj* pObj;
	RetCode ret;
	DecParam decParam;
	DecOutputInfo outInfo;
	int startFrameOK=0;
	int nExtCount;		// for external while loop
		
	if(InHandle==NULL) 
	{
		VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);	
		return VPU_DEC_RET_INVALID_HANDLE;
	}
	pVpuObj=(VpuDecHandleInternal *)InHandle;
	pObj=&pVpuObj->obj;


	switch (pObj->state)
	{
		//allowed state
		case VPU_DEC_STATE_OPEN:	//not sure the feasibility ??
			VPU_ERROR("calling flush operation before seq init ok \r\n");
		case VPU_DEC_STATE_INITOK:	
		case VPU_DEC_STATE_REGFRMOK:
			if(0==VpuBitFlush(pVpuObj, 0/*before decode state*/))
			{
				return VPU_DEC_RET_FAILURE;
			}
			//need not change state
			return VPU_DEC_RET_SUCCESS;
		case VPU_DEC_STATE_DEC:
			break;
		case VPU_DEC_STATE_STARTFRAMEOK:
			startFrameOK=1;	//in this case, vpu may be busy status, we should not call vpu_DecBitBufferFlush()
			//adjust state !!!
			//pObj->state=VPU_DEC_STATE_DEC;
			break;
		case VPU_DEC_STATE_EOS:
			break;
		case VPU_DEC_STATE_CORRUPT:
			//do nothing
			return VPU_DEC_RET_SUCCESS;
		default:
			//forbidden state
			//user should not call flush before seq init OK (eg, before getting correct resolution ???)
			VPU_ERROR("%s: failure: error state: %d \r\n",__FUNCTION__,pObj->state);
			return VPU_DEC_RET_FAILURE;
	}

	if(VPU_V_MJPG==pObj->CodecFormat)
	{	
		pObj->mjpg_frmidx=0;
		goto FLUSH_FINISH;
	}

#ifdef VPU_FILEMODE_PBCHUNK_FLUSH_WORKAROUND
	//pObj->pbClips=0;	//need not clear it .
	if((0!=pObj->pbPacket)&&(1==pObj->dataUsedInFileMode)&&(1==pObj->filemode))
	{
		VPU_LOG("PB chunk is not completed !!!\r\n");
		if(0==VpuPBChunkFlush(pVpuObj))
		{
			return VPU_DEC_RET_FAILURE;
		}
	}
#endif	

#ifdef VPU_FLUSH_BEFORE_DEC_WORKAROUND
	if(0==pObj->realWork)
	{
		//FIXME: for stream mode, it is still unsure !!!!!
		ASSERT(pObj->filemode==1);
		if(pObj->filemode==0)
		{
			//should skip calling bit flush in VpuDecClearOperationEOStoDEC() ???
			return VPU_DEC_RET_SUCCESS;
		}
		//must skip update 0, otherwise, vpu always return -1(EOS) even feed it with valid data later
		goto FLUSH_FINISH;
	}
#endif


#ifdef VPU_FAKE_FLUSH	//for testing: don't update 0
	if(startFrameOK==1)
	{
#if 1 //1  for iMX6
		VPU_API("calling vpu_WaitForInt(%d) \r\n",VPU_TIME_OUT);
		if(0!=vpu_WaitForInt(VPU_TIME_OUT))
		{
			VPU_LOG("LEVEL: 1: in imx6 stream mode:  fake flush : no enough data , we should update 0 \r\n");
			VPU_API("calling vpu_DecUpdateBitstreamBuffer(): %d \r\n",0);
			ret=vpu_DecUpdateBitstreamBuffer(pVpuObj->handle, 0);	
			if (ret != RETCODE_SUCCESS)
			{
				VPU_ERROR("%s: vpu update data failure: ret = 0x%X \r\n",__FUNCTION__,ret);	
				return VPU_DEC_RET_FAILURE_TIMEOUT;
			}
			VPU_API("calling vpu_WaitForInt(%d) \r\n",VPU_TIME_OUT);
			if(0!=vpu_WaitForInt(VPU_TIME_OUT))			
			{
				VPU_ERROR("LEVEL: 1: in imx6 stream mode: fake flush failure: timeout after update 0 \r\n");
				return VPU_DEC_RET_FAILURE_TIMEOUT;
			}			
		}		
#else
		int cnt=0;
		VPU_API("calling vpu_WaitForInt(%d) \r\n",VPU_TIME_OUT);
		while(0!=vpu_WaitForInt(VPU_TIME_OUT))
		{
			cnt++;
			if(cnt >VPU_MAX_TIME_OUT_CNT)
			{
				VPU_ERROR("%s: flush time out \r\n",__FUNCTION__);	
				pObj->state=VPU_DEC_STATE_CORRUPT;
				return VPU_DEC_RET_FAILURE_TIMEOUT;
			}
		}
#endif
		VPU_API("calling vpu_DecGetOutputInfo() \r\n");
		ret = vpu_DecGetOutputInfo(pVpuObj->handle, &outInfo);
		VPU_API("calling vpu_DecGetOutputInfo(), indexFrameDecoded: %d, return indexFrameDisplay: %d, success: 0x%X  \r\n",outInfo.indexFrameDecoded,outInfo.indexFrameDisplay,outInfo.decodingSuccess);
		
		if (ret != RETCODE_SUCCESS)
		{
			VPU_ERROR("%s: vpu get output info failure: ret = 0x%X \r\n",__FUNCTION__,ret);
			return VPU_DEC_RET_FAILURE;
		}
#if 1 //1 for iMX6 rollback mode
		if (outInfo.decodingSuccess & 0x10)
		{
			//current frame is not integrated, rollback to frame header
		}
		else
#endif			
		{		
			if(outInfo.indexFrameDisplay>=0)
			{
				VPU_API("%s: calling vpu_DecClrDispFlag(): %d \r\n",__FUNCTION__,outInfo.indexFrameDisplay);
				ret=vpu_DecClrDispFlag(pVpuObj->handle,outInfo.indexFrameDisplay);
				if(RETCODE_SUCCESS!=ret)
				{
					VPU_ERROR("%s: vpu clear display frame failure, index=0x%X, ret=%d \r\n",__FUNCTION__,outInfo.indexFrameDisplay,ret);
					return VPU_DEC_RET_FAILURE;
				}
			}
		}
	}
	
	if(0==VpuBitFlush(pVpuObj, 1/*before eos*/))
	{
		return VPU_DEC_RET_FAILURE;
	}
	goto FLUSH_FINISH;
#endif

	//flush bitstream to improve performance
	if(startFrameOK==0)
	{
		if(0==VpuBitFlush(pVpuObj, 1/*before eos*/))
		{
			return VPU_DEC_RET_FAILURE;
		}
	}
	else
	{
		//may enter dead loop if calling vpu_DecBitBufferFlush()
		//so we need to call vpu_DecBitBufferFlush() after next vpu_DecStartOneFrame()
	}

	//send EOS flag
	VPU_API("calling vpu_DecUpdateBitstreamBuffer(): %d \r\n",0);
	ret=vpu_DecUpdateBitstreamBuffer(pVpuObj->handle, 0);	
	if (ret != RETCODE_SUCCESS)
	{
		VPU_ERROR("%s: vpu update data failure: ret = 0x%X \r\n",__FUNCTION__,ret);	
		return VPU_DEC_RET_FAILURE;
	}

	//set dec parameters		
	//clear 0 firstly
	vpu_memset(&decParam,0,sizeof(DecParam));

#if 0 //if we have flushed bitstream before, we need not set skip mode
	decParam.skipframeMode=0;
	decParam.skipframeNum=0;
	decParam.iframeSearchEnable=0;
#else
	decParam.skipframeMode=3;  // skip all
	decParam.skipframeNum=1;
	decParam.iframeSearchEnable=0;
#endif	

	decParam.chunkSize=0;	//for file mode

	if(startFrameOK==0)
	{
		// start decode frame
		VPU_TRACE;
		VPU_API("calling vpu_DecStartOneFrame() \r\n");
		ret = vpu_DecStartOneFrame(pVpuObj->handle, &decParam);
		VPU_TRACE;
		if (ret != RETCODE_SUCCESS)
		{
			VPU_ERROR("%s: vpu start one frame failure: ret = 0x%X \r\n",__FUNCTION__,ret);
			return VPU_DEC_RET_FAILURE;
		}
	}
	else
	{
		//we already called vpu_DecStartOneFrame() before
	}

	nExtCount=0;
	while(1)
	{
		int nCount=0;			// for internal while loop
		// wait finished
#if 1 
		VPU_API("calling vpu_WaitForInt(%d) \r\n",VPU_TIME_OUT);
		while(0!=vpu_WaitForInt(VPU_TIME_OUT))
		{
			nCount++;
			if(nCount >VPU_MAX_TIME_OUT_CNT)
			{
				VPU_ERROR("%s: flush time out \r\n",__FUNCTION__);	
				pObj->state=VPU_DEC_STATE_CORRUPT;
				return VPU_DEC_RET_FAILURE_TIMEOUT;
			}
		}
#else
		VPU_API("calling vpu_IsBusy() \r\n");
		while (vpu_IsBusy())
		{
			nCount++;
			if(nCount >VPU_MAX_TIME_OUT_CNT)
			{
				VPU_ERROR("%s: flush time out \r\n",__FUNCTION__);	
				pObj->state=VPU_DEC_STATE_CORRUPT;
				return VPU_DEC_RET_FAILURE_TIMEOUT;
			}
			VPU_API("calling vpu_WaitForInt(%d): %d \r\n",VPU_TIME_OUT,nCount);
			vpu_WaitForInt(VPU_TIME_OUT);
		};
#endif
		// get output
		VPU_TRACE;
		VPU_API("calling vpu_DecGetOutputInfo() \r\n");
		ret = vpu_DecGetOutputInfo(pVpuObj->handle, &outInfo);
		VPU_API("calling vpu_DecGetOutputInfo(), indexFrameDecoded: %d, return indexFrameDisplay: %d , success: 0x%X \r\n",outInfo.indexFrameDecoded,outInfo.indexFrameDisplay,outInfo.decodingSuccess);
		VPU_TRACE;
		if (ret != RETCODE_SUCCESS)
		{
			VPU_ERROR("%s: vpu get output info failure: ret = 0x%X \r\n",__FUNCTION__,ret);
			return VPU_DEC_RET_FAILURE;
		}

		if((outInfo.indexFrameDecoded<0)&&(outInfo.indexFrameDisplay<0)&&(VPU_OUT_DIS_INDEX_EOS!=outInfo.indexFrameDisplay))
		{
			//Be carefully: we may get many skipped output frames when skipmode is enabled, So, we had better call vpu_DecBitBufferFlush() !!!
			nExtCount++;
			//fixed case for: VGA1000kbps23fps128kbps44kHz.avi
			//in such case, we can not get the EOS output. As result, enter dead loop
			if(nExtCount>VPU_MAX_EOS_DEAD_LOOP_CNT)
			{
				VPU_ERROR("%s: eos time out \r\n",__FUNCTION__);
				pObj->state=VPU_DEC_STATE_CORRUPT;
				return VPU_DEC_RET_FAILURE_TIMEOUT;
			}
#ifdef VPU_SUPPORT_NO_ENOUGH_FRAME
			if(VPU_OUT_DEC_INDEX_EOS==outInfo.indexFrameDecoded)
			{
				//avoid eos time out !!!
				//can repeat call clear flag !!!
				VpuClearAllDispFrameFlag(pVpuObj->handle, pVpuObj->obj.frameNum);
			}
#endif
			
		}
		else
		{
			nExtCount=0;
		}

		//clear output frame
		if(outInfo.indexFrameDisplay>=0)
		{
			VPU_TRACE;
			VPU_API("%s: calling vpu_DecClrDispFlag(): %d \r\n",__FUNCTION__,outInfo.indexFrameDisplay);
			ret=vpu_DecClrDispFlag(pVpuObj->handle,outInfo.indexFrameDisplay);
			VPU_TRACE;
			if(RETCODE_SUCCESS!=ret)
			{
				VPU_ERROR("%s: vpu clear display frame failure, index=0x%X, ret=%d \r\n",__FUNCTION__,outInfo.indexFrameDisplay,ret);
				return VPU_DEC_RET_FAILURE;
			}

			//reset historical info
			//if(pVpuObj->obj.frameBuf[outInfo.indexFrameDisplay]==pVpuObj->obj.pPreDisplayFrameBuf)
			//{
			//	pVpuObj->obj.pPreDisplayFrameBuf=NULL;
			//}				
		}

		//check the EOS
		if(VPU_OUT_DIS_INDEX_EOS==outInfo.indexFrameDisplay)
		{
			break;
		}

		//flush bitstream to :
		//(1) improve performance
		//(2) avoid get big nExtCount value (=> fake corrupt event)
		if(startFrameOK==0)
		{
			//vpu_DecBitBufferFlush() have already been called before !
		}
		else
		{
			if(0==VpuBitFlush(pVpuObj, 1/*before eos*/))
			{
				return VPU_DEC_RET_FAILURE;
			}		
			startFrameOK=0; //clear it to avoid repeat calling vpu_DecBitBufferFlush() in current while(1) loop
		}

		// start decode frame
		VPU_TRACE;
		VPU_API("calling vpu_DecStartOneFrame() \r\n");
		ret = vpu_DecStartOneFrame(pVpuObj->handle, &decParam);
		VPU_TRACE;
		if (ret != RETCODE_SUCCESS)
		{
			VPU_ERROR("%s: vpu start one frame failure: ret = 0x%X \r\n",__FUNCTION__,ret);
			return VPU_DEC_RET_FAILURE;
		}

	}

//#ifdef VPU_FAKE_FLUSH
FLUSH_FINISH:
//#endif

	//FIX case: if user send 0 bytes after flush operation, decoder will always return EOS. it is not reasonable.
	//so we set VPU_DEC_STATE_EOS state, but not VPU_DEC_STATE_DEC.
	pObj->state=VPU_DEC_STATE_EOS;	

#ifdef VPU_SUPPORT_UNCLOSED_GOP
	pObj->refCnt=0;
	pObj->dropBCnt=0;
	pObj->keyCnt=0;
#endif		
#ifdef VPU_IFRAME_SEARCH
	pObj->keyDecCnt=0;
#endif
#ifdef VPU_SEEK_ANYPOINT_WORKAROUND
	pObj->seekKeyLoc=0;
	pObj->recommendFlush=0;
#endif
#ifdef VPU_SUPPORT_NO_ENOUGH_FRAME
	pObj->dataUsedInFileMode=0;
	pObj->lastDatLenInFileMode=0;
	pObj->lastConfigMode=0;
#endif

#ifdef VPU_FILEMODE_SUPPORT_INTERLACED_SKIPMODE
	pObj->firstFrameMode=0;
	pObj->fieldCnt=0;		/*now, we suppose parser always feed integrated frame !!!!*/
#endif

#ifdef VPU_FILEMODE_MERGE_INTERLACE_DEBUG
	//pObj->needMergeFields=0;
	pObj->lastFieldOffset=0;
#endif

#ifdef VPU_FILEMODE_INTERLACE_TIMESTAMP_ENHANCE
	//pObj->fieldDecoding=;
	pObj->oweFieldTS=0;
#endif

#ifdef VPU_WRAPPER_DUMP
	g_seek_dump=1;
#endif
	return VPU_DEC_RET_SUCCESS;
}


VpuDecRetCode VPU_DecClose(VpuDecHandle InHandle)
{
	VpuDecHandleInternal * pVpuObj;
	RetCode ret;
	
	if(InHandle==NULL) 
	{
		VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);	
		return VPU_DEC_RET_INVALID_HANDLE;
	}

	pVpuObj=(VpuDecHandleInternal *)InHandle;

	//switch(pVpuObj->obj.state)
	//{
	//	case VPU_DEC_STATE_CORRUPT:
	//		break;
	//	default:
	//		break;
	//}
#if 0 //for iMX6: reset is not recommended !!!!
	//add robust : if busy(fix some timeout issue) , reset it 
	VPU_API("calling vpu_IsBusy() \r\n");
	if(vpu_IsBusy())
	{
		VPU_API("calling vpu_SWReset(0x%X,0) \r\n",(unsigned int)pVpuObj->handle);
		ret=vpu_SWReset(pVpuObj->handle,0);
		if(RETCODE_SUCCESS!=ret)
		{
			VPU_ERROR("%s: vpu reset failure, ret=%d \r\n",__FUNCTION__,ret);
			//return VPU_DEC_RET_FAILURE;
		}	
	}	
#endif	
#ifdef IMX6_MULTI_FORMATS_WORKAROUND	//below has been merged into vpu_SWReset()
	else
	{
		printf("imx6: will reset , handle: 0x%X, index: %d \r\n",(unsigned int)pVpuObj->handle,0);
#if 0	//board will dead !!!!
		vpu_SWReset(pVpuObj->handle,0);	//must be called before vpu_DecClose(), otherwise, invalid handle will be returned
#else
		IOClkGateSet(1);
		VpuWriteReg(0x24, 0x1F8);
		//usleep(1000);
		// wait until reset is done
		while(VpuReadReg(0x34) != 0){};
		// clear sw reset (not automatically cleared)
		VpuWriteReg(0x24, 0);
		IOClkGateSet(0);
#endif
	}
#endif

	//normal close
	VPU_TRACE;
	VPU_API("calling vpu_DecClose() \r\n");
	ret=vpu_DecClose(pVpuObj->handle);
	VPU_TRACE;
	if(RETCODE_SUCCESS!=ret)
	{
		VPU_ERROR("%s: vpu close failure, ret=%d \r\n",__FUNCTION__,ret);
		return VPU_DEC_RET_FAILURE;
	}	

	return VPU_DEC_RET_SUCCESS;
}


VpuDecRetCode VPU_UnLoad()
{
#if 0//#ifdef IMX6_MULTI_FORMATS_WORKAROUND	//below has been merged into vpu_SWReset()
	printf("imx6: will reset \r\n");
	ret=vpu_SWReset(pVpuObj->handle,0);
/*
	IOClkGateSet(1);
	VpuWriteReg(0x24, 0x1F8);
	usleep(1000);
	// wait until reset is done
	while(VpuReadReg(0x34) != 0){};
	// clear sw reset (not automatically cleared)
	VpuWriteReg(0x24, 0);
	IOClkGateSet(0);
*/	
#endif

#ifdef VPU_RESET_TEST
#if 1  //for iMX6
	//loading fw, avoid reset board after fw changing
	IOClkGateSet(1);
	VpuWriteReg(0x0, 0);
	VpuWriteReg(0x14, 1);		
	IOClkGateSet(0);
#else	//
	IOSysSWReset();	//for iMX5, iMX6 has not implemented it now
#endif
#endif
	VPU_TRACE;
	VPU_API("calling vpu_UnInit() \r\n");
	vpu_UnInit();
	VPU_TRACE;

	TIMER_MARK_REPORT(TIMER_MARK_GETOUTPUT_ID);
	//TIMER_REPORT(TIMER_CLEARDISP_ID);
	
	return VPU_DEC_RET_SUCCESS;	
}


VpuDecRetCode VPU_DecReset(VpuDecHandle InHandle)
{
	VpuDecHandleInternal * pVpuObj;
	RetCode ret;
	VPU_LOG("in VPU_DecReset, InHandle: 0x%X  \r\n",InHandle);
	
	if(InHandle==NULL) 
	{
#if 0	//no use	
#define MAX_NUM_INSTANCE	4	//in vpu_util.h
		//VPU_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);
		//return VPU_DEC_RET_INVALID_HANDLE;
		//reset all instances:
		int index;
		for(index=0;index<MAX_NUM_INSTANCE;index++)
		{
			VPU_API("calling vpu_SWReset(0,%d) \r\n",index);
			ret=vpu_SWReset(0,index);
			if(RETCODE_SUCCESS!=ret)
			{
				VPU_ERROR("%s: vpu reset failure, ret=%d \r\n",__FUNCTION__,ret);
				return VPU_DEC_RET_FAILURE;
			}	
		}
#endif		
		return VPU_DEC_RET_SUCCESS;
	}

	pVpuObj=(VpuDecHandleInternal *)InHandle;

	//TODO: current SWReset need to re-register all frame buffers again.
	VPU_TRACE;
	VPU_API("calling vpu_SWReset(0x%X,0) \r\n",(unsigned int)pVpuObj->handle);
	ret=vpu_SWReset(pVpuObj->handle,0);
	VPU_TRACE;

#ifdef IMX6_MULTI_FORMATS_WORKAROUND	//1 for iMX6
#if 0	//below has been merged into vpu_SWReset()
	printf("imx6: will reset \r\n");
	IOClkGateSet(1);
	VpuWriteReg(0x24, 0x1F8);
	usleep(1000);
	// wait until reset is done
	while(VpuReadReg(0x34) != 0){};
	// clear sw reset (not automatically cleared)
	VpuWriteReg(0x24, 0);
	IOClkGateSet(0);
#endif
#if 0	//need ??
	//loading fw, avoid reset board after fw changing
	IOClkGateSet(1);
	VpuWriteReg(0x0, 0);
	VpuWriteReg(0x14, 1);	
	IOClkGateSet(0);
#endif	
#endif

	if(RETCODE_SUCCESS!=ret)
	{
		VPU_ERROR("%s: vpu reset failure, ret=%d \r\n",__FUNCTION__,ret);
		return VPU_DEC_RET_FAILURE;
	}	

	return VPU_DEC_RET_SUCCESS;
	
}


VpuDecRetCode VPU_DecGetMem(VpuMemDesc* pInOutMem)
{
	int ret;
	
#ifdef __WINCE
	VPUMemAlloc buff;
	ret=vpu_AllocPhysMem(pInOutMem->nSize,&buff);
	if(ret!=RETCODE_SUCCESS)
	{
		VPU_ERROR("%s: get memory failure: size=%d, ret=%d \r\n",__FUNCTION__,pInOutMem->nSize,ret);
		return VPU_DEC_RET_FAILURE;
	}	
	pInOutMem->nPhyAddr=buff.PhysAdd;
	pInOutMem->nVirtAddr=buff.VirtAdd;
	pInOutMem->nCpuAddr=buff.Reserved;	
#else
	vpu_mem_desc buff;
	buff.size=pInOutMem->nSize;
	ret=IOGetPhyMem(&buff);
	if(ret) //if(ret!=RETCODE_SUCCESS)
	{
		VPU_ERROR("%s: get physical memory failure: size=%d, ret=%d \r\n",__FUNCTION__,buff.size,ret);
		return VPU_DEC_RET_FAILURE;
	}
	ret=IOGetVirtMem(&buff);
	if(ret<=0) //if(ret!=RETCODE_SUCCESS)
	{
		VPU_ERROR("%s: get virtual memory failure: size=%d, ret=%d \r\n",__FUNCTION__,buff.size,ret);
		return VPU_DEC_RET_FAILURE;
	}

	pInOutMem->nPhyAddr=buff.phy_addr;
	pInOutMem->nVirtAddr=buff.virt_uaddr;
	pInOutMem->nCpuAddr=buff.cpu_addr;
#endif

	return VPU_DEC_RET_SUCCESS;
}

VpuDecRetCode VPU_DecFreeMem(VpuMemDesc* pInMem)
{
	int ret;

#ifdef __WINCE
	VPUMemAlloc buff;
	buff.PhysAdd=pInMem->nPhyAddr;
	buff.VirtAdd=pInMem->nVirtAddr;
	buff.Reserved=pInMem->nCpuAddr;	
	ret=vpu_FreePhysMem(&buff);
	if(ret!=RETCODE_SUCCESS)
	{
		VPU_ERROR("%s: free memory failure: size=%d, ret=%d \r\n",__FUNCTION__,pInMem->nSize,ret);
		return VPU_DEC_RET_FAILURE;
	}	
#else
	vpu_mem_desc buff;
	buff.size=pInMem->nSize;
	buff.phy_addr=pInMem->nPhyAddr;
	buff.virt_uaddr=pInMem->nVirtAddr;
	buff.cpu_addr=pInMem->nCpuAddr;
	ret=IOFreeVirtMem(&buff);
	if(ret!=RETCODE_SUCCESS)
	{
		VPU_ERROR("%s: free virtual memory failure: size=%d, ret=%d \r\n",__FUNCTION__,buff.size,ret);
		return VPU_DEC_RET_FAILURE;
	}	
	ret=IOFreePhyMem(&buff);
	if(ret!=RETCODE_SUCCESS)
	{
		VPU_ERROR("%s: free phy memory failure: size=%d, ret=%d \r\n",__FUNCTION__,buff.size,ret);
		return VPU_DEC_RET_FAILURE;
	}	
#endif

	return VPU_DEC_RET_SUCCESS;	
}

/****************************** encoder part **************************************/


typedef struct
{
	int nHeaderNeeded;	// indicate whether need to fill header(vos/pps/sps/...) info
#ifdef VPU_ENC_SEQ_DATA_SEPERATE
	int nJustOutputOneHeader;	//record the state
	int nOutputHeaderCnt;		//for H.264: In fact, muxer may only receive the first header, So we had better 
								//(1) for first header: output header and frame seperately
								//(2) for non-first header: merge header and frame data
#endif
	int nDynamicEnabled;			//0: output is bitstream buf, 1(not supported on iMX6): output is pointed by user
	unsigned char* pPhyBitstream;
	unsigned char* pVirtBitstream;
	int nBitstreamSize;
	unsigned char* pPhyScratch;		//for mpeg4	
	unsigned char* pVirtScratch;		//for mpeg4
	int nScratchSize;
	int nFrameCnt;
}VpuEncObj;

typedef struct 
{
	EncHandle handle;
	VpuEncObj obj;	
}VpuEncHandleInternal;


#ifdef VPU_ENC_OUTFRAME_ALIGN
#define	VPU_ALIGN_BYTES_NUM	8	//for H.264, seems 4 bytes is not enough when writing SPS/PPS header ??? 
#endif

#define Align(ptr,align)	(((unsigned int)ptr+(align)-1)/(align)*(align))
//#define MemAlign(mem,align)	((((unsigned int)mem)%(align))==0)
//#define MemNotAlign(mem,align)	((((unsigned int)mem)%(align))!=0)

#define VPU_ENC_MAX_FRAME_INDEX	30
//#define ENC_MAX_FRAME_NUM		(VPU_ENC_MAX_NUM_MEM)
//#define ENC_FRAME_SURPLUS	(1)

//#define DEFAULT_ENC_FRM_WIDTH		(640)
//#define DEFAULT_ENC_FRM_HEIGHT		(480)
//#define DEFAULT_ENC_FRM_RATE			(30 * Q16_SHIFT)

//#define DEFAULT_ENC_BUF_IN_CNT		0x3
//#define DEFAULT_ENC_BUF_IN_SIZE		(DEFAULT_ENC_FRM_WIDTH*DEFAULT_ENC_FRM_HEIGHT*3/2)
//#define DEFAULT_ENC_BUF_OUT_CNT		0x3

#define ENC_VIRT_INDEX	0
#define ENC_PHY_INDEX	1

#define VPU_ENC_MEM_ALIGN			0x8
#define VPU_ENC_BITS_BUF_SIZE		(1024*1024)		//bitstream buffer size 
#define VPU_ENC_MPEG4_SCRATCH_SIZE	0x080000	//for mpeg4 data partition

#define VPU_ENC_WAIT_TIME_OUT			(500)	//used for prescan mode
#define VPU_ENC_MAX_BUSY_CNT			(4)		//used for prescan mode: max counts 


#ifdef VPU_ENC_OUTFRAME_ALIGN
int VpuEncFillZeroBytesForAlign(unsigned int nInPhyAddr, unsigned int nInVirtAddr)
{
	unsigned int nBaseAddr;
	unsigned int nAlignedAddr;
	int nFillZeroSize;
	nBaseAddr=nInPhyAddr;
	nAlignedAddr=(unsigned int)Align(nBaseAddr, VPU_ALIGN_BYTES_NUM);
	nFillZeroSize=nAlignedAddr-nBaseAddr;
	vpu_memset((void*)nInVirtAddr,0,nFillZeroSize);	
	ASSERT(nFillZeroSize>=0);
	return nFillZeroSize;
}
#endif

int VpuEncFillHeader(EncHandle InHandle,VpuEncEncParam* pInParam, unsigned char* pInHeaderBufPhy,
	int* pOutHeaderLen,int* pOutPadLen,unsigned char* pInHeaderBufVirt,int mode,
	unsigned char* pInBitstreamPhy,unsigned char* pInBitstreamVirt)
{
#define BitVirtAddr(phy)	((int)pInBitstreamVirt+(int)phy-(int)pInBitstreamPhy)
	EncHeaderParam sEncHdrParam;
	int nMbPicNum;
	unsigned char* pPhyPtr=pInHeaderBufPhy;
	unsigned char* pVirtPtr=pInHeaderBufVirt;
	int nHeaderLen=0;
	int nFilledZeroBytes=0;

	vpu_memset(&sEncHdrParam, 0, sizeof(EncHeaderParam));
	//Now, sEncHdrParam.buf is only valid when mode==0 or 1(eg. dynamic)
	sEncHdrParam.buf=(PhysicalAddress)pPhyPtr;
	
	/* Must put encode header before encoding */
	//for MPEG4: at least VOL is required
	//for H264: SPS/PPS are required
	if(pInParam->eFormat==VPU_V_MPEG4)
	{
		int nFrameRate=pInParam->nFrameRate;
		int nEncPicWidth=pInParam->nPicWidth;
		int nEncPicHeight=pInParam->nPicHeight;
		
		sEncHdrParam.headerType = VOS_HEADER;
		/*
		* Please set userProfileLevelEnable to 0 if you need to generate
		* user profile and level automaticaly by resolution, here is one
		* sample of how to work when userProfileLevelEnable is 1.
		*/
		sEncHdrParam.userProfileLevelEnable = 1;
		nMbPicNum = ((nEncPicWidth + 15) / 16) *((nEncPicHeight+ 15) / 16);
		/* Please set userProfileLevelIndication to 8 if L0 is needed */
		if (nEncPicWidth<= 176 && nEncPicHeight <= 144 &&	nMbPicNum * nFrameRate <= 1485)
		{
			sEncHdrParam.userProfileLevelIndication = 8; /* L1 */
		}
		else if (nEncPicWidth <= 352 && nEncPicHeight<= 288 &&	nMbPicNum * nFrameRate <= 5940)
		{
			sEncHdrParam.userProfileLevelIndication = 2; /* L2 */
		}
		else if (nEncPicWidth <= 352 && nEncPicHeight <= 288 &&nMbPicNum * nFrameRate <= 11880)
		{
			sEncHdrParam.userProfileLevelIndication = 3; /* L3 */
		}
		else if (nEncPicWidth <= 640 && nEncPicHeight<= 480 &&	nMbPicNum * nFrameRate <= 36000)
		{
			sEncHdrParam.userProfileLevelIndication = 4; /* L4a */
		}
		else if (nEncPicWidth <= 720 && nEncPicHeight <= 576 &&nMbPicNum * nFrameRate <= 40500)
		{
			sEncHdrParam.userProfileLevelIndication = 5; /* L5 */
		}
		else
		{
			sEncHdrParam.userProfileLevelIndication = 6; /* L6 */
		}

		VPU_ENC_API("calling vpu_EncGiveCommand(VOS_HEADER) \r\n");
		vpu_EncGiveCommand(InHandle, ENC_PUT_MP4_HEADER, &sEncHdrParam);
		VPU_ENC_LOG("VOS length: %d \r\n",sEncHdrParam.size);		
		if(mode==1)
		{
			nHeaderLen+=sEncHdrParam.size;	//record VOS length
			nFilledZeroBytes=VpuEncFillZeroBytesForAlign((unsigned int)(pPhyPtr+nHeaderLen),(unsigned int)(pVirtPtr+nHeaderLen));
			nHeaderLen+=nFilledZeroBytes;
		}
		else if(mode==2)
		{
			VPU_ENC_LOG("header memcpy: dst: 0x%X, src: 0x%X, size: %d \r\n",(pVirtPtr+nHeaderLen),BitVirtAddr(sEncHdrParam.buf),sEncHdrParam.size);
			memcpy((void*)(pVirtPtr+nHeaderLen),(void*)BitVirtAddr(sEncHdrParam.buf),sEncHdrParam.size);
			nHeaderLen+=sEncHdrParam.size;	//record VOS length
		}
		sEncHdrParam.headerType = VIS_HEADER;
		sEncHdrParam.buf=(PhysicalAddress)(pPhyPtr+nHeaderLen);	//skip VOS
		VPU_ENC_API("calling vpu_EncGiveCommand(VIS_HEADER) \r\n");		
		vpu_EncGiveCommand(InHandle, ENC_PUT_MP4_HEADER, &sEncHdrParam);
		VPU_ENC_LOG("VIS length: %d \r\n",sEncHdrParam.size);		
		if(mode==1)
		{
			nHeaderLen+=sEncHdrParam.size;	//record VIS length
			nFilledZeroBytes=VpuEncFillZeroBytesForAlign((unsigned int)(pPhyPtr+nHeaderLen),(unsigned int)(pVirtPtr+nHeaderLen));
			nHeaderLen+=nFilledZeroBytes;
		}
		else if(mode==2)
		{
			VPU_ENC_LOG("header memcpy: dst: 0x%X, src: 0x%X, size: %d \r\n",(pVirtPtr+nHeaderLen),BitVirtAddr(sEncHdrParam.buf),sEncHdrParam.size);
			memcpy((void*)(pVirtPtr+nHeaderLen),(void*)BitVirtAddr(sEncHdrParam.buf),sEncHdrParam.size);
			nHeaderLen+=sEncHdrParam.size;	//record VIS length
		}
		sEncHdrParam.headerType = VOL_HEADER;
		sEncHdrParam.buf=(PhysicalAddress)(pPhyPtr+nHeaderLen);	//skip VOS and VIS
		VPU_ENC_API("calling vpu_EncGiveCommand(VOL_HEADER) \r\n");		
		vpu_EncGiveCommand(InHandle, ENC_PUT_MP4_HEADER, &sEncHdrParam);
		VPU_ENC_LOG("VOL length: %d \r\n",sEncHdrParam.size);		
		if(mode==1)
		{
			nHeaderLen+=sEncHdrParam.size;	//record VOL length
			nFilledZeroBytes=VpuEncFillZeroBytesForAlign((unsigned int)(pPhyPtr+nHeaderLen),(unsigned int)(pVirtPtr+nHeaderLen));
			nHeaderLen+=nFilledZeroBytes;
		}
		else if(mode==2)
		{
			VPU_ENC_LOG("header memcpy: dst: 0x%X, src: 0x%X, size: %d \r\n",(pVirtPtr+nHeaderLen),BitVirtAddr(sEncHdrParam.buf),sEncHdrParam.size);	
			memcpy((void*)(pVirtPtr+nHeaderLen),(void*)BitVirtAddr(sEncHdrParam.buf),sEncHdrParam.size);
			nHeaderLen+=sEncHdrParam.size;	//record VOL length		
		}
	}
	else if (pInParam->eFormat == VPU_V_AVC) 
	{
		sEncHdrParam.headerType = SPS_RBSP;
		VPU_ENC_API("calling vpu_EncGiveCommand(SPS_RBSP) \r\n");
		vpu_EncGiveCommand(InHandle, ENC_PUT_AVC_HEADER, &sEncHdrParam);
		VPU_ENC_LOG("SPS_RBSP length: %d \r\n",sEncHdrParam.size);
		if(mode==1)
		{
			nHeaderLen+=sEncHdrParam.size;	//record SPS length
			nFilledZeroBytes=VpuEncFillZeroBytesForAlign((unsigned int)(pPhyPtr+nHeaderLen),(unsigned int)(pVirtPtr+nHeaderLen));
			nHeaderLen+=nFilledZeroBytes;
		}
		else if(mode==2)
		{
			VPU_ENC_LOG("header memcpy: dst: 0x%X, src: 0x%X, size: %d \r\n",(pVirtPtr+nHeaderLen),BitVirtAddr(sEncHdrParam.buf),sEncHdrParam.size);	
			memcpy((void*)(pVirtPtr+nHeaderLen),(void*)BitVirtAddr(sEncHdrParam.buf),sEncHdrParam.size);
			nHeaderLen+=sEncHdrParam.size;	//record SPS length		
		}
		sEncHdrParam.headerType = PPS_RBSP;
		sEncHdrParam.buf=(PhysicalAddress)(pPhyPtr+nHeaderLen);	//skip SPS 
		VPU_ENC_API("calling vpu_EncGiveCommand(PPS_RBSP) \r\n");
		vpu_EncGiveCommand(InHandle, ENC_PUT_AVC_HEADER, &sEncHdrParam);
		VPU_ENC_LOG("PPS_RBSP length: %d \r\n",sEncHdrParam.size);
		if(mode==1)
		{
			nHeaderLen+=sEncHdrParam.size;	//record PPS length
			nFilledZeroBytes=VpuEncFillZeroBytesForAlign((unsigned int)(pPhyPtr+nHeaderLen),(unsigned int)(pVirtPtr+nHeaderLen));
			nHeaderLen+=nFilledZeroBytes;
		}
		else if(mode==2)
		{
			VPU_ENC_LOG("header memcpy: dst: 0x%X, src: 0x%X, size: %d \r\n",(pVirtPtr+nHeaderLen),BitVirtAddr(sEncHdrParam.buf),sEncHdrParam.size);			
			memcpy((void*)(pVirtPtr+nHeaderLen),(void*)BitVirtAddr(sEncHdrParam.buf),sEncHdrParam.size);
			nHeaderLen+=sEncHdrParam.size;	//record PPS length		
		}
	}
	else if (pInParam->eFormat == VPU_V_MJPG) 
	{
		VPU_ENC_ERROR("%s: MJPG is not supported \r\n",__FUNCTION__);
		return -1;
	}	
	else
	{
		//
	}

	*pOutHeaderLen=nHeaderLen;	
	*pOutPadLen=nFilledZeroBytes;
	return 1;
}

int VpuEncSetSrcFrame(FrameBuffer* pFrame, unsigned char* pSrc, int nSize, int nPadW,int nPadH, unsigned char format)
{
	int yStride;
	int uvStride;	
	int ySize;
	int uvSize;
	int mvSize;	

	yStride=nPadW;
	uvStride=yStride/2;

	ySize=yStride*nPadH;
	uvSize=ySize/4;
	mvSize=ySize/4;	//1 set 0 ??

	pFrame->bufY=(PhysicalAddress)pSrc;
	pFrame->bufCb=(PhysicalAddress)(pSrc+ySize);
	pFrame->bufCr=(PhysicalAddress)(pSrc+ySize+uvSize);
	pFrame->bufMvCol=(PhysicalAddress)(pSrc+ySize+uvSize*2);	//1 ??
	pFrame->strideY=yStride;
	pFrame->strideC=uvStride;

	return 1;	
}

int VpuEncWaitBusy()
{
	int busy_cnt=0;

	VPU_ENC_API("while: calling vpu_WaitForInt(%d) \r\n",VPU_ENC_WAIT_TIME_OUT);
	while(0!=vpu_WaitForInt(VPU_ENC_WAIT_TIME_OUT))
	{
		busy_cnt++;
		if(busy_cnt> VPU_ENC_MAX_BUSY_CNT)
		{
			VPU_ENC_ERROR("while: wait busy : time out : count: %d \r\n",(UINT32)busy_cnt);
			return -1;             //time out for some corrupt clips
		}
	}
	return 1;
}

int VpuEncGetRotStride(int nInRot,int nInOriWidth,int nInOriHeight,int* pOutWidth,int* pOutHeight)
{
	if ((nInRot== 90) || (nInRot == 270)) 
	{
		*pOutWidth =nInOriHeight;
		*pOutHeight = nInOriWidth;
	} 
	else
	{
		*pOutWidth =nInOriWidth;
		*pOutHeight = nInOriHeight;
	}
	return 1;
}

int VpuEncGetIntraQP(VpuEncOpenParamSimp * pInParam)
{
#if 0 
	//FIXME: we need set one appropriate value for it based on other parameters (such as bitrate,resolution,framerate,...)
	//based on bits/pixel  ???
	if(VPU_V_AVC==pInParam->eFormat)
	{
		return 20;	//0-51
	}
	else
	{
		return 15;	//1-31
	}
#else
	return -1;
#endif
}

VpuEncRetCode VPUEnc_Load()
{
	RetCode ret;
	VPU_ENC_API("calling vpu_Init() \r\n");	
	ret=vpu_Init(NULL);
	if(RETCODE_SUCCESS !=ret)
	{
		VPU_ENC_ERROR("%s: vpu init failure \r\n",__FUNCTION__);	
		return VPU_ENC_RET_FAILURE;
	}

	return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPUEnc_UnLoad()
{
#ifndef IMX6_LD_BUG_WORKAROUND
	VPU_ENC_API("calling vpu_UnInit() \r\n");
#endif
	vpu_UnInit();
	return VPU_ENC_RET_SUCCESS;	
}

VpuEncRetCode VPUEnc_Reset(VpuEncHandle InHandle)
{
	VpuEncHandleInternal * pVpuObj;
	RetCode ret;
	
	if(InHandle==NULL) 
	{
		return VPU_ENC_RET_SUCCESS;
	}

	pVpuObj=(VpuEncHandleInternal *)InHandle;

	//TODO: current SWReset need to re-register all frame buffers again.
	VPU_ENC_API("calling vpu_SWReset(0x%X,0) \r\n",(UINT32)pVpuObj->handle);
	ret=vpu_SWReset(pVpuObj->handle,0);
	if(RETCODE_SUCCESS!=ret)
	{
		VPU_ENC_ERROR("%s: vpu reset failure, ret=%d \r\n",__FUNCTION__,ret);
		return VPU_ENC_RET_FAILURE;
	}	
	
	return VPU_ENC_RET_SUCCESS;
	
}

VpuEncRetCode VPUEnc_Open(VpuEncHandle *pOutHandle, VpuMemInfo* pInMemInfo,VpuEncOpenParam* pInParam)
{
	VpuMemSubBlockInfo * pMemPhy;
	VpuMemSubBlockInfo * pMemVirt;
	VpuEncHandleInternal* pVpuObj;
	VpuEncObj* pObj;

	RetCode ret;
	EncOpenParam sEncOpenParam;
	int nValidWidth;
	int nValidHeight;
	
	pMemVirt=&pInMemInfo->MemSubBlock[ENC_VIRT_INDEX];
	pMemPhy=&pInMemInfo->MemSubBlock[ENC_PHY_INDEX];
	if ((pMemVirt->pVirtAddr==NULL) || MemNotAlign(pMemVirt->pVirtAddr,VPU_ENC_MEM_ALIGN)
		||(pMemVirt->nSize!=sizeof(VpuEncHandleInternal)))
	{
		VPU_ENC_ERROR("%s: failure: invalid parameter ! \r\n",__FUNCTION__);	
		return VPU_ENC_RET_INVALID_PARAM;
	}

	if ((pMemPhy->pVirtAddr==NULL) || MemNotAlign(pMemPhy->pVirtAddr,VPU_ENC_MEM_ALIGN)
		||(pMemPhy->pPhyAddr==NULL) || MemNotAlign(pMemPhy->pPhyAddr,VPU_ENC_MEM_ALIGN)
		||(pMemPhy->nSize<VPU_ENC_BITS_BUF_SIZE))
	{
		VPU_ENC_ERROR("%s: failure: invalid parameter !! \r\n",__FUNCTION__);	
		return VPU_ENC_RET_INVALID_PARAM;
	}

	nValidWidth=pInParam->nPicWidth;
	nValidHeight=pInParam->nPicHeight;
#ifdef VPU_ENC_ALIGN_LIMITATION
	nValidWidth=nValidWidth/16*16;
	nValidHeight=nValidHeight/16*16;
#endif

	pVpuObj=(VpuEncHandleInternal*)pMemVirt->pVirtAddr;
	pObj=&pVpuObj->obj;

	//clear 0 firstly
	vpu_memset(&sEncOpenParam, 0, sizeof(EncOpenParam));
	vpu_memset(pObj, 0, sizeof(VpuEncObj));
	
	//set parameters
	sEncOpenParam.bitstreamBuffer =  (PhysicalAddress)pMemPhy->pPhyAddr;
	sEncOpenParam.bitstreamBufferSize = VPU_ENC_BITS_BUF_SIZE;

#if 1 //for iMX6
	ASSERT(pMemVirt->nSize>=VPU_ENC_BITS_BUF_SIZE+VPU_ENC_MPEG4_SCRATCH_SIZE);
	pObj->pPhyBitstream=pMemPhy->pPhyAddr;
	pObj->pVirtBitstream=pMemPhy->pVirtAddr;
	pObj->nBitstreamSize=VPU_ENC_BITS_BUF_SIZE;
	pObj->pPhyScratch=pMemPhy->pPhyAddr+VPU_ENC_BITS_BUF_SIZE; //make sure it is aligned
	pObj->pVirtScratch=pMemPhy->pVirtAddr+VPU_ENC_BITS_BUF_SIZE; //make sure it is aligned	
	pObj->nScratchSize=VPU_ENC_MPEG4_SCRATCH_SIZE;
	VPU_ENC_LOG("bitstream: phy: 0x%X, virt: 0x%X, size: %d \r\n",pObj->pPhyBitstream,pObj->pVirtBitstream,pObj->nBitstreamSize);
#endif

#if 0 //1 eagle debug
	pInParam->BistreamPhy=(unsigned int)pMemPhy->pPhyAddr;
	pInParam->BistreamVirt=(unsigned int)pMemPhy->pVirtAddr;
	pInParam->BitstreamSize=(unsigned int)pMemPhy->nSize;
#endif

	/* If rotation angle is 90 or 270, pic width and height are swapped */
	VpuEncGetRotStride(pInParam->nRotAngle,nValidWidth,nValidHeight,(int*)(&sEncOpenParam.picWidth),(int*)(&sEncOpenParam.picHeight));

	sEncOpenParam.frameRateInfo = pInParam->nFrameRate;
	sEncOpenParam.bitRate = pInParam->nBitRate;
	sEncOpenParam.gopSize = pInParam->nGOPSize;
	sEncOpenParam.slicemode.sliceMode = pInParam->sliceMode.sliceMode;	/* 0: 1 slice per picture; 1: Multiple slices per picture */
	sEncOpenParam.slicemode.sliceSizeMode = pInParam->sliceMode.sliceSizeMode; /* 0: silceSize defined by bits; 1: sliceSize defined by MB number*/
	sEncOpenParam.slicemode.sliceSize = pInParam->sliceMode.sliceSize;  /* Size of a slice in bits or MB numbers */

	sEncOpenParam.initialDelay = pInParam->nInitialDelay;
	sEncOpenParam.vbvBufferSize = pInParam->nVbvBufferSize;        /* 0 = ignore 8 */
	//sEncOpenParam.enableAutoSkip = 1;
	sEncOpenParam.intraRefresh = pInParam->nIntraRefresh;
	sEncOpenParam.sliceReport = 0;
	sEncOpenParam.mbReport = 0;
	sEncOpenParam.mbQpReport = 0;
	sEncOpenParam.rcIntraQp = pInParam->nRcIntraQp;
	sEncOpenParam.userQpMax = pInParam->nUserQpMax;
	sEncOpenParam.userQpMin = pInParam->nUserQpMin;
	sEncOpenParam.userQpMinEnable = pInParam->nUserQpMinEnable;
	sEncOpenParam.userQpMaxEnable = pInParam->nUserQpMaxEnable;

	sEncOpenParam.userGamma = pInParam->nUserGamma;         /*  (0*32768 <= gamma <= 1*32768) */
	sEncOpenParam.RcIntervalMode= pInParam->nRcIntervalMode;        /* 0:normal, 1:frame_level, 2:slice_level, 3: user defined Mb_level */
	sEncOpenParam.MbInterval = pInParam->nMbInterval;
	sEncOpenParam.avcIntra16x16OnlyModeEnable = pInParam->nAvcIntra16x16OnlyModeEnable;

	sEncOpenParam.ringBufferEnable = 0;
	sEncOpenParam.dynamicAllocEnable = 1;	//1  using dynamic method
#if 1 //for iMX6
	sEncOpenParam.dynamicAllocEnable = 0;	//dynamic is not supported on iMX6
	pObj->nDynamicEnabled=0;	
#endif
	sEncOpenParam.chromaInterleave = pInParam->nChromaInterleave;

	switch(pInParam->eFormat)
	{
		case VPU_V_MPEG4:
			sEncOpenParam.EncStdParam.mp4Param.mp4_dataPartitionEnable = pInParam->VpuEncStdParam.mp4Param.mp4_dataPartitionEnable;
			sEncOpenParam.EncStdParam.mp4Param.mp4_reversibleVlcEnable = pInParam->VpuEncStdParam.mp4Param.mp4_reversibleVlcEnable;
			sEncOpenParam.EncStdParam.mp4Param.mp4_intraDcVlcThr = pInParam->VpuEncStdParam.mp4Param.mp4_intraDcVlcThr;
			sEncOpenParam.EncStdParam.mp4Param.mp4_hecEnable = pInParam->VpuEncStdParam.mp4Param.mp4_hecEnable;
			sEncOpenParam.EncStdParam.mp4Param.mp4_verid = pInParam->VpuEncStdParam.mp4Param.mp4_verid;
			sEncOpenParam.bitstreamFormat = STD_MPEG4;
			break;
		case VPU_V_H263:
			sEncOpenParam.EncStdParam.h263Param.h263_annexIEnable = pInParam->VpuEncStdParam.h263Param.h263_annexIEnable;
			sEncOpenParam.EncStdParam.h263Param.h263_annexJEnable = pInParam->VpuEncStdParam.h263Param.h263_annexJEnable;
			sEncOpenParam.EncStdParam.h263Param.h263_annexKEnable = pInParam->VpuEncStdParam.h263Param.h263_annexKEnable;
			sEncOpenParam.EncStdParam.h263Param.h263_annexTEnable = pInParam->VpuEncStdParam.h263Param.h263_annexTEnable;
			sEncOpenParam.bitstreamFormat = STD_H263;
			break;
		case VPU_V_AVC:
			sEncOpenParam.EncStdParam.avcParam.avc_constrainedIntraPredFlag = pInParam->VpuEncStdParam.avcParam.avc_constrainedIntraPredFlag;
			sEncOpenParam.EncStdParam.avcParam.avc_disableDeblk = pInParam->VpuEncStdParam.avcParam.avc_disableDeblk;
			sEncOpenParam.EncStdParam.avcParam.avc_deblkFilterOffsetAlpha = pInParam->VpuEncStdParam.avcParam.avc_deblkFilterOffsetAlpha;
			sEncOpenParam.EncStdParam.avcParam.avc_deblkFilterOffsetBeta = pInParam->VpuEncStdParam.avcParam.avc_deblkFilterOffsetBeta;
			sEncOpenParam.EncStdParam.avcParam.avc_chromaQpOffset = pInParam->VpuEncStdParam.avcParam.avc_chromaQpOffset;
			sEncOpenParam.EncStdParam.avcParam.avc_audEnable = pInParam->VpuEncStdParam.avcParam.avc_audEnable;
#if 1 //for iMX6
			sEncOpenParam.EncStdParam.avcParam.avc_frameCroppingFlag = 0;
			sEncOpenParam.EncStdParam.avcParam.avc_frameCropLeft = 0;
			sEncOpenParam.EncStdParam.avcParam.avc_frameCropRight = 0;
			sEncOpenParam.EncStdParam.avcParam.avc_frameCropTop = 0;
			sEncOpenParam.EncStdParam.avcParam.avc_frameCropBottom = 0;
			if (pInParam->nRotAngle != 90 && pInParam->nRotAngle != 270 && sEncOpenParam.picHeight == 1080)
			{
				//only for 1080 ?
				sEncOpenParam.EncStdParam.avcParam.avc_frameCroppingFlag = 1;
				sEncOpenParam.EncStdParam.avcParam.avc_frameCropBottom = 8;
			}
			/* will be supported on imx6 in future ?
			sEncOpenParam.EncStdParam.avcParam.avc_fmoEnable = pInParam->VpuEncStdParam.avcParam.avc_fmoEnable;
			sEncOpenParam.EncStdParam.avcParam.avc_fmoType = pInParam->VpuEncStdParam.avcParam.avc_fmoType;
			sEncOpenParam.EncStdParam.avcParam.avc_fmoSliceNum = pInParam->VpuEncStdParam.avcParam.avc_fmoSliceNum;
			sEncOpenParam.EncStdParam.avcParam.avc_fmoSliceSaveBufSize = pInParam->VpuEncStdParam.avcParam.avc_fmoSliceSaveBufSize;			
			*/
#else
			sEncOpenParam.EncStdParam.avcParam.avc_fmoEnable = pInParam->VpuEncStdParam.avcParam.avc_fmoEnable;
			sEncOpenParam.EncStdParam.avcParam.avc_fmoType = pInParam->VpuEncStdParam.avcParam.avc_fmoType;
			sEncOpenParam.EncStdParam.avcParam.avc_fmoSliceNum = pInParam->VpuEncStdParam.avcParam.avc_fmoSliceNum;
			sEncOpenParam.EncStdParam.avcParam.avc_fmoSliceSaveBufSize = pInParam->VpuEncStdParam.avcParam.avc_fmoSliceSaveBufSize;
#endif
			sEncOpenParam.bitstreamFormat = STD_AVC;
			break;
		case VPU_V_MJPG:
#if 1
			//unsupported
			VPU_ENC_ERROR("%s: MJPG is not supported \r\n",__FUNCTION__);
			return VPU_ENC_RET_INVALID_PARAM;
#else
			sEncOpenParam.EncStdParam.mjpgParam.mjpg_sourceFormat = 0; /* encConfig.mjpgChromaFormat */
			sEncOpenParam.EncStdParam.mjpgParam.mjpg_restartInterval = 60;
			sEncOpenParam.EncStdParam.mjpgParam.mjpg_thumbNailEnable = 0;
			sEncOpenParam.EncStdParam.mjpgParam.mjpg_thumbNailWidth = 0;
			sEncOpenParam.EncStdParam.mjpgParam.mjpg_thumbNailHeight = 0;
			sEncOpenParam.EncStdParam.mjpgParam.mjpg_hufTable = huffTable;
			sEncOpenParam.EncStdParam.mjpgParam.mjpg_qMatTable = qMatTable;	
			sEncOpenParam.bitstreamFormat = STD_MJPG;
			break;
#endif			
		default:
			//unknow format ?
			//return VPU_ENC_RET_INVALID_PARAM;
			break;
	}

	sEncOpenParam.MESearchRange=pInParam->nMESearchRange;
	sEncOpenParam.MEUseZeroPmv=pInParam->nMEUseZeroPmv;
	sEncOpenParam.IntraCostWeight=pInParam->nIntraCostWeight;

#if 1 //for iMX6
	if(VPU_V_H263==pInParam->eFormat)
	{
		sEncOpenParam.MESearchRange=3; // must set 3 for H.263
	}
#endif

	VPU_ENC_API("calling vpu_EncOpen() \r\n");
	ret= vpu_EncOpen(&pVpuObj->handle, &sEncOpenParam);
	if(ret!=RETCODE_SUCCESS)
	{
		VPU_ENC_ERROR("%s: vpu open failure: ret=%d \r\n",__FUNCTION__,ret);
		return VPU_ENC_RET_FAILURE;
	}

	//give commands for rotation
	if (0!=pInParam->nRotAngle) 
	{
		VPU_ENC_API("calling vpu_EncGiveCommand(ENABLE_ROTATION) \r\n");
		vpu_EncGiveCommand(pVpuObj->handle, ENABLE_ROTATION, 0);
		VPU_ENC_API("calling vpu_EncGiveCommand(ENABLE_MIRRORING) \r\n");
		vpu_EncGiveCommand(pVpuObj->handle, ENABLE_MIRRORING, 0);
		VPU_ENC_API("calling vpu_EncGiveCommand(SET_ROTATION_ANGLE) \r\n");
		vpu_EncGiveCommand(pVpuObj->handle, SET_ROTATION_ANGLE,&pInParam->nRotAngle);
		VPU_ENC_API("calling vpu_EncGiveCommand(SET_MIRROR_DIRECTION) \r\n");
		vpu_EncGiveCommand(pVpuObj->handle, SET_MIRROR_DIRECTION, &pInParam->sMirror);
	}

	pObj->nHeaderNeeded=1;
#ifdef VPU_ENC_SEQ_DATA_SEPERATE
	pObj->nJustOutputOneHeader=0;
	pObj->nOutputHeaderCnt=0;
#endif

	*pOutHandle=(VpuEncHandle)pVpuObj;	

	return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPUEnc_OpenSimp(VpuEncHandle *pOutHandle, VpuMemInfo* pInMemInfo,VpuEncOpenParamSimp * pInParam)
{
	VpuEncRetCode ret;
	VpuEncOpenParam sEncOpenParamMore;

	sEncOpenParamMore.eFormat=pInParam->eFormat;
	sEncOpenParamMore.nPicWidth=pInParam->nPicWidth;
	sEncOpenParamMore.nPicHeight=pInParam->nPicHeight;
	sEncOpenParamMore.nRotAngle=pInParam->nRotAngle;
	sEncOpenParamMore.nFrameRate= pInParam->nFrameRate;
	sEncOpenParamMore.nBitRate= pInParam->nBitRate;
	sEncOpenParamMore.nGOPSize= pInParam->nGOPSize;

	sEncOpenParamMore.nChromaInterleave= pInParam->nChromaInterleave;	
	sEncOpenParamMore.sMirror= pInParam->sMirror;	

	sEncOpenParamMore.sliceMode.sliceMode = 0;	/* 0: 1 slice per picture; 1: Multiple slices per picture */
	sEncOpenParamMore.sliceMode.sliceSizeMode = 0; /* 0: silceSize defined by bits; 1: sliceSize defined by MB number*/
	sEncOpenParamMore.sliceMode.sliceSize = 4000;  /* Size of a slice in bits or MB numbers */

	sEncOpenParamMore.nInitialDelay=0;
	sEncOpenParamMore.nVbvBufferSize=0;

	sEncOpenParamMore.nIntraRefresh = 0;
	//sEncOpenParamMore.nRcIntraQp = -1;
	sEncOpenParamMore.nRcIntraQp =VpuEncGetIntraQP(pInParam);

	sEncOpenParamMore.nUserQpMax = 0;
	sEncOpenParamMore.nUserQpMin = 0;
	sEncOpenParamMore.nUserQpMinEnable = 0;
	sEncOpenParamMore.nUserQpMaxEnable = 0;

	sEncOpenParamMore.nUserGamma = (int)(0.75*32768);         /*  (0*32768 <= gamma <= 1*32768) */
	sEncOpenParamMore.nRcIntervalMode= 0;        /* 0:normal, 1:frame_level, 2:slice_level, 3: user defined Mb_level */
	sEncOpenParamMore.nMbInterval = 0;
	sEncOpenParamMore.nAvcIntra16x16OnlyModeEnable = 0;

	//set some default value structure 'VpuEncOpenParamMore'
	switch(pInParam->eFormat)
	{
		case VPU_V_MPEG4:
			sEncOpenParamMore.VpuEncStdParam.mp4Param.mp4_dataPartitionEnable = 0;
			sEncOpenParamMore.VpuEncStdParam.mp4Param.mp4_reversibleVlcEnable = 0;
			sEncOpenParamMore.VpuEncStdParam.mp4Param.mp4_intraDcVlcThr = 0;
			sEncOpenParamMore.VpuEncStdParam.mp4Param.mp4_hecEnable = 0;
			sEncOpenParamMore.VpuEncStdParam.mp4Param.mp4_verid = 2;
			break;
		case VPU_V_H263:
			sEncOpenParamMore.VpuEncStdParam.h263Param.h263_annexIEnable = 0;
			sEncOpenParamMore.VpuEncStdParam.h263Param.h263_annexJEnable = 1;
			sEncOpenParamMore.VpuEncStdParam.h263Param.h263_annexKEnable = 0;
			sEncOpenParamMore.VpuEncStdParam.h263Param.h263_annexTEnable = 0;
			break;
		case VPU_V_AVC:
			sEncOpenParamMore.VpuEncStdParam.avcParam.avc_constrainedIntraPredFlag = 0;
			sEncOpenParamMore.VpuEncStdParam.avcParam.avc_disableDeblk = 0;
			sEncOpenParamMore.VpuEncStdParam.avcParam.avc_deblkFilterOffsetAlpha = 6;
			sEncOpenParamMore.VpuEncStdParam.avcParam.avc_deblkFilterOffsetBeta = 0;
			sEncOpenParamMore.VpuEncStdParam.avcParam.avc_chromaQpOffset = 10;
			sEncOpenParamMore.VpuEncStdParam.avcParam.avc_audEnable = 0;
			sEncOpenParamMore.VpuEncStdParam.avcParam.avc_fmoEnable = 0;
			sEncOpenParamMore.VpuEncStdParam.avcParam.avc_fmoType = 0;
			sEncOpenParamMore.VpuEncStdParam.avcParam.avc_fmoSliceNum = 1;
			sEncOpenParamMore.VpuEncStdParam.avcParam.avc_fmoSliceSaveBufSize = 32; /* FMO_SLICE_SAVE_BUF_SIZE */			
			break;
		//case VPU_V_MJPG:
		default:
			//unknow format ?
			//return VPU_ENC_RET_INVALID_PARAM;
			break;
	}

	sEncOpenParamMore.nMESearchRange=0;
	sEncOpenParamMore.nMEUseZeroPmv=0;
	sEncOpenParamMore.nIntraCostWeight=0;
	ret=VPUEnc_Open(pOutHandle,pInMemInfo,&sEncOpenParamMore);

	return ret;
}

VpuEncRetCode VPUEnc_Close(VpuEncHandle InHandle)
{
	VpuEncHandleInternal * pVpuObj;
	RetCode ret;
	
	if(InHandle==NULL) 
	{
		VPU_ENC_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);	
		return VPU_ENC_RET_INVALID_HANDLE;
	}

	pVpuObj=(VpuEncHandleInternal *)InHandle;

	//add robust : if busy(fix some timeout issue) , reset it 
	VPU_ENC_API("calling vpu_IsBusy() \r\n");
	if(vpu_IsBusy())
	{
		VPU_ENC_API("calling vpu_SWReset(0x%X,0) \r\n",(UINT32)pVpuObj->handle);
		ret=vpu_SWReset(pVpuObj->handle,0);
		if(RETCODE_SUCCESS!=ret)
		{
			VPU_ENC_ERROR("%s: vpu reset failure, ret=%d \r\n",__FUNCTION__,ret);
			//return VPU_ENC_RET_FAILURE;
		}	
	}	

	//normal close
	VPU_ENC_API("calling vpu_EncClose() \r\n");
	ret=vpu_EncClose(pVpuObj->handle);
	if(RETCODE_SUCCESS!=ret)
	{
		VPU_ENC_ERROR("%s: vpu close failure, ret=%d \r\n",__FUNCTION__,ret);
		return VPU_ENC_RET_FAILURE;
	}	
	
	return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPUEnc_GetInitialInfo(VpuEncHandle InHandle, VpuEncInitInfo * pOutInitInfo)
{
	RetCode ret;
	VpuEncHandleInternal * pVpuObj;
	EncInitialInfo sInitInfo;

	if(InHandle==NULL) 
	{
		VPU_ENC_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);	
		return VPU_ENC_RET_INVALID_HANDLE;
	}

	pVpuObj=(VpuEncHandleInternal *)InHandle;

	VPU_ENC_API("calling vpu_EncGetInitialInfo() \r\n");
	//ret = vpu_EncGetInitialInfo(pVpuObj->handle, &pOutInitInfo->sInitInfo);
	ret = vpu_EncGetInitialInfo(pVpuObj->handle, &sInitInfo);
	if (ret != RETCODE_SUCCESS)
	{
		VPU_ENC_ERROR("%s: Encoder GetInitialInfo failed \r\n",__FUNCTION__);
		return VPU_ENC_RET_FAILURE;
	}
	pOutInitInfo->nMinFrameBufferCount=sInitInfo.minFrameBufferCount;
#if 1	//for iMX6
	//FIXME: In generally, every subsamp buffer need about 1/4 frame buff size. we can move this into QueryMem() to save memory size
	pOutInitInfo->nMinFrameBufferCount+=2;	//for subsamp A,B
#endif
	return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPUEnc_GetVersionInfo(VpuVersionInfo * pOutVerInfo)
{
	vpu_versioninfo ver;
	RetCode ret;

	if(pOutVerInfo==NULL)
	{
		VPU_ENC_ERROR("%s: failure: invalid parameterl \r\n",__FUNCTION__);	
		return VPU_ENC_RET_INVALID_PARAM;
	}
	VPU_ENC_API("calling vpu_GetVersionInfo() \r\n");
	ret=vpu_GetVersionInfo(&ver);
	if(RETCODE_SUCCESS!=ret)
	{
		VPU_ENC_ERROR("%s: get vpu version failure, ret=%d \r\n",__FUNCTION__,ret);
		return VPU_ENC_RET_FAILURE;
	}
	else
	{
		VPU_ENC_LOG("%s: VPU FW: [major.minor.release]=[%d.%d.%d] \r\n",__FUNCTION__,ver.fw_major,ver.fw_minor,ver.fw_release);
		VPU_ENC_LOG("%s: VPU LIB: [major.minor.release]=[%d.%d.%d] \r\n",__FUNCTION__,ver.lib_major,ver.lib_minor,ver.lib_release);
	}
	pOutVerInfo->nPrjnum=1;
	pOutVerInfo->nFwMajor=ver.fw_major;
	pOutVerInfo->nFwMinor=ver.fw_minor;
	pOutVerInfo->nFwRelease=ver.fw_release;
	pOutVerInfo->nLibMajor=ver.lib_major;
	pOutVerInfo->nLibMinor=ver.lib_minor;
	pOutVerInfo->nLibRelease=ver.lib_release;

	return VPU_ENC_RET_SUCCESS;

}

VpuEncRetCode VPUEnc_RegisterFrameBuffer(VpuEncHandle InHandle,VpuFrameBuffer *pInFrameBufArray, int nNum,int nSrcStride)
{
	VpuEncHandleInternal * pVpuObj;
	RetCode ret;
	FrameBuffer framebuf[VPU_ENC_MAX_FRAME_INDEX];
	int i;
	ExtBufCfg sScratch;
	
	if(InHandle==NULL) 
	{
		VPU_ENC_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);	
		return VPU_ENC_RET_INVALID_HANDLE;
	}

	pVpuObj=(VpuEncHandleInternal *)InHandle;

	if(nNum>VPU_ENC_MAX_FRAME_INDEX)
	{
		VPU_ENC_ERROR("%s: failure: register frame number is too big(%d) \r\n",__FUNCTION__,nNum);		
		return VPU_ENC_RET_INVALID_PARAM;
	}

	for(i=0;i<nNum;i++)
	{
		//record frame buf info
		//pVpuObj->obj.frameBuf[i]=*pInFrameBufArray;
#ifdef USE_NEW_VPU_API
		framebuf[i].myIndex=i;
#endif
		//re-map frame buf info for vpu register
		framebuf[i].strideY=(unsigned long)pInFrameBufArray->nStrideY;
		framebuf[i].strideC=(unsigned long)pInFrameBufArray->nStrideC;
		framebuf[i].bufY=(PhysicalAddress)pInFrameBufArray->pbufY;
		framebuf[i].bufCb=(PhysicalAddress)pInFrameBufArray->pbufCb;
		framebuf[i].bufCr=(PhysicalAddress)pInFrameBufArray->pbufCr;
		framebuf[i].bufMvCol=(PhysicalAddress)pInFrameBufArray->pbufMvCol;

		VPU_ENC_LOG("register frame %d: (phy)	Y:0x%X, U:0x%X, V:0x%X \r\n",(UINT32)i,(UINT32)framebuf[i].bufY,(UINT32)framebuf[i].bufCb,(UINT32)framebuf[i].bufCr);
		VPU_ENC_LOG("register frame %d: (virt)	Y:0x%X, U:0x%X, V:0x%X \r\n",(UINT32)i,(UINT32)pInFrameBufArray->pbufVirtY,(UINT32)pInFrameBufArray->pbufVirtCb,(UINT32)pInFrameBufArray->pbufVirtCr);
		VPU_ENC_LOG("register mv    %d: (phy)	0x%X,    (virt)    0x%X \r\n",(UINT32)i,(UINT32)framebuf[i].bufMvCol,(UINT32)pInFrameBufArray->pbufVirtMvCol);		
		pInFrameBufArray++;
	}
	//pVpuObj->obj.frameNum=nNum;

#if 1 //for iMX6
	nNum-=2;
	sScratch.bufferBase=(PhysicalAddress)pVpuObj->obj.pPhyScratch;
	sScratch.bufferSize=pVpuObj->obj.nScratchSize;
	pVpuObj->obj.nFrameCnt=nNum;
#endif

	VPU_ENC_API("calling vpu_EncRegisterFrameBuffer() \r\n");
	//here, we expect the source stride == enc stride
#ifdef USE_NEW_VPU_API
	#if (VPU_LIB_VERSION_CODE >=VPU_LIB_VERSION(5,3,3))
	VPU_ENC_LOG("register: num: %d, subsamp A: 0x%X, subsamp B: 0x%X, scratch: 0x%X(size: %d) \r\n",nNum,framebuf[nNum].bufY,framebuf[nNum+1].bufY,sScratch.bufferBase,sScratch.bufferSize);
	ret = vpu_EncRegisterFrameBuffer(pVpuObj->handle,framebuf,nNum, framebuf[0].strideY, /*framebuf[0].strideY*/nSrcStride,framebuf[nNum].bufY,framebuf[nNum+1].bufY,&sScratch);
	#else
	ret = vpu_EncRegisterFrameBuffer(pVpuObj->handle,framebuf,nNum, framebuf[0].strideY, /*framebuf[0].strideY*/nSrcStride,0,0);
	#endif
#else
	ret = vpu_EncRegisterFrameBuffer(pVpuObj->handle,framebuf,nNum, framebuf[0].strideY, /*framebuf[0].strideY*/nSrcStride);
#endif
	if (ret != RETCODE_SUCCESS) 
	{
		VPU_ENC_ERROR("%s: Register frame buffer failed \r\n",__FUNCTION__);
		return VPU_ENC_RET_FAILURE;
	}
	return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPUEnc_QueryMem(VpuMemInfo* pOutMemInfo)
{
	VpuMemSubBlockInfo * pMem;

	if(pOutMemInfo==NULL)
	{
		VPU_ENC_ERROR("%s: failure: invalid parameterl \r\n",__FUNCTION__);	
		return VPU_ENC_RET_INVALID_PARAM;	
	}
	pMem=&pOutMemInfo->MemSubBlock[ENC_VIRT_INDEX];
	pMem->MemType=VPU_MEM_VIRT;
	pMem->nAlignment=VPU_ENC_MEM_ALIGN;
	pMem->nSize=sizeof(VpuEncHandleInternal);
	pMem->pVirtAddr=NULL;
	pMem->pPhyAddr=NULL;

	pMem=&pOutMemInfo->MemSubBlock[ENC_PHY_INDEX];
	pMem->MemType=VPU_MEM_PHY;
	pMem->nAlignment=VPU_ENC_MEM_ALIGN;
	pMem->nSize=VPU_ENC_BITS_BUF_SIZE;
#if 1 //for iMX6	
	//pMem->nSize+=VPU_ENC_MEM_ALIGN+width*heigth*3/8;	//subsamp A,B
	pMem->nSize+=VPU_ENC_MEM_ALIGN+VPU_ENC_MPEG4_SCRATCH_SIZE;	//for scratch
#endif	
	pMem->pVirtAddr=NULL;
	pMem->pPhyAddr=NULL;

	pOutMemInfo->nSubBlockNum=2;
	
	return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPUEnc_GetMem(VpuMemDesc* pInOutMem)
{
	int ret;
	vpu_mem_desc buff;
	buff.size=pInOutMem->nSize;
	ret=IOGetPhyMem(&buff);
	if(ret) //if(ret!=RETCODE_SUCCESS)
	{
		VPU_ENC_ERROR("%s: get physical memory failure: size=%d, ret=%d \r\n",__FUNCTION__,buff.size,(UINT32)ret);
		return VPU_ENC_RET_FAILURE;
	}
	ret=IOGetVirtMem(&buff);
	if(ret<=0) //if(ret!=RETCODE_SUCCESS)
	{
		VPU_ENC_ERROR("%s: get virtual memory failure: size=%d, ret=%d \r\n",__FUNCTION__,buff.size,(UINT32)ret);
		return VPU_ENC_RET_FAILURE;
	}

	pInOutMem->nPhyAddr=buff.phy_addr;
	pInOutMem->nVirtAddr=buff.virt_uaddr;
	pInOutMem->nCpuAddr=buff.cpu_addr;

	VPU_ENC_LOG("%s: size: %d, phy addr: 0x%X, virt addr: 0x%X \r\n",__FUNCTION__,buff.size,(UINT32)buff.phy_addr,(UINT32)buff.virt_uaddr);
	return VPU_ENC_RET_SUCCESS;
}


VpuEncRetCode VPUEnc_FreeMem(VpuMemDesc* pInMem)
{
	int ret;
	vpu_mem_desc buff;
	buff.size=pInMem->nSize;
	buff.phy_addr=pInMem->nPhyAddr;
	buff.virt_uaddr=pInMem->nVirtAddr;
	buff.cpu_addr=pInMem->nCpuAddr;
	ret=IOFreeVirtMem(&buff);
	if(ret!=RETCODE_SUCCESS)
	{
		VPU_ENC_ERROR("%s: free virtual memory failure: size=%d, ret=%d \r\n",__FUNCTION__,buff.size,(UINT32)ret);
		return VPU_ENC_RET_FAILURE;
	}	
	ret=IOFreePhyMem(&buff);
	if(ret!=RETCODE_SUCCESS)
	{
		VPU_ENC_ERROR("%s: free phy memory failure: size=%d, ret=%d \r\n",__FUNCTION__,buff.size,(UINT32)ret);
		return VPU_ENC_RET_FAILURE;
	}	

	return VPU_ENC_RET_SUCCESS;	
}

VpuEncRetCode VPUEnc_Config(VpuEncHandle InHandle, VpuEncConfig InEncConf, void* pInParam)
{
	VpuEncHandleInternal * pVpuObj;
	VpuEncObj* pObj;
	if(InHandle==NULL)
	{
		VPU_ENC_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);		
		return VPU_ENC_RET_INVALID_HANDLE;
	}
	
	pVpuObj=(VpuEncHandleInternal *)InHandle;
	pObj=&pVpuObj->obj;
	
	switch(InEncConf)
	{
		//case VPU_DEC_CONF_SKIPNONE:
		//	break;
		case VPU_ENC_CONF_NONE:
			break;
		default:
			VPU_ENC_ERROR("%s: failure: invalid setting \r\n",__FUNCTION__);	
			return VPU_ENC_RET_INVALID_PARAM;
	}
	
	return VPU_ENC_RET_SUCCESS;
}

VpuEncRetCode VPUEnc_EncodeFrame(VpuEncHandle InHandle, VpuEncEncParam* pInOutParam)
{
	VpuEncHandleInternal * pVpuObj;
	RetCode ret;
	EncParam sEncParam;
	EncOutputInfo sEncOutInfo;
	FrameBuffer sFramBuf;
	VpuEncBufRetCode bufRet=VPU_ENC_INPUT_NOT_USED;
	int nHeaderLen=0;
	int nPadLen=0;
#ifdef VPU_WRAPPER_DUMP
	static FILE* fpYUV=NULL;	//input
	int nPhy_virt_offset=0;
	static FILE* fpBitstream=NULL;	//output 
#endif
	if(InHandle==NULL)
	{
		VPU_ENC_ERROR("%s: failure: handle is null \r\n",__FUNCTION__);		
		return VPU_ENC_RET_INVALID_HANDLE;
	}

	pVpuObj=(VpuEncHandleInternal *)InHandle;

	//For H.264, we will insert sps/pps before every IDR frame
	if((1==pVpuObj->obj.nHeaderNeeded)||((VPU_V_AVC==pInOutParam->eFormat)&&(0!=pInOutParam->nForceIPicture)))
	{
#ifdef VPU_ENC_SEQ_DATA_SEPERATE	
		if(1==pVpuObj->obj.nJustOutputOneHeader)
		{
			//avoid dead loop in filling header
		}
		else
#endif
		{
			unsigned char* pHeaderBufPhy;
			unsigned char* pHeaderBufVirt;
			int mode=0;	
			/*
			mode 0: dynamic 1: needn't pad zero
			mode 1: dynamic 1: need pad zero
			mode 2: non-dynamic, non-ringbuf(or ringbuf ?)
			*/ 
			if(pVpuObj->obj.nDynamicEnabled)
			{
				pHeaderBufPhy=(unsigned char*)pInOutParam->nInPhyOutput;
				pHeaderBufVirt=(unsigned char*)pInOutParam->nInVirtOutput;
#ifdef VPU_ENC_OUTFRAME_ALIGN
				mode=1;
#endif
			}
			else
			{
				pHeaderBufPhy=(unsigned char*)pInOutParam->nInPhyOutput;	
				pHeaderBufVirt=(unsigned char*)pInOutParam->nInVirtOutput;
				mode=2;
			}
			//In fact, we need not send pHeaderBufVirt, but vpu has some limitation about address aligment.
			//As result we need to fill some zero bytes in header 
			if(-1==VpuEncFillHeader(pVpuObj->handle,pInOutParam,pHeaderBufPhy,&nHeaderLen,&nPadLen,pHeaderBufVirt,mode,pVpuObj->obj.pPhyBitstream,pVpuObj->obj.pVirtBitstream))
			{
				return VPU_ENC_RET_FAILURE;
			}
			//only fill header info once
			pVpuObj->obj.nHeaderNeeded=0;
			ASSERT(nHeaderLen<=(int)pInOutParam->nInOutputBufLen);
		}
	}	

#ifdef VPU_ENC_SEQ_DATA_SEPERATE
	//if(nHeaderLen>0)
	if((nHeaderLen>0)&&(0==pVpuObj->obj.nOutputHeaderCnt))
	{
		//output sequence header firstly
		ASSERT(nHeaderLen-nPadLen>0);
		pInOutParam->nOutOutputSize=nHeaderLen-nPadLen;	//needn't align boundary again if we only output header
		pInOutParam->eOutRetCode=(VpuEncBufRetCode)(VPU_ENC_INPUT_NOT_USED|VPU_ENC_OUTPUT_SEQHEADER);	
		pVpuObj->obj.nJustOutputOneHeader=1;	//output header
		pVpuObj->obj.nOutputHeaderCnt++;
	}
	else
#endif
	{
		//clear 0 firstly
		vpu_memset(&sEncParam, 0, sizeof(EncParam));
		vpu_memset(&sFramBuf,0,sizeof(FrameBuffer));

		//set encoder parameters
#if 1 //for iMX6: it is very important !!!
		sFramBuf.myIndex=pVpuObj->obj.nFrameCnt+1;
#endif
		sEncParam.sourceFrame=&sFramBuf;
		VpuEncSetSrcFrame(sEncParam.sourceFrame,(unsigned char*)pInOutParam->nInPhyInput,pInOutParam->nInInputSize,pInOutParam->nPicWidth,pInOutParam->nPicHeight,NULL);
		sEncParam.quantParam = pInOutParam->nQuantParam;
		sEncParam.forceIPicture = pInOutParam->nForceIPicture;
		sEncParam.skipPicture = pInOutParam->nSkipPicture;
		sEncParam.enableAutoSkip = pInOutParam->nEnableAutoSkip;

		sEncParam.encLeftOffset = 0;
		sEncParam.encTopOffset = 0;

		if(pVpuObj->obj.nDynamicEnabled)
		{
			//set output for dynmaic method	
			sEncParam.picStreamBufferAddr=(PhysicalAddress)(pInOutParam->nInPhyOutput+nHeaderLen);
			//sEncParam.picStreamBufferSize=pInOutParam->nInOutputBufLen;
		}
		else
		{
			//sEncParam.picStreamBufferAddr=(PhysicalAddress)pVpuObj->obj.pPhyBitstream;
		}

#ifdef VPU_WRAPPER_DUMP
		{
			int colorformat=0;
			//if(VPU_V_MJPG==pObj->CodecFormat)
			//{
			//	colorformat=pObj->initInfo.nMjpgSourceFormat;
			//}
			nPhy_virt_offset=pInOutParam->nInVirtInput-pInOutParam->nInPhyInput;
			WrapperFileDumpYUV(&fpYUV, (unsigned char*)sEncParam.sourceFrame->bufY+nPhy_virt_offset, (unsigned char*)sEncParam.sourceFrame->bufCb+nPhy_virt_offset, (unsigned char*)sEncParam.sourceFrame->bufCr+nPhy_virt_offset, sEncParam.sourceFrame->strideY*pInOutParam->nPicHeight, sEncParam.sourceFrame->strideC*pInOutParam->nPicHeight/2,colorformat);
		}	
#endif

		VPU_ENC_LOG("sourceframe: y: 0x%X, u: 0x%X, v: 0x%X, ystride: %d, uvstride: %d, size: %d \r\n",
			(UINT32)sEncParam.sourceFrame->bufY,(UINT32)sEncParam.sourceFrame->bufCb,(UINT32)sEncParam.sourceFrame->bufCr,
			(UINT32)sEncParam.sourceFrame->strideY,(UINT32)sEncParam.sourceFrame->strideC,(UINT32)pInOutParam->nInInputSize);
		VPU_ENC_API("calling vpu_EncStartOneFrame(): dynamic buff: 0x%X , size: %d \r\n",(UINT32)sEncParam.picStreamBufferAddr,sEncParam.picStreamBufferSize);
		ret = vpu_EncStartOneFrame(pVpuObj->handle, &sEncParam);
		if (ret != RETCODE_SUCCESS)
		{
			VPU_ENC_ERROR("vpu_EncStartOneFrame failed Err code:%d \r\n",ret);
			return VPU_ENC_RET_FAILURE;
		}

		if(-1==VpuEncWaitBusy())
		{
			return VPU_ENC_RET_FAILURE_TIMEOUT;
		}

		//clear 0 firstly ??
		vpu_memset(&sEncOutInfo, 0, sizeof(EncOutputInfo));

		VPU_ENC_API("calling vpu_EncGetOutputInfo() \r\n");
		ret = vpu_EncGetOutputInfo(pVpuObj->handle, &sEncOutInfo);
		if (ret != RETCODE_SUCCESS) 
		{
			VPU_ENC_ERROR("vpu_EncGetOutputInfo failed Err code: %d \r\n",ret);
			return VPU_ENC_RET_FAILURE;
		}
		VPU_ENC_LOG("out frame: type: %d, addr: 0x%X, size: %d \r\n",(UINT32)sEncOutInfo.picType,(UINT32)sEncOutInfo.bitstreamBuffer,(UINT32)sEncOutInfo.bitstreamSize);

		if(sEncOutInfo.skipEncoded)
		{
			//
		}

		if(sEncOutInfo.bitstreamBuffer)
		{
			//valid output
			bufRet=(VpuEncBufRetCode)(VPU_ENC_INPUT_USED|VPU_ENC_OUTPUT_DIS);
			if(0==pVpuObj->obj.nDynamicEnabled)
			{
				unsigned char* pVirt=(unsigned char*)((int)pVpuObj->obj.pVirtBitstream+(int)sEncOutInfo.bitstreamBuffer-(int)pVpuObj->obj.pPhyBitstream);
				ASSERT(pVirt==pVpuObj->obj.pVirtBitstream);
				VPU_ENC_LOG("frame memcpy: dst: 0x%X, src: 0x%X, size: %d \r\n",(pInOutParam->nInVirtOutput+nHeaderLen),pVirt,sEncOutInfo.bitstreamSize);	
				memcpy((void*)(pInOutParam->nInVirtOutput+nHeaderLen),(void*)pVirt,sEncOutInfo.bitstreamSize);
			}
		}
		else
		{
			bufRet=(VpuEncBufRetCode)(VPU_ENC_INPUT_USED|VPU_ENC_OUTPUT_NODIS);
		}

		pInOutParam->nOutOutputSize=sEncOutInfo.bitstreamSize+nHeaderLen;
		pInOutParam->eOutRetCode=bufRet;
#ifdef VPU_ENC_SEQ_DATA_SEPERATE		
		pVpuObj->obj.nJustOutputOneHeader=0;	//output data
#endif
	}

#ifdef VPU_WRAPPER_DUMP
	WrapperFileDumpBitstrem(&fpBitstream,(unsigned char*)pInOutParam->nInVirtOutput,pInOutParam->nOutOutputSize);
#endif

	//ASSERT(pInOutParam->nOutOutputSize<=(int)pInOutParam->nInOutputBufLen);
	return VPU_ENC_RET_SUCCESS;
}




 
