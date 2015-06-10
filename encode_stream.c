/*
 *  Copyright (c) 2010-2014, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

/*
 *	encode_stream.c
 *	this file is the interface between unit test and vpu wrapper
 *
 *	History :
 *	Date	(y.m.d)		Author			Version			Description
 *	2010-12-31		eagle zhou		0.1				Created
 */

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "vpu_wrapper.h"
#include "encode_stream.h"
#include "unistd.h"		//for usleep()


#ifdef ENC_STREAM_DEBUG
#define ENC_STREAM_PRINTF printf
#else
#define ENC_STREAM_PRINTF(...)
#endif

#define MAX_FRAME_NUM	(10)

#define Align(ptr,align)	(((unsigned int)ptr+(align)-1)/(align)*(align))
#define VPU_ENC_MAX_NUM_MEM_REQS	(30)

typedef struct
{
	//virtual mem info
	int nVirtNum;
	unsigned int virtMem[VPU_ENC_MAX_NUM_MEM_REQS];

	//phy mem info
	int nPhyNum;
	unsigned int phyMem_virtAddr[VPU_ENC_MAX_NUM_MEM_REQS];
	unsigned int phyMem_phyAddr[VPU_ENC_MAX_NUM_MEM_REQS];
	unsigned int phyMem_cpuAddr[VPU_ENC_MAX_NUM_MEM_REQS];
	unsigned int phyMem_size[VPU_ENC_MAX_NUM_MEM_REQS];	
}EncMemInfo;

#if 1 // timer related part
#include <sys/time.h>
#define TIME_ENC_ID	0
#define TIME_TOTAL_ID	1
static struct timeval time_beg[2];
static struct timeval time_end[2];
static unsigned long long total_time[2];

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



int EncConvertCodecFormat(int codec, VpuCodStd* pCodec)
{
	switch (codec)
	{
		case 0:
			*pCodec=VPU_V_MPEG4;
			break;
		case 1:
			*pCodec=VPU_V_H263;
			break;			
		case 2:
			*pCodec=VPU_V_AVC;
			break;
		case 3:
			*pCodec=VPU_V_MJPG;
			break;
		default:
			return 0;			
	}
	return 1;
}

int EncConvertMirror(int nMirror, VpuEncMirrorDirection* pMirror)
{
	switch (nMirror)
	{
		case 0:
			*pMirror=VPU_ENC_MIRDIR_NONE;
			break;
		case 1:
			*pMirror=VPU_ENC_MIRDIR_VER;
			break;			
		case 2:
			*pMirror=VPU_ENC_MIRDIR_HOR;
			break;
		case 3:
			*pMirror=VPU_ENC_MIRDIR_HOR_VER;
			break;
		default:
			return 0;			
	}
	return 1;
}


int EncFreeMemBlock(EncMemInfo* pEncMem)
{
	int i;
	VpuMemDesc vpuMem;
	VpuEncRetCode vpuRet;
	int retOk=1;

	//free virtual mem
	for(i=0;i<pEncMem->nVirtNum;i++)
	{
		free((void*)pEncMem->virtMem[i]);
	}

	//free physical mem
	for(i=0;i<pEncMem->nPhyNum;i++)
	{
		vpuMem.nPhyAddr=pEncMem->phyMem_phyAddr[i];
		vpuMem.nVirtAddr=pEncMem->phyMem_virtAddr[i];
		vpuMem.nCpuAddr=pEncMem->phyMem_cpuAddr[i];
		vpuMem.nSize=pEncMem->phyMem_size[i];
		vpuRet=VPU_EncFreeMem(&vpuMem);
		if(vpuRet!=VPU_ENC_RET_SUCCESS)
		{
			ENC_STREAM_PRINTF("%s: free vpu memory failure : ret=%d \r\n",__FUNCTION__,(unsigned int)vpuRet);
			retOk=0;
		}
	}
	
	return retOk;
}


int EncMallocMemBlock(VpuMemInfo* pMemBlock,EncMemInfo* pEncMem)
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
				ENC_STREAM_PRINTF("%s: get virtual memory failure, size=%d \r\n",__FUNCTION__,(unsigned int)size);
				goto failure;
			}		
			pMemBlock->MemSubBlock[i].pVirtAddr=(unsigned char*)Align(ptr,pMemBlock->MemSubBlock[i].nAlignment);

			//record virtual base addr
			pEncMem->virtMem[pEncMem->nVirtNum]=(unsigned int)ptr;
			pEncMem->nVirtNum++;
		}
		else// if(memInfo.MemSubBlock[i].MemType==VPU_MEM_PHY)
		{
			VpuMemDesc vpuMem;
			VpuEncRetCode ret;
			vpuMem.nSize=size;
			ret=VPU_EncGetMem(&vpuMem);
			if(ret!=VPU_ENC_RET_SUCCESS)
			{
				ENC_STREAM_PRINTF("%s: get vpu memory failure, size=%d, ret=%d \r\n",__FUNCTION__,size,ret);
				goto failure;
			}		
			pMemBlock->MemSubBlock[i].pVirtAddr=(unsigned char*)Align(vpuMem.nVirtAddr,pMemBlock->MemSubBlock[i].nAlignment);
			pMemBlock->MemSubBlock[i].pPhyAddr=(unsigned char*)Align(vpuMem.nPhyAddr,pMemBlock->MemSubBlock[i].nAlignment);

			//record physical base addr
			pEncMem->phyMem_phyAddr[pEncMem->nPhyNum]=(unsigned int)vpuMem.nPhyAddr;
			pEncMem->phyMem_virtAddr[pEncMem->nPhyNum]=(unsigned int)vpuMem.nVirtAddr;
			pEncMem->phyMem_cpuAddr[pEncMem->nPhyNum]=(unsigned int)vpuMem.nCpuAddr;
			pEncMem->phyMem_size[pEncMem->nPhyNum]=size;
			pEncMem->nPhyNum++;			
		}
	}	

	return 1;
	
failure:
	EncFreeMemBlock(pEncMem);
	return 0;
	
}

int EncResetBitstream(EncContxt * pEncContxt,int offset)
{
	fseek(pEncContxt->fin,offset,SEEK_SET);
	return 1;
}

int EncGetOneYUVFrameSize(int nColor,int nWidth,int nHeight,int* pYSize, int * pUSize,int*pVSize)
{
	int nFrmSize=0;
	//don't pad the width/height
	switch(nColor)
	{
		case 0:	//4:2:0
			nFrmSize=nWidth*Align(nHeight,16)*3/2; //still need to align input yuv buffer
			*pYSize=nWidth*nHeight;
			*pUSize=nWidth*nHeight/4;
			*pVSize=nWidth*nHeight/4;
			break;
		case 1:	//4:2:2 hor
			nFrmSize=nWidth*Align(nHeight,16)*2;
			*pYSize=nWidth*nHeight;
			*pUSize=nWidth*nHeight/2;
			*pVSize=nWidth*nHeight/2;
			break;
		case 2:	//4:2:2 ver
			nFrmSize=nWidth*Align(nHeight,16)*2;
			*pYSize=nWidth*nHeight;
			*pUSize=nWidth*nHeight/2;
			*pVSize=nWidth*nHeight/2;
			break;
		case 3:	//4:4:4
			nFrmSize=nWidth*Align(nHeight,16)*3;
			*pYSize=nWidth*nHeight;
			*pUSize=nWidth*nHeight;
			*pVSize=nWidth*nHeight;
			break;
		case 4:	//4:0:0
			nFrmSize=nWidth*Align(nHeight,16);
			*pYSize=nWidth*nHeight;
			*pUSize=0;
			*pVSize=0;
			break;
		default:	//4:2:0
			nFrmSize=nWidth*Align(nHeight,16)*3/2;
			*pYSize=nWidth*nHeight;
			*pUSize=nWidth*nHeight/4;
			*pVSize=nWidth*nHeight/4;			
			break;			
	}	
	return nFrmSize;
}

int EncReadOneYUVFrame(EncContxt * pEncContxt, unsigned char* pInput,int length, int nTileAlign,
	int nYSize, int nUSize, int nVSize,unsigned char** ppOutYBot,unsigned char** ppOutCbTop,unsigned char** ppOutCbBot)
{
	int readbytes=0;
	//static int totalReadSize=0;
	if(nTileAlign<=1)
	{
		//ENC_STREAM_PRINTF("read %d bytes \r\n",length);
		//readbytes=fread(pInput,1,length,pEncContxt->fin);
		readbytes=fread(pInput,1,nYSize+nUSize+nVSize,pEncContxt->fin);	//read Y/U/V
	}
	else
	{
		unsigned char* ptr;
		//need to read Y/UV seperately
		if(pEncContxt->nMapType==2)
		{
			int fieldsize=0;
			int size=0;
			fieldsize=nYSize/2;
			ptr=pInput;
			size=fread(ptr,1,fieldsize,pEncContxt->fin);	//read Y top 
			readbytes+=size;
			if(size==fieldsize)
			{
				ptr=(unsigned char*)Align((ptr+fieldsize),nTileAlign);
				*ppOutYBot=ptr;
				size=fread(ptr,1,fieldsize,pEncContxt->fin);	//read Y bot
				readbytes+=size;
				if(size==fieldsize)
				{
					ptr=(unsigned char*)Align((ptr+fieldsize),nTileAlign);
					*ppOutCbTop=ptr;
					fieldsize=(nUSize+nVSize)/2;
					size=fread(ptr,1,fieldsize,pEncContxt->fin);	//read UV top
					readbytes+=size;
					if(size==fieldsize)
					{
						ptr=(unsigned char*)Align((ptr+fieldsize),nTileAlign);
						*ppOutCbBot=ptr;
						size=fread(ptr,1,fieldsize,pEncContxt->fin);	//read UV bot
						readbytes+=size;
					}
				}
			}
			//ENC_STREAM_PRINTF("Ytop: 0x%X, Ybot: 0x%X, Cbtop: 0x%X, Cbbot: 0x%X \r\n",pInput,*ppOutYBot,*ppOutCbTop,*ppOutCbBot);
		}
		else
		{
			readbytes=fread(pInput,1,nYSize,pEncContxt->fin);	//read Y
			if(readbytes==nYSize)
			{
				ptr=(unsigned char*)Align((pInput+nYSize),nTileAlign);
				*ppOutCbTop=ptr;
				readbytes=fread(ptr,1,nUSize+nVSize,pEncContxt->fin);	//read UV
				readbytes+=nYSize;
			}
		}
	}

	//totalReadSize+=readbytes;
	//printf("total read size: %d \r\n",totalReadSize);
	return readbytes;
}


int EncOutputFrameBitstream(EncContxt * pDecContxt,unsigned char* pFrame,int nLength)
{
	//output file
	if(pDecContxt->fout)
	{
		fwrite(pFrame,1,nLength,pDecContxt->fout);
	}

	// call registered callback functions
	if(pDecContxt->pfOneFrameOk)
	{
		pDecContxt->pfOneFrameOk(pDecContxt->pApp,pFrame,nLength);
	}
	return 1;	
}

int EncOutFrameBufCreateRegisterFrame(VpuCodStd eFormat,int nInColor,
	VpuFrameBuffer* pOutRegisterFrame,int nInCnt,int nWidth,int nHeight,
	EncMemInfo* pOutEncMemInfo, int nInRot,int* pOutSrcStride,int nInAlign,int nInMapType)
{
	int i;
	VpuEncRetCode ret;	
	int yStride;
	int uvStride;	
	int ySize;
	int uvSize;
	int mvSize;	
	VpuMemDesc vpuMem;
	unsigned char* ptr;
	unsigned char* ptrVirt;
	int nPadW;
	int nPadH;
	int multifactor=1;

	nPadW=Align(nWidth,16);
	nPadH=Align(nHeight,16);
	if((nInRot==90)||(nInRot==270))
	{
		yStride=nPadH;
		ySize=yStride*nPadW;	
	}
	else
	{
		yStride=nPadW;
		ySize=yStride*nPadH;
	}
	if(VPU_V_MJPG==eFormat)
	{
		switch(nInColor)
		{
			case 0:	//4:2:0
				ENC_STREAM_PRINTF("MJPG: 4:2:0 \r\n");
				uvStride=yStride/2;
				uvSize=ySize/4;
				mvSize=uvSize;
				break;
			case 1:	//4:2:2 hor
				ENC_STREAM_PRINTF("MJPG: 4:2:2 hor \r\n");
				uvStride=yStride/2;
				uvSize=ySize/2;
				mvSize=uvSize;
				break;
			case 2:	//4:2:2 ver
				ENC_STREAM_PRINTF("MJPG: 4:2:2 ver \r\n");
				uvStride=yStride;
				uvSize=ySize/2;
				mvSize=uvSize;
				break;
			case 3:	//4:4:4
				ENC_STREAM_PRINTF("MJPG: 4:4:4 \r\n");
				uvStride=yStride;
				uvSize=ySize;
				mvSize=uvSize;
				break;
			case 4:	//4:0:0
				ENC_STREAM_PRINTF("MJPG: 4:0:0 \r\n");
				uvStride=0;
				uvSize=0;
				mvSize=uvSize;
				break;
			default:	//4:2:0
				ENC_STREAM_PRINTF("unknown color format: %d \r\n",nInColor);
				uvStride=yStride/2;
				uvSize=ySize/4;
				mvSize=uvSize;
				break;			
		}
	}	
	else
	{
		//4:2:0 for all video
		uvStride=yStride/2;
		uvSize=ySize/4;
		mvSize=uvSize;	//1 set 0 ??
	}

	if(nInMapType==2)
	{
		//only consider Y since interleave must be enabled
		multifactor=2;	//for field, we need to consider alignment for top and bot
	}

	//we need to align the Y/Cb/Cr address
	if(nInAlign>1)
	{
		ySize=Align(ySize,multifactor*nInAlign);
		uvSize=Align(uvSize,nInAlign);
	}
	
	for(i=0;i<nInCnt;i++)
	{
		vpuMem.nSize=ySize+uvSize*2+mvSize+nInAlign;
		ret=VPU_EncGetMem(&vpuMem);
		if(VPU_ENC_RET_SUCCESS!=ret)
		{
			ENC_STREAM_PRINTF("%s: vpu malloc frame buf failure: ret=0x%X \r\n",__FUNCTION__,ret);	
			return -1;//OMX_ErrorInsufficientResources;
		}

		ptr=(unsigned char*)vpuMem.nPhyAddr;
		ptrVirt=(unsigned char*)vpuMem.nVirtAddr;

		/*align the base address*/
		if(nInAlign>1)
		{
			ptr=(unsigned char*)Align(ptr,nInAlign);
			ptrVirt=(unsigned char*)Align(ptrVirt,nInAlign);
		}
		
		/* fill stride info */
		pOutRegisterFrame[i].nStrideY=yStride;
		pOutRegisterFrame[i].nStrideC=uvStride;

		/* fill phy addr*/
		pOutRegisterFrame[i].pbufY=ptr;
		pOutRegisterFrame[i].pbufCb=ptr+ySize;
		pOutRegisterFrame[i].pbufCr=ptr+ySize+uvSize;
		pOutRegisterFrame[i].pbufMvCol=ptr+ySize+uvSize*2;

		/* fill virt addr */
		pOutRegisterFrame[i].pbufVirtY=ptrVirt;
		pOutRegisterFrame[i].pbufVirtCb=ptrVirt+ySize;
		pOutRegisterFrame[i].pbufVirtCr=ptrVirt+ySize+uvSize;
		pOutRegisterFrame[i].pbufVirtMvCol=ptrVirt+ySize+uvSize*2;	

		/* fill bottom address for field tile*/
		if(nInMapType==2)
		{
			pOutRegisterFrame[i].pbufY_tilebot=pOutRegisterFrame[i].pbufY+ySize/2;
			pOutRegisterFrame[i].pbufCb_tilebot=pOutRegisterFrame[i].pbufCr;
			pOutRegisterFrame[i].pbufVirtY_tilebot=pOutRegisterFrame[i].pbufVirtY+ySize/2;
			pOutRegisterFrame[i].pbufVirtCb_tilebot=pOutRegisterFrame[i].pbufVirtCr;
		}
		else
		{
			pOutRegisterFrame[i].pbufY_tilebot=0;
			pOutRegisterFrame[i].pbufCb_tilebot=0;
			pOutRegisterFrame[i].pbufVirtY_tilebot=0;
			pOutRegisterFrame[i].pbufVirtCb_tilebot=0;
		}

		//record memory info for release
		pOutEncMemInfo->phyMem_phyAddr[pOutEncMemInfo->nPhyNum]=vpuMem.nPhyAddr;
		pOutEncMemInfo->phyMem_virtAddr[pOutEncMemInfo->nPhyNum]=vpuMem.nVirtAddr;
		pOutEncMemInfo->phyMem_cpuAddr[pOutEncMemInfo->nPhyNum]=vpuMem.nCpuAddr;
		pOutEncMemInfo->phyMem_size[pOutEncMemInfo->nPhyNum]=vpuMem.nSize;
		pOutEncMemInfo->nPhyNum++;

	}

	*pOutSrcStride=nWidth;//nPadW;
	return i;
}

int EncSetMoreOpenPara(VpuEncOpenParam* pEncOpenMore,EncContxt * pEncContxt)
{
	pEncOpenMore->nInitialDelay=pEncContxt->nInitialDelay;//0;
	pEncOpenMore->nVbvBufferSize=pEncContxt->nVbvBufSize;//0;
	
	pEncOpenMore->sliceMode.sliceMode = pEncContxt->nSliceMode;//0;	/* 0: 1 slice per picture; 1: Multiple slices per picture */
	pEncOpenMore->sliceMode.sliceSizeMode = pEncContxt->nSliceSizeMode;//0; /* 0: silceSize defined by bits; 1: sliceSize defined by MB number*/
	pEncOpenMore->sliceMode.sliceSize = pEncContxt->nSliceSize;//4000;  /* Size of a slice in bits or MB numbers */

	//sEncOpenParam.enableAutoSkip = 1;
	pEncOpenMore->nIntraRefresh = pEncContxt->nIntraRefresh;//0;
	pEncOpenMore->nRcIntraQp = pEncContxt->nRcIntraQp;
	pEncOpenMore->nUserQpMax = pEncContxt->nUserQPMax;//0;
	pEncOpenMore->nUserQpMin = pEncContxt->nUserQPMin;//0;
	pEncOpenMore->nUserQpMinEnable = pEncContxt->nUserQPMinEnable;//0;
	pEncOpenMore->nUserQpMaxEnable = pEncContxt->nUserQPMaxEnable;//0;

	pEncOpenMore->nUserGamma = pEncContxt->nUserGamma;         /*  (0*32768 <= gamma <= 1*32768) */
	pEncOpenMore->nRcIntervalMode=pEncContxt->nRcIntervalMode; //1;        /* 0:normal, 1:frame_level, 2:slice_level, 3: user defined Mb_level */
	pEncOpenMore->nMbInterval = pEncContxt->nMBInterval;//0;
	pEncOpenMore->nAvcIntra16x16OnlyModeEnable = pEncContxt->nAvc_Intra16x16Only;

	switch(pEncOpenMore->eFormat)
	{
		case VPU_V_MPEG4:
			pEncOpenMore->VpuEncStdParam.mp4Param.mp4_dataPartitionEnable =pEncContxt->nMp4_dataPartitionEnable;//0;
			pEncOpenMore->VpuEncStdParam.mp4Param.mp4_reversibleVlcEnable =pEncContxt->nMp4_reversibleVlcEnable;//0;
			pEncOpenMore->VpuEncStdParam.mp4Param.mp4_intraDcVlcThr = pEncContxt->nMp4_intraDcVlcThr;//0;
			pEncOpenMore->VpuEncStdParam.mp4Param.mp4_hecEnable = pEncContxt->nMp4_hecEnable;//0;
			pEncOpenMore->VpuEncStdParam.mp4Param.mp4_verid = pEncContxt->nMp4_verid;//2;
			break;
		case VPU_V_H263:
			pEncOpenMore->VpuEncStdParam.h263Param.h263_annexJEnable = pEncContxt->nH263_annexJEnable;//1;
			pEncOpenMore->VpuEncStdParam.h263Param.h263_annexKEnable = pEncContxt->nH263_annexKEnable;//0;
			pEncOpenMore->VpuEncStdParam.h263Param.h263_annexTEnable = pEncContxt->nH263_annexTEnable;//0;
			break;
		case VPU_V_AVC:
			pEncOpenMore->VpuEncStdParam.avcParam.avc_constrainedIntraPredFlag = pEncContxt->nAvc_constrainedIntraPredFlag;
			pEncOpenMore->VpuEncStdParam.avcParam.avc_disableDeblk = pEncContxt->nAvc_disableDeblk;
			pEncOpenMore->VpuEncStdParam.avcParam.avc_deblkFilterOffsetAlpha = pEncContxt->nAvc_deblkFilterOffsetAlpha;
			pEncOpenMore->VpuEncStdParam.avcParam.avc_deblkFilterOffsetBeta = pEncContxt->nAvc_deblkFilterOffsetBeta;
			pEncOpenMore->VpuEncStdParam.avcParam.avc_chromaQpOffset = pEncContxt->nAvc_chromaQpOffset;
			pEncOpenMore->VpuEncStdParam.avcParam.avc_audEnable = pEncContxt->nAvc_audEnable;//0;
			pEncOpenMore->VpuEncStdParam.avcParam.avc_fmoEnable = pEncContxt->nAvc_fmoEnable;//0;
			pEncOpenMore->VpuEncStdParam.avcParam.avc_fmoType = pEncContxt->nAvc_fmoType;//0;
			pEncOpenMore->VpuEncStdParam.avcParam.avc_fmoSliceNum = pEncContxt->nAvc_fmoSliceNum;//1;
			pEncOpenMore->VpuEncStdParam.avcParam.avc_fmoSliceSaveBufSize = 32; /* FMO_SLICE_SAVE_BUF_SIZE */			
			break;
		//case VPU_V_MJPG:
		default:
			//unknow format ?
			//return VPU_ENC_RET_INVALID_PARAM;
			break;
	}	
	return 1;
}

int EncodeLoop(VpuEncHandle handle,EncContxt * pEncContxt, 
	unsigned char* pInputPhy,unsigned char* pOutputPhy,unsigned char* pInputVirt,unsigned char* pOutputVirt,
	int nInputBufSize,int nOuputBufSize, int nTileAlign,int nYSize, int nUSize, int nVSize)
{
	VpuEncRetCode ret;
	int err=0;
	int nEncodedFrameNum=0;
	int nNeedInput=1;
	int nValidOutSize=0;
	int nReadBytes;
	VpuEncEncParam sEncEncParam;
	int repeatNum=pEncContxt->nRepeatNum;
	VpuFrameBuffer sFrameBuf;
	unsigned char* pCbTopVir=NULL;	//for tile
	unsigned char* pYBotVir=NULL;	//for field tile
	unsigned char* pCbBotVir=NULL;	//for field tile
	unsigned long long totalTime=0;

	time_init(TIME_ENC_ID);
	time_init(TIME_TOTAL_ID);
	time_start(TIME_TOTAL_ID);

RepeatEncode:

	//reset init value
	err=0;
	//nEncodedFrameNum=0;
	nNeedInput=1;
	
	while(nEncodedFrameNum < pEncContxt->nMaxNum)
	{
		//read input YUV data
		if(1==nNeedInput)
		{
			nReadBytes=EncReadOneYUVFrame(pEncContxt, pInputVirt,nInputBufSize,nTileAlign,nYSize,nUSize,nVSize,&pYBotVir,&pCbTopVir,&pCbBotVir);
			if((nYSize+nUSize+nVSize)!=nReadBytes) //if(nInputBufSize!=nReadBytes)
			{
				//eos
				break;
			}
		}

		
		//clear 0 firstly
		memset(&sEncEncParam,0,sizeof(VpuEncEncParam));
		EncConvertCodecFormat(pEncContxt->nCodec,&sEncEncParam.eFormat);
		sEncEncParam.nPicWidth=pEncContxt->nPicWidth;
		sEncEncParam.nPicHeight=pEncContxt->nPicHeight;	
		sEncEncParam.nFrameRate=pEncContxt->nFrameRate;
		sEncEncParam.nQuantParam=pEncContxt->nQuantParam;	
		sEncEncParam.nInPhyInput=(unsigned int)pInputPhy;
		sEncEncParam.nInVirtInput=(unsigned int)pInputVirt;
		sEncEncParam.nInInputSize=nInputBufSize;
		sEncEncParam.nInPhyOutput=(unsigned int)pOutputPhy;
		sEncEncParam.nInVirtOutput=(unsigned int)pOutputVirt;
		sEncEncParam.nInOutputBufLen=nOuputBufSize;

		if(pEncContxt->nGOPSize==0){
			if(nEncodedFrameNum==0){
				//only insert one IDR for the whole clip
				sEncEncParam.nForceIPicture = 1;
			}
			else{
				sEncEncParam.nForceIPicture = 0;
			}
		}
		else {
			if(((nEncodedFrameNum%pEncContxt->nGOPSize)==0) && (VPU_V_AVC==sEncEncParam.eFormat)){
				sEncEncParam.nForceIPicture = 1;
				//ENC_STREAM_PRINTF("Force IDR \r\n");
			}
			else{
				sEncEncParam.nForceIPicture = 0;
			}
		}
		sEncEncParam.nSkipPicture = 0;
		sEncEncParam.nEnableAutoSkip = pEncContxt->nEnableAutoSkip;//1;

		if(pEncContxt->pfOneFrameBeg)
		{
			pEncContxt->pfOneFrameBeg(pEncContxt->pApp);
		}
#if 1
		if(nTileAlign>1)
		{
			//the input buf isn't continuous, need to set Y/Cb/Cr address through sEncEncParam.pInFrame
			memset(&sFrameBuf,0,sizeof(VpuFrameBuffer));
			sEncEncParam.pInFrame=&sFrameBuf;
			sFrameBuf.pbufY=(unsigned char*)sEncEncParam.nInPhyInput;			
			sFrameBuf.pbufY_tilebot=(unsigned char*)(sEncEncParam.nInPhyInput+((unsigned int)pYBotVir-sEncEncParam.nInVirtInput));
			//sFrameBuf.pbufCb=(unsigned char*)Align((sFrameBuf.pbufY+nYSize),nTileAlign);
			sFrameBuf.pbufCb=(unsigned char*)(sEncEncParam.nInPhyInput+((unsigned int)pCbTopVir-sEncEncParam.nInVirtInput));;
			sFrameBuf.pbufCb_tilebot=(unsigned char*)(sEncEncParam.nInPhyInput+((unsigned int)pCbBotVir-sEncEncParam.nInVirtInput));;
			sFrameBuf.pbufCr=NULL;	//no meaning
			sFrameBuf.nStrideY=sEncEncParam.nPicWidth;
			switch(pEncContxt->nColor)
			{
				case 0:	//4:2:0
					sFrameBuf.nStrideC=sFrameBuf.nStrideY/2;
					break;
				case 1:	//4:2:2 hor
					sFrameBuf.nStrideC=sFrameBuf.nStrideY/2;
					break;
				case 2:	//4:2:2 ver
					sFrameBuf.nStrideC=sFrameBuf.nStrideY;
					break;
				case 3:	//4:4:4
					sFrameBuf.nStrideC=sFrameBuf.nStrideY;
					break;
				case 4:	//4:0:0
					sFrameBuf.nStrideC=0;
					break;
				default:
					sFrameBuf.nStrideC=sFrameBuf.nStrideY/2	;
					break;
			}			
			//ENC_STREAM_PRINTF("phy input addr: Ytop: 0x%X, Ybot: 0x%X, Cbtop: 0x%X, Cbbot: 0x%X \r\n",sFrameBuf.pbufY,sFrameBuf.pbufY_tilebot,sFrameBuf.pbufCb,sFrameBuf.pbufVirtCb_tilebot);	
		}
		else	
		{
			//the input buf is continuous, vpu will compute Y/Cb/Cr address automatically
			sEncEncParam.pInFrame=NULL;
		}
#endif
		
		//encode frame
		time_start(TIME_ENC_ID);		
		ret=VPU_EncEncodeFrame(handle, &sEncEncParam);
		time_stop(TIME_ENC_ID);

		if(VPU_ENC_RET_SUCCESS!=ret)
		{
			if(VPU_ENC_RET_FAILURE_TIMEOUT==ret)
			{
				ENC_STREAM_PRINTF("%s: encode frame timeout \r\n",__FUNCTION__);
				VPU_EncReset(handle);
			}
			err=1;
			goto Exit;
		}

		//check input
		if(sEncEncParam.eOutRetCode & VPU_ENC_INPUT_USED)
		{
			nNeedInput=1;
		}
		else
		{
			nNeedInput=0;
			ENC_STREAM_PRINTF("%s: encode frame : input is not used \r\n",__FUNCTION__);
		}

		//check output
		if((sEncEncParam.eOutRetCode & VPU_ENC_OUTPUT_DIS)||(sEncEncParam.eOutRetCode & VPU_ENC_OUTPUT_SEQHEADER))
		{
			//has output or seqheader
			if(pEncContxt->pfOneFrameEnd)
			{
				pEncContxt->pfOneFrameEnd(pEncContxt->pApp);
			}

			nValidOutSize=sEncEncParam.nOutOutputSize;
			EncOutputFrameBitstream(pEncContxt, pOutputVirt, nValidOutSize);
			if(sEncEncParam.eOutRetCode & VPU_ENC_OUTPUT_DIS){
				nEncodedFrameNum++;
			}
		}
		else
		{
			//no output
		}	
	}

	if(repeatNum>0)
	{
		repeatNum--;
		// re-encode stream
		EncResetBitstream(pEncContxt,0);
		ENC_STREAM_PRINTF("repeat encode: %d \r\n", repeatNum);
		goto RepeatEncode;
	}
	
Exit:
	//set output info for user
	time_stop(TIME_TOTAL_ID);	
	pEncContxt->nFrameNum=nEncodedFrameNum;
	pEncContxt->nErr=err;
	totalTime=time_report(TIME_ENC_ID);
	pEncContxt->nEncFps=(unsigned long long)1000000*nEncodedFrameNum/totalTime;
	totalTime=time_report(TIME_TOTAL_ID);
	pEncContxt->nTotalFps=(unsigned long long)1000000*nEncodedFrameNum/totalTime;	
	return ((err==0)?1:0);
}


int encode_stream(EncContxt * pEncContxt)
{
	VpuEncRetCode ret;
	VpuVersionInfo ver;	
	VpuWrapperVersionInfo w_ver;
	VpuEncHandle handle;	
	VpuMemInfo sMemInfo;	
	EncMemInfo sEncMemInfo;	

	VpuEncOpenParam sEncOpenParam;
	VpuEncOpenParamSimp sEncOpenParamSimp;
	
	VpuEncInitInfo sEncInitInfo;
	VpuFrameBuffer sFrameBuf[MAX_FRAME_NUM];
	unsigned char* pInputPhy;
	unsigned char* pInputVirt;
	unsigned char* pOutputPhy;
	unsigned char* pOutputVirt;	
	int nBufNum;
	int nSrcStride;
	int nInputBufSize;
	int nOutputBufSize;
	int noerr=1;	
	int nTileAlign=1;	//for tile input
	int nYSize,nUSize,nVSize;	//for tile input 
	VpuCodStd eFormat;

	//init 
	memset(&sMemInfo,0,sizeof(VpuMemInfo));
	memset(&sEncMemInfo,0,sizeof(EncMemInfo));
	memset(&sFrameBuf,0,sizeof(VpuFrameBuffer)*MAX_FRAME_NUM);
	
	//load vpu
	ret=VPU_EncLoad();
	if (ret!=VPU_ENC_RET_SUCCESS)
	{
		ENC_STREAM_PRINTF("%s: vpu load failure: ret=0x%X \r\n",__FUNCTION__,ret);
		return 0;
	}

	//version info
	ret=VPU_EncGetVersionInfo(&ver);
	if (ret!=VPU_ENC_RET_SUCCESS)
	{
		ENC_STREAM_PRINTF("%s: vpu get version failure: ret=0x%X \r\n",__FUNCTION__,ret);
		VPU_EncUnLoad();
		return 0;
	}
	ENC_STREAM_PRINTF("vpu lib version : major.minor.rel=%d.%d.%d \r\n",ver.nLibMajor,ver.nLibMinor,ver.nLibRelease);
	ENC_STREAM_PRINTF("vpu fw version : major.minor.rel_rcode=%d.%d.%d_r%d \r\n",ver.nFwMajor,ver.nFwMinor,ver.nFwRelease,ver.nFwCode);

	//wrapper version info
	ret=VPU_EncGetWrapperVersionInfo(&w_ver);
	if (ret!=VPU_ENC_RET_SUCCESS)
	{
		ENC_STREAM_PRINTF("%s: vpu get wrapper version failure: ret=%d \r\n",__FUNCTION__,ret);
		VPU_EncUnLoad();
		return 0;
	}
	ENC_STREAM_PRINTF("vpu wrapper version : major.minor.rel=%d.%d.%d: %s \r\n",w_ver.nMajor,w_ver.nMinor,w_ver.nRelease,w_ver.pBinary);

	//query memory
	ret=VPU_EncQueryMem(&sMemInfo);
	if (ret!=VPU_ENC_RET_SUCCESS)
	{
		ENC_STREAM_PRINTF("%s: vpu query memory failure: ret=0x%X \r\n",__FUNCTION__,ret);
		VPU_EncUnLoad();
		return 0;
	}
	
	//malloc memory for vpu 
	if(0==EncMallocMemBlock(&sMemInfo,&sEncMemInfo))
	{
		ENC_STREAM_PRINTF("%s: malloc memory failure: \r\n",__FUNCTION__);
		VPU_EncUnLoad();
		return 0;
	}

	if(0==EncConvertCodecFormat(pEncContxt->nCodec, &eFormat)){
		ENC_STREAM_PRINTF("%s: unsupported codec format: id=%d \r\n",__FUNCTION__,pEncContxt->nCodec);
		VPU_EncUnLoad();
		return 0;
	}

	if(pEncContxt->nSimpleApi){
		ENC_STREAM_PRINTF("using VPU_EncOpenSimp Interface \r\n");
		memset(&sEncOpenParamSimp,0,sizeof(VpuEncOpenParamSimp));

		if(0==EncConvertMirror(pEncContxt->nMirror, &sEncOpenParamSimp.sMirror)){
			ENC_STREAM_PRINTF("%s: unsupported mirror method: id=%d \r\n",__FUNCTION__,pEncContxt->nMirror);
			VPU_EncUnLoad();
			return 0;		
		}
		sEncOpenParamSimp.eFormat=eFormat;
		sEncOpenParamSimp.nPicWidth= pEncContxt->nPicWidth;
		sEncOpenParamSimp.nPicHeight=pEncContxt->nPicHeight;
		sEncOpenParamSimp.nRotAngle=pEncContxt->nRotAngle;
		sEncOpenParamSimp.nFrameRate=pEncContxt->nFrameRate;
		sEncOpenParamSimp.nBitRate=pEncContxt->nBitRate;
		sEncOpenParamSimp.nGOPSize=pEncContxt->nGOPSize;
		sEncOpenParamSimp.nIntraRefresh=pEncContxt->nIntraRefresh;
		sEncOpenParamSimp.nChromaInterleave=pEncContxt->nChromaInterleave;
		//open vpu			
		ret=VPU_EncOpenSimp(&handle, &sMemInfo,&sEncOpenParamSimp);
	}
	else{
		ENC_STREAM_PRINTF("using VPU_EncOpen Interface \r\n");
		//clear 0 firstly
		memset(&sEncOpenParam,0,sizeof(VpuEncOpenParam));

		if(0==EncConvertMirror(pEncContxt->nMirror, &sEncOpenParam.sMirror))
		{
			ENC_STREAM_PRINTF("%s: unsupported mirror method: id=%d \r\n",__FUNCTION__,pEncContxt->nMirror);
			VPU_EncUnLoad();
			return 0;		
		}
		sEncOpenParam.eFormat=eFormat;
		sEncOpenParam.nPicWidth= pEncContxt->nPicWidth;
		sEncOpenParam.nPicHeight=pEncContxt->nPicHeight;
		sEncOpenParam.nRotAngle=pEncContxt->nRotAngle;
		sEncOpenParam.nFrameRate=pEncContxt->nFrameRate;
		sEncOpenParam.nBitRate=pEncContxt->nBitRate;
		sEncOpenParam.nGOPSize=pEncContxt->nGOPSize;
		sEncOpenParam.nChromaInterleave=pEncContxt->nChromaInterleave;

		sEncOpenParam.nMapType=pEncContxt->nMapType;
		sEncOpenParam.nLinear2TiledEnable=pEncContxt->nLinear2TiledEnable;
		sEncOpenParam.eColorFormat=pEncContxt->nColor;

		//open vpu			
		EncSetMoreOpenPara(&sEncOpenParam, pEncContxt);
		ret=VPU_EncOpen(&handle, &sMemInfo,&sEncOpenParam);
	}
	if (ret!=VPU_ENC_RET_SUCCESS)
	{
		ENC_STREAM_PRINTF("%s: vpu open failure: ret=0x%X \r\n",__FUNCTION__,ret);
		VPU_EncUnLoad();
		return 0;
	}			

	//set default config
	ret=VPU_EncConfig(handle, VPU_ENC_CONF_NONE, NULL);
	if(VPU_ENC_RET_SUCCESS!=ret)
	{
		ENC_STREAM_PRINTF("%s: vpu config failure: config=0x%X, ret=%d \r\n",__FUNCTION__,(unsigned int)VPU_ENC_CONF_NONE,ret);
		VPU_EncClose(handle);
		VPU_EncUnLoad();
		return 0;
	}	

	//set intra refresh mode
	if(pEncContxt->nIntraRefreshMode){
		VPU_EncConfig(handle, VPU_ENC_CONF_INTRA_REFRESH_MODE, &pEncContxt->nIntraRefreshMode);
	}

	//get initinfo
	ret=VPU_EncGetInitialInfo(handle,&sEncInitInfo);
	if(VPU_ENC_RET_SUCCESS!=ret)
	{
		ENC_STREAM_PRINTF("%s: init vpu failure \r\n",__FUNCTION__);
		VPU_EncClose(handle);
		VPU_EncUnLoad();
		return 0;
	}

	nBufNum=sEncInitInfo.nMinFrameBufferCount;
	ENC_STREAM_PRINTF("Init OK: min buffer cnt: %d, alignment: %d \r\n",sEncInitInfo.nMinFrameBufferCount,sEncInitInfo.nAddressAlignment);
	//fill frameBuf[]
	if(-1==EncOutFrameBufCreateRegisterFrame(eFormat,pEncContxt->nColor,sFrameBuf, nBufNum,pEncContxt->nPicWidth, pEncContxt->nPicHeight, &sEncMemInfo,pEncContxt->nRotAngle,&nSrcStride,sEncInitInfo.nAddressAlignment,pEncContxt->nMapType))
	{
		ENC_STREAM_PRINTF("%s: allocate vpu frame buffer failure \r\n",__FUNCTION__);
		VPU_EncClose(handle);
		VPU_EncUnLoad();
		return 0;		
	}
	
	//register frame buffs
	ret=VPU_EncRegisterFrameBuffer(handle, sFrameBuf, nBufNum,nSrcStride);
	if(VPU_ENC_RET_SUCCESS!=ret)
	{
		ENC_STREAM_PRINTF("%s: vpu register frame failure: ret=0x%X \r\n",__FUNCTION__,ret);	
		VPU_EncClose(handle);
		VPU_EncUnLoad();		
		return 0;
	}	

	//allocate one input and one output buffer
	//nInputBufSize=pEncContxt->nPicWidth*pEncContxt->nPicHeight*3/2;	// YUV
	nInputBufSize=EncGetOneYUVFrameSize(pEncContxt->nColor, pEncContxt->nPicWidth,pEncContxt->nPicHeight,&nYSize,&nUSize,&nVSize);
	nInputBufSize+=sEncInitInfo.nAddressAlignment*2;	//consider alignment for Y/U/V addr, set big enough
	if(pEncContxt->nMapType==2)
	{
		nInputBufSize+=sEncInitInfo.nAddressAlignment*2; //set additional align for bottom field
	}
	nOutputBufSize=nInputBufSize;	//set big enough 
	memset(&sMemInfo,0,sizeof(VpuMemInfo));
	sMemInfo.nSubBlockNum=2;
	sMemInfo.MemSubBlock[0].MemType=VPU_MEM_PHY;
	sMemInfo.MemSubBlock[0].nAlignment=sEncInitInfo.nAddressAlignment;//8;
	sMemInfo.MemSubBlock[0].nSize=nInputBufSize;
	sMemInfo.MemSubBlock[1].MemType=VPU_MEM_PHY;
	sMemInfo.MemSubBlock[1].nAlignment=sEncInitInfo.nAddressAlignment;//8;
	sMemInfo.MemSubBlock[1].nSize=nOutputBufSize;	
	if(0==EncMallocMemBlock(&sMemInfo,&sEncMemInfo))
	{
		ENC_STREAM_PRINTF("%s: malloc memory failure: \r\n",__FUNCTION__);
		VPU_EncUnLoad();
		return 0;
	}	
	//set input/output address
	pInputPhy=sMemInfo.MemSubBlock[0].pPhyAddr;
	pInputVirt=sMemInfo.MemSubBlock[0].pVirtAddr;
	pOutputPhy=sMemInfo.MemSubBlock[1].pPhyAddr;
	pOutputVirt=sMemInfo.MemSubBlock[1].pVirtAddr;

	if((pEncContxt->nMapType!=0)&&(pEncContxt->nLinear2TiledEnable==0))
	{
		//tile input
		nTileAlign=sEncInitInfo.nAddressAlignment;
	}
	else
	{
		//linear input
		nTileAlign=1;
	}
	//encoder loop
	noerr=EncodeLoop(handle,pEncContxt,pInputPhy,pOutputPhy,pInputVirt,pOutputVirt,nInputBufSize,nOutputBufSize,nTileAlign,nYSize,nUSize,nVSize);

	if(0==noerr)
	{
		ENC_STREAM_PRINTF("%s: vpu reset: handle=0x%X \r\n",__FUNCTION__,handle);
		VPU_EncReset(handle);
	}

	//close vpu
	ret=VPU_EncClose(handle);
	if (ret!=VPU_ENC_RET_SUCCESS)
	{
		ENC_STREAM_PRINTF("%s: vpu close failure: ret=%d \r\n",__FUNCTION__,ret);
		noerr=0;
	}	

	//unload
	ret=VPU_EncUnLoad();
	if (ret!=VPU_ENC_RET_SUCCESS)
	{
		ENC_STREAM_PRINTF("%s: vpu unload failure: ret=%d \r\n",__FUNCTION__,ret);
		noerr=0;
	}

	//release mem
	if(0==EncFreeMemBlock(&sEncMemInfo))
	{
		ENC_STREAM_PRINTF("%s: free memory failure:  \r\n",__FUNCTION__);
		noerr=0;
	}

	return noerr;
}

//int encode_reset()
//{
//	ENC_STREAM_PRINTF("reset encoder : not supported  \r\n");
//	VPU_EncReset(0);
//	return 1;
//}


