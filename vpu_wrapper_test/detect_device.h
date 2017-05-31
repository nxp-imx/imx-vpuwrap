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

#ifndef __DETECT_DEVICE_H__
#define __DETECT_DEVICE_H__

#define MAX_V4L2_DEVICE 32

class detect_device {
  public:
    detect_device () {};
    virtual ~detect_device () {};
    void list_codec ();
    char *get_device (unsigned int codec_type);
    int open_device (char *device);
    void close_device (int fd);
  private:
    char *find_device (unsigned int type, unsigned int codec_type);
    int enumerate_format (int fd, int type);
    char devname[20];
};

#endif
