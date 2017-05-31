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

#ifndef __V4L2_CODEC_H__
#define __V4L2_CODEC_H__

#include <pthread.h>
#include "input.h"
#include "output.h"
#include "v4l2.h"
#include "codec_api.h"

class v4l2_codec : public codec_api {
  public:
    v4l2_codec (int fd);
    virtual ~v4l2_codec ();
    bool set_input (input *input);
    bool set_output (output *output);
    bool set_format (Format *format);
    bool start ();
    bool seek (int portion, int64 timems);
    bool stop ();
    void *input_thread ();
    void *output_thread ();
  private:
    int mfd;
    v4l2 *mv4l2_output, *mv4l2_capture;
    pthread_t input_thread_id, output_thread_id;
    input *minput;
    output *moutput;
    Format mformat;
    int seek_portion;
    int64 seek_timems;
    int mcount;
    bool bseek;
    bool bstop;
};

#endif
