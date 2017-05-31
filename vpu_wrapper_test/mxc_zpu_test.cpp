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
 * @file mxc_zpu_test.cpp
 *
 * @brief Mxc Video For Linux 2 video codec driver test application.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <error.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <argp.h>
#include "detect_device.h"
#include "file_input.h"
#include "output.h"
#include "v4l2_codec.h"
#include "common.h"
#include "log.h"
#include "vpu_wrapper_decoder.h"

#define START_MEDIATIME_INFO_THREAD(thread, output)\
  do{\
    if (thread == NULL){\
      exit_thread = false;\
      pthread_create(&(thread), NULL, display_media_time, (output));\
    }\
  }while(0)

#define STOP_MEDIATIME_INFO_THREAD(thread)\
  do{\
    if((thread && exit_thread == false)) {\
      exit_thread = true;\
      pthread_join ((thread), NULL);\
      (thread)=NULL;\
    }\
  }while(0)


#define START_SHOW_MEDIATIME_INFO \
  do{\
    bstartmediatime = true;\
  }while(0) 

#define STOP_SHOW_MEDIATIME_INFO \
  do{\
    bstartmediatime = false;\
  }while(0) 


FILE *pLogFile = NULL;
int nLogLevel = LOG_LEVEL_DEBUG;

static pthread_t media_time_thread = NULL;
static bool exit_thread = false;
static bool bstartmediatime = false;

static volatile sig_atomic_t quit_flag = 0;

const char *argp_program_version =
  "mxc_zpu_test 0.1";
const char *argp_program_bug_address =
  "Multimedia Team <shmmmw@freescale.com>";

/* Program documentation. */
static char doc[] = "mxc_zpu_test -- Mxc Video For Linux 2\
                     video codec driver test application.";

/* A description of the arguments we accept. */
static char args_doc[] = " --help --list --encoder/--decoder --codec_mode ... --input_mode ... --input_file ... --input_meta ... --output_mode ... --output_file ... --output_meta ... --output_file_ref ... --output_meta_ref ... --memory_mode ... --count ... --seek_to ... --format ... --width ... --height ... --fps_n ... --fps_d ... --yuv_format .. --bitrate ... --gop ... --quantization ... --interactive --verbose";

/* The options we understand. */
static struct argp_option options[] = {
  {"verbose",  'v', 0, 0,
   "Print more information, stream information and performance data" },
  {"list",  'l', 0, 0,
   "List all V4L2 video codec device and show all capability" },
  {"interactive",  'a', 0, 0, "Can accept command for seek and exit" },
  {"encoder",  'e', 0, 0, "Encoder test case" },
  {"decoder",  'd', 0, 0, "Decoder test case" },
  {"codec_mode",   'k', "number", 0,
   "Video codec V4L2 mode: 0(default) for single plane, 1 for multiplane."},
  {"input_mode",   'i', "number", 0,
   "Input can be file"},
  {"input_file",   'n', "FILE",  0,
   "Input stream. Compressed video for decoder, raw video for encoder" },
  {"input_meta",   'p', "FILE",  0,
   "Input time stamp and frame position for decoder" },
  {"output_mode",   'o', "number", 0,
   "Output can be file or display onto screen: 0 (default) for screen, 1 for file"},
  {"output_file",   'u', "FILE",  0,
   "Output stream. Raw video for decoder, compressed video for encoder" },
  {"output_meta",   't', "FILE",  0,
   "Output video information and output time stamp for decoder" },
  {"output_file_ref",   'r', "FILE",  0,
   "Output stream refence. Raw video for decoder" },
  {"output_meta_ref",   'z', "FILE",  0,
   "Output video information and output time stamp for decoder" },
  {"memory_mode",   'm', "number", 0,
   "Memory mode can be MMAP, USERPTR or DMA: 1 (default) for MMAP, 2 for USERPTR, 4 for DMA"},
  {"count",   'c', "number", 0,
  "Number of frames to decoder/encoder"},
  {"seek_to",   's', "number", 0,
  "Seek to the position, can be percentage of input video file (xx%) or accurate time (xxx) ms"},
  {"format",   'x', "number", 0,
  "Video compress format, FOURCC format, default is H264"},
  {"width",   'w', "number", 0, "Video width"},
  {"height",   'h', "number", 0, "Video height"},
  {"fps_n",   'f', "number", 0, "Video frame rate"},
  {"fps_d",   'j', "number", 0, "Video frame rate"},
  {"yuv_format",   'y', "number", 0,
    "Rew video format, FOURCC format, default is YUV420"},
  {"repeat",   'r', "number", 0,
   "Repeat the output COUNT (default 10) times"},
  {"vpu",   'W', "number", 0,
     "run vpu decoder test"},
  {0,0,0,0, "The following options are only for video encoder:" },
  {"bitrate",   'b', "number", 0, "bitrate in kbps"},
  {"gop",   'g', "number", 0, "GOP size"},
  {"quantization",   'q', "number", 0,
   "Quantization parameter for encoder"},
  { 0 }
};

/* Used by main to communicate with parse_opt. */
struct arguments
{
  int verbose, list, interactive, encoder, decoder;
  int codec_type;
  int codec_mode;
  int input_mode;
  char *input_file, *input_meta;
  int output_mode;
  char *output_file, *output_meta;
  char *output_file_ref, *output_meta_ref;
  int memory_mode;
  int count;
  int seek_portion;
  int seek_timems;
  char *format;
  int width, height, fps_n, fps_d;
  char *yuv_format;
  int bitrate, gop, quantization;
  int vpu_test;
};

static void signal_handler(int signum)
{
  switch(signum)
  {
    case SIGINT:
    case SIGUSR1:
      quit_flag = 1;
      break;
    case SIGTTIN:
      /* Nothing need do */
      break;
    default:
      break;
  }
}

/* Parse a single option. */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  /* Get the input argument from argp_parse, which we
     know is a pointer to our arguments structure. */
  struct arguments *arguments = (struct arguments *)state->input;

  switch (key)
    {
    case 'v':
      arguments->verbose = 1;
      nLogLevel = LOG_LEVEL_DEBUG;
      break;
    case 'l':
      arguments->list = 1;
      break;
    case 'a':
      arguments->interactive = 1;
      break;
    case 'e':
      arguments->encoder = 1;
      break;
    case 'd':
      arguments->decoder = 1;
      break;
    case 'k':
      arguments->codec_mode = atoi (arg);
      break;
    case 'i':
      arguments->input_mode = atoi (arg);
      break;
    case 'n':
      arguments->input_file = arg;
      break;
    case 'p':
      arguments->input_meta = arg;
      break;
    case 'o':
      arguments->output_mode = atoi (arg);
      break;
    case 'u':
      arguments->output_file = arg;
      break;
    case 't':
      arguments->output_meta = arg;
      break;
    case 'r':
      arguments->output_file_ref = arg;
      break;
    case 'z':
      arguments->output_meta_ref = arg;
      break;
    case 'm':
      arguments->memory_mode = atoi (arg);
      break;
    case 'c':
      arguments->count = atoi (arg);
      break;
    case 's':
      {
        if (arg[2]=='%'){
          arg[2] = 0;
          arguments->seek_portion = atoi(arg);
          if(arguments->seek_portion<0 || arguments->seek_portion>100  ) {
            LOG_ERROR ("Invalid seek point!\n");
          }
        }else{
          arguments->seek_timems = atoi(arg);
        }
      }
      break;
    case 'x':
      arguments->format = arg;
      break;
    case 'w':
      arguments->width = atoi (arg);
      break;
    case 'h':
      arguments->height = atoi (arg);
      break;
    case 'f':
      arguments->fps_n = atoi (arg);
      break;
    case 'j':
      arguments->fps_d = atoi (arg);
      break;
    case 'y':
      arguments->yuv_format = arg;
      break;
    case 'b':
      arguments->bitrate = atoi (arg);
      break;
    case 'g':
      arguments->gop = atoi (arg);
      break;
    case 'q':
      arguments->quantization = atoi (arg);
      break;
    case 'W':
      arguments->vpu_test = atoi (arg);
      LOG_ERROR ("enable vpu test\n");
      break;
    case ARGP_KEY_INIT:
      break;

    case ARGP_KEY_NO_ARGS:
      break;
      //argp_usage (state);
 
    case ARGP_KEY_ARG:
      /* Here we know that state->arg_num == 0, since we
         force argument parsing to end before any more arguments can
         get here. */
      //arguments->arg1 = arg;

      /* Now we consume all the rest of the arguments.
         state->next is the index in state->argv of the
         next argument to be parsed, which is the first string
         we’re interested in, so we can just use
         &state->argv[state->next] as the value for
         arguments->strings.

         In addition, by setting state->next to the end
         of the arguments, we can force argp to stop parsing here and
         return. */
      //arguments->strings = &state->argv[state->next];
      state->next = state->argc;

      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

/* Our argp parser. */
static struct argp argp = { options, parse_opt, args_doc, doc };

static void * display_media_time (void *arg)
{
  output *moutput = 	(output *)arg;
  int64 media_time;
  unsigned int hours;
  unsigned int minutes;
  unsigned int seconds;

  while(exit_thread == false) {
    if (bstartmediatime) {
      media_time = 0;
      if(media_time = moutput->get_mediatime ()) {
        hours = (media_time/1000) / 3600;
        minutes = (media_time/ (60*1000)) % 60;
        seconds = ((media_time %(3600*1000)) % (60*1000))/1000;
        printf("\r[Current Media Time] %03d:%02d:%02d", 
            hours, minutes, seconds);
        fflush(stdout);
      }
      usleep (500000);
    }
    else
      usleep (50000);
  }

  return NULL;
}

static void main_menu()
{
  printf("\nSelect Command:\n");
  printf("\t[s]Seek\n");
  printf("\t[x]Exit\n\n");
}

static bool setup_codec_test (codec_api * api,input *minput,
    output *moutput, struct arguments *arguments)
{
  Format mformat = {0};

  if (!api) {
    LOG_ERROR ("new v4l2_codec fail.\n");
    return NULL;
  }

  mformat.codec_type = arguments->codec_type;
  mformat.codec_mode = arguments->codec_mode;
  mformat.input_file = arguments->input_file;
  mformat.input_meta = arguments->input_meta;
  mformat.output_mode = arguments->output_mode;
  mformat.output_file = arguments->output_file;
  mformat.output_meta = arguments->output_meta;
  mformat.output_file_ref = arguments->output_file_ref;
  mformat.output_meta_ref = arguments->output_meta_ref;
  mformat.memory_mode = arguments->memory_mode;
  mformat.count = arguments->count;
  mformat.seek_portion = arguments->seek_portion;
  mformat.seek_timems = arguments->seek_timems;
  mformat.format = arguments->format;
  mformat.width = arguments->width;
  mformat.height = arguments->height;
  mformat.fps_n = arguments->fps_n;
  mformat.fps_d = arguments->fps_d;
  mformat.yuv_format = arguments->yuv_format;
  mformat.bitrate = arguments->bitrate;
  mformat.gop = arguments->gop;
  mformat.quantization = arguments->quantization;

  if (!minput->set_format (&mformat)) {
    LOG_ERROR ("input set_format fail.\n");
    return false;
  }
  if (!moutput->set_format (&mformat)) {
    LOG_ERROR ("output set_format fail.\n");
    return false;
  }

  api->set_input (minput);
  api->set_output (moutput);

  if (!api->set_format (&mformat)) {
    LOG_ERROR ("vpu wrapper set_format fail.\n");
    return false;
  }
 
  return true;
}

int
main (int argc, char **argv)
{
  struct arguments arguments = {0};
  detect_device *mdetect_device = NULL;
  file_input *minput = NULL;
  output *moutput = NULL;
  char *device;
  bool bexit = false;
  bool read_input = true;
  char rep[128];
  int mdevice = 0;
  int i, j;
  bool bEnableVpu = false;
  codec_api * codec_api = NULL;

  struct sigaction act;
  act.sa_handler = signal_handler;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  sigaction(SIGINT, &act, NULL);
  sigaction(SIGTTIN, &act, NULL);
  /* SIGUSR1 used to report EOS and the stop */
  sigaction(SIGUSR1, &act, NULL);

  arguments.memory_mode = 1;
  arguments.count = INVALID_PARAM;
  arguments.seek_portion = INVALID_PARAM;
  arguments.seek_timems = INVALID_PARAM;
  arguments.yuv_format = "NV12";
  /* Parse our arguments; every option seen by parse_opt will be
     reflected in arguments. */
  argp_parse (&argp, argc, argv, 0, 0, &arguments);

  if(arguments.vpu_test > 0)
    bEnableVpu = true;

  pLogFile = fopen("vpu.log", "w");
  if (!pLogFile) {
    LOG_ERROR ("open log file fail");
    goto bail;
  }

  if(!bEnableVpu){
      mdetect_device = new detect_device ();
      if (mdetect_device == NULL) {
        LOG_ERROR ("new detect_device fail.\n");
        goto bail;
      }


      if (arguments.list) {
        mdetect_device->list_codec ();
        goto bail;
      }
  }

  if (arguments.decoder) {
    arguments.codec_type = CODEC_TYPE_DECODER;
  } else if (arguments.encoder) {
    arguments.codec_type = CODEC_TYPE_ENCODER;
  } else {
    LOG_ERROR ("don't know test vide encoder or decoder.\n");
    goto bail;
  }

  if(!bEnableVpu){
      device = mdetect_device->get_device (arguments.codec_type);
      if (device == NULL) {
        LOG_ERROR ("Can't find required device.\n");
        goto bail;
      }

      mdevice = mdetect_device->open_device (device);
      if (mdevice <= 0) {
        LOG_ERROR ("Can't open device: %s.\n", device);
        goto bail;
      }
  }

  minput = new file_input ();
  if (minput == NULL) {
    LOG_ERROR ("new input fail.\n");
    goto bail;
  }

  moutput = new output ();
  if (moutput == NULL) {
    LOG_ERROR ("new input fail.\n");
    goto bail;
  }

  if(bEnableVpu){
    codec_api = new vpu_wrapper_decoder(0);
  }else{
    codec_api = new v4l2_codec (mdevice);
  }

  if(!setup_codec_test(codec_api,minput, moutput, &arguments)){
    LOG_ERROR ("setup_codec_test.\n");
    goto bail;
  }
    

  LOG_INFO ("setup vpu_wrapper_test video codec successfully\n");

  if (!codec_api->start ()) {
    LOG_ERROR ("v4l2_codec start fail.\n");
    goto bail;
  }


  if (arguments.interactive)
    START_MEDIATIME_INFO_THREAD(media_time_thread, moutput);
  START_SHOW_MEDIATIME_INFO;

  while(bexit == false) {
    {
      if (read_input){
        if (arguments.interactive)
          main_menu();
        scanf("%s", rep);
        usleep (500000);
      }
      read_input=true;
      if (quit_flag) {
        STOP_SHOW_MEDIATIME_INFO;
        codec_api->stop ();
        bexit = true;
      }
      else if(rep[0] == 's') {
        int seek_portion;
        int seek_timems;
        LOG_INFO ("Seek to the position, can be percentage of input video ");
        LOG_INFO ("file (xx%) or accurate time (xxx) ms.\n");
        scanf("%s", rep);
        if (rep[2]=='%'){
          rep[2] = 0;
          seek_portion = atoi(rep);
          if(seek_portion<0 || seek_portion>100  ) {
            LOG_ERROR ("Invalid seek point!\n");
          } else
            codec_api->seek (seek_portion, INVALID_PARAM);
        }else{
          seek_timems = atoi(rep);
          codec_api->seek (INVALID_PARAM, seek_timems);
        }
      }
      else if(rep[0] == 'x')
      {
        STOP_SHOW_MEDIATIME_INFO;

        codec_api->stop ();
        bexit = true;
      }
      else if(rep[0] == '*') {
        sleep(1);
      }
      else if(rep[0] == '#') {
        sleep(10);
      }
    }
  }

  STOP_MEDIATIME_INFO_THREAD(media_time_thread);

bail:
  if (mdevice)
    mdetect_device->close_device (mdevice);
  if (mdetect_device)
    delete mdetect_device;
  if (codec_api)
    delete codec_api;
  if (minput)
    delete minput;
  if (moutput)
    delete moutput;
  if (pLogFile)
    fclose (pLogFile);

  exit (0);
}

