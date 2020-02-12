/*
 *  Copyright (c) 2010-2014, Freescale Semiconductor Inc.,
 *  Copyright 2019-2020 NXP
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 *
 */

/*   
 *	decode_stream.c
 *	this file is the interface between unit test and vpu wrapper
 *
 *	History :
 *	Date	(y.m.d)		Author			Version			Description
 *	2010-09-14		eagle zhou		0.1				Created
 */



#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "vpu_wrapper.h"
#include "decode_stream.h"
#include "fb_render.h"
#include "unistd.h"		//for usleep()

//#define VPU_FILE_MODE_TEST

#define CHECK_DEAD_LOOP
#ifdef CHECK_DEAD_LOOP
#define MAX_NOTUSED_LOOP	(1000) //(200)
#define MAX_NULL_LOOP		(1000) //(200)
#endif

#ifdef DEC_STREAM_DEBUG
#define DEC_STREAM_PRINTF printf
//#define DEC_TRACE	printf("%s: %d \r\n",__FUNCTION__,__LINE__);
#define DEC_TRACE
#else
#define DEC_STREAM_PRINTF
#define DEC_TRACE
#endif

#define USLEEP	usleep
#define SLEEP_TIME_US		(1000) //(1000)	

//#define FILL_DATA_UNIT	(16*1024)
#define MAX_FRAME_NUM	(30)
#define FRAME_SURPLUS	(0)//(3)
#define FRAME_ALIGN		(16)

#if 1	//avoid buf is too big to malloc by vpu
#define VPU_DEC_MAX_NUM_MEM_NUM	20
#else
#define VPU_DEC_MAX_NUM_MEM_NUM	VPU_DEC_MAX_NUM_MEM_REQS
#endif

#define Align(ptr,align)	(((unsigned long)ptr+(align)-1)/(align)*(align))

typedef struct
{
	//virtual mem info
	int nVirtNum;
	unsigned long virtMem[VPU_DEC_MAX_NUM_MEM_NUM];

	//phy mem info
	int nPhyNum;
	unsigned long phyMem_virtAddr[VPU_DEC_MAX_NUM_MEM_NUM];
	unsigned long phyMem_phyAddr[VPU_DEC_MAX_NUM_MEM_NUM];
	unsigned long phyMem_cpuAddr[VPU_DEC_MAX_NUM_MEM_NUM];
	unsigned int phyMem_size[VPU_DEC_MAX_NUM_MEM_NUM];	
}DecMemInfo;


#ifdef VPU_FILE_MODE_TEST
#define FILE_MODE_MAX_FRAME_LEN		(1024*1024)	//1M bytes
#define FILE_MODE_MAX_FRAME_NUM		40
#define FILE_MODE_LOG	printf
unsigned int g_filemode_frame_length[FILE_MODE_MAX_FRAME_NUM]={
	//from zdfhd.ts
	131923, 40129,38102,40147,20749,
	19142,36248,21235,21153,35797,
	33383,21124,19286,38835,18808,
	20024,103980,47883,43615,39538,
	19854,24160,45832,22873,23503,
	41762,36254,24498,20684,35544,
	16691,18975,145114,43136,39354,
	33491,15813,15431,33030,16289,
};
static unsigned int g_filemode_curloc=0;
#endif

#if 1 // timer related part
#include <sys/time.h>
#define TIME_DEC_ID	0
#define TIME_TOTAL_ID	1
#define TIME_RESOLUTION_ID	2
static struct timeval time_beg[3];
static struct timeval time_end[3];
static unsigned long long total_time[3];

static void time_init(int id)
{
	total_time[id]=0;
}

static void time_start(int id)
{
	gettimeofday(&time_beg[id], 0);
}

static void time_stop(int id)
{
	unsigned int tm_1, tm_2;
	gettimeofday(&time_end[id], 0);

	tm_1 = time_beg[id].tv_sec * 1000000 + time_beg[id].tv_usec;
	tm_2 = time_end[id].tv_sec * 1000000 + time_end[id].tv_usec;
	total_time[id] = total_time[id] + (tm_2-tm_1);
}

static unsigned long long time_report(int id)
{
	return total_time[id];
}
#endif

int MapTileSpace(int x, int y, int stride, int blockW, int blockH,int field,unsigned char* pTop, unsigned char* pBot, unsigned char** ppTileAddr)
{
	/*	input parameter:
			x: x coordinate in linear space
			y: y coordinate in linear space
			blockW: tile block unit width
			blockH: tile block unit height	
		output parameter:
			pTileOffset: offset in the whole tile buffer
		in tile space: every [blockW x blockH] is divided by two sub-block: [blockW/2 x blockH]		
	*/
	int blockNum=0;
	int subblockNum=0;
	int blockLength;
	int subblockLength;
	int subblockOffset;
	int tileOffset=0;
	unsigned char* pBase;

	if(0==field)
	{
		pBase=pTop;
	}
	else
	{		
		if(0==(y&1))
		{
			pBase=pTop;
		}
		else
		{
			pBase=pBot;
		}
		y=y>>1;
	}

	blockLength=blockW*blockH;
	subblockLength=blockLength/2;
	blockNum=(y/blockH)*(stride/blockW)+(x/blockW);
	if((x%blockW)>=(blockW/2))
	{
		subblockNum=1;
	}
	subblockOffset=(y%blockH)*(blockW/2)+(x%(blockW/2));
	tileOffset=blockNum*blockLength+subblockNum*subblockLength+subblockOffset;
	//printf("field: %d, stride: %d, block[%dx%d], linear: [x,y]: [%d,%d],  tile offset: %d \r\n",field,stride,blockW,blockH,x,y,tileOffset);
	*ppTileAddr=pBase+tileOffset;

	return 1;	
}

int ConvertDataFromTileToLinear(int maptype, int width, int height, 
		unsigned char* pSrcYTop,unsigned char* pSrcYBot,unsigned char* pSrcCbTop,unsigned char* pSrcCbBot,
		unsigned char* pDstY,unsigned char* pDstCb,unsigned char* pDstCr)
{
	int x,y,j;
	unsigned char* pTileBlockAddr;
	unsigned char temp_buf[8];
	int blockW,blockH;
	int field=0;

	//fill Y buffer
	if(maptype==1)
	{
		//frame tile: luma--w(8+8)xh16
		blockW=16;
		blockH=16;
		field=0;
	}
	else
	{
		//field tile: luma--w(8+8)xh8
		blockW=16;
		blockH=8;
		field=1;
	}
	for(y=0;y<height;y++)
	{
		for(x=0;x<width;x+=8)
		{
			MapTileSpace(x, y, width, blockW, blockH,field,pSrcYTop,pSrcYBot, &pTileBlockAddr);
			memcpy(pDstY+y*width+x, pTileBlockAddr, 8);
		}
	}

	//fill Cb/Cr buffer
	if(maptype==1)
	{
		//frame tile: Chroma--w(8+8)xh8
		blockW=16;
		blockH=8;
		field=0;
	}
	else
	{
		//field tile: Chroma--w(8+8)xh4
		blockW=16;
		blockH=4;
		field=1;
	}
	for(y=0;y<height/2;y++)
	{
		for(x=0;x<width;x+=8)
		{
			MapTileSpace(x, y, width, blockW, blockH,field,pSrcCbTop,pSrcCbBot,&pTileBlockAddr);
			memcpy(temp_buf, pTileBlockAddr, 8);
			for (j = 0; j < 4; j++)
			{
				*pDstCb++=*(temp_buf+j*2);
				*pDstCr++=*(temp_buf+j*2+1);
			}
		}
	}
	
	return 1;
}

int ConvertCodecFormat(int codec, VpuCodStd* pCodec)
{
	switch (codec)
	{
		case 1:
			*pCodec=VPU_V_MPEG2;
			break;
		case 2:
			*pCodec=VPU_V_MPEG4;
			break;			
		case 3:
			*pCodec=VPU_V_DIVX3;
			break;
		case 4:
			*pCodec=VPU_V_DIVX4;
			break;
		case 5:
			*pCodec=VPU_V_DIVX56;
			break;
		case 6:
			*pCodec=VPU_V_XVID;
			break;			
		case 7:
			*pCodec=VPU_V_H263;
			break;
		case 8:
			*pCodec=VPU_V_AVC;
			break;
		case 9:
			*pCodec=VPU_V_VC1; //VPU_V_VC1_AP
			break;
		case 10:
			*pCodec=VPU_V_RV;
			break;			
		case 11:
			*pCodec=VPU_V_MJPG;
			break;
		case 12:
			*pCodec=VPU_V_AVS;
			break;
		case 13:
			*pCodec=VPU_V_VP8;
			break;
		case 14:
			*pCodec=VPU_V_AVC_MVC;
			break;
		case 15:
			*pCodec=VPU_V_HEVC;
			break;
		default:
			return 0;			
	}
	return 1;
}


int ConvertSkipMode(int skip, VpuDecConfig* pConfig,int * pPara)
{
	switch (skip)
	{
		case 0:
			*pConfig=VPU_DEC_CONF_SKIPMODE;
			*pPara=VPU_DEC_SKIPNONE;
			DEC_STREAM_PRINTF("normal mode \r\n");
			break;
		case 1:
			*pConfig=VPU_DEC_CONF_SKIPMODE;
			*pPara=VPU_DEC_SKIPPB;
			DEC_STREAM_PRINTF("skip PB frames \r\n");
			break;
		case 2:
			*pConfig=VPU_DEC_CONF_SKIPMODE;
			*pPara=VPU_DEC_SKIPB;
			DEC_STREAM_PRINTF("skip B frames \r\n");
			break;			
		case 3:
			*pConfig=VPU_DEC_CONF_SKIPMODE;
			*pPara=VPU_DEC_SKIPALL;
			DEC_STREAM_PRINTF("skip all frames \r\n");
			break;
		case 4:
			*pConfig=VPU_DEC_CONF_SKIPMODE;
			*pPara=VPU_DEC_ISEARCH;	//only search I, not skip I
			DEC_STREAM_PRINTF("I frame search \r\n");
			break;
		default:
			DEC_STREAM_PRINTF("unsupported skip mode: %d \r\n",skip);
			return 0;
	}
	return 1;
}

int FreeMemBlockFrame(DecMemInfo* pDecMem, int nFrmNum)
{
	VpuMemDesc vpuMem;
	VpuDecRetCode vpuRet;	
	int cnt=0;
	int retOk=1;
	int i;

	//free physical mem
	for(i=pDecMem->nPhyNum-1;i>=0;i--)
	{
		vpuMem.nPhyAddr=pDecMem->phyMem_phyAddr[i];
		vpuMem.nVirtAddr=pDecMem->phyMem_virtAddr[i];
		vpuMem.nCpuAddr=pDecMem->phyMem_cpuAddr[i];
		vpuMem.nSize=pDecMem->phyMem_size[i];
		vpuRet=VPU_DecFreeMem(&vpuMem);
		if(vpuRet!=VPU_DEC_RET_SUCCESS)
		{
			DEC_STREAM_PRINTF("%s: free vpu memory failure : ret=%d \r\n",__FUNCTION__,vpuRet);
			retOk=0;
		}
		cnt++;
		if(cnt==nFrmNum) break;
	}
	pDecMem->nPhyNum=pDecMem->nPhyNum-cnt;
	if(cnt!=nFrmNum) 
	{
		DEC_STREAM_PRINTF("error: only freed %d frames, required frame numbers: %d \r\n",cnt,nFrmNum);
		retOk=0;
	}	
	return retOk;
}

int FreeMemBlock(DecMemInfo* pDecMem)
{
	int i;
	VpuMemDesc vpuMem;
	VpuDecRetCode vpuRet;
	int retOk=1;

	//free virtual mem
	for(i=0;i<pDecMem->nVirtNum;i++)
	{
		if((void*)pDecMem->virtMem[i]) free((void*)pDecMem->virtMem[i]);
	}
	pDecMem->nVirtNum=0;

	//free physical mem
	for(i=0;i<pDecMem->nPhyNum;i++)
	{
		vpuMem.nPhyAddr=pDecMem->phyMem_phyAddr[i];
		vpuMem.nVirtAddr=pDecMem->phyMem_virtAddr[i];
		vpuMem.nCpuAddr=pDecMem->phyMem_cpuAddr[i];
		vpuMem.nSize=pDecMem->phyMem_size[i];
		vpuRet=VPU_DecFreeMem(&vpuMem);
		if(vpuRet!=VPU_DEC_RET_SUCCESS)
		{
			DEC_STREAM_PRINTF("%s: free vpu memory failure : ret=%d \r\n",__FUNCTION__,vpuRet);
			retOk=0;
		}
	}
	pDecMem->nPhyNum	=0;
	
	return retOk;
}


int MallocMemBlock(VpuMemInfo* pMemBlock,DecMemInfo* pDecMem)
{
	int i;
	unsigned char * ptr=NULL;
	int size;
	
	for(i=0;i<pMemBlock->nSubBlockNum;i++)
	{
		size=pMemBlock->MemSubBlock[i].nAlignment+pMemBlock->MemSubBlock[i].nSize;
		if(pMemBlock->MemSubBlock[i].MemType==VPU_MEM_VIRT)
		{
			ptr=(unsigned char *)malloc(size);
			if(ptr==NULL)
			{
				DEC_STREAM_PRINTF("%s: get virtual memory failure, size=%d \r\n",__FUNCTION__,size);
				goto failure;
			}		
			pMemBlock->MemSubBlock[i].pVirtAddr=(unsigned char*)Align(ptr,pMemBlock->MemSubBlock[i].nAlignment);

			//record virtual base addr
			pDecMem->virtMem[pDecMem->nVirtNum]=(unsigned long)ptr;
			pDecMem->nVirtNum++;
		}
		else// if(memInfo.MemSubBlock[i].MemType==VPU_MEM_PHY)
		{
			VpuMemDesc vpuMem;
			VpuDecRetCode ret;
			vpuMem.nSize=size;
			ret=VPU_DecGetMem(&vpuMem);
			if(ret!=VPU_DEC_RET_SUCCESS)
			{
				DEC_STREAM_PRINTF("%s: get vpu memory failure, size=%d, ret=%d \r\n",__FUNCTION__,size,ret);
				goto failure;
			}		
			pMemBlock->MemSubBlock[i].pVirtAddr=(unsigned char*)Align(vpuMem.nVirtAddr,pMemBlock->MemSubBlock[i].nAlignment);
			pMemBlock->MemSubBlock[i].pPhyAddr=(unsigned char*)Align(vpuMem.nPhyAddr,pMemBlock->MemSubBlock[i].nAlignment);

			//record physical base addr
			pDecMem->phyMem_phyAddr[pDecMem->nPhyNum]=(unsigned long)vpuMem.nPhyAddr;
			pDecMem->phyMem_virtAddr[pDecMem->nPhyNum]=(unsigned long)vpuMem.nVirtAddr;
			pDecMem->phyMem_cpuAddr[pDecMem->nPhyNum]=(unsigned long)vpuMem.nCpuAddr;
			pDecMem->phyMem_size[pDecMem->nPhyNum]=size;
			pDecMem->nPhyNum++;			
		}
	}	

	return 1;
	
failure:
	FreeMemBlock(pDecMem);
	return 0;
	
}

int ResetBitstream(DecContxt * pDecContxt,int offset)
{
	fseek(pDecContxt->fin,offset,SEEK_SET);
	return 1;
}


int ReadBitstream(DecContxt * pDecContxt, unsigned char* pBitstream,int length)
{
	int readbytes;
	//static int totalReadSize=0;

	//DEC_STREAM_PRINTF("read %d bytes \r\n",length);
	readbytes=fread(pBitstream,1,length,pDecContxt->fin);

	//totalReadSize+=readbytes;
	//printf("total read size: %d \r\n",totalReadSize);
	return readbytes;
}


int ReadCodecData(DecContxt * pDecContxt, unsigned char* pCodecData,int length)
{
	int readbytes;
	//static int totalReadSize=0;

	//DEC_STREAM_PRINTF("read %d bytes \r\n",length);
	readbytes=fread(pCodecData,1,length,pDecContxt->fcodecdata);

	//totalReadSize+=readbytes;
	//printf("total read size: %d \r\n",totalReadSize);
	return readbytes;
}


int OutputFrame(DecContxt * pDecContxt,VpuDecOutFrameInfo* pOutFrame,int width, int height,int fbHandle,int frameNum)
{
	int ySize;
	int uvSize;
	VpuFrameBuffer* pFrame=pOutFrame->pDisplayFrameBuf;
	//VpuCodStd codecFormat;
	VpuFrameBuffer sLinearFrame;
	VpuMemDesc vpuMem;
	int NeedConvert=0;
	int wrlen;

	/*DEC_STREAM_PRINTF("dynamic resolution: [width x height]=[%d x %d]: crop: [left, top, right, bottom]=[%d, %d, %d, %d] \r\n",
		pOutFrame->pExtInfo->nFrmWidth,pOutFrame->pExtInfo->nFrmHeight,
		pOutFrame->pExtInfo->FrmCropRect.nLeft,pOutFrame->pExtInfo->FrmCropRect.nTop,
		pOutFrame->pExtInfo->FrmCropRect.nRight,pOutFrame->pExtInfo->FrmCropRect.nBottom);*/
	//DEC_STREAM_PRINTF("output one frame, [width x height]=[%d x %d] \r\n",width,height);
	//DEC_STREAM_PRINTF("output one frame, y:0x%X, u:0x%X, v:0x%X \r\n",(unsigned int)pFrame->pbufVirtY,(unsigned int)pFrame->pbufVirtCb,(unsigned int)pFrame->pbufVirtCr);
	ySize=Align(width, FRAME_ALIGN)*Align(height,FRAME_ALIGN);
	switch(pDecContxt->eOutColorFmt)
	{
		case DEC_OUT_420:
			uvSize=ySize/4;
			break;
		case DEC_OUT_422H:
		case DEC_OUT_422V:
			uvSize=ySize/2;
			break;			
		case DEC_OUT_444:
			uvSize=ySize;
			break;
		case DEC_OUT_400:
			uvSize=0;
			break;
 		default:
			uvSize=ySize/4;
			break;
	}

	if((pDecContxt->nMapType!=0) && (pDecContxt->nTile2LinearEnable==0)
		&& (pDecContxt->fout || fbHandle))
	{
		NeedConvert=1;
	}

	if(NeedConvert)
	{
		VpuDecRetCode ret;
		memset(&vpuMem,0,sizeof(VpuMemDesc));
		memset(&sLinearFrame,0,sizeof(VpuFrameBuffer));
		vpuMem.nSize=ySize+uvSize+uvSize;
		ret=VPU_DecGetMem(&vpuMem);
		if(VPU_DEC_RET_SUCCESS!=ret)
		{
			DEC_STREAM_PRINTF("%s: vpu malloc tile buf failure: ret=%d, size: %d \r\n",__FUNCTION__,ret,vpuMem.nSize);
			goto FAIL;
		}

#if 1
		if(pDecContxt->fout)
		{
			//for debug: output nv12 tile file
			FILE* fp;
			fp = fopen("temp_tile.tile", "ab");
			if(pDecContxt->nMapType==2)		// tile field
			{
				wrlen=fwrite(pFrame->pbufVirtY,1,ySize/2,fp);
				wrlen=fwrite(pFrame->pbufVirtY_tilebot,1,ySize/2,fp);
				wrlen=fwrite(pFrame->pbufVirtCb,1,uvSize,fp);
				wrlen=fwrite(pFrame->pbufVirtCb_tilebot,1,uvSize,fp);
			}
			else		// tile frame
			{
				wrlen=fwrite(pFrame->pbufVirtY,1,ySize,fp);
				wrlen=fwrite(pFrame->pbufVirtCb,1,2*uvSize,fp);
				//fwrite(pFrame->pbufVirtCr,1,uvSize,fp);				
			}
			fclose(fp);
		}	
#endif
		//FIXME: now only care pbufVirtY/Cb/Cr three address
		sLinearFrame.pbufVirtY=(unsigned char*)vpuMem.nVirtAddr;
		sLinearFrame.pbufVirtCb=sLinearFrame.pbufVirtY+ySize;
		sLinearFrame.pbufVirtCr=sLinearFrame.pbufVirtCb+uvSize;
		//printf("convert tile space: YTob: 0x%X, YBot: 0x%X, CbTop: 0x%X, CbBot: 0x%X \r\n",pFrame->pbufVirtY, pFrame->pbufVirtY_tilebot, pFrame->pbufVirtCb, pFrame->pbufVirtCb_tilebot);
		ConvertDataFromTileToLinear(pDecContxt->nMapType, width, height, 
			pFrame->pbufVirtY, pFrame->pbufVirtY_tilebot, pFrame->pbufVirtCb, pFrame->pbufVirtCb_tilebot, 
			sLinearFrame.pbufVirtY,sLinearFrame.pbufVirtCb, sLinearFrame.pbufVirtCr);
		pFrame=&sLinearFrame;
	}

	DEC_TRACE;
	//output file
	if(pDecContxt->fout)
	{
		wrlen=fwrite(pFrame->pbufVirtY,1,ySize,pDecContxt->fout);
		wrlen=fwrite(pFrame->pbufVirtCb,1,uvSize,pDecContxt->fout);
		wrlen=fwrite(pFrame->pbufVirtCr,1,uvSize,pDecContxt->fout);
	}
	DEC_TRACE;
	//display 
	if(fbHandle)
	{
		fb_render_drawYUVframe(fbHandle, pFrame->pbufVirtY, pFrame->pbufVirtCb, pFrame->pbufVirtCr, Align(width, FRAME_ALIGN), Align(height,FRAME_ALIGN));
	}

	//ConvertCodecFormat(pDecContxt->nCodec, &codecFormat);
	switch(pOutFrame->ePicType)
	{
		case VPU_I_PIC:
			DEC_STREAM_PRINTF("frame : %d (I) \r\n",frameNum);
			break;
		case VPU_P_PIC:
			DEC_STREAM_PRINTF("frame : %d (P) \r\n",frameNum);
			break;
		case VPU_B_PIC:
			DEC_STREAM_PRINTF("frame : %d (B) \r\n",frameNum);
			break;
		case VPU_BI_PIC:
			DEC_STREAM_PRINTF("frame : %d (BI) \r\n",frameNum);
			break;			
		case VPU_IDR_PIC:
			DEC_STREAM_PRINTF("frame : %d (IDR) \r\n",frameNum);
			break;			
		case VPU_SKIP_PIC:
			DEC_STREAM_PRINTF("frame : %d (SKIP) \r\n",frameNum);
			break;			
		default:
			DEC_STREAM_PRINTF("frame : %d (*) \r\n",frameNum);
			break;
	}

	if(NeedConvert)
	{
		VPU_DecFreeMem(&vpuMem);	
	}
	return 1;	
FAIL:
	return 0;
}

int RenderInit(DecContxt * pDecContxt,VpuDecInitInfo* pInitInfo,int* pFbHandle)
{
	int ipu_ret=0;
	//1 only support 4:2:0 !!!
	//init dispaly device 
	if(*pFbHandle)
	{
		//release before re-init
		fb_render_uninit(*pFbHandle);
	}
	if(0)//if(InitInfo.nInterlace)
	{
		ipu_ret=fb_render_init(pFbHandle, pDecContxt->nFbNo, Align(pInitInfo->nPicWidth,FRAME_ALIGN),Align(pInitInfo->nPicHeight,2*FRAME_ALIGN));
	}
	else
	{
		ipu_ret=fb_render_init(pFbHandle, pDecContxt->nFbNo, Align(pInitInfo->nPicWidth,FRAME_ALIGN),Align(pInitInfo->nPicHeight,FRAME_ALIGN));
	}
	if(0==ipu_ret)
	{	
		DEC_STREAM_PRINTF("%s: init fb render failure: \r\n",__FUNCTION__);
		//err=1;
		//break;
		*pFbHandle=(int)NULL;
	}
	return 1;
}

int ProcessInitInfo(DecContxt * pDecContxt,VpuDecHandle handle,VpuDecInitInfo* pInitInfo,DecMemInfo* pDecMemInfo, int*pOutFrmNum)
{
	VpuDecRetCode ret;
	VpuFrameBuffer frameBuf[MAX_FRAME_NUM];
	VpuMemDesc vpuMem;
	int BufNum;
	int i;
	int totalSize=0;
	int mvSize=0;
	int ySize=0;
	int uSize=0;
	int vSize=0;
	int yStride=0;
	int uStride=0;
	int vStride=0;
	unsigned char* ptr;
	unsigned char* ptrVirt;
	int nAlign;
	int multifactor=1;

	//get init info	
	DEC_TRACE;
	ret=VPU_DecGetInitialInfo(handle, pInitInfo);
	DEC_TRACE;
	if(VPU_DEC_RET_SUCCESS!=ret)
	{
		DEC_STREAM_PRINTF("%s: vpu get init info failure: ret=%d \r\n",__FUNCTION__,ret);	
		return 0;
	}
	//malloc frame buffs
	BufNum=pInitInfo->nMinFrameBufferCount+FRAME_SURPLUS;
	if(BufNum>MAX_FRAME_NUM)
	{
		DEC_STREAM_PRINTF("%s: vpu request too many frames : num=0x%X \r\n",__FUNCTION__,pInitInfo->nMinFrameBufferCount);	
		return 0;		
	}

	yStride=Align(pInitInfo->nPicWidth,FRAME_ALIGN);
	if(pInitInfo->nInterlace)
	{
		ySize=Align(pInitInfo->nPicWidth,FRAME_ALIGN)*Align(pInitInfo->nPicHeight,(2*FRAME_ALIGN));
	}
	else
	{
		ySize=Align(pInitInfo->nPicWidth,FRAME_ALIGN)*Align(pInitInfo->nPicHeight,FRAME_ALIGN);
	}

#ifdef ILLEGAL_MEMORY_DEBUG
	//in such debug case, we always allocate big enough frame buffers
	DEC_STREAM_PRINTF("enable illegal memory detect, buffer size: 1920*(1088+64) \r\n");
	ySize=1920*(1088+64);
#endif

	//for MJPG: we need to check 4:4:4/4:2:2/4:2:0/4:0:0
	{
		VpuCodStd vpuCodec=0;
		ConvertCodecFormat(pDecContxt->nCodec, &vpuCodec);
		if(VPU_V_MJPG==vpuCodec)
		{
			switch(pInitInfo->nMjpgSourceFormat)
			{
				case 0:	//4:2:0
					DEC_STREAM_PRINTF("MJPG: 4:2:0 \r\n");
					uStride=yStride/2;
					vStride=uStride;
					uSize=ySize/4;
					vSize=uSize;	
					mvSize=uSize;
					pDecContxt->eOutColorFmt=DEC_OUT_420;
					break;
				case 1:	//4:2:2 hor
					DEC_STREAM_PRINTF("MJPG: 4:2:2 hor \r\n");
					uStride=yStride/2;
					vStride=uStride;
					uSize=ySize/2;
					vSize=uSize;	
					mvSize=uSize;
					pDecContxt->eOutColorFmt=DEC_OUT_422H;
					break;
				case 2:	//4:2:2 ver
					DEC_STREAM_PRINTF("MJPG: 4:2:2 ver \r\n");				
					uStride=yStride;
					vStride=uStride;
					uSize=ySize/2;
					vSize=uSize;	
					mvSize=uSize;
					pDecContxt->eOutColorFmt=DEC_OUT_422V;
					break;
				case 3:	//4:4:4
					DEC_STREAM_PRINTF("MJPG: 4:4:4 \r\n");				
					uStride=yStride;
					vStride=uStride;
					uSize=ySize;
					vSize=uSize;	
					mvSize=uSize;
					pDecContxt->eOutColorFmt=DEC_OUT_444;
					break;
				case 4:	//4:0:0
					DEC_STREAM_PRINTF("MJPG: 4:0:0 \r\n");				
					uStride=0;
					vStride=uStride;
					uSize=0;
					vSize=uSize;	
					mvSize=uSize;
					pDecContxt->eOutColorFmt=DEC_OUT_400;
					break;
				default:	//4:2:0
					DEC_STREAM_PRINTF("unknown color format: %d \r\n",vpuCodec);
					uStride=yStride/2;
					vStride=uStride;
					uSize=ySize/4;
					vSize=uSize;	
					mvSize=uSize;
					pDecContxt->eOutColorFmt=DEC_OUT_420;
					break;			
			}
		}
		else
		{
			//4:2:0 for all video
			uStride=yStride/2;
			vStride=uStride;
			uSize=ySize/4;
			vSize=uSize;	
			mvSize=uSize;
			pDecContxt->eOutColorFmt=DEC_OUT_420;
		}
	}

	nAlign=pInitInfo->nAddressAlignment;
	if(pDecContxt->nMapType==2)
	{
		//only consider Y since interleave must be enabled
		multifactor=2;	//for field, we need to consider alignment for top and bot
	}
	if(nAlign>1)
	{
		ySize=Align(ySize,multifactor*nAlign);
		uSize=Align(uSize,nAlign);
		vSize=Align(vSize,nAlign);
	}
	
#if 1	// avoid buffer is too big to malloc by vpu
	for(i=0;i<BufNum;i++)
	{
		totalSize=(ySize+uSize+vSize+mvSize+nAlign)*1;

		vpuMem.nSize=totalSize;
		DEC_TRACE;
		ret=VPU_DecGetMem(&vpuMem);
		DEC_TRACE;
		if(VPU_DEC_RET_SUCCESS!=ret)
		{
			DEC_STREAM_PRINTF("%s: vpu malloc frame buf failure: ret=%d \r\n",__FUNCTION__,ret);	
			return 0;
		}
		//record memory info for release
		pDecMemInfo->phyMem_phyAddr[pDecMemInfo->nPhyNum]=vpuMem.nPhyAddr;
		pDecMemInfo->phyMem_virtAddr[pDecMemInfo->nPhyNum]=vpuMem.nVirtAddr;
		pDecMemInfo->phyMem_cpuAddr[pDecMemInfo->nPhyNum]=vpuMem.nCpuAddr;
		pDecMemInfo->phyMem_size[pDecMemInfo->nPhyNum]=vpuMem.nSize;
		pDecMemInfo->nPhyNum++;

		//fill frameBuf
		ptr=(unsigned char*)vpuMem.nPhyAddr;
		ptrVirt=(unsigned char*)vpuMem.nVirtAddr;

		/*align the base address*/
		if(nAlign>1)
		{
			ptr=(unsigned char*)Align(ptr,nAlign);
			ptrVirt=(unsigned char*)Align(ptrVirt,nAlign);
		}

		/* fill stride info */
		frameBuf[i].nStrideY=yStride;
		frameBuf[i].nStrideC=uStride;	

		/* fill phy addr*/
		frameBuf[i].pbufY=ptr;
		frameBuf[i].pbufCb=ptr+ySize;
		frameBuf[i].pbufCr=ptr+ySize+uSize;
		frameBuf[i].pbufMvCol=ptr+ySize+uSize+vSize;
		//ptr+=ySize+uSize+vSize+mvSize;
		/* fill virt addr */
		frameBuf[i].pbufVirtY=ptrVirt;
		frameBuf[i].pbufVirtCb=ptrVirt+ySize;
		frameBuf[i].pbufVirtCr=ptrVirt+ySize+uSize;
		frameBuf[i].pbufVirtMvCol=ptrVirt+ySize+uSize+vSize;
		//ptrVirt+=ySize+uSize+vSize+mvSize;

#ifdef ILLEGAL_MEMORY_DEBUG
		memset(frameBuf[i].pbufVirtY,0,ySize);
		memset(frameBuf[i].pbufVirtCb,0,uSize);
		memset(frameBuf[i].pbufVirtCr,0,uSize);
#endif

		/* fill bottom address for field tile*/
		if(pDecContxt->nMapType==2)
		{
			frameBuf[i].pbufY_tilebot=frameBuf[i].pbufY+ySize/2;
			frameBuf[i].pbufCb_tilebot=frameBuf[i].pbufCr;
			frameBuf[i].pbufVirtY_tilebot=frameBuf[i].pbufVirtY+ySize/2;
			frameBuf[i].pbufVirtCb_tilebot=frameBuf[i].pbufVirtCr;			
		}
		else
		{
			frameBuf[i].pbufY_tilebot=0;
			frameBuf[i].pbufCb_tilebot=0;
			frameBuf[i].pbufVirtY_tilebot=0;
			frameBuf[i].pbufVirtCb_tilebot=0;
		}
	}
#else
	mvSize=Align(mvSize,nAlign);
	totalSize=(ySize+uSize+vSize+mvSize)*BufNum+nAlign;

	vpuMem.nSize=totalSize;
	DEC_TRACE;
	ret=VPU_DecGetMem(&vpuMem);
	DEC_TRACE;
	if(VPU_DEC_RET_SUCCESS!=ret)
	{
		DEC_STREAM_PRINTF("%s: vpu malloc frame buf failure: ret=%d \r\n",__FUNCTION__,ret);	
		return 0;
	}
	//record memory info for release
	pDecMemInfo->phyMem_phyAddr[pDecMemInfo->nPhyNum]=vpuMem.nPhyAddr;
	pDecMemInfo->phyMem_virtAddr[pDecMemInfo->nPhyNum]=vpuMem.nVirtAddr;
	pDecMemInfo->phyMem_cpuAddr[pDecMemInfo->nPhyNum]=vpuMem.nCpuAddr;
	pDecMemInfo->phyMem_size[pDecMemInfo->nPhyNum]=vpuMem.nSize;
	pDecMemInfo->nPhyNum++;

	//fill frameBuf
	ptr=(unsigned char*)vpuMem.nPhyAddr;
	ptrVirt=(unsigned char*)vpuMem.nVirtAddr;

	/*align the base address*/
	if(nAlign>1)
	{
		ptr=(unsigned char*)Align(ptr,nAlign);
		ptrVirt=(unsigned char*)Align(ptrVirt,nAlign);
	}
	
	for(i=0;i<BufNum;i++)
	{
		/* fill stride info */
		frameBuf[i].nStrideY=yStride;
		frameBuf[i].nStrideC=uStride;	

		/* fill phy addr*/
		frameBuf[i].pbufY=ptr;
		frameBuf[i].pbufCb=ptr+ySize;
		frameBuf[i].pbufCr=ptr+ySize+uSize;
		frameBuf[i].pbufMvCol=ptr+ySize+uSize+vSize;
		ptr+=ySize+uSize+vSize+mvSize;
		/* fill virt addr */
		frameBuf[i].pbufVirtY=ptrVirt;
		frameBuf[i].pbufVirtCb=ptrVirt+ySize;
		frameBuf[i].pbufVirtCr=ptrVirt+ySize+uSize;
		frameBuf[i].pbufVirtMvCol=ptrVirt+ySize+uSize+vSize;
		ptrVirt+=ySize+uSize+vSize+mvSize;

		/* fill bottom address for field tile*/
		if(pDecContxt->nMapType==2)
		{
			frameBuf[i].pbufY_tilebot=frameBuf[i].pbufY+ySize/2;
			frameBuf[i].pbufCb_tilebot=frameBuf[i].pbufCr;
			frameBuf[i].pbufVirtY_tilebot=frameBuf[i].pbufVirtY+ySize/2;
			frameBuf[i].pbufVirtCb_tilebot=frameBuf[i].pbufVirtCr;			
		}
		else
		{
			frameBuf[i].pbufY_tilebot=0;
			frameBuf[i].pbufCb_tilebot=0;
			frameBuf[i].pbufVirtY_tilebot=0;
			frameBuf[i].pbufVirtCb_tilebot=0;
		}		
	}
#endif

	VpuBufferNode in_data = {0};
    int buf_ret;
	VpuDecRetCode dec_ret;
    dec_ret=VPU_DecDecodeBuf(handle, &in_data, &buf_ret);
	if (dec_ret == VPU_DEC_RET_FAILURE) {
      DEC_STREAM_PRINTF("VPU_DecDecodeBuf fail \r\n");
	  return 0;
	}

	//register frame buffs
	DEC_TRACE;
	ret=VPU_DecRegisterFrameBuffer(handle, frameBuf, BufNum);
	DEC_TRACE;
	if(VPU_DEC_RET_SUCCESS!=ret)
	{
		DEC_STREAM_PRINTF("%s: vpu register frame failure: ret=%d \r\n",__FUNCTION__,ret);	
		return 0;
	}	

	*pOutFrmNum=BufNum;
	return 1;
}


int DecodeLoop(VpuDecHandle handle,DecContxt * pDecContxt, unsigned char* pBitstream, unsigned char* pCodecData,DecMemInfo* pDecMemInfo,int* pFbHandle)
{
	int err;
	int fileeos;
	int dispeos;
	int readbytes=0;
	int readCodecData=0;
	int bufNull;
	int dispFrameNum;
	int repeatNum=pDecContxt->nRepeatNum;
	int unitDataValidNum;
	int unitDataSize=pDecContxt->nUnitDataSize;
	VpuDecRetCode ret;
	VpuBufferNode InData;
	int bufRetCode=0;
	VpuDecInitInfo InitInfo;
	VpuDecOutFrameInfo frameInfo;
	unsigned long long totalTime=0;
	int capability=0;
	VpuDecFrameLengthInfo decFrmLengthInfo;
	unsigned int totalDecConsumedBytes;	//stuffer + frame
	int nFrmNum;
	int streamLen;

#ifdef CHECK_DEAD_LOOP
	int NotUsedLoopCnt=0;
	int NULLDataLoopCnt=0;	
#endif

	VPU_DecGetCapability(handle, VPU_DEC_CAP_FRAMESIZE, &capability);
	DEC_STREAM_PRINTF("capability: report frame size supported: %d \r\n",capability);

RepeatPlay:

	//reset init value
	err=0;
	fileeos=0;
	dispeos=0;	
	dispFrameNum=0;
	unitDataValidNum=0;
	totalDecConsumedBytes=0;

	time_init(TIME_DEC_ID);
	time_init(TIME_TOTAL_ID);
	time_start(TIME_TOTAL_ID);

#ifdef VPU_FILE_MODE_TEST
	g_filemode_curloc=0;
#endif
	
	//init buff status
	bufNull=1;

	//here, we use the one config for the whole stream
	{
		VpuDecConfig config;		
		int param;

		//config skip type
		if(0==ConvertSkipMode(pDecContxt->nSkipMode,&config,&param))
		{
			DEC_STREAM_PRINTF("unvalid skip mode: %d, ignored \r\n",pDecContxt->nSkipMode);
			config=VPU_DEC_CONF_SKIPMODE;
			param=VPU_DEC_SKIPNONE;
		}
		ret=VPU_DecConfig(handle, config, &param);
		if(VPU_DEC_RET_SUCCESS!=ret)
		{
			DEC_STREAM_PRINTF("%s: vpu config failure: config=0x%X, ret=%d \r\n",__FUNCTION__,(unsigned int)config,ret);
			err=1;
			goto Exit;
		}	

		//config delay buffer size
		if(pDecContxt->nDelayBufSize>=0)
		{
			config=VPU_DEC_CONF_BUFDELAY;
			param=pDecContxt->nDelayBufSize;
			DEC_STREAM_PRINTF("set delay buffer size: %d bytes \r\n",param);
			ret=VPU_DecConfig(handle, config, &param);
			if(VPU_DEC_RET_SUCCESS!=ret)
			{
				DEC_STREAM_PRINTF("%s: vpu config failure: config=0x%X, ret=%d \r\n",__FUNCTION__,(unsigned int)config,ret);
				err=1;
				goto Exit;
			}
		}

		//config input type: normal
		config=VPU_DEC_CONF_INPUTTYPE;
		param=VPU_DEC_IN_NORMAL;
		DEC_STREAM_PRINTF("set input type : normal(%d)  \r\n",param);
		ret=VPU_DecConfig(handle, config, &param);
		if(VPU_DEC_RET_SUCCESS!=ret)
		{
			DEC_STREAM_PRINTF("%s: vpu config failure: config=0x%X, ret=%d \r\n",__FUNCTION__,(unsigned int)config,ret);
			err=1;
			goto Exit;
		}
	}

	//main loop for playing
	while((0==dispeos) && (dispFrameNum < pDecContxt->nMaxNum) && (unitDataValidNum<pDecContxt->nUintDataNum))
	{
		//you can add flexible setting here
		if(1)
		{
			/*
			Allowed APIs including:
				VPU_DecConfig(handle, config, &param);
				VPU_DecFlushLeftStream(handle);
				VPU_DecFlushLeftFrame(handle);
			*/	
		}

#ifdef CHECK_DEAD_LOOP
		//avoid dead loop : can not write data into decoder
		if(0==bufNull)
		{
			NotUsedLoopCnt++;
		}
		else
		{
			NotUsedLoopCnt=0;
		}
		if(NotUsedLoopCnt>MAX_NOTUSED_LOOP)
		{
			DEC_STREAM_PRINTF("dead loop %d times (decoder hang), and return directly !!! \r\n",NotUsedLoopCnt);
			err=1;
			break;
		}

		// avoid dead loop: decoder still has no valid response when reaching file end
		if((readbytes==0)&&((bufRetCode&0x2E)==0)) //bit[5:1]
		{
			NULLDataLoopCnt++;
		}
		else
		{
			NULLDataLoopCnt=0;
		}
		if(NULLDataLoopCnt>MAX_NULL_LOOP)
		{
			DEC_STREAM_PRINTF("dead loop %d times (decoder has no valid response), and return directly !!! \r\n",NULLDataLoopCnt);
			err=1;
			break;
		}			
#endif		

		if(fileeos)
		{
			if(bufNull)
			{
				readbytes=0;
#if 1
				{
					//config input type: drain
					VpuDecConfig config;		
					int param;
					config=VPU_DEC_CONF_INPUTTYPE;
					param=VPU_DEC_IN_DRAIN;
					//DEC_STREAM_PRINTF("set input type : drain(%d)  \r\n",param);
					ret=VPU_DecConfig(handle, config, &param);
					if(VPU_DEC_RET_SUCCESS!=ret)
					{
						DEC_STREAM_PRINTF("%s: vpu config failure: config=0x%X, ret=%d \r\n",__FUNCTION__,(unsigned int)config,ret);
						err=1;
						break;
					}
				}
#endif
			}
			else
			{
				//do nothing, continue last data
			}
		}
		else
		{
			if(bufNull)
			{
				//read new data into bitstream buf
#ifdef VPU_FILE_MODE_TEST				
				unitDataSize=g_filemode_frame_length[g_filemode_curloc];
				FILE_MODE_LOG("frame [%d]: length: %d \r\n",g_filemode_curloc,unitDataSize);
				readbytes=ReadBitstream(pDecContxt, pBitstream,unitDataSize);
				if(pDecContxt->isavcc)
					readCodecData=ReadCodecData(pDecContxt, pCodecData,unitDataSize);
				g_filemode_curloc=(g_filemode_curloc+1)%FILE_MODE_MAX_FRAME_NUM;
#else
				readbytes=ReadBitstream(pDecContxt, pBitstream, unitDataSize);
				if(pDecContxt->isavcc)
					readCodecData=ReadCodecData(pDecContxt, pCodecData,unitDataSize);
#endif
				if(unitDataSize!=readbytes)
				{
					//EOS
					DEC_STREAM_PRINTF("%s: read file end: last size=0x%X \r\n",__FUNCTION__,readbytes);
					fileeos=1;
				}
				if(pDecContxt->isavcc)
				{
					if(unitDataSize!=readCodecData)
						DEC_STREAM_PRINTF("%s: read codec data end: last size=0x%X \r\n",__FUNCTION__,readCodecData);
				}
				bufNull=0;
				streamLen = readbytes;
			}
			else
			{
				//skip read
				//DEC_STREAM_PRINTF("skip read  \r\n");
			}
		}		

		//DEC_STREAM_PRINTF("readbytes: %d , bufRetCode: %d  \r\n",readbytes,bufRetCode);
		//decode bitstream buf
		InData.nSize=readbytes;
		InData.pPhyAddr=NULL;
		InData.pVirAddr=pBitstream;
		InData.sCodecData.pData=NULL;
		InData.sCodecData.nSize=0;
		if(pDecContxt->isavcc)
		{
			InData.sCodecData.pData=pCodecData;
			InData.sCodecData.nSize=readCodecData;
		}

		//all bytes are consumed -> eos
		DEC_STREAM_PRINTF ("\n=== totalDecConsumedBytes: 0x%X, streamLen: 0x%X \n",totalDecConsumedBytes, streamLen);
		if (totalDecConsumedBytes == streamLen)
		{
			InData.nSize=0;
			InData.pVirAddr=(unsigned char *) 0x1;
		}

		if((pDecContxt->nCodec==13)||((pDecContxt->nCodec==3)))
		{
			//backdoor:  to notify vpu this is raw data. vpu wrapper shouldn't add other additional headers.
#ifndef IMX5_PLATFORM  //1 check iMX5 or iMX6 ??
			InData.sCodecData.nSize=0xFFFFFFFF;	//for iMX6: VP8, DivX3
#else			
			InData.sCodecData.nSize=0;	//for iMX5:DivX3:  (1)file mode;  (2) stream mode(need to feed .avi ?)
#endif
		}
		DEC_TRACE;
		time_init(TIME_RESOLUTION_ID);
		time_start(TIME_RESOLUTION_ID);		
		time_start(TIME_DEC_ID);
		ret=VPU_DecDecodeBuf(handle, &InData,&bufRetCode);
		time_stop(TIME_DEC_ID);
		DEC_TRACE;
		//DEC_STREAM_PRINTF("%s: bufRetCode=0x%X \r\n",__FUNCTION__,bufRetCode);	
		if(VPU_DEC_RET_SUCCESS!=ret)
		{
			DEC_STREAM_PRINTF("%s: vpu dec buf failure: ret=%d \r\n",__FUNCTION__,ret);	
			err=1;
			break;
		}
		
		//check input buff	
		if(bufRetCode&VPU_DEC_INPUT_USED)
		{
			bufNull=1;
			unitDataValidNum++;
		}

		//check init info
		if(bufRetCode&VPU_DEC_INIT_OK)
		{
			//process init info
			if(0==ProcessInitInfo(pDecContxt,handle,&InitInfo,pDecMemInfo,&nFrmNum))
			{
				DEC_STREAM_PRINTF("%s: vpu process init info failure: \r\n",__FUNCTION__);
				err=1;
				break;
			}
			DEC_STREAM_PRINTF("%s: Init OK, [width x height]=[%d x %d], Interlaced: %d, MinFrm: %d \r\n",__FUNCTION__,InitInfo.nPicWidth,InitInfo.nPicHeight,InitInfo.nInterlace,InitInfo.nMinFrameBufferCount);

			if(pDecContxt->nDisplay)
			{
				RenderInit(pDecContxt, &InitInfo, pFbHandle);
			}
		}

		//check resolution change
		if(bufRetCode&VPU_DEC_RESOLUTION_CHANGED)
		{
			long long ts;
			DEC_STREAM_PRINTF("receive resolution changed event, will release previous frames %d \r\n",nFrmNum);
			//time_init(TIME_RESOLUTION_ID);
			//time_start(TIME_RESOLUTION_ID);
			//release previous frames
			FreeMemBlockFrame(pDecMemInfo, nFrmNum);
			//get new init info
			if(0==ProcessInitInfo(pDecContxt,handle,&InitInfo,pDecMemInfo,&nFrmNum))
			{
				DEC_STREAM_PRINTF("%s: vpu process re-init info failure: \r\n",__FUNCTION__);
				err=1;
				break;
			}
			time_stop(TIME_RESOLUTION_ID);
			ts=time_report(TIME_RESOLUTION_ID);
			DEC_STREAM_PRINTF("%s: Re-Init OK, [width x height]=[%d x %d], Interlaced: %d, MinFrm: %d \r\n",__FUNCTION__,InitInfo.nPicWidth,InitInfo.nPicHeight,InitInfo.nInterlace,InitInfo.nMinFrameBufferCount);
			DEC_STREAM_PRINTF("time for process of resolution change event is %lld(us) \r\n",ts);
			if(pDecContxt->nDisplay)
			{
				RenderInit(pDecContxt, &InitInfo, pFbHandle);
			}
			if(pDecContxt->fout)
			{
				DEC_STREAM_PRINTF("seek to head of write file \r\n");
				fseek(pDecContxt->fout,0,SEEK_SET);
			}
		}
		
		//check frame size
		if(capability)
		{
			if(bufRetCode&VPU_DEC_ONE_FRM_CONSUMED || bufRetCode&VPU_DEC_NO_ENOUGH_INBUF)
			{
				ret=VPU_DecGetConsumedFrameInfo(handle, &decFrmLengthInfo);
				if(VPU_DEC_RET_SUCCESS!=ret)
				{
					DEC_STREAM_PRINTF("%s: vpu get consumed frame info failure: ret=%d \r\n",__FUNCTION__,ret);	
					err=1;
					break;
				}
				totalDecConsumedBytes+=decFrmLengthInfo.nFrameLength+decFrmLengthInfo.nStuffLength;
				DEC_STREAM_PRINTF("[total:0x%X]:one frame is consumed: 0x%X, consumed total size: %d (stuff size: %d, frame size: %d) \r\n",totalDecConsumedBytes,(unsigned int)decFrmLengthInfo.pFrame,decFrmLengthInfo.nStuffLength+decFrmLengthInfo.nFrameLength,decFrmLengthInfo.nStuffLength,decFrmLengthInfo.nFrameLength);
			}
		}

		//check output buff
		if((bufRetCode&VPU_DEC_OUTPUT_DIS)||(bufRetCode&VPU_DEC_OUTPUT_MOSAIC_DIS))
		{
			//get output frame
			ret=VPU_DecGetOutputFrame(handle, &frameInfo);
			if(VPU_DEC_RET_SUCCESS!=ret)
			{
				DEC_STREAM_PRINTF("%s: vpu get output frame failure: ret=%d \r\n",__FUNCTION__,ret);	
				err=1;
				break;
			}			

			//update display frame count
			dispFrameNum++;

			//display or output frame buff
			DEC_TRACE;
			OutputFrame(pDecContxt,&frameInfo,InitInfo.nPicWidth,InitInfo.nPicHeight,*pFbHandle,dispFrameNum);
			DEC_TRACE;

			//clear frame display flag
			ret=VPU_DecOutFrameDisplayed(handle,frameInfo.pDisplayFrameBuf);
			if(VPU_DEC_RET_SUCCESS!=ret)
			{
				DEC_STREAM_PRINTF("%s: vpu clear frame display failure: ret=%d \r\n",__FUNCTION__,ret);	
				err=1;
				break;
			}
			//DEC_STREAM_PRINTF("return frame: 0x%X \r\n",(unsigned int)frameInfo.pDisplayFrameBuf);
		}
		else if (bufRetCode&VPU_DEC_OUTPUT_EOS)
		{
			dispeos=1;
		}
		else if (bufRetCode&VPU_DEC_OUTPUT_NODIS)
		{
			DEC_TRACE;
		}
		else if (bufRetCode&VPU_DEC_OUTPUT_REPEAT)
		{
			DEC_TRACE;
		}
		else if (bufRetCode&VPU_DEC_OUTPUT_DROPPED)
		{
			DEC_TRACE;
		}
		else
		{
			DEC_TRACE;
		}

		//check whether some frames are skipped by vpu
		if(bufRetCode&VPU_DEC_SKIP)
		{
			//need to get one time stamp to sync the count between frame and timestamp
		}

		//other cases ?
		if(0==(bufRetCode&VPU_DEC_OUTPUT_DIS))
		{
			USLEEP(SLEEP_TIME_US);
		}

	}


	//check other options when no err
	if(err==0)
	{
		//flush bits and frames, it is useful when 'nMaxNum' option is used
		//we should call VPU_DecFlushAll() before close vpu, since some operations may be missing(after enabled nUintDataNum), such as vpu_DecGetOutputInfo() !!!
		ret=VPU_DecFlushAll(handle);
		if(VPU_DEC_RET_SUCCESS!=ret)
		{
			DEC_STREAM_PRINTF("%s: vpu flush failure: ret=%d \r\n",__FUNCTION__,ret);
			err=1;
			goto Exit;
		}

		if(repeatNum>0)
		{
			repeatNum--;
			//replay stream, for VC1/VP8/RV/DIVX3, we need to seek to frame boundary !!!
			DEC_STREAM_PRINTF("repeat: seek to offset: %d(0x%X) bytes \r\n",pDecContxt->nOffset,pDecContxt->nOffset);
			ResetBitstream(pDecContxt,pDecContxt->nOffset);
			goto RepeatPlay;
		}
	}

Exit:
	time_stop(TIME_TOTAL_ID);
	//set output info for user
	pDecContxt->nFrameNum=dispFrameNum;
	pDecContxt->nWidth=InitInfo.nPicWidth;
	pDecContxt->nHeight=InitInfo.nPicHeight;
	pDecContxt->nErr=err;
	totalTime=time_report(TIME_DEC_ID);
	pDecContxt->nDecFps=(unsigned long long)1000000*dispFrameNum/totalTime;
	totalTime=time_report(TIME_TOTAL_ID);
	pDecContxt->nTotalFps=(unsigned long long)1000000*dispFrameNum/totalTime;	

	return ((err==0)?1:0);

}

int decode_stream(DecContxt * pDecContxt)
{
	DecMemInfo decMemInfo;
	VpuDecRetCode ret;
	VpuVersionInfo ver;
	VpuWrapperVersionInfo w_ver;
	VpuMemInfo memInfo;
	VpuDecHandle handle;
	VpuDecOpenParam decOpenParam;
	int fbHandle=(int)NULL;
	int noerr;
	int capability=0;

	unsigned char* pBitstream=NULL;
	unsigned char* pCodecData=NULL;
#ifdef VPU_FILE_MODE_TEST		
	int nUnitDataSize=FILE_MODE_MAX_FRAME_LEN;
#else
	int nUnitDataSize=pDecContxt->nUnitDataSize;
#endif

	//alloc bitstream buffer
	pBitstream=malloc(nUnitDataSize);
	if(NULL==pBitstream)
	{
		DEC_STREAM_PRINTF("%s: alloc bitstream buf failure: size=0x%X \r\n",__FUNCTION__,nUnitDataSize);
		return 0;
	}

	//alloc codec date buffer
	pCodecData=malloc(nUnitDataSize);
	if(NULL==pCodecData)
	{
		DEC_STREAM_PRINTF("%s: alloc codec data buf failure: size=0x%X \r\n",__FUNCTION__,nUnitDataSize);
		return 0;
	}
	//clear 0
	memset(&decOpenParam, 0, sizeof(VpuDecOpenParam));
	memset(&memInfo,0,sizeof(VpuMemInfo));
	memset(&decMemInfo,0,sizeof(DecMemInfo));

	//load vpu
	ret=VPU_DecLoad();
	if (ret!=VPU_DEC_RET_SUCCESS)
	{
		DEC_STREAM_PRINTF("%s: vpu load failure: ret=%d \r\n",__FUNCTION__,ret);
		//goto finish;		
		free(pBitstream);
		return 0;
	}

	//version info
	ret=VPU_DecGetVersionInfo(&ver);
	if (ret!=VPU_DEC_RET_SUCCESS)
	{
		DEC_STREAM_PRINTF("%s: vpu get version failure: ret=%d \r\n",__FUNCTION__,ret);
		goto finish;
	}
	DEC_STREAM_PRINTF("vpu lib version : major.minor.rel=%d.%d.%d \r\n",ver.nLibMajor,ver.nLibMinor,ver.nLibRelease);
	DEC_STREAM_PRINTF("vpu fw version : major.minor.rel_rcode=%d.%d.%d_r%d \r\n",ver.nFwMajor,ver.nFwMinor,ver.nFwRelease,ver.nFwCode);

	//wrapper version info
	ret=VPU_DecGetWrapperVersionInfo(&w_ver);
	if (ret!=VPU_DEC_RET_SUCCESS)
	{
		DEC_STREAM_PRINTF("%s: vpu get wrapper version failure: ret=%d \r\n",__FUNCTION__,ret);
		goto finish;
	}
	DEC_STREAM_PRINTF("vpu wrapper version : major.minor.rel=%d.%d.%d: %s \r\n",w_ver.nMajor,w_ver.nMinor,w_ver.nRelease,w_ver.pBinary);

	//query memory
	ret=VPU_DecQueryMem(&memInfo);
	if (ret!=VPU_DEC_RET_SUCCESS)
	{
		DEC_STREAM_PRINTF("%s: vpu query memory failure: ret=%d \r\n",__FUNCTION__,ret);
		goto finish;		
	}

	//malloc memory for vpu wrapper
	if(0==MallocMemBlock(&memInfo,&decMemInfo))
	{
		DEC_STREAM_PRINTF("%s: malloc memory failure: \r\n",__FUNCTION__);
		goto finish;		
	}

	//set open params
	if(0==ConvertCodecFormat(pDecContxt->nCodec, &decOpenParam.CodecFormat))
	{
		DEC_STREAM_PRINTF("%s: unsupported codec format: id=%d \r\n",__FUNCTION__,pDecContxt->nCodec);
		goto finish;		
	}	
	
	decOpenParam.nReorderEnable=1;	//for H264
#ifdef VPU_FILE_MODE_TEST	
	decOpenParam.nEnableFileMode=1;	//unit test: using file mode
#else
	decOpenParam.nEnableFileMode=0;	//unit test: using stream mode
#endif	
	//if(decOpenParam.CodecFormat==VPU_V_DIVX3)
	//{
	//	decOpenParam.nPicWidth=pDecContxt->nInWidth;
	//	decOpenParam.nPicHeight=pDecContxt->nInHeight;
	//}

	//check capabilities
	VPU_DecGetCapability((VpuDecHandle)NULL, VPU_DEC_CAP_FILEMODE, &capability);
	DEC_STREAM_PRINTF("capability: file mode supported: %d \r\n",capability);
	VPU_DecGetCapability((VpuDecHandle)NULL, VPU_DEC_CAP_TILE, &capability);
	DEC_STREAM_PRINTF("capability: tile format supported: %d \r\n",capability);
	if((capability==0)&&(pDecContxt->nMapType!=0))
	{
		DEC_STREAM_PRINTF("ERROR: tile format is not supported \r\n");
	}

	decOpenParam.nChromaInterleave=pDecContxt->nChromaInterleave;
	decOpenParam.nMapType=pDecContxt->nMapType;
	decOpenParam.nTiled2LinearEnable=pDecContxt->nTile2LinearEnable;
#ifdef USE_IMX8MM
	decOpenParam.nEnableVideoCompressor = 0;
#endif
	//open vpu
	ret=VPU_DecOpen(&handle, &decOpenParam, &memInfo);
	if (ret!=VPU_DEC_RET_SUCCESS)
	{
		DEC_STREAM_PRINTF("%s: vpu open failure: ret=%d \r\n",__FUNCTION__,ret);
		goto finish;
	}	
	
	//decoding loop
	noerr=DecodeLoop(handle, pDecContxt, pBitstream,pCodecData,&decMemInfo,&fbHandle);
	
	if(0==noerr)
	{
		DEC_STREAM_PRINTF("%s: vpu reset: handle=0x%X \r\n",__FUNCTION__,handle);
		VPU_DecReset(handle);
	}
	//else
	{
		//close vpu
		ret=VPU_DecClose(handle);
		if (ret!=VPU_DEC_RET_SUCCESS)
		{
			DEC_STREAM_PRINTF("%s: vpu close failure: ret=%d \r\n",__FUNCTION__,ret);
		}	

	}

finish:
	//release mem
	if(0==FreeMemBlock(&decMemInfo))
	{
		DEC_STREAM_PRINTF("%s: free memory failure:  \r\n",__FUNCTION__);
	}

	if(pBitstream)
	{
		free(pBitstream);
	}

	if(pCodecData)
	{
		free(pCodecData);
	}

	//release fb render
	if(fbHandle)
	{
		fb_render_uninit(fbHandle);
	}
	

	//unload
	ret=VPU_DecUnLoad();
	if (ret!=VPU_DEC_RET_SUCCESS)
	{
		DEC_STREAM_PRINTF("%s: vpu unload failure: ret=%d \r\n",__FUNCTION__,ret);
	}
	return 1;
}

int decode_reset()
{
	DEC_STREAM_PRINTF("reset decoder (all instances) \r\n");
	VPU_DecReset(0);
	return 1;
}


