/*
 *  Copyright (c) 2016, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#include "utils.h"

static int nVpuLogLevel=0;
#ifdef ANDROID_BUILD
#include "Log.h"
#define LOG_PRINTF LogOutput
#else
#define LOG_PRINTF printf
#endif

#define VPU_LOG(...)
#define VPU_TRACE
#define VPU_API(...) if(nVpuLogLevel&0x1) {LOG_PRINTF(__VA_ARGS__);}
#define VPU_ERROR(...) if(nVpuLogLevel&0x1) {LOG_PRINTF(__VA_ARGS__);}
#define ASSERT(exp) if((!(exp))&&(nVpuLogLevel&0x1)) {LOG_PRINTF("%s: %d : assert condition !!!\r\n",__FUNCTION__,__LINE__);}

#define vpu_memset	memset
#define vpu_memcpy	memcpy
#define vpu_malloc	malloc
#define vpu_free		free

int VpuDetectAvcc(unsigned char* pCodecData, unsigned int nSize, int * pIsAvcc, int * pNalSizeLength,int* pNalNum)
{
	*pIsAvcc=0;
	if(pCodecData[0]==1){
		int nalsizelen=(pCodecData[4]&0x3)+1;
		/*possible nal size length: 1,2,3,4*/
		VPU_LOG("avcc format is detected, nal_size_length % d \r\n",nalsizelen);
		*pIsAvcc=1;
		*pNalSizeLength=nalsizelen;
		*pNalNum=0; // init 0
	}
	return 1;
}

int VpuDetectHvcc(unsigned char* pCodecData, unsigned int nSize, int * pIsHvcc, int * pNalSizeLength,int* pNalNum)
{
	*pIsHvcc=0;
	if(pCodecData[0]==1){
		int nalsizelen=(pCodecData[21]&0x3)+1;
		/*possible nal size length: 1,2,3,4*/
		VPU_LOG("hvcc format is detected, nal_size_length % d \r\n",nalsizelen);
		*pIsHvcc=1;
		*pNalSizeLength=nalsizelen;
		*pNalNum=0; // init 0
	}
	return 1;
}

int VpuConvertAvccHeader(unsigned char* pCodecData, unsigned int nSize, unsigned char** ppOut, unsigned int * pOutSize)
{
	/*will allocate and return one new buffer, caller is responsible to free it  */
	unsigned char* p=pCodecData;
	unsigned char* pDes;
	unsigned char* pSPS, *pPPS;
	int spsSize,ppsSize;
	int numPPS, outSize=0;
	unsigned char* pTemp=NULL;
	int tempBufSize=0;
	/* [0]: version */
	/* [1]: profile */
	/* [2]: profile compat */
	/* [3]: level */
	/* [4]: 6 bits reserved (111111) + 2 bits nal size length - 1 (11) */
	/* [5]: 3 bits reserved (111) + 5 bits number of sps (00001) */
	/*[6,7]: 16bits: sps_size*/
	/*sps data*/
	/*number of pps*/
	/*16bits: pps_size*/
	/*pps data */
	if(nSize<8){
		goto corrupt_header;
	}
	spsSize=(p[6]<<8)|p[7];
	p+=8;
	pSPS=p;
	p+=spsSize;
	if(p>=pCodecData+nSize){
		goto corrupt_header;
	}
	numPPS=*p++;

	VPU_LOG("spsSize: %d , num of PPS: %d \r\n",spsSize, numPPS);
	tempBufSize=nSize+2*numPPS; //need to allocate more bytes since startcode occupy 4 bytes, while pps size is 2 bytes.
	pTemp=vpu_malloc(tempBufSize); 
	if(pTemp==NULL){
		VPU_ERROR("error: malloc %d bytes fail !\r\n", tempBufSize);
		//do nothing, return
		*ppOut=pCodecData;
		*pOutSize=nSize;
		return 0;
	}
	pDes=pTemp;
	pDes[0]=pDes[1]=pDes[2]=0; /*fill start code*/
	pDes[3]=0x1;
	pDes+=4;
	vpu_memcpy(pDes,pSPS,spsSize); /*fill sps*/
	pDes+=spsSize;
	outSize+=4+spsSize;
	while(numPPS>0){
		if((p+2) > (pCodecData+nSize)){
			goto corrupt_header;
		}
		ppsSize=(p[0]<<8)|p[1];
		p+=2;
		pPPS=p;
		outSize+=4+ppsSize;
		if(outSize>tempBufSize){
			VPU_ERROR("error: convert avcc header overflow ! \r\n");
			//discard left pps data and return
			*ppOut=pTemp;
			*pOutSize=(outSize-4-ppsSize);
			return 0;
		}
		VPU_LOG("fill one pps: %d bytes \r\n", ppsSize);
		pDes[0]=pDes[1]=pDes[2]=0; /*fill start code*/
		pDes[3]=0x1;
		pDes+=4;
		vpu_memcpy(pDes,pPPS,ppsSize); /*fill pps*/
		pDes+=ppsSize;
		numPPS--;
		p+=ppsSize;
	}
	*ppOut=pTemp;
	*pOutSize=outSize;
	return 1;

corrupt_header:
	//do nothing, return
	VPU_ERROR("error: codec data corrupted ! \r\n");
	*ppOut=pCodecData;
	*pOutSize=nSize;
	if(pTemp){
		vpu_free(pTemp);
	}
	return 0;
}

int VpuConvertHvccHeader(unsigned char* pCodecData, unsigned int nSize, unsigned char** ppOut, unsigned int * pOutSize)
{
	/*will allocate and return one new buffer, caller is responsible to free it  */
	unsigned char* p=pCodecData;
	unsigned char* pDes;
	unsigned char* pTemp=NULL;
  int i, j, outSize=0, size, numArray, numNal, length;

	if(nSize<23){
		goto corrupt_header;
	}
	pTemp=vpu_malloc(nSize); 
	if(pTemp==NULL){
		VPU_ERROR("error: malloc %d bytes fail !\r\n", nSize);
		//do nothing, return
		*ppOut=pCodecData;
    *pOutSize=nSize;
    return 0;
  }

	pDes=pTemp;
  p += 22;
  size = nSize - 22;

  numArray = (char)p[0];
  p += 1;
  size -= 1;

  for (i = 0; i < numArray; i++) {
    if (size < 3) {
      goto corrupt_header;
    }
    p += 1;
    size -= 1;

    numNal = (p[0]<<8) | p[1];

    p += 2;
    size -= 2;

    for (j = 0; j < numNal; j++) {
      if (size < 2) {
        goto corrupt_header;
      }
      length = (p[0]<<8) | p[1];

      p += 2;
      size -= 2;

      if (size < length) {
        goto corrupt_header;
      }

      pDes[0]=pDes[1]=pDes[2]=0;
      pDes[3]=0x1; /*fill 4 bytes of startcode*/
      pDes+=4;
      outSize+=4;

      vpu_memcpy(pDes, p, length);
      pDes+=length;
      outSize+=length;

      p += length;
      size -= length;
    }
  }
	*ppOut=pTemp;
	*pOutSize=outSize;
	return 1;

corrupt_header:
	//do nothing, return
	VPU_ERROR("error: codec data corrupted ! \r\n");
	*ppOut=pCodecData;
	*pOutSize=nSize;
	if(pTemp){
		vpu_free(pTemp);
	}
	return 0;
}

static int VpuScanAvccFrameNalNum(unsigned char* pData, unsigned int nSize, int nNalSizeLength)
{
	int leftSize=nSize;
	unsigned char* p=pData;
	unsigned int dataSize;
	int num=0;
	
	while(leftSize>0){
		if(((p+nNalSizeLength) > (pData+nSize)) || (p < pData)){
			goto corrupt_data;
		}
		
		if(nNalSizeLength==4){
			dataSize=(p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
			p+=dataSize+4;
			leftSize-=dataSize+4;
		}
		else if(nNalSizeLength==3){
			dataSize=(p[0]<<16)|(p[1]<<8)|p[2];
			p+=dataSize+3;
			leftSize-=dataSize+3;
		}
		else if(nNalSizeLength==2){
			dataSize=(p[0]<<8) |p[1];
			p+=dataSize+2;
			leftSize-=dataSize+2;
		}
		else {
			dataSize=p[0];
			p+=dataSize+1;
			leftSize-=dataSize+1;
		}
		num++;
	}
	if(leftSize!=0){
		goto corrupt_data;
	}
	VPU_LOG("nal number: %d \r\n",num);
	return num;
	
corrupt_data:
	VPU_ERROR("error: the nal data corrupted ! can't scan the nal number \r\n");
	return 0;
}

int VpuConvertAvccFrame(unsigned char* pData, unsigned int nSize,
    int nNalSizeLength, unsigned char** ppFrm, unsigned int* pSize, int * pNalNum)
{
	/*for nal size length 3 or 4: will change the nalsize with start code,
   * the buffer size won't be changed for nal size length 1 or 2: will
   * re-malloc new frame data	*/
	int leftSize=nSize;
	unsigned char * p=pData;
	unsigned char* pEnd;
	unsigned char* pStart;
	unsigned char* pOldFrm=NULL;
	unsigned int nNewSize=0;
	unsigned char* pNewFrm=NULL;

	ASSERT(NULL!=pData);
	*ppFrm=pData;
	*pSize=nSize;
	pStart=pData;
	pEnd=pData+nSize;
	
	if(nNalSizeLength<3){
		int nNalNum;
		nNalNum=VpuScanAvccFrameNalNum(pData,nSize,nNalSizeLength);
		if(nNalNum==0){
			return 0;
		}
		if(((*pNalNum)!=0) && ((*pNalNum)!=nNalNum)){
			/*if nNalNum not fixed value, we need to consider how to update consumed size in VpuAccumulateConsumedBytes() ? */
			VPU_ERROR("warning: the num of nal not fixed in every frame, previous: %d, new: %d \r\n",*pNalNum,nNalNum);
		}
		*pNalNum=nNalNum;  //update
		nNewSize=nSize+(4-nNalSizeLength)*nNalNum;
		pNewFrm=vpu_malloc(nNewSize);
		if(pNewFrm==NULL){
			VPU_ERROR("malloc failure: %d bytes \r\n",nNewSize);
			return 0;
		}
		pStart=pNewFrm;
		pEnd=pNewFrm+nNewSize;
		p=pNewFrm;
		pOldFrm=pData;
		leftSize=nNewSize;
	}

	while(leftSize>0){
		unsigned int dataSize;

		if(nNalSizeLength==4){
			if(((p+4) > pEnd) || (p < pStart)){
				goto corrupt_data;
			}
			dataSize=(p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
			p[0]=p[1]=p[2]=0;
			p[3]=0x1; /*fill 4 bytes of startcode*/
			p+=dataSize+4;
			leftSize-=dataSize+4;
		}
		else if(nNalSizeLength==3){
			if(((p+3) > pEnd) || (p < pStart)){
				goto corrupt_data;
			}
			dataSize=(p[0]<<16)|(p[1]<<8)|p[2];
			p[0]=p[1]=0;
			p[2]=0x1;  /*fill 3 bytes of startcode*/
			p+=dataSize+3;
			leftSize-=dataSize+3;
		}
		else if(nNalSizeLength==2){
			if(((p+4) > pEnd) || (p < pStart) || (pOldFrm<pData) || ((pOldFrm+2)>(pData+nSize))){
				goto corrupt_data;
			}
			dataSize=(pOldFrm[0]<<8) |pOldFrm[1];
			p[0]=p[1]=p[2]=0;
			p[3]=0x1; /*fill 4 bytes of startcode*/
			p+=4;
			pOldFrm+=2;
			if((dataSize > (unsigned int)(pEnd-p)) || (dataSize>(unsigned int)(pData+nSize-pOldFrm))){
				goto corrupt_data;
			}
			vpu_memcpy(p,pOldFrm,dataSize);
			p+=dataSize;
			pOldFrm+=dataSize;
			leftSize-=dataSize+4;
		}
		else{ /*1 byte*/
			if(((p+4) > pEnd) || (p < pStart) || (pOldFrm<pData) || ((pOldFrm+1)>(pData+nSize))){
				goto corrupt_data;
			}
			dataSize=pOldFrm[0];
			p[0]=p[1]=p[2]=0;
			p[3]=0x1; /*fill 4 bytes of startcode*/
			p+=4;
			pOldFrm+=1;
			if((dataSize > (unsigned int)(pEnd-p)) || (dataSize>(unsigned int)(pData+nSize-pOldFrm))){
				goto corrupt_data;
			}
			vpu_memcpy(p,pOldFrm,dataSize);
			p+=dataSize;
			pOldFrm+=dataSize;
			leftSize-=dataSize+4;
		}
		VPU_LOG("fill one %d bytes of start code for nal data(%d bytes) \r\n", nNalSizeLength,dataSize);
	}
	if(leftSize!=0){
		goto corrupt_data;
	}

	if(nNalSizeLength<3){
		*ppFrm=pNewFrm;
		*pSize=nNewSize;
	}
	return 1;

corrupt_data:
	VPU_ERROR("error: the nal data corrupted ! \r\n");
	if(pNewFrm){
		vpu_free(pNewFrm);
	}
	return 0;
}

