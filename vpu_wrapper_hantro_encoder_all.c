/*!
 *	CopyRight Notice:
 *	Copyright 2019 NXP
 *
 *	History :
 *	Date	(y.m.d)		Author			Version			Description
 *	2019-07-10		    Hou Qi		    0.1		        Created
 */

/** vpu_wrapper_hantro_encoder_all.c
 *	chose related hantro video encoder api according to soc id
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vpu_wrapper.h"

#define CHIPCODE(a,b,c,d)( (((unsigned int)((a)))<<24) | (((unsigned int)((b)))<<16)|(((unsigned int)((c)))<<8)|(((unsigned int)((d)))))
typedef enum
{
  CC_MX23 = CHIPCODE ('M', 'X', '2', '3'),
  CC_MX25 = CHIPCODE ('M', 'X', '2', '5'),
  CC_MX27 = CHIPCODE ('M', 'X', '2', '7'),
  CC_MX28 = CHIPCODE ('M', 'X', '2', '8'),
  CC_MX31 = CHIPCODE ('M', 'X', '3', '1'),
  CC_MX35 = CHIPCODE ('M', 'X', '3', '5'),
  CC_MX37 = CHIPCODE ('M', 'X', '3', '7'),
  CC_MX50 = CHIPCODE ('M', 'X', '5', '0'),
  CC_MX51 = CHIPCODE ('M', 'X', '5', '1'),
  CC_MX53 = CHIPCODE ('M', 'X', '5', '3'),
  CC_MX6Q = CHIPCODE ('M', 'X', '6', 'Q'),
  CC_MX60 = CHIPCODE ('M', 'X', '6', '0'),
  CC_MX6SL = CHIPCODE ('M', 'X', '6', '1'),
  CC_MX6SX = CHIPCODE ('M', 'X', '6', '2'),
  CC_MX6UL = CHIPCODE ('M', 'X', '6', '3'),
  CC_MX6SLL = CHIPCODE ('M', 'X', '6', '4'),
  CC_MX7D = CHIPCODE ('M', 'X', '7', 'D'),
  CC_MX7ULP = CHIPCODE ('M', 'X', '7', 'U'),
  CC_MX8 = CHIPCODE ('M', 'X', '8', '0'),
  CC_MX8QM = CHIPCODE ('M', 'X', '8', '1'),
  CC_MX8QXP = CHIPCODE ('M', 'X', '8', '3'),
  CC_MX8M = CHIPCODE ('M', 'X', '8', '2'),
  CC_MX8MM = CHIPCODE ('M', 'X', '8', '4'),
  CC_MX8MN = CHIPCODE ('M', 'X', '8', '5'),
  CC_MX8MP = CHIPCODE ('M', 'X', '8', '6'),
  CC_UNKN = CHIPCODE ('U', 'N', 'K', 'N')

} CHIP_CODE;

typedef struct {
  CHIP_CODE code;
  char *name;
} SOC_INFO;

static SOC_INFO soc_info[] = {
  {CC_MX23, "i.MX23"},
  {CC_MX25, "i.MX25"},
  {CC_MX27, "i.MX27"},
  {CC_MX28, "i.MX28"},
  {CC_MX31, "i.MX31"},
  {CC_MX35, "i.MX35"},
  {CC_MX37, "i.MX37"},
  {CC_MX50, "i.MX50"},
  {CC_MX51, "i.MX51"},
  {CC_MX53, "i.MX53"},
  {CC_MX6Q, "i.MX6DL"},
  {CC_MX6Q, "i.MX6Q"},
  {CC_MX6Q, "i.MX6QP"},
  {CC_MX6SL, "i.MX6SL"},
  {CC_MX6SLL, "i.MX6SLL"},
  {CC_MX6SX, "i.MX6SX"},
  {CC_MX6UL, "i.MX6UL"},
  {CC_MX6UL, "i.MX6ULL"},
  {CC_MX7D, "i.MX7D"},
  {CC_MX7ULP, "i.MX7ULP"},
  {CC_MX8, "i.MX8DV"},
  {CC_MX8QM, "i.MX8QM"},
  {CC_MX8QXP, "i.MX8QXP"},
  {CC_MX8M, "i.MX8MQ"},
  {CC_MX8MM, "i.MX8MM"},
  {CC_MX8MN, "i.MX8MN"},
  {CC_MX8MP, "i.MX8MP"},
};

static CHIP_CODE getChipCodeFromSocid (void)
{
  FILE *fp = NULL;
  char soc_name[100];
  CHIP_CODE code = CC_UNKN;

  fp = fopen("/sys/devices/soc0/soc_id", "r");
  if (fp == NULL) {
    printf("open /sys/devices/soc0/soc_id failed.\n");
    return  CC_UNKN;
  }

  if (fscanf(fp, "%100s", soc_name) != 1) {
    printf("fscanf soc_id failed.\n");
    fclose(fp);
    return CC_UNKN;
  }
  fclose(fp);

  //GST_INFO("SOC is %s\n", soc_name);

  int num = sizeof(soc_info) / sizeof(SOC_INFO);
  int i;
  for(i=0; i<num; i++) {
    if(!strcmp(soc_name, soc_info[i].name)) {
      code = soc_info[i].code;
      break;
    }
  }

  return code;
}

/* getChipCodeFromSocid () is used to distinguish boards*/
VpuEncRetCode VPU_EncLoad() {
    if (getChipCodeFromSocid () == CC_MX8MM)
        return VPU_EncLoad_H1();
    else
        return VPU_EncLoad_VC8000E();
}

VpuEncRetCode VPU_EncUnLoad() {
    if (getChipCodeFromSocid () == CC_MX8MM)
        return VPU_EncUnLoad_H1();
    else
        return VPU_EncUnLoad_VC8000E();
}

VpuEncRetCode VPU_EncReset(VpuEncHandle InHandle) {
    if (getChipCodeFromSocid () == CC_MX8MM)
        return VPU_EncReset_H1(InHandle);
    else
        return VPU_EncReset_VC8000E(InHandle);
}

VpuEncRetCode VPU_EncOpenSimp(VpuEncHandle *pOutHandle, VpuMemInfo* pInMemInfo,VpuEncOpenParamSimp * pInParam) {
    if (getChipCodeFromSocid () == CC_MX8MM)
        return VPU_EncOpenSimp_H1(pOutHandle, pInMemInfo, pInParam);
    else
        return VPU_EncOpenSimp_VC8000E(pOutHandle, pInMemInfo, pInParam);
}

VpuEncRetCode VPU_EncOpen(VpuEncHandle *pOutHandle, VpuMemInfo* pInMemInfo,VpuEncOpenParam* pInParam) {
    if (getChipCodeFromSocid () == CC_MX8MM)
        return VPU_EncOpen_H1(pOutHandle, pInMemInfo, pInParam);
    else
        return VPU_EncOpen_VC8000E(pOutHandle, pInMemInfo, pInParam);
}

VpuEncRetCode VPU_EncClose(VpuEncHandle InHandle) {
    if (getChipCodeFromSocid () == CC_MX8MM)
        return VPU_EncClose_H1(InHandle);
    else
        return VPU_EncClose_VC8000E(InHandle);
}

VpuEncRetCode VPU_EncGetInitialInfo(VpuEncHandle InHandle, VpuEncInitInfo * pOutInitInfo) {
    if (getChipCodeFromSocid () == CC_MX8MM)
        return VPU_EncGetInitialInfo_H1(InHandle, pOutInitInfo);
    else
        return VPU_EncGetInitialInfo_VC8000E(InHandle, pOutInitInfo);
}

VpuEncRetCode VPU_EncGetVersionInfo(VpuVersionInfo * pOutVerInfo) {
    if (getChipCodeFromSocid () == CC_MX8MM)
        return VPU_EncGetVersionInfo_H1(pOutVerInfo);
    else
        return VPU_EncGetVersionInfo_VC8000E(pOutVerInfo);
}

VpuEncRetCode VPU_EncGetWrapperVersionInfo(VpuWrapperVersionInfo * pOutVerInfo) {
    if (getChipCodeFromSocid () == CC_MX8MM)
        return VPU_EncGetWrapperVersionInfo_H1(pOutVerInfo);
    else
        return VPU_EncGetWrapperVersionInfo_VC8000E(pOutVerInfo);
}

VpuEncRetCode VPU_EncRegisterFrameBuffer(VpuEncHandle InHandle,VpuFrameBuffer *pInFrameBufArray, int nNum,int nSrcStride) {
    if (getChipCodeFromSocid () == CC_MX8MM)
        return VPU_EncRegisterFrameBuffer_H1(InHandle, pInFrameBufArray, nNum, nSrcStride);
    else
        return VPU_EncRegisterFrameBuffer_VC8000E(InHandle, pInFrameBufArray, nNum, nSrcStride);
}

VpuEncRetCode VPU_EncQueryMem(VpuMemInfo* pOutMemInfo) {
    if (getChipCodeFromSocid () == CC_MX8MM)
        return VPU_EncQueryMem_H1(pOutMemInfo);
    else
        return VPU_EncQueryMem_VC8000E(pOutMemInfo);
}

VpuEncRetCode VPU_EncGetMem(VpuMemDesc* pInOutMem) {
    if (getChipCodeFromSocid () == CC_MX8MM)
        return VPU_EncGetMem_H1(pInOutMem);
    else
        return VPU_EncGetMem_VC8000E(pInOutMem);
}

VpuEncRetCode VPU_EncFreeMem(VpuMemDesc* pInMem) {
    if (getChipCodeFromSocid () == CC_MX8MM)
        return VPU_EncFreeMem_H1(pInMem);
    else
        return VPU_EncFreeMem_VC8000E(pInMem);
}

VpuEncRetCode VPU_EncConfig(VpuEncHandle InHandle, VpuEncConfig InEncConf, void* pInParam) {
    if (getChipCodeFromSocid () == CC_MX8MM)
        return VPU_EncConfig_H1(InHandle, InEncConf, pInParam);
    else
        return VPU_EncConfig_VC8000E(InHandle, InEncConf, pInParam);
}

VpuEncRetCode VPU_EncEncodeFrame(VpuEncHandle InHandle, VpuEncEncParam* pInOutParam) {
    if (getChipCodeFromSocid () == CC_MX8MM)
        return VPU_EncEncodeFrame_H1(InHandle, pInOutParam);
    else
        return VPU_EncEncodeFrame_VC8000E(InHandle, pInOutParam);
}

