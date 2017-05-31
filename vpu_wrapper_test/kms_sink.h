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

#ifndef __KMS_SINK_H__
#define __KMS_SINK_H__

#include <xf86drm.h>
#include <xf86drmMode.h>
#include "sink.h"
#include "common.h"

typedef struct _KMSMemory {
  uint32_t fb_id;
  uint32_t handle[4];
  uint32_t pitch;
  void *bo[4];
  void *vaddr[4];
} KMSMemory;

class kms_sink : public sink {
  public:
    kms_sink ();
    virtual ~kms_sink ();
    bool set_format (Format *format);
    bool alloc_buffer (Buffer *buf);
    bool put_buffer (Buffer *buf);
    int64 get_mediatime ();
    bool flush ();
    bool free_buffer (Buffer *buf);
  private:
    bool set_format_sink (Format *format);
    bool put_buffer_sink (Buffer *buf);
    bool find_mode_and_plane (Rect * dim);
    KMSMemory *kms_alloc (int size);
    void kms_free (KMSMemory * mem);
    void *kms_map (KMSMemory * mem);
    void kms_unmap (KMSMemory * mem);
    void kms_reset ();
    Format mformat;
    int fd;
    int crtc_id;
    drmModeRes *resources;
    drmModePlaneRes *plane_resources;
    drmModeConnector *connector;
    drmModeEncoder *encoder;
    drmModeModeInfo *mode;
    drmModePlane *plane;
    struct kms_driver *driver;
    KMSMemory *pbuf[MAX_V4L2_BUFFERS][MAX_V4L2_PLANES];
    Rect dest;
    char *device;
    int connector_id;
    int64 mediatime;
    bool need_copy;
};

#endif
