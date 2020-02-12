/*
 *  Copyright (c) 2010-2012, Freescale Semiconductor Inc.,
 *	Copyright 2020 NXP
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 *
 */

/*
 *  vpu_wrapper_timer.h
 *	header file contain all related struct info in timer.c
 *
 *	History :
 *	Date	(y.m.d)		Author			Version			Description
 *	2010-12-10		eagle zhou		0.1				Created
 */


#ifndef VPU_WRAPPER_TIMER_H
#define VPU_WRAPPER_TIMER_H

void timer_init();
void timer_mark(int id);
void timer_start(int id);
void timer_stop(int id);
void timer_mark_report(int id);
void timer_report(int id);

#endif  //#ifndef VPU_WRAPPER_TIMER_H

