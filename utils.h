/*
 *  Copyright (c) 2016, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

#ifndef UTILS_H
#define UTILS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int VpuDetectAvcc(unsigned char* pCodecData, unsigned int nSize, int * pIsAvcc,
    int * pNalSizeLength,int* pNalNum);
int VpuDetectHvcc(unsigned char* pCodecData, unsigned int nSize, int * pIsHvcc,
    int * pNalSizeLength,int* pNalNum);
int VpuConvertAvccHeader(unsigned char* pCodecData, unsigned int nSize,
    unsigned char** ppOut, unsigned int * pOutSize);
int VpuConvertHvccHeader(unsigned char* pCodecData, unsigned int nSize,
    unsigned char** ppOut, unsigned int * pOutSize);
int VpuConvertAvccFrame(unsigned char* pData, unsigned int nSize, int
    nNalSizeLength, unsigned char** ppFrm, unsigned int* pSize, int * pNalNum);

int VC1CreateNALSeqHeader(unsigned char* pHeader, int* pHeaderLen, 
	unsigned char* pCodecPri,int nCodecSize, unsigned int* pData, int nMaxHeader);
int VC1CreateRCVSeqHeader(unsigned char* pHeader, int* pHeaderLen, 
	unsigned char* pCodecPri,unsigned int nFrameSize,int nWidth,int nHeight,int* pNoError);
int VC1CreateNalFrameHeader(unsigned char* pHeader, int* pHeaderLen,unsigned int*pInData );
int VC1CreateRCVFrameHeader(unsigned char* pHeader, int* pHeaderLen,unsigned int nInSize );

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  //#ifndef UTILS_H

