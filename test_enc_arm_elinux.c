/*
 *  Copyright (c) 2010-2014, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

/*
 *	test_enc_arm_elinux.c
 *	vpu unit test application
 *	History :
 *	Date	(y.m.d)		Author			Version			Description
 *	2010-12-31		eagle zhou		0.1				Created
 */

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "encode_stream.h"

#ifdef APP_DEBUG
#define APP_DEBUG_PRINTF printf
#else
#define APP_DEBUG_PRINTF
#endif

//#define NULL  (void*)0

#define NAME_SIZE 256

#define CASE_FIRST(x)   if (strncmp(argv[0], x, strlen(x)) == 0)
#define CASE(x)         else if (strncmp(argv[0], x, strlen(x)) == 0)
#define DEFAULT         else

typedef struct
{
	char 	infile[NAME_SIZE];	// -i
	char 	outfile[NAME_SIZE];	// -o

	int     saveBitstream;	// -o
	int     maxnum;		// -n

	int     codec;			// -f
	int     width;			// -w
	int     height;			// -h
	int     rotation;		// -r
	int     repeatnum;		// -p
	int     interleave;		// -interleave
	int     map;			// -map
	int     linear2tile;		// -linear2tile
	int     color;			// -color
	int     bitrate;			// -bitrate
	int     gop;			// -gop
	int     quan;			// -quan
	int     framerate;		// -framerate
	int     simpleApi;		//-simple
	int     gamma;		//-gamma
	int     initialdelay;	//-initial
	int     refreshratio;	//-refreshratio
	int     refreshmode;	//-refreshmode
}
IOParams;

int CallBack_EncOneFrameOK(void* pApp,unsigned char* pFrmBits,int frameLen)
{
	APP_DEBUG_PRINTF("%s: one frame is OK , length: %d \r\n",__FUNCTION__,frameLen);
#if 0	
{
	int i,j;
	printf("addr: 0x%X \r\n",(unsigned int)pFrmBits);
	for(i=0;i<1;i++)
	{
		for(j=0;j<32;j++)
		{
			printf("%2X ",pFrmBits[j]);         
		}
		printf("\r\n");
		pFrmBits+=32;
	}
	printf("\r\n");	
}
#endif
	return 1;
}

static void usage(char*program)
{
	APP_DEBUG_PRINTF("\nUsage: %s [options] -i yuv_file -w width -h height \n", program);
	APP_DEBUG_PRINTF("options:\n"
		   "	-o <file_name>	:Save encoded bitstream \n"
		   "			[default: no save]\n"
		   "	-n <frame_num>	:encode max <frame_num> frames\n"
		   "			[default: all frames will be encoded]\n"
		   "	-f <codec>	:set codec format with <codec>.\n"
		   "			Mpeg4:	0 (default)\n"
		   "			H263:	1 \n"
		   "			H264:	2 \n"
		   "			MJPG:	3 \n"
		   "	-w <width>	:input picture width \n"
		   "	-h <height>	:input picture height \n"
		   "	-r <rotation>	:rotation, only support 0(default),90,180,270 \n"
		   "	-p <rp_times>	:repeate <rp_times> times.(default:0)\n"		   
		   "	-interleave <interleave>:yuv chroma interleave: 0(default)--no interleave; 1--interleave \n"
		   "	-map <map>		:map type: 0(default)--linear ; 1--frame tile; 2--field tile \n"
		   "	-linear2tile <linear2tile>:linear to tile enable(valid when map!=0): 0(default)--tile input; 1--yuv input \n"
		   "	-color <colorformat>	:color format: 0--420(default); 1--422H; 2--422V; 3--444; 4--400 \n"
		   "	-bitrate <bitrate>	:bit rate, unit is kbps (default 0--auto) \n"
		   "	-gop <gopsize>		:gop size (default 15) \n"
		   "	-quan <quantization>	:quantization value (default 10) \n"
		   "	-framerate <framerate>	:frame rate (default 30) \n"
		   "	-simple		:call encoder simple API(default: 1) \n"
		   "	-gamma		:gamma value(only valid when simple=0):  default (0.75*32768)  \n"
		   "	-initial	:initial dealy(only valid when simple=0): default 0 \n"
		   "	-refreshratio	:intra refresh percentage: default 0, range is [0,100] \n"
		   "	-refreshmode	:intra refresh mode: 0(default)--normal; 1-cyclic \n"
		   );
	exit(0);
}

static void GetUserInput(IOParams *pIO, int argc, char *argv[])
{
	int bitFileDone = 0;
	int validWidth=0;
	int validHeight=0;

	argc--;
	argv++;

	while (argc)
	{
		if (argv[0][0] == '-')
		{
			CASE_FIRST("-interleave")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->interleave);
				}
			}
			CASE("-initial")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->initialdelay);
				}
			}
			CASE("-refreshratio")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->refreshratio);
				}
				if(pIO->refreshratio>100){
					usage(pIO->infile);
				}
			}
			CASE("-refreshmode")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->refreshmode);
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
			CASE("-o")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					strcpy((char *)pIO->outfile, argv[0]);
					pIO->saveBitstream= 1;
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
			CASE("-framerate")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->framerate);
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
			CASE("-w")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->width);
					validWidth=1;
				}
			}
			CASE("-h")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->height);
					validHeight=1;
				}
			}		
			CASE("-r")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->rotation);
				}
			}					
			CASE("-p")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->repeatnum);
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
			CASE("-linear2tile")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->linear2tile);
				}
			}
			CASE("-color")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->color);
				}
			}
			CASE("-bitrate")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->bitrate);
				}
			}
			CASE("-quan")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->quan);
				}
			}
			CASE("-gop")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->gop);
				}
			}
			CASE("-simple")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->simpleApi);
				}
			}
			CASE("-gamma")
			{
				argc--;
				argv++;
				if (argv[0] != NULL)
				{
					sscanf(argv[0], "%d", &pIO->gamma);
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
	
	if ((0==bitFileDone)||(0==validWidth)||(0==validHeight))
	{
		usage(pIO->infile);
	}
}

int main(int argc, char **argv)
{
	IOParams ioParams;
	EncContxt encContxt;
	int noerr=1;
	FILE* fout=NULL;
	FILE* fin=NULL;
	
	// Defaults: 0
	memset(&ioParams,0,sizeof(IOParams));
	// set maxnum to infinity
	ioParams.maxnum = 0x7FFFFFFF;
	ioParams.interleave=0;//default: no interleave
	ioParams.map=0;	//default: using linear format
	ioParams.linear2tile=0;
	ioParams.color=0;	//default: 420
	ioParams.bitrate=0;
	ioParams.quan=10;
	ioParams.gop=15;
	ioParams.framerate=30;
	ioParams.simpleApi=1;
	ioParams.gamma=(int)(0.75*32768);         /*  (0*32768 <= gamma <= 1*32768) */
	ioParams.initialdelay=0;
	ioParams.refreshratio=0;
	ioParams.refreshmode=0;

	//get input from user
	GetUserInput(&ioParams, argc, argv);

	//open in/out files
	fin = fopen(ioParams.infile, "rb");
	if(fin==NULL)
	{
		APP_DEBUG_PRINTF("can not open input file %s.\n", ioParams.infile);
		return -1;
	}

	if(ioParams.saveBitstream)
	{
		fout = fopen(ioParams.outfile, "wb");
		if(NULL==fout)
		{
			APP_DEBUG_PRINTF("can not open output file %s.\n", ioParams.outfile);
			return -1;
		}
	}

	APP_DEBUG_PRINTF("input YUV file : %s \r\n",ioParams.infile);
	APP_DEBUG_PRINTF("max frame number : %d  \r\n",ioParams.maxnum);

	//encode
	memset(&encContxt,0,sizeof(EncContxt));
	encContxt.fin=fin;
	encContxt.fout=fout;
	encContxt.nMaxNum=ioParams.maxnum;
	encContxt.nCodec=ioParams.codec;
	encContxt.nPicWidth=ioParams.width;
	encContxt.nPicHeight=ioParams.height;
	encContxt.nRotAngle=ioParams.rotation;
	encContxt.nFrameRate=ioParams.framerate;
	encContxt.nBitRate=ioParams.bitrate;
	encContxt.nGOPSize=ioParams.gop;
	encContxt.nChromaInterleave=ioParams.interleave;
	encContxt.nMirror=0;
	encContxt.nMapType=ioParams.map;
	encContxt.nLinear2TiledEnable=ioParams.linear2tile;
	encContxt.nColor=ioParams.color;
	encContxt.nQuantParam=ioParams.quan;
	encContxt.pApp=NULL;	//don't consider reentry
	encContxt.pfOneFrameOk=CallBack_EncOneFrameOK;
	encContxt.pfOneFrameBeg=NULL;
	encContxt.pfOneFrameEnd=NULL;

	encContxt.nRcIntraQp=-1;
	encContxt.nUserGamma=ioParams.gamma;
	encContxt.nH263_annexJEnable=1;
	//encContxt.nRcIntervalMode=1;
	encContxt.nAvc_constrainedIntraPredFlag=1;
	//encContxt.nAvc_Intra16x16Only=1;

	encContxt.nRepeatNum=ioParams.repeatnum;
	encContxt.nSimpleApi=ioParams.simpleApi;
	encContxt.nInitialDelay=ioParams.initialdelay;
	encContxt.nIntraRefresh=(0==ioParams.refreshratio)?0:(ioParams.width*ioParams.height*ioParams.refreshratio/256/100);
	encContxt.nIntraRefreshMode=ioParams.refreshmode;
	if(encContxt.nSimpleApi==0){
		APP_DEBUG_PRINTF("gamma: %d, initial delay: %d, intra refresh: %d, intra refresh mode: %d \r\n",
			encContxt.nUserGamma,encContxt.nInitialDelay,encContxt.nIntraRefresh,encContxt.nIntraRefreshMode);
	}
	noerr=encode_stream(&encContxt);
	APP_DEBUG_PRINTF("Frame Num: %d,  [width x height] = [%d x %d], enc FPS: %d, total FPS: %d \r\n",encContxt.nFrameNum,encContxt.nPicWidth,encContxt.nPicHeight,encContxt.nEncFps,encContxt.nTotalFps);
	if((0==noerr) || (0!=encContxt.nErr))
	{
		APP_DEBUG_PRINTF("Encode Failure \r\n");
	}
	else
	{
		APP_DEBUG_PRINTF("Encode OK  \r\n");
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

	return 0;
}

