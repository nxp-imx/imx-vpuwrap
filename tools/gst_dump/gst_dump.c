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
 * @file gst_dump.cpp
 *
 * @brief Dump tool based on GStreamer for ZPU unit test.
 *
 */

#include <gst/gst.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define LOG_ERROR g_print
#define LOG_INFO g_print
#define LOG_DEBUG g_print
#define LOG_LOG

typedef struct _Dump {
  FILE *in;
  FILE *in_meta;
  FILE *out;
  FILE *out_meta;
  gboolean dump_raw;
  int nIsAvcc;/*only for H.264 format*/
  int nNalSizeLen;
  int nNalNum; /*added for nal_size_length = 1 or 2*/
  int nIsHvcc;
} Dump;

int VpuDetectAvcc(unsigned char* pCodecData, unsigned int nSize, int * pIsAvcc, int * pNalSizeLength,int* pNalNum)
{
	*pIsAvcc=0;
	if(pCodecData[0]==1){
		int nalsizelen=(pCodecData[4]&0x3)+1;
		/*possible nal size length: 1,2,3,4*/
		*pIsAvcc=1;
		*pNalSizeLength=nalsizelen;
		*pNalNum=0;
	}
	return 1;
}
int VpuDetectHvcc(unsigned char* pCodecData, unsigned int nSize, int * pIsHvcc, int * pNalSizeLength,int* pNalNum)
{
	*pIsHvcc=0;
    pCodecData[0]=1;
	if(pCodecData[0]==1){
		int nalsizelen=(pCodecData[21]&0x3)+1;
		/*possible nal size length: 1,2,3,4*/
		LOG_DEBUG("hvcc format is detected, nal_size_length % d \r\n",nalsizelen);
		*pIsHvcc=1;
		*pNalSizeLength=nalsizelen;
		*pNalNum=0;
	}
	return 1;
}


int VpuConvertAvccHeader(unsigned char* pCodecData, unsigned int nSize, unsigned char** ppOut, unsigned int * pOutSize)
{
	/*will allocate and return one new buffer, caller is responsible to free it  */
	unsigned char* p=pCodecData;
	unsigned char* pDes;
	unsigned char* pSPS, *pPPS;
	int spsSize,ppsSize;
	int numPPS, outSize=0;
	unsigned char* pTemp;
	int tempBufSize=0;
	/* [0]: version */
	/* [1]: profile */
	/* [2]: profile compat */
	/* [3]: level */
	/* [4]: 6 bits reserved (111111) + 2 bits nal size length - 1 (11) */
	/* [5]: 3 bits reserved (111) + 5 bits number of sps (00001) */
	/*[6,7]: 16bits: sps_size*/
	/*sps data*/
	/*number of pps*/
	/*16bits: pps_size*/
	/*pps data */
	if(nSize<8){
		goto corrupt_header;
	}
	spsSize=(p[6]<<8)|p[7];
	p+=8;
	pSPS=p;
	p+=spsSize;
	if(p>=pCodecData+nSize){
		goto corrupt_header;
	}
	numPPS=*p++;

	LOG_DEBUG("spsSize: %d , num of PPS: %d \r\n",spsSize, numPPS);
	tempBufSize=nSize+2*numPPS;
	pTemp=malloc(tempBufSize); 
	if(pTemp==NULL){
		LOG_ERROR("error: malloc %d bytes fail !\r\n", tempBufSize);
		*ppOut=pCodecData;
		*pOutSize=nSize;
		return 0;
	}
	pDes=pTemp;
	pDes[0]=pDes[1]=pDes[2]=0; /*fill start code*/
	pDes[3]=0x1;
	pDes+=4;
	memcpy(pDes,pSPS,spsSize); /*fill sps*/
	pDes+=spsSize;
	outSize+=4+spsSize;
	while(numPPS>0){
		if((p+2) > (pCodecData+nSize)){
			goto corrupt_header;
		}
		ppsSize=(p[0]<<8)|p[1];
		p+=2;
		pPPS=p;
		outSize+=4+ppsSize;
		if(outSize>tempBufSize){
			LOG_ERROR("error: convert avcc header overflow ! \r\n");
			*ppOut=pTemp;
			*pOutSize=(outSize-4-ppsSize);
			return 0;
		}
		LOG_DEBUG("fill one pps: %d bytes \r\n", ppsSize);
		pDes[0]=pDes[1]=pDes[2]=0; /*fill start code*/
		pDes[3]=0x1;
		pDes+=4;
		memcpy(pDes,pPPS,ppsSize); /*fill pps*/
		pDes+=ppsSize;
		numPPS--;
		p+=ppsSize;
	}
	*ppOut=pTemp;
	*pOutSize=outSize;
	return 1;

corrupt_header:
	LOG_ERROR("error: codec data corrupted ! \r\n");
	*ppOut=pCodecData;
	*pOutSize=nSize;
	return 0;
}

int VpuScanAvccFrameNalNum(unsigned char* pData, unsigned int nSize, int nNalSizeLength)
{
	int leftSize=nSize;
	unsigned char* p=pData;
	unsigned int dataSize;
	int num=0;
	
	while(leftSize>0){
		if(((p+nNalSizeLength) > (pData+nSize)) || (p < pData)){
			goto corrupt_data;
		}
		
		if(nNalSizeLength==4){
			dataSize=(p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
			p+=dataSize+4;
			leftSize-=dataSize+4;
		}
		else if(nNalSizeLength==3){
			dataSize=(p[0]<<16)|(p[1]<<8)|p[2];
			p+=dataSize+3;
			leftSize-=dataSize+3;
		}
		else if(nNalSizeLength==2){
			dataSize=(p[0]<<8) |p[1];
			p+=dataSize+2;
			leftSize-=dataSize+2;
		}
		else {
			dataSize=p[0];
			p+=dataSize+1;
			leftSize-=dataSize+1;
		}
		num++;
	}
	if(leftSize!=0){
		goto corrupt_data;
	}
	LOG_DEBUG("nal number: %d \r\n",num);
	return num;
	
corrupt_data:
	LOG_ERROR("error: the nal data corrupted ! can't scan the nal number \r\n");
	return 0;
}

int VpuConvertAvccFrame(unsigned char* pData, unsigned int nSize, int nNalSizeLength, unsigned char** ppFrm, unsigned int* pSize, int * pNalNum)
{
	/*for nal size length 3 or 4: will change the nalsize with start code , the buffer size won't be changed
	   for nal size length 1 or 2: will re-malloc new frame data	*/
	int leftSize=nSize;
	unsigned char * p=pData;
	unsigned char* pEnd;
	unsigned char* pStart;
	unsigned char* pOldFrm=NULL;
	unsigned int nNewSize=0;
	unsigned char* pNewFrm=NULL;

	*ppFrm=pData;
	*pSize=nSize;
	pStart=pData;
	pEnd=pData+nSize;
	
	if(nNalSizeLength<3){
		int nNalNum;
		nNalNum=VpuScanAvccFrameNalNum(pData,nSize,nNalSizeLength);
		if(nNalNum==0){
			return 0;
		}
		if(((*pNalNum)!=0) && ((*pNalNum)!=nNalNum)){
			/*if nNalNum not fixed value, we need to consider how to update consumed size in VpuAccumulateConsumedBytes() ? */
			LOG_ERROR("warning: the num of nal not fixed in every frame, previous: %d, new: %d \r\n",*pNalNum,nNalNum);
		}
		*pNalNum=nNalNum;
		nNewSize=nSize+(4-nNalSizeLength)*nNalNum;
		pNewFrm=malloc(nNewSize);
		if(pNewFrm==NULL){
			LOG_ERROR("malloc failure: %d bytes \r\n",nNewSize);
			return 0;
		}
		pStart=pNewFrm;
		pEnd=pNewFrm+nNewSize;
		p=pNewFrm;
		pOldFrm=pData;
		leftSize=nNewSize;
	}

	while(leftSize>0){
		unsigned int dataSize;

		if(nNalSizeLength==4){
			if(((p+4) > pEnd) || (p < pStart)){
				goto corrupt_data;
			}
			dataSize=(p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
			p[0]=p[1]=p[2]=0;
			p[3]=0x1; /*fill 4 bytes of startcode*/
			p+=dataSize+4;
			leftSize-=dataSize+4;
		}
		else if(nNalSizeLength==3){
			if(((p+3) > pEnd) || (p < pStart)){
				goto corrupt_data;
			}
			dataSize=(p[0]<<16)|(p[1]<<8)|p[2];
			p[0]=p[1]=0;
			p[2]=0x1;  /*fill 3 bytes of startcode*/
			p+=dataSize+3;
			leftSize-=dataSize+3;
		}
		else if(nNalSizeLength==2){
			if(((p+4) > pEnd) || (p < pStart) || (pOldFrm<pData) || ((pOldFrm+2)>(pData+nSize))){
				goto corrupt_data;
			}
			dataSize=(pOldFrm[0]<<8) |pOldFrm[1];
			p[0]=p[1]=p[2]=0;
			p[3]=0x1; /*fill 4 bytes of startcode*/
			p+=4;
			pOldFrm+=2;
			if((dataSize > (unsigned int)(pEnd-p)) || (dataSize>(unsigned int)(pData+nSize-pOldFrm))){
				goto corrupt_data;
			}
			memcpy(p,pOldFrm,dataSize);
			p+=dataSize;
			pOldFrm+=dataSize;
			leftSize-=dataSize+4;
		}
		else{ /*1 byte*/
			if(((p+4) > pEnd) || (p < pStart) || (pOldFrm<pData) || ((pOldFrm+1)>(pData+nSize))){
				goto corrupt_data;
			}
			dataSize=pOldFrm[0];
			p[0]=p[1]=p[2]=0;
			p[3]=0x1; /*fill 4 bytes of startcode*/
			p+=4;
			pOldFrm+=1;
			if((dataSize > (unsigned int)(pEnd-p)) || (dataSize>(unsigned int)(pData+nSize-pOldFrm))){
				goto corrupt_data;
			}
			memcpy(p,pOldFrm,dataSize);
			p+=dataSize;
			pOldFrm+=dataSize;
			leftSize-=dataSize+4;
		}
		LOG_LOG ("fill one %d bytes of start code for nal data(%d bytes) \r\n", nNalSizeLength,dataSize);
	}
	if(leftSize!=0){
		goto corrupt_data;
	}

	if(nNalSizeLength<3){
		*ppFrm=pNewFrm;
		*pSize=nNewSize;
	}
	return 1;

corrupt_data:
	LOG_ERROR("error: the nal data corrupted ! \r\n");
	return 0;
}
int VpuConvertHvccHeader(unsigned char* pCodecData, unsigned int nSize, unsigned char** ppOut, unsigned int * pOutSize)
{
	/*will allocate and return one new buffer, caller is responsible to free it  */
	unsigned char* p=pCodecData;
	unsigned char* pDes;
	unsigned char* pTemp=NULL;
  int i, j, outSize=0, size, numArray, numNal, length;

	if(nSize<23){
		goto corrupt_header;
	}
	pTemp=malloc(nSize); 
	if(pTemp==NULL){
		LOG_ERROR("error: malloc %d bytes fail !\r\n", nSize);
		*ppOut=pCodecData;
    *pOutSize=nSize;
    return 0;
  }

	pDes=pTemp;
  p += 22;
  size = nSize - 22;

  numArray = (char)p[0];
  p += 1;
  size -= 1;

  for (i = 0; i < numArray; i++) {
    if (size < 3) {
      goto corrupt_header;
    }
    p += 1;
    size -= 1;

    numNal = (p[0]<<8) | p[1];

    p += 2;
    size -= 2;

    for (j = 0; j < numNal; j++) {
      if (size < 2) {
        goto corrupt_header;
      }
      length = (p[0]<<8) |p[1];

      p += 2;
      size -= 2;

      if (size < length) {
        goto corrupt_header;
      }
			pDes[0]=pDes[1]=pDes[2]=0;
			pDes[3]=0x1; /*fill 4 bytes of startcode*/
      pDes+=4;
      outSize+=4;


      memcpy(pDes, p, length);
      pDes+=length;
      outSize+=length;

      p += length;
      size -= length;
    }
  }
	*ppOut=pTemp;
	*pOutSize=outSize;
	return 1;

corrupt_header:
	LOG_ERROR("error: codec data corrupted ! \r\n");
	*ppOut=pCodecData;
	*pOutSize=nSize;
	if(pTemp){
		free(pTemp);
	}
	return 0;
}
int VpuConvertHvccFrame(unsigned char* pData, unsigned int nSize,
    int nNalSizeLength, unsigned char** ppFrm, unsigned int* pSize, int * pNalNum)
{
	/*for nal size length 3 or 4: will change the nalsize with start code,
   * the buffer size won't be changed for nal size length 1 or 2: will
   * re-malloc new frame data	*/
	int leftSize=nSize;
	unsigned char * p=pData;
	unsigned char* pEnd;
	unsigned char* pStart;
	unsigned char* pOldFrm=NULL;
	unsigned int nNewSize=0;
	unsigned char* pNewFrm=NULL;

	*ppFrm=pData;
	*pSize=nSize;
	pStart=pData;
	pEnd=pData+nSize;
	if(nNalSizeLength != 4)
        return 0;

	while(leftSize>0){
		unsigned int dataSize;

		if(nNalSizeLength==4){
			if(((p+4) > pEnd) || (p < pStart)){
				goto corrupt_data;
			}
			dataSize=(p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
			p[0]=p[1]=p[2]=0;
			p[3]=0x1; /*fill 4 bytes of startcode*/
			p+=dataSize+4;
			leftSize-=dataSize+4;
		}
		LOG_DEBUG("fill one %d bytes of start code for nal data(%d bytes) \r\n", nNalSizeLength,dataSize);
	}
	if(leftSize!=0){
		goto corrupt_data;
	}

	if(nNalSizeLength<3){
		*ppFrm=pNewFrm;
		*pSize=nNewSize;
	}
	return 1;

corrupt_data:
	LOG_ERROR("error: the nal data corrupted ! \r\n");
	if(pNewFrm){
		free(pNewFrm);
	}
	return 0;
}
static GstPadProbeReturn
event_probe (GstPad * pad, GstPadProbeInfo * info, gpointer data)
{
  Dump *mdump = (Dump *) data;
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
  gboolean is_sink = GST_PAD_IS_SINK(pad);

  g_print ("event probe [%s]\n", GST_EVENT_TYPE_NAME (event));
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS: {
      GstCaps *caps = NULL;
      const gchar *name;
      const GstStructure *s;
      const GValue *value_list;
      const GValue *value;
      const gchar *str;
      char output_file_name[32];
      char output_file_name_meta[32];
      gint width, height, framerate_numerator, framerate_denominator;
      const GValue *codec_data;
      GstBuffer *buffer;
      GstMapInfo minfo;
      gint size;
      char *subfix;

      gst_event_parse_caps (event, &caps);
      s = gst_caps_get_structure (caps, 0);

      str = gst_structure_get_string (s, "format");
      if (str == NULL) {
        name = gst_structure_get_name (s);
        if (name == NULL) {
          g_print ("format unknow.\n");
        } else {
          str = name + 8;
        }
      }
      g_print ("event_probe [format]=%s \n",str);
      if (!gst_structure_get_int (s, "width", (gint *)(&width))) {
        value_list = gst_structure_get_value (s, "width");
        value = gst_value_list_get_value (value_list, 0);

        width = g_value_get_int (value);
      }
      if (!gst_structure_get_int (s, "height", (gint *)(&height))) {
        value_list = gst_structure_get_value (s, "height");
        value = gst_value_list_get_value (value_list, 0);

        height = g_value_get_int (value);
      }
      if (!gst_structure_get_fraction (s, "framerate",
            &framerate_numerator, &framerate_denominator)) {
        value_list = gst_structure_get_value (s, "framerate");
        value = gst_value_list_get_value (value_list, 0);

        framerate_numerator = gst_value_get_fraction_numerator (value);
        framerate_denominator = gst_value_get_fraction_denominator (value);
      }
      sprintf (output_file_name, "test_stream_%dx%d.%s", width, height, str);
      sprintf (output_file_name_meta, "test_stream_%dx%d.%s_meta", width, height, str);
      subfix = strrchr (output_file_name, '.');
      if (subfix) {
        char *pch = subfix;
        while (*pch != '\0') {
          *pch = tolower (*pch);
          pch++; 
        }   
      }
      subfix = strrchr (output_file_name_meta, '.');
      if (subfix) {
        char *pch = subfix;
        while (*pch != '\0') {
          *pch = tolower (*pch);
          pch++; 
        }   
      }

      if (is_sink) {
        if (!mdump->in) {
          mdump->in = fopen(output_file_name, "w");
          if (!mdump->in) {
            g_print ("open data file fail: %s", output_file_name);
          }
        }
        if (!mdump->in_meta) {
          mdump->in_meta = fopen(output_file_name_meta, "w");
          if (!mdump->in_meta) {
            g_print ("open data file fail: %s", output_file_name);
          }
        }

        codec_data = gst_structure_get_value (s, "codec_data");
        if (codec_data && G_VALUE_TYPE (codec_data) == GST_TYPE_BUFFER) {
          guchar *pHeader;
          gint headerLen;
          buffer = GST_BUFFER (g_value_dup_boxed (codec_data));
          gst_buffer_map (buffer, &minfo, GST_MAP_READ);
          if(!strncmp (str, "h264", 4) && 0 == mdump->nIsAvcc){
            VpuDetectAvcc(minfo.data, minfo.size, &mdump->nIsAvcc,
                &mdump->nNalSizeLen, &mdump->nNalNum);
          }else if(!strncmp (str, "h265", 4) && 0 == mdump->nIsHvcc){
            g_print("VpuDetectHvcc \n");
            VpuDetectHvcc(minfo.data, minfo.size, &mdump->nIsHvcc,
                &mdump->nNalSizeLen, &mdump->nNalNum);
            g_print("VpuDetectHvcc nIsHvcc=%d\n",mdump->nIsHvcc);
          }

          if(mdump->nIsAvcc) {
            VpuConvertAvccHeader(minfo.data, minfo.size, &pHeader,&headerLen);
          }else if(mdump->nIsHvcc){
            g_print("VpuConvertHvccHeader \n");
            VpuConvertHvccHeader(minfo.data, minfo.size, &pHeader,&headerLen);
            g_print("VpuConvertHvccHeader header len=%d\n",headerLen);
          } else {
            g_print("copy header \n");
            pHeader=minfo.data;
            headerLen=minfo.size;
          }
          if (mdump->in_meta) {
            fprintf (mdump->in_meta, "PTS: %lld\n", 0);
            fprintf (mdump->in_meta, "DTS: %lld\n", 0);
            fprintf (mdump->in_meta, "offset: %lld\n", 0);
            fprintf (mdump->in_meta, "length: %d\n", headerLen);
            fprintf (mdump->in_meta, "key frame: %d\n", 1);
          }
          if (mdump->in) {
            size = fwrite (pHeader, 1, headerLen, mdump->in);
            if (size != headerLen) {
              g_print ("fwrite fail: %d\n", errno);
            }
          }
          gst_buffer_unmap (buffer, &minfo);
          if(mdump->nIsAvcc)
            free (pHeader);
          if(mdump->nIsHvcc)
            free (pHeader);
        }
      } else {
        if (!mdump->out && mdump->dump_raw) {
          mdump->out = fopen(output_file_name, "w");
          if (!mdump->out) {
            g_print ("open data file fail: %s", output_file_name);
          }
        }
        if (!mdump->out_meta) {
          mdump->out_meta = fopen(output_file_name_meta, "w");
          if (!mdump->out_meta) {
            g_print ("open data file fail: %s", output_file_name);
          }
        }
      }
      break;
    }
    default:
      break;
  }
  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
buffer_probe (GstPad * pad, GstPadProbeInfo * info, gpointer data)
{
  Dump *mdump = (Dump *) data;
  GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);
  gboolean is_sink = GST_PAD_IS_SINK(pad);
  GstMapInfo minfo;
  gint size;

  gst_buffer_map (buffer, &minfo, GST_MAP_READ);

  if (is_sink) {
		if(mdump->nIsAvcc){
			unsigned char* pFrm=NULL;
			unsigned int nFrmSize;
      guchar *tmp = malloc (minfo.size);
      memcpy (tmp, minfo.data, minfo.size);
			VpuConvertAvccFrame(tmp, minfo.size, mdump->nNalSizeLen, &pFrm, &nFrmSize,
          &mdump->nNalNum);
      if (mdump->in) {
        size = fwrite (pFrm, 1, nFrmSize, mdump->in);
        if (size != minfo.size) {
          g_print ("fwrite fail: %d\n", errno);
        }
      }
			if(pFrm!=tmp){
				free(pFrm);
			}
      free (tmp);
		}
		else if(mdump->nIsHvcc){
			unsigned char* pFrm=NULL;
			unsigned int nFrmSize;
      guchar *tmp = malloc (minfo.size);
      memcpy (tmp, minfo.data, minfo.size);
			VpuConvertHvccFrame(tmp, minfo.size, mdump->nNalSizeLen, &pFrm, &nFrmSize,
          &mdump->nNalNum);
      if (mdump->in) {
        size = fwrite (pFrm, 1, nFrmSize, mdump->in);
        if (size != minfo.size) {
          g_print ("fwrite fail: %d\n", errno);
        }
      }
			if(pFrm!=tmp){
				free(pFrm);
			}
      free (tmp);
		}
		else{
          if (mdump->in) {
            size = fwrite (minfo.data, 1, minfo.size, mdump->in);
            if (size != minfo.size) {
              g_print ("fwrite fail: %d\n", errno);
            }
          }
		}
    if (mdump->in_meta) {
      fprintf (mdump->in_meta, "PTS: %lld\n", GST_BUFFER_PTS (buffer));
      fprintf (mdump->in_meta, "DTS: %lld\n", GST_BUFFER_DTS (buffer));
      fprintf (mdump->in_meta, "offset: %lld\n", ftell (mdump->in) - size);
      fprintf (mdump->in_meta, "length: %d\n", size);
      fprintf (mdump->in_meta, "key frame: %d\n", !GST_BUFFER_FLAG_IS_SET(buffer,GST_BUFFER_FLAG_DELTA_UNIT));
    }
  } else {
    if (mdump->out) {
      size = fwrite (minfo.data, 1, minfo.size, mdump->out);
      if (size != minfo.size) {
        g_print ("fwrite fail: %d\n", errno);
      }
    }
    if (mdump->out_meta) {
      fprintf (mdump->out_meta, "PTS: %lld\n", GST_BUFFER_PTS (buffer));
    }
  }
  gst_buffer_unmap (buffer, &minfo);
  return GST_PAD_PROBE_OK;
}

static GstBusSyncReply
bus_sync_handler (GstBus * bus, GstMessage * message, gpointer data)
{
  gboolean isvideodec = FALSE;

      /* we only care about video decoder state change messages */
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_STATE_CHANGED: {
      GstElement *video_decoder = GST_ELEMENT (GST_MESSAGE_SRC (message));
      GstElementFactory *factory = gst_element_get_factory (video_decoder);

      if (factory)
        isvideodec = gst_element_factory_list_is_type (factory,
            GST_ELEMENT_FACTORY_TYPE_DECODER |
            GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO |
            GST_ELEMENT_FACTORY_TYPE_MEDIA_IMAGE);

      if (isvideodec && video_decoder) {
        GstState oldstate, newstate, pending;
        gst_message_parse_state_changed (message, &oldstate, &newstate, &pending);
        g_print ("Found video decode\n");
        if (oldstate == GST_STATE_NULL && newstate == GST_STATE_READY) {
            GstPad *pad;

            pad = gst_element_get_static_pad (video_decoder, "sink");
            gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, event_probe,
                data, NULL);
            gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER, buffer_probe,
                data, NULL);
            gst_object_unref (pad);

            pad = gst_element_get_static_pad (video_decoder, "src");
            gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, event_probe,
                data, NULL);
            gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER, buffer_probe,
                data, NULL);
            gst_object_unref (pad);
        }
      }
      break;
    }
    default:
      break;
  }
  return GST_BUS_PASS;
}

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:{
      g_print ("End-of-stream\n");
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_ERROR:{
      gchar *debug;
      GError *err;

      gst_message_parse_error (msg, &err, &debug);
      g_printerr ("Debugging info: %s\n", (debug) ? debug : "none");
      g_free (debug);

      g_print ("Error: %s\n", err->message);
      g_error_free (err);

      g_main_loop_quit (loop);

      break;
    }
    default:
      break;
  }
  return TRUE;
}

gint
main (gint argc, gchar * argv[])
{
  GstElement *playbin, *fakesink;
  GMainLoop *loop;
  GstBus *bus;
  guint bus_watch_id;
  gchar *uri;
  Dump mdump = {0};

  gst_init (&argc, &argv);

  if (argc < 2) {
    g_print ("usage: %s <media file or uri> [--dump_raw]\n", argv[0]);
    return 1;
  }

  playbin = gst_element_factory_make ("playbin", NULL);
  if (!playbin) {
    g_print ("'playbin' gstreamer plugin missing\n");
    return 1;
  }
  g_object_set (playbin, "flags", 0x61, NULL);

  /* take the commandline argument and ensure that it is a uri */
  if (gst_uri_is_valid (argv[1]))
    uri = g_strdup (argv[1]);
  else
    uri = gst_filename_to_uri (argv[1], NULL);
  g_object_set (playbin, "uri", uri, NULL);
  g_free (uri);

  fakesink = gst_element_factory_make ("fakesink", NULL);
  if (!fakesink) {
    g_print ("'fakesink' gstreamer plugin missing\n");
    return 1;
  }
  g_object_set (playbin, "video-sink", fakesink, NULL);
  g_object_unref (fakesink);

  fakesink = gst_element_factory_make ("fakesink", NULL);
  if (!fakesink) {
    g_print ("'fakesink' gstreamer plugin missing\n");
    return 1;
  }
  g_object_set (playbin, "audio-sink", fakesink, NULL);
  g_object_unref (fakesink);

  /* create and event loop and feed gstreamer bus mesages to it */
  loop = g_main_loop_new (NULL, FALSE);

  bus = gst_element_get_bus (playbin);
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_bus_set_sync_handler (bus, bus_sync_handler, (gpointer) &mdump, NULL);
  g_object_unref (bus);

  /* start play back and listed to events */
  gst_element_set_state (playbin, GST_STATE_PLAYING);
  g_main_loop_run (loop);

  /* cleanup */
  gst_element_set_state (playbin, GST_STATE_NULL);
  g_object_unref (playbin);
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);
  if (mdump.in)
    fclose (mdump.in);
  if (mdump.in_meta)
    fclose (mdump.in_meta);
  if (mdump.out)
    fclose (mdump.out);
  if (mdump.out_meta)
    fclose (mdump.out_meta);

  return 0;
}

