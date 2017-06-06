#include "stdio.h"
#include "stdlib.h"
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "vpu_wrapper_decoder.h"
#include "log.h"
#include <linux/videodev2.h>


vpu_wrapper_decoder::vpu_wrapper_decoder (int fd)
{

    inBufferCnt = 0;
}
vpu_wrapper_decoder::~vpu_wrapper_decoder()
{
    pthread_mutex_destroy(&stream_lock);
}
bool vpu_wrapper_decoder::set_input (input *input)
{
    CHECK_NULL (input);
    minput = input;
    return true;
}
bool vpu_wrapper_decoder::set_output (output *output)
{
  CHECK_NULL (output);
  moutput = output;
  return true;
}
bool vpu_wrapper_decoder::set_format (Format *format)
{
  CHECK_NULL (format);
  mformat = *format;
  return true;
}
bool vpu_wrapper_decoder::start ()
{
    pthread_mutex_destroy(&stream_lock);
    pthread_mutex_init(&stream_lock, NULL);

    if(0 == start_decode())
        return true;
    return false;
}
bool vpu_wrapper_decoder::seek (int portion, int64 timems)
{
  seek_portion = portion;
  seek_timems = timems;
  bseek = true;
}
bool vpu_wrapper_decoder::stop ()
{

    stop_decode();
    return true;
}
void *vpu_wrapper_decoder::input_thread ()
{
  memset(&inputBuf,0, sizeof(Buffer));
  while (!bstop && !bInputEos) {
    LOG_DEBUG ("input thread begin\n");

    pthread_mutex_lock(&stream_lock);
    if (bseek) {
      minput->set_position (seek_portion, seek_timems);
      VPU_DecFlushAll(handle);
      moutput->flush ();
      bseek = false;
      LOG_DEBUG ("input_thread seek positon=%d,ts=%lld \n",seek_portion,seek_timems);
    }
    
    if(!hasInput){
        memset(&inputBuf,0, sizeof(Buffer));
        inputBuf.plane_num = 1;
        inputBuf.alloc_size[0] = 1024*1024;
        minput->get_buffer (&inputBuf);
        hasInput = true;
        if(inputBuf.size[0] > 0)
            inBufferCnt ++;
        else if(inputBuf.size[0] == 0){
            bInputEos = true;
            LOG_DEBUG("input_thread inputBuf EOS \n");
        }

        LOG_DEBUG ("input thread get input buffer size=%d,cnt=%d\n",inputBuf.size[0],inBufferCnt);
    }
    pthread_mutex_unlock(&stream_lock);
    usleep(5000);
  }

  LOG_DEBUG ("input thread exit.\n");
    return NULL;
}
void *vpu_wrapper_decoder::output_thread ()
{
    VpuDecRetCode ret;

  while (!bstop) {
    LOG_DEBUG ("output thread begin\n");
    pthread_mutex_lock(&stream_lock);
    if(hasOutput){
        Buffer buf = {0};
        VpuDecOutFrameInfo frameInfo;
        ret =VPU_DecGetOutputFrame(handle, &frameInfo);
        if( ret != VPU_DEC_RET_SUCCESS){
            LOG_ERROR ("VPU_DecGetOutputFrame failed ret=%d\n",ret);
            pthread_mutex_unlock(&stream_lock);
            continue;
        }
        hasOutput = false;

        LOG_DEBUG ("VPU_DecGetOutputFrame %p, %p\n",frameInfo.pDisplayFrameBuf->pbufVirtY,frameInfo.pDisplayFrameBuf->pbufVirtCb);
        buf.plane_num = 2;
        buf.data[0] = (char*)frameInfo.pDisplayFrameBuf->pbufVirtY;
        buf.data[1] = (char*)frameInfo.pDisplayFrameBuf->pbufVirtCb;
        buf.phy_data[0] = (char*)frameInfo.pDisplayFrameBuf->pbufY;
        buf.phy_data[1] = (char*)frameInfo.pDisplayFrameBuf->pbufCb;
        //buf.data[2] = (char*)frameInfo.pDisplayFrameBuf->pbufVirtCr;
        buf.size[0] = buf.alloc_size[0] = mformat.image_size[0];
        buf.size[1] = buf.alloc_size[1] = mformat.image_size[1];
        //buf.size[2] = buf.alloc_size[2] = mformat.image_size[2];

        #if 0
        FILE * pfile;
        pfile = fopen("raw.yuv","ab");
        if(pfile){
            fwrite(buf.data[0],1,buf.size[0],pfile);
            fwrite(buf.data[1],1,buf.size[1],pfile);
            fclose(pfile);
        }
        #endif
        LOG_DEBUG("output_thread size0=%d,size1=%d \n",buf.size[0],buf.size[1]);

        moutput->put_buffer (&buf);
        mcount ++;
        LOG_DEBUG ("VPU_DecOutFrameDisplayed BEGIN mcount=%d\n",mcount);
        ret=VPU_DecOutFrameDisplayed(handle,frameInfo.pDisplayFrameBuf);
        if(ret != VPU_DEC_RET_SUCCESS){
            LOG_ERROR ("VPU_DecOutFrameDisplayed failed\n");
        }

    }

    pthread_mutex_unlock(&stream_lock);

    if (bOutputEos){
        sigval value;
        bstop = true;
        sigqueue (getpid (), SIGUSR1, value);
        LOG_WARNING ("EOS, exit\n");
        break;
     }

    usleep(10000);
    LOG_DEBUG ("video codec count: %d\n", mcount);
    }

    return NULL;
}
void *vpu_wrapper_decoder::decode_thread ()
{
    int capability=0;
    VpuDecFrameLengthInfo decFrmLengthInfo;
    unsigned int totalDecConsumedBytes;	//stuffer + frame
    int nFrmNum;
    unsigned char  dummy;
    VPU_DecGetCapability(handle, VPU_DEC_CAP_FRAMESIZE, &capability);

    while(!bstop){
        VpuDecRetCode ret = VPU_DEC_RET_SUCCESS;
        VpuBufferNode InData;
        int bufRetCode=0;
        VpuDecInitInfo InitInfo;
        int nFrmNum;

        LOG_DEBUG ("decode thread begin\n");

        pthread_mutex_lock(&stream_lock);

        if((hasInput || bInputEos) && !hasOutput && !bOutputEos){

            InData.nSize=inputBuf.size[0];
            InData.pPhyAddr=NULL;
            InData.pVirAddr=(unsigned char*)inputBuf.data[0];
            if(0 == InData.nSize)
                InData.pVirAddr = (unsigned char *)0x01;//for eos event

            InData.sCodecData.pData=NULL;
            InData.sCodecData.nSize=0;
            LOG_DEBUG ("decode_thread BEGIN size=%d,inCnt=%lld,vaddr=%p\n",InData.nSize,inBufferCnt,InData.pVirAddr);
            ret=VPU_DecDecodeBuf(handle, &InData,&bufRetCode);
            LOG_DEBUG ("decode_thread ret=%x,inCnt=%lld,bufRetCode=%x\n",ret,inBufferCnt,bufRetCode);
        }

        if(bufRetCode&VPU_DEC_INPUT_USED){
            for (int i = 0; i < inputBuf.plane_num; i ++){
                if(inputBuf.data[i] != NULL)
                    free(inputBuf.data[i]);
            }
            memset(&inputBuf,0, sizeof(Buffer));
            hasInput = false;
        }

        if(bufRetCode&VPU_DEC_INIT_OK){
            LOG_DEBUG("VPU_DEC_INIT_OK \n");
            ProcessInitInfo(handle,&InitInfo,&decMemInfo,&nFrmNum);
        }

        if(bufRetCode&VPU_DEC_RESOLUTION_CHANGED){
            LOG_DEBUG("VPU_DEC_RESOLUTION_CHANGED \n");
            FreeMemBlockFrame(&decMemInfo, nFrmNum);
            ProcessInitInfo(handle,&InitInfo,&decMemInfo,&nFrmNum);
        }

        if(capability){
            if(bufRetCode&VPU_DEC_ONE_FRM_CONSUMED){
                ret=VPU_DecGetConsumedFrameInfo(handle, &decFrmLengthInfo);
                if(VPU_DEC_RET_SUCCESS!=ret){
                    LOG_ERROR("vpu get consumed frame info failure\n");
                }
                LOG_DEBUG("frame is consumed: %d\n",decFrmLengthInfo.nFrameLength);
            }
        }

        if((bufRetCode&VPU_DEC_OUTPUT_DIS)||(bufRetCode&VPU_DEC_OUTPUT_MOSAIC_DIS)){
            LOG_DEBUG("VPU_DEC_OUTPUT_DIS \n");
            hasOutput = true;
        }

        if(bufRetCode&VPU_DEC_OUTPUT_EOS){
            bOutputEos = true;
            LOG_DEBUG("VPU_DEC_OUTPUT_EOS \n");
        }

        pthread_mutex_unlock(&stream_lock);

        usleep(5000);
    }

    {
        VpuDecRetCode ret = VPU_DEC_RET_SUCCESS;
        VPU_DecReset(handle);
        
        ret=VPU_DecClose(handle);
        if(ret != 0)
            LOG_DEBUG("vpu unload failure ret=%d\n",ret);


        FreeMemBlock(&decMemInfo);
        
        ret = VPU_DecUnLoad();
        LOG_DEBUG("VPU_DecUnLoad ret=%d\n",ret);
    }

    return NULL;
}
static void *input_thread_wrap (void *arg)
{
  vpu_wrapper_decoder *vpu_wrapper =    (vpu_wrapper_decoder *)arg;
  return vpu_wrapper->input_thread ();
}
static void *output_thread_wrap (void *arg)
{
  vpu_wrapper_decoder *vpu_wrapper =    (vpu_wrapper_decoder *)arg;
  return vpu_wrapper->output_thread ();
}
static void *decode_thread_wrap (void *arg)
{
  vpu_wrapper_decoder *vpu_wrapper =    (vpu_wrapper_decoder *)arg;
  return vpu_wrapper->decode_thread ();
}

#define MAX_INPUT_FRAME_LEN	    (1024*1024) //1M bytes
#ifdef ENABLE_LOG
#define VPU_TEST_PRINTF printf
#define VPU_TRACE
#else
#define VPU_TEST_PRINTF
#define VPU_TRACE
#endif

#define Align(ptr,align)	(((unsigned int64)ptr+(align)-1)/(align)*(align))

#define FRAME_ALIGN (32)

int vpu_wrapper_decoder::MallocMemBlock(VpuMemInfo* pMemBlock,DecMemInfo* pDecMem)
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
				LOG_DEBUG("%s: get virtual memory failure, size=%d \r\n",__FUNCTION__,size);
				goto failure;
			}		
			pMemBlock->MemSubBlock[i].pVirtAddr=(unsigned char*)Align(ptr,pMemBlock->MemSubBlock[i].nAlignment);

			//record virtual base addr
			pDecMem->virtMem[pDecMem->nVirtNum]=(unsigned int64)ptr;
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
				LOG_DEBUG("%s: get vpu memory failure, size=%d, ret=%d \r\n",__FUNCTION__,size,ret);
				goto failure;
			}
			pMemBlock->MemSubBlock[i].pVirtAddr=(unsigned char*)Align(vpuMem.nVirtAddr,pMemBlock->MemSubBlock[i].nAlignment);
			pMemBlock->MemSubBlock[i].pPhyAddr=(unsigned char*)Align(vpuMem.nPhyAddr,pMemBlock->MemSubBlock[i].nAlignment);

			//record physical base addr
			pDecMem->phyMem_phyAddr[pDecMem->nPhyNum]=(unsigned int)vpuMem.nPhyAddr;
			pDecMem->phyMem_virtAddr[pDecMem->nPhyNum]=(unsigned int)vpuMem.nVirtAddr;
			pDecMem->phyMem_cpuAddr[pDecMem->nPhyNum]=(unsigned int)vpuMem.nCpuAddr;
			pDecMem->phyMem_size[pDecMem->nPhyNum]=size;
			pDecMem->nPhyNum++;			
		}
	}	

    LOG_DEBUG("MallocMemBlock virtnum=%d,phynum=%d\n",pDecMem->nVirtNum,pDecMem->nPhyNum);
	return 1;
	
failure:
	FreeMemBlock(pDecMem);
	return 0;
	
}

int vpu_wrapper_decoder::FreeMemBlock(DecMemInfo* pDecMem)
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
        LOG_DEBUG("FreeMemBlock i=%d,virtaddr=%p,phyaddr=%p \n",i,vpuMem.nVirtAddr,vpuMem.nPhyAddr);
        vpuRet=VPU_DecFreeMem(&vpuMem);
        if(vpuRet!=VPU_DEC_RET_SUCCESS)
        {
            LOG_ERROR("%s: free vpu memory failure : ret=%d \r\n",__FUNCTION__,vpuRet);
            retOk=0;
        }

    }
    pDecMem->nPhyNum=0;

    return retOk;
}

int vpu_wrapper_decoder::FreeMemBlockFrame(DecMemInfo* pDecMem, int nFrmNum)
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
			LOG_ERROR("%s: free vpu memory failure : ret=%d \r\n",__FUNCTION__,vpuRet);
			retOk=0;
		}
		cnt++;
		if(cnt==nFrmNum) break;
	}
	pDecMem->nPhyNum=pDecMem->nPhyNum-cnt;
	if(cnt!=nFrmNum) 
	{
		LOG_ERROR("error: only freed %d frames, required frame numbers: %d \r\n",cnt,nFrmNum);
		retOk=0;
	}	
	return retOk;
}

int vpu_wrapper_decoder::ConvertCodecFormat(VpuCodStd* pCodec)
{

    if(!strcmp(mformat.format,"H264")){
        *pCodec = VPU_V_AVC;
    }else if(!strcmp(mformat.format,"HEVC")){
        *pCodec = (VpuCodStd)16;//VPU_V_HEVC;
    }else if(!strcmp(mformat.format,"MPEG4")){
        *pCodec = VPU_V_MPEG4;
    }else if(!strcmp(mformat.format,"H263")){
        *pCodec = VPU_V_H263;
    }else if(!strcmp(mformat.format,"MPEG2")){
        *pCodec = VPU_V_MPEG2;
    }else if(!strcmp(mformat.format,"VP8")){
        *pCodec = VPU_V_VP8;
    }

    return 1;
}


int vpu_wrapper_decoder::start_decode()
{
    VpuMemInfo memInfo;
    int nUnitDataSize=MAX_INPUT_FRAME_LEN;
    VpuDecRetCode ret;
    VpuVersionInfo ver;
    VpuWrapperVersionInfo w_ver;
    VpuDecOpenParam decOpenParam;
    int capability=0;
    memset(&decOpenParam, 0, sizeof(VpuDecOpenParam));

    ret = VPU_DecLoad();
    if(ret != 0)
        return ret;

    //version info
    ret=VPU_DecGetVersionInfo(&ver);

    if(ret != 0)
        goto finish;

    //wrapper version info
    ret=VPU_DecGetWrapperVersionInfo(&w_ver);
    if (ret!=VPU_DEC_RET_SUCCESS)
    {
        LOG_ERROR("%s: vpu get wrapper version failure: ret=%d \r\n",__FUNCTION__,ret);
        goto finish;
    }

    //query memory
    ret=VPU_DecQueryMem(&memInfo);
    if (ret!=VPU_DEC_RET_SUCCESS)
    {
        LOG_ERROR("%s: vpu query memory failure: ret=%d \r\n",__FUNCTION__,ret);
        goto finish;
    }

    //malloc memory for vpu wrapper
    if(0==MallocMemBlock(&memInfo,&decMemInfo))
    {
        LOG_ERROR("%s: malloc memory failure: \r\n",__FUNCTION__);
        goto finish;
    }

    //set open params
    if(0==ConvertCodecFormat(&decOpenParam.CodecFormat))
    {
        LOG_ERROR("unsupported codec format: \r\n");
        goto finish;
    }

    decOpenParam.nEnableFileMode=0;//using stream mode

    //check capabilities
    VPU_DecGetCapability((VpuDecHandle)NULL, VPU_DEC_CAP_FILEMODE, &capability);
    LOG_DEBUG("capability: file mode supported: %d \r\n",capability);
    VPU_DecGetCapability((VpuDecHandle)NULL, VPU_DEC_CAP_TILE, &capability);
    LOG_DEBUG("capability: tile format supported: %d \r\n",capability);

    VPU_DecGetCapability(handle, VPU_DEC_CAP_FRAMESIZE, &capability);
    LOG_DEBUG("capability: report frame size supported: %d \r\n",capability);
    
    decOpenParam.nChromaInterleave=1;//default: enable interleave for NV12 format or it will be i420
    decOpenParam.nMapType=0;//default: using linear format
    decOpenParam.nTiled2LinearEnable=0;//default: no additional convert
    decOpenParam.nReorderEnable = 1;//enable vpu reorder for B frame

    //open vpu
    ret=VPU_DecOpen(&handle, &decOpenParam, &memInfo);
    if(ret != VPU_DEC_RET_SUCCESS){
        LOG_ERROR("%s: vpu open failure: ret=%d \r\n",__FUNCTION__,ret);
        goto finish;
    }


    //here, we use the one config for the whole stream
    {
        VpuDecConfig config;		
        int param;

        //config skip type
        #if 0
        if(0==ConvertSkipMode(0,&config,&param))
        {
            LOG_ERROR("unvalid skip mode:ignored \r\n",);
            config=VPU_DEC_CONF_SKIPMODE;
            param=VPU_DEC_SKIPNONE;
        }
        #endif

        config=VPU_DEC_CONF_SKIPMODE;
        param=VPU_DEC_SKIPNONE;

        ret=VPU_DecConfig(handle, config, &param);
        if(VPU_DEC_RET_SUCCESS!=ret)
        {
            LOG_ERROR("%s: vpu config failure: config=0x%X, ret=%d \r\n",__FUNCTION__,(unsigned int)config,ret);
        }

        //config delay buffer size
        config = VPU_DEC_CONF_BUFDELAY;
        param=0;
        ret=VPU_DecConfig(handle, config, &param);
        if(VPU_DEC_RET_SUCCESS!=ret)
        {
            LOG_ERROR("%s: vpu config failure: config=0x%X, ret=%d \r\n",__FUNCTION__,(unsigned int)config,ret);
        }

        //config input type: normal
        config=VPU_DEC_CONF_INPUTTYPE;
        param=VPU_DEC_IN_NORMAL;
        LOG_DEBUG("set input type : normal(%d)  \r\n",param);
        ret=VPU_DecConfig(handle, config, &param);
        if(VPU_DEC_RET_SUCCESS!=ret)
        {
            LOG_ERROR("%s: vpu config failure: config=0x%X, ret=%d \r\n",__FUNCTION__,(unsigned int)config,ret);
        }
    }

    LOG_DEBUG("open vpu successfully");
    mcount = 0;
    bInputEos = false;
    bOutputEos = false;
    bstop = false;
    bseek = false;
    inBufferCnt = 0;
    hasInput = false;
    hasOutput = false;

    pthread_create(&(input_thread_id), NULL, input_thread_wrap, this);
    pthread_create(&(output_thread_id), NULL, output_thread_wrap, this);

    pthread_create(&(decode_thread_id), NULL, decode_thread_wrap, this);

    return 0;
finish:
    ret=VPU_DecUnLoad();
    if(ret != 0)
        LOG_DEBUG("vpu unload failure ret=%d",ret);
    return 1;
}
int vpu_wrapper_decoder::stop_decode()
{
    VpuDecRetCode ret;

    bstop = true;

    pthread_join (input_thread_id, NULL);
    pthread_join (output_thread_id, NULL);
    pthread_join (decode_thread_id, NULL);
    moutput->flush();
    LOG_DEBUG ("stop_decode END\n");

    return ret;
}
int vpu_wrapper_decoder::ProcessInitInfo(VpuDecHandle handle,VpuDecInitInfo* pInitInfo,DecMemInfo* pDecMemInfo, int*pOutFrmNum)
{
    VpuDecRetCode ret;
    int BufNum = 0;
    VpuMemDesc vpuMem;
    VpuFrameBuffer frameBuf[VPU_DEC_MAX_NUM_MEM_NUM];
    int totalSize=0;
    int ySize=0;
    int uSize=0;
    int vSize=0;
    int yStride=0;
    int uStride=0;
    int vStride=0;
    int mvSize=0;
    unsigned char* ptr;
    unsigned char* ptrVirt;

    ret=VPU_DecGetInitialInfo(handle, pInitInfo);
    if(VPU_DEC_RET_SUCCESS!=ret)
        return ret;

    mformat.width = pInitInfo->nPicWidth;
    mformat.height = pInitInfo->nPicHeight;

    mformat.crop_left = pInitInfo->PicCropRect.nLeft;
    mformat.crop_top = pInitInfo->PicCropRect.nTop;
    mformat.crop_width = pInitInfo->PicCropRect.nRight;
    mformat.crop_height = pInitInfo->PicCropRect.nBottom;

    //yuv:4:2:0
    yStride = Align(pInitInfo->nPicWidth,FRAME_ALIGN);
    ySize = Align(pInitInfo->nPicWidth,FRAME_ALIGN) * Align(pInitInfo->nPicHeight,FRAME_ALIGN);

    uStride = yStride/2;
    vStride=uStride;
    uSize=ySize/4;
    vSize=uSize;
    mvSize=uSize;

    //NV12:
    mformat.stride[0] = yStride;
    mformat.stride[1] = uStride + vStride;
    //mformat.stride[2] = vStride;

    mformat.image_size[0] = ySize;
    mformat.image_size[1] = uSize + vSize;
    //mformat.image_size[2] = vSize;

    mformat.yuv_format_v4l2 = V4L2_PIX_FMT_NV12;

    LOG_INFO("ProcessInitInfo w=%d,h=%d,[l,t,w,h]=[%d,%d,%d,%d]\n",mformat.width,mformat.height
        ,mformat.crop_left,mformat.crop_top,mformat.crop_width,mformat.crop_height);

    LOG_INFO("image_size=%d,%d",mformat.image_size[0],mformat.image_size[1]);

    mformat.plane_num = 2;

    BufNum=pInitInfo->nMinFrameBufferCount+3;

    moutput->set_format (&mformat);

    for(int i = 0; i < BufNum; i++){
        totalSize=(ySize+uSize+vSize)*1;

        vpuMem.nSize=totalSize;

        ret=VPU_DecGetMem(&vpuMem);

        if(VPU_DEC_RET_SUCCESS!=ret)
        {
            LOG_ERROR("%s: vpu malloc frame buf failure: ret=%d \r\n",__FUNCTION__,ret);
            return ret;
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
        if(FRAME_ALIGN>1)
        {
            ptr=(unsigned char*)Align(ptr,FRAME_ALIGN);
            ptrVirt=(unsigned char*)Align(ptrVirt,FRAME_ALIGN);
        }

        /* fill stride info */
        frameBuf[i].nStrideY=yStride;
        frameBuf[i].nStrideC=uStride;

        /* fill phy addr*/
        frameBuf[i].pbufY=ptr;
        frameBuf[i].pbufCb=ptr+ySize;
        frameBuf[i].pbufCr=ptr+ySize+uSize;
        frameBuf[i].pbufMvCol=ptr+ySize+uSize+vSize;

        LOG_DEBUG("VPU_DecRegisterFrameBuffer addr=%p,phy=%p \n",ptrVirt,ptr);
        /* fill virt addr */
        frameBuf[i].pbufVirtY=ptrVirt;
        frameBuf[i].pbufVirtCb=ptrVirt+ySize;
        frameBuf[i].pbufVirtCr=ptrVirt+ySize+uSize;
        frameBuf[i].pbufVirtMvCol=ptrVirt+ySize+uSize+vSize;

#ifdef ILLEGAL_MEMORY_DEBUG
        memset(frameBuf[i].pbufVirtY,0,ySize);
        memset(frameBuf[i].pbufVirtCb,0,uSize);
        memset(frameBuf[i].pbufVirtCr,0,uSize);
#endif

        /* fill bottom address for field tile*/
        frameBuf[i].pbufY_tilebot=0;
        frameBuf[i].pbufCb_tilebot=0;
        frameBuf[i].pbufVirtY_tilebot=0;
        frameBuf[i].pbufVirtCb_tilebot=0;

    }

    ret=VPU_DecRegisterFrameBuffer(handle, frameBuf, BufNum);
    if(VPU_DEC_RET_SUCCESS != ret)
    {
        LOG_ERROR("vpu register frame failure: ret=%d\n",ret);
        return ret;
    }
    *pOutFrmNum=BufNum;

    return 0;
}
