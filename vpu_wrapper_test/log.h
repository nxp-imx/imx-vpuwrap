/*
 * Copyright 2015 Freescale Semiconductor, Inc. All rights reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*
 * @log.h
 *
 * @brief ZPU unit test log
 *
 */

#ifndef __LOG_H__
#define __LOG_H__

#include <stdio.h>
#include <stdlib.h>

typedef enum {
  LOG_LEVEL_NONE = 0,
  LOG_LEVEL_ERROR,
  LOG_LEVEL_WARNING,
  LOG_LEVEL_INFO,
  LOG_LEVEL_APIINFO,
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_BUFFER,
  LOG_LEVEL_LOG,
  /* add more */
  LOG_LEVEL_COUNT
} LogLevel;

extern int nLogLevel;
extern FILE *pLogFile;

#define START do
#define END while (0)

#define LOG(LEVEL, ...)      START{ \
	if (nLogLevel >= LEVEL) { \
		if (NULL != pLogFile) { \
			fprintf((FILE *)pLogFile, "LEVEL: %d FILE: %s FUNCTION: %s LINE: %d ", LEVEL, \
					__FILE__, __FUNCTION__, __LINE__); \
			fprintf((FILE *)pLogFile, __VA_ARGS__); \
			fflush((FILE *)pLogFile); \
		} \
		else { \
			fprintf(stdout, "LEVEL: %d FILE: %s FUNCTION: %s LINE: %d ", LEVEL, __FILE__, \
					__FUNCTION__, __LINE__); \
			fprintf(stdout, __VA_ARGS__); \
			fflush(stdout); \
		} \
	} \
}END

#define LOG_ERROR(...)   LOG(LOG_LEVEL_ERROR, __VA_ARGS__) 
#define LOG_WARNING(...) LOG(LOG_LEVEL_WARNING, __VA_ARGS__) 
#define LOG_INFO(...)    LOG(LOG_LEVEL_INFO, __VA_ARGS__) 
#define LOG_DEBUG(...)   LOG(LOG_LEVEL_DEBUG, __VA_ARGS__) 
#define LOG_BUFFER(...)  LOG(LOG_LEVEL_BUFFER, __VA_ARGS__) 
#define LOG_LOG(...)     LOG(LOG_LEVEL_LOG, __VA_ARGS__) 


	//if (debug>=1) fprintf(stderr, "error: "); fprintf(stderr, fmt, ##arg)

#endif
