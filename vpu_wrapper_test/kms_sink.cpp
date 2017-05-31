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

#include <unistd.h>
#include <stdlib.h>
#include <libkms/libkms.h>
#include <drm_fourcc.h>
#include <linux/videodev2.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "kms_sink.h"
#include "log.h"

kms_sink::kms_sink ()
{
  int i, j;
  connector_id = 34;

  encoder = NULL;
  connector = NULL;
  driver = NULL;
  plane = NULL;
  plane_resources = NULL;
  resources = NULL;
  fd = -1;
  mediatime = 0;
  need_copy = false;
  for (i = 0; i < MAX_V4L2_BUFFERS; i ++) {
    for (j = 0; j < MAX_V4L2_PLANES; j++) {
      pbuf[i][j] = NULL;
    }
  }
}

kms_sink::~kms_sink ()
{
  kms_reset ();
}

bool kms_sink::find_mode_and_plane (Rect * dim)
{
  drmModePlane *plane_tmp;
  int i, pipe;
  bool ret;

  ret = false;

  /* First, find the connector & mode */
  connector = drmModeGetConnector (fd, connector_id);
  if (!connector)
    goto error_no_connector;

  if (connector->count_modes == 0)
    goto error_no_mode;

  /* Now get the encoder */
  encoder = drmModeGetEncoder (fd, connector->encoder_id);
  if (!encoder)
    goto error_no_encoder;

  /* XXX: just pick the first available mode, which has the highest
   * resolution. */
  mode = &connector->modes[0];

  dim->top = dim->left = 0;
  dim->width = mode->hdisplay;
  dim->height = mode->vdisplay;
  LOG_INFO ("connector mode = %dx%d", dim->width, dim->height);

  crtc_id = encoder->crtc_id;

  /* and figure out which crtc index it is: */
  pipe = -1;
  for (i = 0; i < resources->count_crtcs; i++) {
    if (crtc_id == (int) resources->crtcs[i]) {
      pipe = i;
      break;
    }
  }

  if (pipe == -1)
    goto error_no_crtc;

  for (i = 0; i < plane_resources->count_planes; i++) {
    plane_tmp = drmModeGetPlane (fd, plane_resources->planes[i]);
    if (plane_tmp->possible_crtcs & (1 << pipe)) {
      plane = plane_tmp;
      break;
    } else {
      drmModeFreePlane (plane_tmp);
    }
  }

  if (!plane)
    goto error_no_plane;

  ret = true;

fail:

  return ret;

error_no_connector:
  LOG_ERROR ("could not get connector (%d): %s",
      connector_id, strerror (errno));
  goto fail;

error_no_mode:
  LOG_ERROR ("could not find a valid mode (count_modes %d)",
      connector->count_modes);
  goto fail;

error_no_encoder:
  LOG_ERROR ("could not get encoder: %s", strerror (errno));
  goto fail;

error_no_crtc:
  LOG_ERROR ("couldn't find a crtc");
  goto fail;

error_no_plane:
  LOG_ERROR ("couldn't find a plane");
  goto fail;
}

KMSMemory *kms_sink::kms_alloc (int size)
{
  KMSMemory *mem = NULL;
  struct kms_bo **bo;
  unsigned attr[] = {
    KMS_BO_TYPE, KMS_BO_TYPE_SCANOUT_X8R8G8B8,
    KMS_WIDTH, mformat.width,
    KMS_HEIGHT, mformat.height,
    KMS_TERMINATE_PROP_LIST
  };

  mem = (KMSMemory *)malloc (sizeof (KMSMemory));
  memset (mem, 0, sizeof (KMSMemory));

  bo = (struct kms_bo **) &mem->bo[0];

  if (kms_bo_create (driver, attr, bo) < 0) {
    LOG_ERROR ("Could not allocate buffer object");
    free (mem);
    return NULL;
  }

  mem->fb_id = -1;
  kms_bo_get_prop (*bo, KMS_HANDLE, &mem->handle[0]);
  kms_bo_get_prop (*bo, KMS_PITCH, &mem->pitch);
  LOG_DEBUG ("pitch: %d\n", mem->pitch);
  mem->handle[2] = mem->handle[1] = mem->handle[0];

  return mem;
}

void kms_sink::kms_free (KMSMemory * mem)
{
  if (kms_bo_destroy ((struct kms_bo **) &mem->bo[0]) < 0)
    LOG_ERROR ("Could not destroy buffer object\n");
  free (mem);
}

void *kms_sink::kms_map (KMSMemory * mem)
{
  void *ret = NULL;

  if (kms_bo_map ((kms_bo *)mem->bo[0], &ret))
    LOG_ERROR ("Could not map buffer object\n");

  return ret;
}

void kms_sink::kms_unmap (KMSMemory * mem)
{
  if (kms_bo_unmap ((kms_bo *)mem->bo[0]))
    LOG_ERROR ("Could not unmap buffer object\n");
}

void kms_sink::kms_reset ()
{
  int i, j;
  for (i = 0; i < mformat.min_buffers; i ++) {
    for (j = 0; j < mformat.plane_num; j++) {
      if (pbuf[i][j]) {
        kms_unmap (pbuf[i][j]);
        kms_free (pbuf[i][j]);
        pbuf[i][j] = NULL;
      }
    }
  }

  if (encoder) {
    drmModeFreeEncoder (encoder);
    encoder = NULL;
  }

  if (connector) {
    drmModeFreeConnector (connector);
    connector = NULL;
  }

  if (driver) {
    kms_destroy (&driver);
    driver = NULL;
  }

  if (plane) {
    drmModeFreePlane (plane);
    plane = NULL;
  }

  if (plane_resources) {
    drmModeFreePlaneResources (plane_resources);
    plane_resources = NULL;
  }

  if (resources) {
    drmModeFreeResources (resources);
    resources = NULL;
  }

  if (fd != -1) {
    close (fd);
    fd = -1;
  }
}

bool kms_sink::set_format_sink (Format *format)
{
  CHECK_NULL (format);
  mformat = *format;

  kms_reset ();

  fd = drmOpen ("imx-drm", NULL);
  if (fd < 0) {
    LOG_ERROR ("drmOpen fail.\n");
    goto fail;
  }

  resources = drmModeGetResources (fd);
  if (resources == NULL) {
    LOG_ERROR ("drmModeGetResources fail.\n");
    goto fail;
  }

  plane_resources = drmModeGetPlaneResources (fd);
  if (plane_resources == NULL) {
    LOG_ERROR ("drmModeGetPlaneResources fail.\n");
    goto fail;
  }

  if (!plane) {
    if (!find_mode_and_plane (&dest)) {
      LOG_ERROR ("find_mode_and_plane fail.\n");
      goto fail;
    }
  }

  if (kms_create (fd, &driver) < 0) {
    LOG_ERROR ("Could not create KMS driver object\n");
    goto fail;
  }

  return true;

fail:

  kms_reset ();
  return false;
}

bool kms_sink::alloc_buffer (Buffer *buf)
{
  return true;
}

bool kms_sink::put_buffer_sink (Buffer *buf)
{
  uint32_t pitches[4] = {0}, offsets[4] = {0};
  KMSMemory *mem = NULL;
  struct kms_bo *bo;
  unsigned int fourcc;
  int size, i;
  int ret;

  CHECK_NULL (buf);
  for (i = 0; i < mformat.plane_num; i ++) {
    if (buf->data[i] == NULL) {
      pbuf[buf->v4l2_index][i] = kms_alloc (buf->alloc_size[i]);
      if (!pbuf[buf->v4l2_index][i]) {
        LOG_ERROR ("allocate %u bytes memory failed: %s",
            buf->alloc_size[i], strerror(errno));
        return false;
      }
      if (mformat.memory_mode == V4L2_MEMORY_USERPTR) {
        buf->data[i] = (char *) kms_map (pbuf[buf->v4l2_index][i]);
      } else if (mformat.memory_mode == V4L2_MEMORY_DMABUF) {
        drmPrimeHandleToFD(fd, pbuf[buf->v4l2_index][i]->handle[0], DRM_CLOEXEC,
            (int *)(&(buf->data[i])));
        LOG_DEBUG ("DMA FD: %d\n", buf->data[i]);
      }
      if (buf->data[i] == NULL) {
        LOG_ERROR ("malloc fail: %d\n", errno);
        return false;
      }
      mem = pbuf[buf->v4l2_index][i];
      buf->size[i] = buf->alloc_size[i];
      fourcc = mformat.yuv_format_v4l2;

      offsets[0] = 0;
      pitches[0] = mformat.width;
      pitches[1] = pitches[0]/2;
      offsets[1] = pitches[0] * mformat.height;
      pitches[2] = pitches[0]/2;
      offsets[2] = offsets[1] + pitches[0] * mformat.height / 4;
      ret = drmModeAddFB2 (fd, mformat.width, mformat.height, fourcc,
          mem->handle, pitches, offsets, &mem->fb_id, 0);
      if (ret) {
        LOG_ERROR ("failed to add fb2: %s\n", strerror(errno));
        return false;
      }
      return true;
    }
  }

  mem = pbuf[buf->v4l2_index][0];
  if (mem == NULL) {
    for (i = 0; i < mformat.plane_num; i ++) {
      pbuf[buf->v4l2_index][i] = kms_alloc (buf->alloc_size[i]);
      if (!pbuf[buf->v4l2_index][i]) {
        LOG_ERROR ("allocate %u bytes memory failed: %s",
            buf->alloc_size[i], strerror(errno));
        return false;
      }
      mem = pbuf[buf->v4l2_index][i];
      mem->vaddr[i] = kms_map (pbuf[buf->v4l2_index][i]);
      if (mem->vaddr[i] == NULL) {
        LOG_ERROR ("malloc fail: %d\n", errno);
        return false;
      }
      fourcc = mformat.yuv_format_v4l2;

      offsets[0] = 0;
      pitches[0] = mformat.width;
      pitches[1] = pitches[0]/2;
      offsets[1] = pitches[0] * mformat.height;
      pitches[2] = pitches[0]/2;
      offsets[2] = offsets[1] + pitches[0] * mformat.height / 4;
      ret = drmModeAddFB2 (fd, mformat.width, mformat.height, fourcc,
          mem->handle, pitches, offsets, &mem->fb_id, 0);
      if (ret) {
        LOG_ERROR ("failed to add fb2: %s\n", strerror(errno));
        return false;
      }
    }
    mem = pbuf[buf->v4l2_index][0];
    need_copy = true;
  }
  if (need_copy) {
    memcpy (mem->vaddr[0], buf->data[0], buf->size[0]);
  }

  //if (mediatime)
    //usleep ((buf->pts - mediatime) * 1000);
  mediatime = buf->pts;

  LOG_DEBUG ("crtc id = %d / plane id = %d / buffer id = %d\n",
      crtc_id, plane->plane_id, mem->fb_id);
#if 1
  if (dest.width > mformat.width)
    dest.width = mformat.width;
  if (dest.height > mformat.height)
    dest.height = mformat.height;

  ret = drmModeSetPlane (fd, plane->plane_id,
      crtc_id, mem->fb_id, 0,
      dest.left, dest.top, dest.width, dest.height,
      /* source/cropping coordinates are given in Q16 */
      //src.x << 16, src.y << 16, src.w << 16, src.h << 16);
      dest.left, dest.top, dest.width << 16, dest.height << 16);
#else
  ret = drmModeSetCrtc(fd, crtc_id, mem->fb_id,
      0, 0, (uint32_t *)&connector_id, 1, mode);
  //drmModeDirtyFB(fd, mem->fb_id, NULL, 0);
#endif
  if (ret) {
    LOG_ERROR ("failed to set mode: %s\n", strerror(errno));
    return false;
  }

  return true;
}

int64 kms_sink::get_mediatime ()
{
  return mediatime;
}

bool kms_sink::flush ()
{
  mediatime = 0;
  return true;
}

bool kms_sink::free_buffer (Buffer *buf)
{
  CHECK_NULL (buf);
  return true;
}
