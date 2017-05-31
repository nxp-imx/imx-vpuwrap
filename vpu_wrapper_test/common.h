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

#ifndef __COMMON_H__
#define __COMMON_H__

typedef long long  int64;

#define CHECK_NULL(in)\
  do{\
    if(!in) {\
      LOG_ERROR ("NULL input parameter.\n");\
      return false;\
    }\
  }while(0)

#define INVALID_TIME (-1ll)
#define INVALID_PARAM (-1)

#define MAX_V4L2_BUFFERS 32
#define MAX_V4L2_PLANES 4

#define CODEC_TYPE_ENCODER  0x00000001
#define CODEC_TYPE_DECODER  0x00000002

typedef enum {
  CODEC_MODE_SINGLE_PLANE,
  CODEC_MODE_MULTI_PLANE
} CODEC_MODE;

typedef struct _Rect {
  int top;
  int left;
  int width;
  int height;
} Rect;

typedef struct _Format {
  int codec_type;
  int codec_mode;
  int input_mode;
  char *input_file, *input_meta;
  int output_mode;
  char *output_file, *output_meta;
  char *output_file_ref, *output_meta_ref;
  int memory_mode;
  int min_buffers;
  int plane_num;
  int count;
  int seek_portion;
  int seek_timems;
  char *format;
  int width, height, fps_n, fps_d;
  int crop_top, crop_left, crop_width, crop_height;
  char *yuv_format;
  int yuv_format_v4l2;
  int bitrate, gop, quantization;
  int stride[MAX_V4L2_PLANES];
  int image_size[MAX_V4L2_PLANES];
} Format;

typedef struct _Buffer {
  int plane_num;
  char *data[MAX_V4L2_PLANES];
  int alloc_size[MAX_V4L2_PLANES];
  int size[MAX_V4L2_PLANES];
  int64 pts; /* time stamp in ms */
  int64 dts; /* time stamp in ms */
  int v4l2_index;
} Buffer;


#endif
