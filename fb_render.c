/*
 *  Copyright (c) 2010-2013, Freescale Semiconductor Inc.,
 *  Copyright 2020 NXP
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 *
 */

/*
 *  fb_render.c
 *	this file is responsible for rendering video frame through ipu's fb
 */

#ifdef IMX5
#include <fcntl.h>
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#include "stdint.h"  

#include "mxc_ipu_hl_lib.h"

#include "sys/ioctl.h"	//for ioctl()
#include "unistd.h" 	//for open()/close()

#ifdef FB_RENDER_DEBUG
#define FB_RENDER_PRINTF printf
#else
#define FB_RENDER_PRINTF
#endif

#define FB_RENDER_MEMCPY	memcpy
#define FB_RENDER_MEMSET	memset
#define FR_RENDER_MALLOC	malloc
#define FR_RENDER_FREE	free


typedef struct
{
	/* parameters required by IPU */
	ipu_lib_handle_t ipu_handle;
	ipu_lib_input_param_t sInParam;
	ipu_lib_output_param_t sOutParam;

	/* record IPU input buffer address */
	unsigned char* YUV[3];  //virtual address for physical successive space

	/* tracing info */
	int nOutBufIdx;
	int ipu_finish;

	/**/
	int test_fb_num;
} IPUInfo;

void ipu_update_input_buff(IPUInfo * pIpuInfo,int width, int height, int index)
{
	int frame_size;
	frame_size=width*height;

	pIpuInfo->YUV[0]=(unsigned char*)pIpuInfo->ipu_handle.inbuf_start[index]; //virtual address pointer to physical space
	pIpuInfo->YUV[1]=pIpuInfo->YUV[0]+frame_size;
	pIpuInfo->YUV[2]=pIpuInfo->YUV[1]+frame_size/4;
	//FB_RENDER_PRINTF("input virtual address: 0x%X \r\n",(unsigned int)pIpuInfo->YUV[0]);		
}

int  ipu_init_fb_device(int fb_num,struct fb_var_screeninfo * fb_var)
{
	int fd_fb = 0;

	int ret = 0;
	struct mxcfb_gbl_alpha gbl_alpha;
	struct mxcfb_color_key key;       

	//If ui to tvout, then we should set fb1's color key and alpha value
	if(fb_num==0)
	{
		if ((fd_fb = open("/dev/fb0", O_RDWR, 0)) < 0)
		{
			FB_RENDER_PRINTF("Unable to open /dev/fb0\n");
			return 0;
		}
	}
	else if(fb_num==1)
	{
		if ((fd_fb = open("/dev/fb1", O_RDWR, 0)) < 0) 
		{
			FB_RENDER_PRINTF("Unable to open /dev/fb0\n");
			return 0;
		}
	}	
	else if(fb_num==2)
	{
		if ((fd_fb = open("/dev/fb2", O_RDWR, 0)) < 0) 
		{
			FB_RENDER_PRINTF("Unable to open /dev/fb0\n");
			return 0;
		}
	} 
	else
	{
		FB_RENDER_PRINTF("error fb num: %d \r\n",fb_num);
		return 0;
	}


	if (ioctl(fd_fb, FBIOGET_VSCREENINFO, fb_var) < 0) 
	{
		FB_RENDER_PRINTF("Get FB var info failed!\n");
		close(fd_fb);
		return 0;
	}

	//fb0/1 ioctl
	//MXCFB_SET_GBL_ALPHA
	//MXCFB_SET_CLR_KEY
	if(fb_num !=1)
	{
		unsigned int enable=1;
		unsigned int key_val=0x00000000;// Black

		FB_RENDER_PRINTF("fb %d : set color key enable: %d, key: %d \r\n",fb_num,enable,key_val);
		key.enable = enable;
		key.color_key = key_val; 
		ret = ioctl(fd_fb, MXCFB_SET_CLR_KEY, &key);
		if(ret <0)
		{
			FB_RENDER_PRINTF("MXCFB_SET_CLR_KEY error!");
			close(fd_fb);
			return 0;
		}
	}

	if(fb_num !=1)
	{
		unsigned int enable=1;
		unsigned int alpha=0;
		if(fb_num==0)
		{
			enable=1;
			alpha=0;
		}
		else  // fb 2
		{
			enable=1;
			alpha=255;
		}
#if 1
		gbl_alpha.alpha = alpha;
		gbl_alpha.enable = enable;
#else
		gbl_alpha.alpha = 255;
		gbl_alpha.enable = 1;
#endif	 
		FB_RENDER_PRINTF("fb %d : set alpha enable:%d, key: %d \r\n",fb_num,enable,alpha);
		ret = ioctl(fd_fb, MXCFB_SET_GBL_ALPHA, &gbl_alpha);
		if(ret <0)
		{
			FB_RENDER_PRINTF("MXCFB_SET_GBL_ALPHA error!");
			close(fd_fb);
			return 0;
		}
	}     

	if(fd_fb)
	{
		close(fd_fb);
	}

	FB_RENDER_PRINTF("fb_var: bits_per_pixel %d,xres %d,yres %d,xres_virtual %d,yres_virtual %d\n",
			fb_var->bits_per_pixel,fb_var->xres,fb_var->yres,fb_var->xres_virtual,fb_var->yres_virtual);

	return 1;	
}

void ipu_output_cb(void *arg, int index)
{
    IPUInfo * pIpuInfo = (IPUInfo * )arg;
    pIpuInfo->nOutBufIdx = index;
    pIpuInfo->ipu_finish=1;
}

int fb_render_init(int* pHandle,int fb_num,int width , int height)
{
	int ret;
	int mode;

	struct fb_var_screeninfo  fb_var;

	IPUInfo * pIpuInfo;
	pIpuInfo=(IPUInfo *)FR_RENDER_MALLOC(sizeof(IPUInfo));
	if(pIpuInfo==NULL)
	{
		FB_RENDER_PRINTF("%s: allocate IPU object failure \r\n",__FUNCTION__);
		return 0;
	}

	/*we need to clear the structure, otherwise segment fault may occur randomly.*/
	FB_RENDER_MEMSET(pIpuInfo,0,sizeof(IPUInfo));
	
	/* set ipu task in parameter */
	pIpuInfo->sInParam.width = width;
	pIpuInfo->sInParam.height = height;
	pIpuInfo->sInParam.fmt = v4l2_fourcc('I', '4', '2', '0');
	pIpuInfo->sInParam.input_crop_win.pos.x = 0;//(sInRect.nLeft + 7)/8*8;
	pIpuInfo->sInParam.input_crop_win.pos.y = 0;//sInRect.nTop;
	pIpuInfo->sInParam.input_crop_win.win_w = width; //(sInRect.nWidth - (sInParam.input_crop_win.pos.x - sInRect.nLeft))/8*8;
	pIpuInfo->sInParam.input_crop_win.win_h = height ;//sInRect.nHeight;

	/* set ipu task out parameter */
	pIpuInfo->sOutParam.width = width;
	pIpuInfo->sOutParam.height = height;
	pIpuInfo->sOutParam.fmt = v4l2_fourcc('R', 'G', 'B', 'P'); //omx2ipu_pxlfmt(eOutColorFmt);
	//ipuInfo.sOutParam.fmt =  v4l2_fourcc('R', 'G', 'B', '3'); // RGB888

	pIpuInfo->sOutParam.output_win.win_w = width;
	pIpuInfo->sOutParam.output_win.win_h = height;
	pIpuInfo->sOutParam.output_win.pos.x = 0;//((nOutWidth - width)/2 + 7)/8*8;
	pIpuInfo->sOutParam.output_win.pos.y = 0;

	/* set fb */	
	pIpuInfo->sOutParam.show_to_fb = 1;
	pIpuInfo->sOutParam.fb_disp.fb_num = 2;
	pIpuInfo->sOutParam.fb_disp.pos.x = 0;
	pIpuInfo->sOutParam.fb_disp.pos.y = 0;
	pIpuInfo->sOutParam.rot = 0;

	if(0==ipu_init_fb_device(fb_num, &fb_var))
	{
		FB_RENDER_PRINTF("%s: init fb device failure \r\n",__FUNCTION__);
		return 0;	
	}

	if((width > fb_var.xres )||(height>fb_var.yres))
	{
		pIpuInfo->sOutParam.width = fb_var.xres;
		pIpuInfo->sOutParam.height = fb_var.yres;
	}
	else
	{
		//ipuInfo.sOutParam.output_win.pos.x =  (fb_var.xres - width)/2;
		//ipuInfo.sOutParam.output_win.pos.y =  (fb_var.yres - height)/2;
		pIpuInfo->sOutParam.fb_disp.pos.x =  (fb_var.xres - width)/2;
		pIpuInfo->sOutParam.fb_disp.pos.y =  (fb_var.yres - height)/2;

		pIpuInfo->sOutParam.width = width;
		pIpuInfo->sOutParam.height = height;
	}

	FB_RENDER_PRINTF("IPU output: width: %d, height: %d \r\n", pIpuInfo->sOutParam.width, pIpuInfo->sOutParam.height);	

	/* ipu lib task init */
	// mode = OP_STREAM_MODE;	
	mode=OP_NORMAL_MODE;
	//ret = mxc_ipu_lib_task_init(&pIpuInfo->sInParam, NULL, &pIpuInfo->sOutParam, NULL, mode, &pIpuInfo->ipu_handle);
	ret = mxc_ipu_lib_task_init(&pIpuInfo->sInParam, NULL, &pIpuInfo->sOutParam, mode, &pIpuInfo->ipu_handle);
	if(ret < 0)
	{
		FB_RENDER_PRINTF("%s: mxc_ipu_lib_task_init failed!\n",__FUNCTION__);
		return 0;
	}	

	FB_RENDER_PRINTF("inbuf_start[0]: 0x%X , inbuf_start[1]: 0x%X \r\n",(unsigned int)pIpuInfo->ipu_handle.inbuf_start[0],(unsigned int)pIpuInfo->ipu_handle.inbuf_start[1]);
	//FB_RENDER_PRINTF("outbuf_start[0]: 0x%X , outbuf_start[1]: 0x%X \r\n",(unsigned int)pIpuInfo->ipu_handle.outbuf_start[0],(unsigned int)pIpuInfo->ipu_handle.outbuf_start[1]);

	// normal mode, and it is ipu to allocate input and output buff
	//pIpuInfo->outbuf[0]=(unsigned char*)pIpuInfo->ipu_handle.outbuf_start0[0]; //virtual address pointer to physical space
	ipu_update_input_buff(pIpuInfo, width,height,0);

	*pHandle=(int)pIpuInfo;
	return 1;
}


int fb_render_uninit(int handle)
{
	IPUInfo * pIpuInfo;

	pIpuInfo=(IPUInfo *)handle;
	mxc_ipu_lib_task_uninit(&pIpuInfo->ipu_handle);

	FR_RENDER_FREE(pIpuInfo);
	return 1;
}

int fb_render_drawYUVframe(int handle,unsigned char* pY,unsigned char* pU,unsigned char* pV,
	int width,int height)
{
	int ret;
	IPUInfo * pIpuInfo;
	int ySize,uvSize;;
	

	pIpuInfo=(IPUInfo *)handle;

	ySize=width*height;
	uvSize=ySize/4;
	
	//copy yuv data into IPU intput buff
	FB_RENDER_MEMCPY(pIpuInfo->YUV[0],pY,ySize);
	FB_RENDER_MEMCPY(pIpuInfo->YUV[1],pU,uvSize);
	FB_RENDER_MEMCPY(pIpuInfo->YUV[2],pV,uvSize);

	pIpuInfo->ipu_finish=0;

	// do CSC with IPU 
	while(1)
	{
		//ret = mxc_ipu_lib_task_buf_update(&pIpuInfo->ipu_handle, (int)pIpuInfo->YUV[0], 0, 0, ipu_output_cb, (void*)pIpuInfo);
		ret = mxc_ipu_lib_task_buf_update(&pIpuInfo->ipu_handle, 0, 0, 0, ipu_output_cb, (void*)pIpuInfo);
		if(ret>=0)
		{
			break;	
		}
		else
		{
			FB_RENDER_PRINTF("wait \r\n");
		}
	}

	//check status
	if(pIpuInfo->ipu_finish==0)
	{
		FB_RENDER_PRINTF("error: callback is not called \r\n");
		return 0;
	}
	else
	{
		//FB_RENDER_PRINTF("IPU CSC Finished ! update in index: %d, output index: %d \r\n",ret,pIpuInfo->nOutBufIdx);	
	}    	

	//update IPU input buff
	ipu_update_input_buff(pIpuInfo, width,height,ret);

	return 1;
	
}
#else
#include "stdio.h"
int fb_render_init(int* pHandle,int fb_num,int width , int height)
{
	printf("%s: not implemented ! \r\n",__FUNCTION__);
	return 1;
}


int fb_render_uninit(int handle)
{
	printf("%s: not implemented ! \r\n",__FUNCTION__);
	return 1;
}

int fb_render_drawYUVframe(int handle,unsigned char* pY,unsigned char* pU,unsigned char* pV,
	int width,int height)
{
	printf("%s: not implemented ! \r\n",__FUNCTION__);
	return 1;
}

#endif

