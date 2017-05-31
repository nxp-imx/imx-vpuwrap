#ifndef __CODEC_API_H__
#define __CODEC_API_H__


#include <pthread.h>
#include "input.h"
#include "output.h"

class codec_api{
	public:
		codec_api();
		~codec_api();
		virtual bool set_input (input *input){return false;};
		virtual bool set_output (output *output){return false;};
		virtual bool set_format (Format *format){return false;};
	    virtual bool start (){return false;};
	    virtual bool seek (int portion, int64 timems){return false;};
		virtual bool stop (){return false;};
		virtual void *input_thread (){};
	    virtual void *output_thread (){};
};

#endif