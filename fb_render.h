/*
 *  Copyright (c) 2010-2012, Freescale Semiconductor Inc.,
 *  Copyright 2020 NXP
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 *
 */

/*
 *  fb_render.h
 *	header file for fb_render.c 
 *
 *	History :
 *	Date	(y.m.d)		Author			Version			Description
 *	2010-09-14		eagle zhou		0.1				Created
 */


#ifndef FB_RENDER_H
#define FB_RENDER_H

int fb_render_init(int* pHandle,int fb_num,int width , int height);
int fb_render_uninit(int handle);
int fb_render_drawYUVframe(int handle,unsigned char* pY,unsigned char* pU,unsigned char* pV, 
	int width,int height);

#endif  //FB_RENDER_H

