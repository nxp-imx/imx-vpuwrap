/*
 *  Copyright (c) 2010-2012, Freescale Semiconductor Inc.,
 *	Copyright 2020, 2026 NXP
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *
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

