/*
 *  Copyright (c) 2016, Freescale Semiconductor Inc.,
 *	Copyright 2018-2020 NXP
 *
 *  The following programs are the sole property of NXP,
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

#define VC1_IS_NOT_NAL(id)		(( id & 0x00FFFFFF) != 0x00010000)
#define RCV_HEADER_LEN			24
#define RCV_CODEC_VERSION		(0x5 << 24) //FOURCC_WMV3_WMV
#define RCV_NUM_FRAMES			0xFFFFFF
#define RCV_SET_HDR_EXT		0x80000000

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
    unsigned char* pCodecPri,unsigned int nFrameSize,int nWidth,int nHeight,int* pNoError)
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
    //it is reasonable to return error immediately since only one sequence header inserted in whole rcv clip
    *pNoError=0;
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

int VpuFindAVCStartCode(unsigned char* pData, int nSize,unsigned char** ppStart)
{
#define AVC_START_CODE 0x00000001
    unsigned int startcode=0xFFFFFFFF;
    unsigned char* p=pData;
    unsigned char* pEnd=pData+nSize;
    while(p<pEnd){
        startcode=(startcode<<8)|p[0];
        if(AVC_START_CODE==startcode){
            break;
        }
        p++;
    }
    if(p>=pEnd){
        VPU_LOG("not find valid start code \r\n");
        *ppStart=NULL;
        return 0;
    }
    *ppStart=p-3;
    return 1;
}

int VpuConvertToAvccData(unsigned char* pData, int nSize)
{
  /*we will replace the 'start code'(00000001) with 'nal size'(4bytes), and the buffer length no changed*/
  unsigned char* pPre=pData;
  int length=nSize;
  int nalSize=0;
  int outSize=0;
  int i=0;
  unsigned char* pNext=NULL;
  VPU_LOG("convert to avcc data: %d bytes \r\n",nSize);
  if(0==VpuFindAVCStartCode(pData,length,&pPre)){
      goto finish;
  }
  pPre+=4; //skip 4 bytes of startcode
  length-=(pPre-pData);
  while(1){
    VpuFindAVCStartCode(pPre,length,&pNext);
    if(pNext){
        nalSize=pNext-pPre;
    }
    else{
        nalSize=length; //last nal
    }
    pPre[-4]=(nalSize>>24)&0xFF;
    pPre[-3]=(nalSize>>16)&0xFF;
    pPre[-2]=(nalSize>>8)&0xFF;
    pPre[-1]=(nalSize)&0xFF;
    VPU_LOG("[%d]: fill one nal size: %d \r\n",i,nalSize);
    i++;
    outSize+=nalSize+4;
    if(pNext==NULL){
        goto finish;
    }
    pNext+=4;
    length-=(pNext-pPre);
    pPre=pNext;
  }
finish:
  if(outSize!=nSize){
      VPU_ERROR("error: size not matched in convert progress of avcc !\r\n");
  }
  if(i==0){
      VPU_ERROR("error: no find any nal start code in convert progress of avcc !\r\n");
  }
  return 1;
}

int VpuConvertToAvccHeader(unsigned char* pData, int nSize, int*pFilledSize)
{
  unsigned char* pPre=pData;
  int spsSize=0, ppsSize=0;
  unsigned char *sps=NULL, *pps=NULL;
  unsigned char* pNext=NULL;
  unsigned char naltype;
  int length=nSize;
  char* pTemp=NULL,*pFilled=NULL;
  int filledSize=0;
  /*search boundary of sps and pps */
  if(0==VpuFindAVCStartCode(pData,length,&pPre)){
      goto search_finish;
  }
  pPre+=4; //skip 4 bytes of startcode
  length-=(pPre-pData);
  if(length<=0){
      goto search_finish;
  }
  while(1){
    int size;
    VpuFindAVCStartCode(pPre,length,&pNext);
    if(pNext){
        size=pNext-pPre;
    }
    else{
        size=length; //last nal
    }
    naltype=pPre[0] & 0x1f;
    VPU_LOG("find one nal, type: 0x%X, size: %d \r\n",naltype,size);
    if (naltype==7) { /* SPS */
        sps=pPre;
        spsSize=size;
    }
    else if (naltype==8) { /* PPS */
        pps= pPre;
        ppsSize=size;
    }
    if(pNext==NULL){
        goto search_finish;
    }
    pNext+=4;
    length-=(pNext-pPre);
    if(length<=0){
        goto search_finish;
    }
    pPre=pNext;
  }
search_finish:
  if((sps==NULL)||(pps==NULL)){
      VPU_ERROR("failed to create avcc header: no sps/pps in codec data !\r\n");
      return 0;
  }

  /*fill valid avcc header*/
  pTemp=vpu_malloc(nSize+20); // need to allocate more bytes(more than 6+2+1+2 bytes) for additonal tag info
  if(pTemp==NULL){
      VPU_ERROR("malloc %d bytes failure \r\n",nSize);
      return 0;
  }
  pFilled=pTemp;
  pFilled[0]=1;       /* version */
  pFilled[1]=sps[1];  /* profile */
  pFilled[2]=sps[2];  /* profile compat */
  pFilled[3]=sps[3];  /* level */
  pFilled[4]=0xFF;    /* 6 bits reserved (111111) + 2 bits nal size length - 1 (11) */
  pFilled[5]=0xE1;    /* 3 bits reserved (111) + 5 bits number of sps (00001) */
  pFilled+=6;

  pFilled[0]=(spsSize>>8)&0xFF; /*sps size*/
  pFilled[1]=spsSize&0xFF;
  pFilled+=2;
  vpu_memcpy(pFilled,sps,spsSize); /*sps data*/
  pFilled+=spsSize;

  pFilled[0]=1;       /* number of pps */
  pFilled++;
  pFilled[0]=(ppsSize>>8)&0xFF;   /*pps size*/
  pFilled[1]=ppsSize&0xFF;
  pFilled+=2;
  vpu_memcpy(pFilled,pps,ppsSize); /*pps data*/

  filledSize=6+2+spsSize+1+2+ppsSize;
  vpu_memcpy(pData,pTemp,filledSize);

  if(pTemp){
      vpu_free(pTemp);
  }
  VPU_LOG("created on avcc header: %d bytes, sps size: %d, pps size: %d \r\n",filledSize,spsSize, ppsSize);
  *pFilledSize=filledSize;
  return 1;
}

/* end of file*/
