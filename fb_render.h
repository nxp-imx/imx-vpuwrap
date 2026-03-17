/*
 *  Copyright (c) 2010-2012, Freescale Semiconductor Inc.,
 *  Copyright 2020, 2026 NXP
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 */


#ifndef FB_RENDER_H
#define FB_RENDER_H

int fb_render_init(int* pHandle,int fb_num,int width , int height);
int fb_render_uninit(int handle);
int fb_render_drawYUVframe(int handle,unsigned char* pY,unsigned char* pU,unsigned char* pV, 
	int width,int height);

#endif  //FB_RENDER_H

