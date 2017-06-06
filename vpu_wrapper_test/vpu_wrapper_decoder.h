#ifndef VPU_WRAPPER_DECODER_H
#define VPU_WRAPPER_DECODER_H

#include <pthread.h>
#include "input.h"
#include "output.h"
#include "vpu_wrapper.h"
#include "codec_api.h"

#define VPU_DEC_MAX_NUM_MEM_NUM	20
typedef struct
{
	//virtual mem info
	int nVirtNum;
	unsigned int virtMem[VPU_DEC_MAX_NUM_MEM_NUM];

	//phy mem info
	int nPhyNum;
	unsigned int phyMem_virtAddr[VPU_DEC_MAX_NUM_MEM_NUM];
	unsigned int phyMem_phyAddr[VPU_DEC_MAX_NUM_MEM_NUM];
	unsigned int phyMem_cpuAddr[VPU_DEC_MAX_NUM_MEM_NUM];
	unsigned int phyMem_size[VPU_DEC_MAX_NUM_MEM_NUM];	
}DecMemInfo;

class vpu_wrapper_decoder : public codec_api{
  public:
    vpu_wrapper_decoder (int fd);
    virtual ~vpu_wrapper_decoder ();
    bool set_input (input *input);
    bool set_output (output *output);
    bool set_format (Format *format);
    bool start ();
    bool seek (int portion, int64 timems);
    bool stop ();
    void *input_thread ();
    void *output_thread ();
    void *decode_thread ();
  private:
    int start_decode();
    int stop_decode();
    int MallocMemBlock(VpuMemInfo* pMemBlock,DecMemInfo* pDecMem);
    int FreeMemBlock(DecMemInfo* pDecMem);
    int FreeMemBlockFrame(DecMemInfo* pDecMem, int nFrmNum);
    int ConvertCodecFormat(VpuCodStd* pCodec);
    int ProcessInitInfo(VpuDecHandle handle,VpuDecInitInfo* pInitInfo,DecMemInfo* pDecMemInfo, int*pOutFrmNum);

    pthread_t input_thread_id, output_thread_id,decode_thread_id;
    input *minput;
    output *moutput;
    Buffer inputBuf;
    Format mformat;
    int seek_portion;
    int64 seek_timems;
    int mcount;
    bool bseek;
    bool bstop;
    bool bInputEos;
    bool bOutputEos;
	VpuDecHandle handle;
    bool hasInput;
    bool hasOutput;
    DecMemInfo decMemInfo;
    pthread_mutex_t stream_lock;
    int64 inBufferCnt;
};




#endif
