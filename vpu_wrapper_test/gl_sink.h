/*
 * Copyright 2017 NXP
 * All rights reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef __GL_SINK_H__
#define __GL_SINK_H__

#include "sink.h"
#include "common.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <X11/Xlib.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>  
#include <stdlib.h>     
#include <unistd.h>  
#include <string.h>
#include <time.h>

typedef struct
{   
  NativePixmapType *dis;
  NativeWindowType win;
  Atom wmDeleteMessage;
} WindowContext;

typedef void (GL_APIENTRY *PFNGLTEXDIRECTVIV)           (GLenum Target, GLsizei Width, GLsizei Height, GLenum Format, GLvoid ** Pixels);
typedef void (GL_APIENTRY *PFNGLTEXDIRECTINVALIDATEVIV) (GLenum Target);
typedef void (GL_APIENTRY *PFNGLTEXDIRECTVIVMAP) (GLenum Target, GLsizei Width, GLsizei Height, GLenum Format, GLvoid ** Logical,
    const GLuint * Physical);

typedef struct texture_info_type
{
  int     width;
  int     height;
  int     format;
  GLuint  textureID;
  void   *planes[3];
  int   planes_size[3];
  int   num_planes;
} texture_info;

class gl_sink : public sink {
  public:
    gl_sink ();
    virtual ~gl_sink ();
    bool set_format_sink (Format *format);
    bool put_buffer_sink (Buffer *buf);
  private:
    Format mformat;
    Buffer render_buffer;
    WindowContext * wndContext;
    EGLDisplay       eglDisplay;
    EGLConfig        eglConfig;
    EGLContext       eglContext;
    EGLSurface       eglWindowSurface;
    PFNGLTEXDIRECTVIV           pFNglTexDirectVIV;
    PFNGLTEXDIRECTINVALIDATEVIV pFNglTexDirectInvalidateVIV;
    PFNGLTEXDIRECTVIVMAP    pFNglTexDirectVIVMap;
    GLuint vsShader;
    GLuint fsShader;
    GLuint program;
    texture_info mTextureInfo;
    GLint locVertices;
    GLint locTexcoord;
    GLint locSampler;
    GLint locTransformMat;
    bool bInit;
    int createWindow(int width, int height);
    int closeWindow();
    void Open_EGL();
    int Load_VIV_API();
    void Close_EGL();
    void LoadShader();
    void DeleteShader();
    void RenderInit();
    int Calculate_frameInfo();
    //int Create_texture(texture_info *texInfo);
    int Create_texture(texture_info *texInfo);
    void Render();
    void RenderCleanup();
    int Map_texture(Buffer *buf);
};

#endif

