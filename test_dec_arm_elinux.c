/*
 *  Copyright (c) 2010-2014, Freescale Semiconductor Inc.,
 *  Copyright 2019-2020 NXP
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 *
 */

/*
 *	test_dec_arm_elinux.c
 *	vpu unit test application
 *
 *	History :
 *	Date	(y.m.d)		Author			Version			Description
 *	2010-09-14		eagle zhou		0.1				Created
 */

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "decode_stream.h"

#ifdef APP_DEBUG
#define APP_DEBUG_PRINTF printf
#else
#define APP_DEBUG_PRINTF
#endif

//#define NULL  (void*)0

#define NAME_SIZE 256

#define DEFAULT_FILL_DATA_UNIT	(3*1024*1024)
#define DEFAULT_DELAY_BUFSIZE		(-1)

#define CASE_FIRST(x)   if (strncmp(argv[0], x, strlen(x)) == 0)
#define CASE(x)         else if (strncmp(argv[0], x, strlen(x)) == 0)
#define DEFAULT         else

typedef struct
{
	char 	infile[NAME_SIZE];	// -i
	char 	outfile[NAME_SIZE];	// -o
	char    codecdatafile[NAME_SIZE];  // -a

	int     isavcc;
	int     saveYUV;		// -o
	int     loop;			//-l
	int     maxnum;		// -n
	int     display;		// -d
	int     fbno;			// -d

	int     repeatnum;	// -r
	int	 offset;		// -r
	int     skipmode;		// -s

	int     codec;			// -f
	int     width;			// -w
	int     height;			// -h

	int     unitsize;            // -test S,N
	int     unitnum;           // -test S,N
	int     delaysize;         // -c

	int     reset;                // reset vpu
	int     interleave;        // -interleave
	int     map;                 // -map
	int     tile2linear;       // -tile2linear
}
IOParams;

static void usage(char*program)
{
	APP_DEBUG_PRINTF("\nUsage: %s [options] -i bitstream_file -f format \n", program);
	APP_DEBUG_PRINTF("\nUsage: %s [options] -i avcc_file -a codec_data -f format \n", program);
	APP_DEBUG_PRINTF("options:\n"
		   "	-o <file_name>	:Save decoded output in YUV 4:2:0 format\n"
		   "			[default: no save]\n"
		   "	-l <loop_count>	:decode the same clip repeatedly: will call all vpu api\n"
		   "			[default:  1]\n"
		   "	-n <frame_num>	:decode max <frame_num> frames\n"
		   "			[default: all frames will be decoded]\n"
		   "	-d <fb_no>	:use frame buffer <fb_no> for render.\n"
		   "	-r num,offset	:repeate 'num' times, and seek to 'offset' bytes location for every repeat.(default:0,0)\n"
		   "	-s <skip_mode>	:skip frames.(default:0) \n"
		   "			skip PB:	1 \n"
		   "			skip B:		2 \n"
		   "			skip ALL:	3 \n"
		   "			I search:	4 \n"
		   "	-f <codec>	:set codec format with <codec>. For 8mm/8mq, only support H264 and HEVC\n"
		   "			Mpeg2:	1 \n"
		   "			Mpeg4:	2 \n"
		   "			DIVX3:	3 \n"
		   "			DIVX4:	4 \n"
		   "			DIVX56:	5 \n"
		   "			XVID:	6 \n"
		   "			H263:	7 \n"
		   "			H264:	8 \n"
		   "			VC1:	9 \n"
		   "			RV:	10 \n"
		   "			JPG:	11 \n"
		   "			AVS:	12 \n"
		   "			VP8:	13 \n"
		   "			MVC:	14 \n"
		   "			HEVC:	15 \n"
		   "	-test S,N	:(internal test): user set size of unit data (S) and numbers of unit data(N)\n"
		   "	-c <delay_size>	:delay buffer size(bytes) for stream mode\n"
		   "			[default: using internal default size]\n"
		   "	-reset 		:reset vpu (all instances).\n"
		   "	-interleave <interleave>	:yuv chroma interleave: 0(default)--no interleave; 1--interleave \n"
		   "	-map <map>			:register frame type: 0(default)--linear ; 1--frame tile ; 2--field tile \n"
		   "	-tile2linear <tile2linear>	:tile to linear enable (valid when map!=0): 0(default)--tile output; 1--yuv output \n"		   
#if 0
		   "	-w		:width (for DivX3).\n"
		   "	-h		:height(for DivX3).\n"
#endif		   
		   );
	exit(0);
}

static void GetUserInput(IOParams *pIO, int argc, char *argv[])
{
	int	bitFileDone = 0;

	argc--;
	argv++;

	while (argc)
	{
		if (argv[0][0] == '-')
		{
			CASE_FIRST("-o")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					strcpy((char *)pIO->outfile, argv[0]);
					pIO->saveYUV = 1;
				}
			}
			CASE("-l")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->loop);
				}
			}
			CASE("-n")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->maxnum);
				}
			}
			CASE("-f")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->codec);
				}
			}
#if 0
			CASE("-w")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->width);
				}
			}
			CASE("-h")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->height);
				}
			}			
#endif
			CASE("-d")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->fbno);
				}			
				pIO->display = 1;
			}
			CASE("-reset")
			{
				pIO->reset=1;
			}
			CASE("-r")
			{
				argc--;
				argv++;
				if (sscanf(argv[0], "%d,%d", &pIO->repeatnum,&pIO->offset) != 2)
				{
					usage(pIO->infile);
				}
				APP_DEBUG_PRINTF("repeat %d times, seek location %d(0x%X) \r\n",pIO->repeatnum,pIO->offset,pIO->offset);
			}
			CASE("-s")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->skipmode);
				}			
				if(pIO->skipmode>4) 
				{
					usage(pIO->infile);
				}
			}
			CASE("-test")
			{
				argc--;
				argv++;
				if (sscanf(argv[0], "%d,%d", &pIO->unitsize,&pIO->unitnum) != 2)
				{
					usage(pIO->infile);	
				}
				APP_DEBUG_PRINTF("user set: unit data size: %d, unit data number: %d \r\n",pIO->unitsize,pIO->unitnum);
			}
			CASE("-c")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->delaysize);
				}
			}
			CASE("-interleave")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->interleave);
				}
			}
			CASE("-i")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					strcpy((char *)pIO->infile, argv[0]);
					bitFileDone = 1;
				}
			}
			CASE("-a")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					strcpy((char *)pIO->codecdatafile, argv[0]);
					pIO->isavcc = 1;
				}
			}
			CASE("-map")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->map);
				}
			}
			CASE("-tile2linear")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->tile2linear);
				}
			}
			DEFAULT                             // Has to be last
			{
				APP_DEBUG_PRINTF("Unsupported option %s\n", argv[0]);
				usage(pIO->infile);
			}
		}
		else
		{
			APP_DEBUG_PRINTF("Unsupported option %s\n", argv[0]);
			usage(pIO->infile);
		}
		argc--;
		argv++;
	}
	
	if ((!bitFileDone)&&(0==pIO->reset))
	{
		usage(pIO->infile);
	}
}

int main(int argc, char **argv)
{
	IOParams ioParams;
	DecContxt decContxt;
	FILE* fout=NULL;
	FILE* fin=NULL;
	FILE* fcodecdata=NULL;
	int loop_cnt=0;
	
	// Defaults: 0
	memset(&ioParams,0,sizeof(IOParams));
	// set maxnum to infinity
	ioParams.maxnum = 0x7FFFFFFF;
	ioParams.unitnum=0x7FFFFFFF;
	ioParams.unitsize=DEFAULT_FILL_DATA_UNIT;
	ioParams.delaysize=DEFAULT_DELAY_BUFSIZE; /*-1: don't set it, using internal default value*/
	ioParams.loop=1;
	ioParams.interleave=0;//default: no interleave
	ioParams.map=0;	//default: using linear format
	ioParams.tile2linear=0;	//default: no additional convert

	//get input from user
	GetUserInput(&ioParams, argc, argv);
	if(ioParams.reset!=0)
	{
		decode_reset();
		return 0;
	}

	if(argc < 5)
	{
		usage(argv[0]);
		return -1;
	}

REPEAT:
	//open in/out files
	fin = fopen(ioParams.infile, "rb");
	if(fin==NULL)
	{
		APP_DEBUG_PRINTF("can not open input file %s.\n", ioParams.infile);
		return -1;
	}

	if(ioParams.saveYUV)
	{
		fout = fopen(ioParams.outfile, "wb");
		if(NULL==fout)
		{
			APP_DEBUG_PRINTF("can not open output file %s.\n", ioParams.outfile);
			return -1;
		}
	}

	if(ioParams.isavcc)
	{
		fcodecdata = fopen(ioParams.codecdatafile, "rb");
		if(fcodecdata==NULL)
		{
			APP_DEBUG_PRINTF("can not open codecdata file %s.\n", ioParams.codecdatafile);
			return -1;
		}
	}

	APP_DEBUG_PRINTF("input bitstream(%d) avccstream(%d) : %s \r\n",!ioParams.isavcc,ioParams.isavcc,ioParams.infile);
	APP_DEBUG_PRINTF("max frame_number : %d, display: %d \r\n",ioParams.maxnum, ioParams.display);

	//decode
	decContxt.fin=fin;
	decContxt.fout=fout;
	decContxt.fcodecdata=fcodecdata;
	decContxt.isavcc=ioParams.isavcc;
	decContxt.nMaxNum=ioParams.maxnum;
	decContxt.nDisplay=ioParams.display;
	decContxt.nFbNo=ioParams.fbno;	
	decContxt.nCodec=ioParams.codec;
	//decContxt.nInWidth=ioParams.width;
	//decContxt.nInHeight=ioParams.height;	
	decContxt.nSkipMode=ioParams.skipmode;
	decContxt.nDelayBufSize=ioParams.delaysize; 
	decContxt.nRepeatNum=ioParams.repeatnum;
	decContxt.nOffset=ioParams.offset;
	decContxt.nUnitDataSize=ioParams.unitsize;
	decContxt.nUintDataNum=ioParams.unitnum;
	decContxt.nChromaInterleave=ioParams.interleave;
	decContxt.nMapType=ioParams.map;
	decContxt.nTile2LinearEnable=ioParams.tile2linear;
	decode_stream(&decContxt);

	APP_DEBUG_PRINTF("Frame Num: %d,  [width x height] = [%d x %d], dec FPS: %d, total FPS: %d \r\n",decContxt.nFrameNum,decContxt.nWidth,decContxt.nHeight,decContxt.nDecFps,decContxt.nTotalFps);
	if(decContxt.nErr)
	{
		APP_DEBUG_PRINTF("Decode Failure \r\n");
	}
	else
	{
		APP_DEBUG_PRINTF("Decode OK: repeat %d times, skip mode: %d \r\n",ioParams.repeatnum,ioParams.skipmode);
	}

	//release 
	if(fout)
	{
		fclose(fout);
	}
	if(fin)
	{
		fclose(fin);
	}

	loop_cnt++;
	if(loop_cnt<ioParams.loop)
	{
		goto REPEAT;
	}
	
	return 0;
}

