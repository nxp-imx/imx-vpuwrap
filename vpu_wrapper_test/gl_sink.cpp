/*
 * Copyright 2017 NXP
 * All rights reserved.
 */

#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <errno.h>
#include "gl_sink.h"
#include "log.h"
#include <linux/videodev2.h>
#include <sys/time.h>

/* GL_VIV_direct_texture */
#ifndef GL_VIV_direct_texture
#define GL_VIV_YV12                     0x8FC0
#define GL_VIV_NV12                     0x8FC1
#define GL_VIV_YUY2                     0x8FC2
#define GL_VIV_UYVY                     0x8FC3
#define GL_VIV_NV21                     0x8FC4
#endif

#define WINDOW_WIDTH (1920)
#define WINDOW_HEIGHT (1080)

/*  Start with an identity matrix. */
GLfloat transformMatrix[16] =
{
  -1.0f, 0.0f, 0.0f, 0.0f,
  0.0f, 1.0f, 0.0f, 0.0f,
  0.0f, 0.0f, 1.0f, 0.0f,
  0.0f, 0.0f, 0.0f, 1.0f,
};
float frontFaceVertexPositions[] = 
{
  -1.0f, -1.0f,  0.0f,  1.0f,
  1.0f, -1.0f,  0.0f,  1.0f,
  1.0f,  1.0f,  0.0f,  1.0f,
  -1.0f,  1.0f,  0.0f,  1.0f,
};

const float frontFaceCoordPositon[] = 
{
  1.0f, 0.95f,
  0.0f, 0.95f,
  0.0f, 0.0f,	
  1.0f, 0.0f,
};

gl_sink::gl_sink ()
{
    wndContext = NULL;
    memset(&mformat,0,sizeof(Format));
    memset(&render_buffer,0,sizeof(Buffer));
    vsShader = 0;
    fsShader = 0;
    program = 0;
    memset(&mTextureInfo,0,sizeof(texture_info));
    bInit = false;

    createWindow(WINDOW_WIDTH,WINDOW_HEIGHT);

}

gl_sink::~gl_sink ()
{
    RenderCleanup();
    closeWindow();
}

bool gl_sink::set_format_sink (Format *format)
{

  CHECK_NULL (format);

  mformat = *format;
  if(0 == Calculate_frameInfo()){
    return false;
  }

  return true;
}

bool gl_sink::put_buffer_sink (Buffer *buf)
{
    mTextureInfo.planes[0] = buf->data[0];
    mTextureInfo.planes[1] = buf->data[1];

    //static struct timeval time_beg;
    //static struct timeval time_end;
    //gettimeofday(&time_beg, 0);

    if(!bInit){
      LOG_DEBUG("call RenderInit \n");
      RenderInit();
      bInit = true;
    }

    if(0 != Map_texture(buf))
        return false;

    Render();
    //gettimeofday(&time_end, 0);
    //LOG_DEBUG("put_buffer_sink render cost time=%lld \n",((int64)time_end.tv_sec * 1000000 + time_end.tv_usec) - ((int64)time_beg.tv_sec * 1000000 + time_beg.tv_usec) );
    return true;
}

int gl_sink::createWindow(int width, int height)
{
  Display *dis;

  wndContext = (WindowContext*)malloc(sizeof(WindowContext));
  memset(wndContext,0,sizeof(WindowContext));

  dis = XOpenDisplay(NULL);
  if(!dis) {
    LOG_ERROR("display open failed!\n");
    return -1;
  }
  wndContext->win = XCreateSimpleWindow (dis,RootWindow(dis, 0), 1, 1, width, height, 0, BlackPixel (dis, 0), BlackPixel(dis, 0));
  LOG_DEBUG ("win: %u\n", wndContext->win);
  if (!wndContext->win) {
    LOG_ERROR ("create window failed.\n");
    return -1;
  }
  XMapWindow(dis, wndContext->win);
  wndContext->wmDeleteMessage = XInternAtom(dis, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(dis, wndContext->win, &wndContext->wmDeleteMessage, 1);
  XFlush(dis);
  wndContext->dis= (NativePixmapType*)dis;

  return 0;
}

int gl_sink::closeWindow()
{

  if (wndContext) {
    XDestroyWindow((Display*)wndContext->dis, (Window)wndContext->win);
    XFlush((Display*)wndContext->dis);
    XCloseDisplay((Display*)wndContext->dis);
    free (wndContext);
    wndContext = NULL;
    LOG_DEBUG("Destory Window\n");
  }

  return 0;
}

void gl_sink::Open_EGL(void)
{
  EGLBoolean ret;
  static  EGLint gl_context_attribs[] =
  {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };

  static  EGLint s_configAttribs[] =
  {
    EGL_RED_SIZE,        5,
    EGL_GREEN_SIZE,      6,
    EGL_BLUE_SIZE,       5,
    EGL_ALPHA_SIZE,      0,
    EGL_DEPTH_SIZE,      16,
    EGL_STENCIL_SIZE,    0,
    EGL_NONE
  };

  EGLint numConfigs;
  EGLint majorVersion;
  EGLint minorVersion;

  eglDisplay       =  EGL_NO_DISPLAY;
  eglConfig;
  eglContext       =  EGL_NO_CONTEXT;
  eglWindowSurface =  EGL_NO_SURFACE;

  eglDisplay = eglGetDisplay((EGLNativeDisplayType) wndContext->dis);
  eglBindAPI(EGL_OPENGL_ES_API);
  eglInitialize(eglDisplay, &majorVersion, &minorVersion);

  eglGetConfigs(eglDisplay, NULL, 0, &numConfigs);
  eglChooseConfig(eglDisplay, s_configAttribs, &eglConfig, 1, &numConfigs);
  eglWindowSurface = eglCreateWindowSurface(eglDisplay, eglConfig, (EGLNativeWindowType)wndContext->win, NULL);
  eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, gl_context_attribs);

  ret = eglMakeCurrent(eglDisplay, eglWindowSurface, eglWindowSurface, eglContext);

  /* Check the extension support.*/
  if(Load_VIV_API() == 1)
  {
    LOG_ERROR("Load_VIV_API Failed\n");
  }

  if (ret == EGL_FALSE)
    LOG_ERROR("eglMakeCurrent failed.\n");
  else
    LOG_DEBUG("Open   EGL success. eglDisplay=%p,eglWindowSurface=%p\n",eglDisplay,eglWindowSurface);
}

int gl_sink::Load_VIV_API()
{
  pFNglTexDirectVIV = NULL;
  pFNglTexDirectInvalidateVIV = NULL;
  pFNglTexDirectVIVMap = NULL;

  if (pFNglTexDirectVIV == NULL)
  {
    /* Get the pointer to the glTexDirectVIV function. */
    pFNglTexDirectVIV = (PFNGLTEXDIRECTVIV)eglGetProcAddress("glTexDirectVIV");

    if (pFNglTexDirectVIV == NULL)
    {
      LOG_ERROR("Required extension not supported.\n");
      /* The glTexDirectVIV function is not available. */
      return 1;
    }
  }

  if (pFNglTexDirectInvalidateVIV == NULL) {
    /* Get the pointer to the glTexDirectInvalidate function. */
    pFNglTexDirectInvalidateVIV = (PFNGLTEXDIRECTINVALIDATEVIV)
      eglGetProcAddress("glTexDirectInvalidateVIV");

    if (pFNglTexDirectInvalidateVIV == NULL)
    {
      LOG_ERROR("Required extension not supported.\n");
      /* The glTexDirectInvalidateVIV function is not available. */
      return 1;
    }
  }

  if (pFNglTexDirectVIVMap == NULL){
    pFNglTexDirectVIVMap = (PFNGLTEXDIRECTVIVMAP)eglGetProcAddress("glTexDirectVIVMap");

    if (pFNglTexDirectVIVMap == NULL)
    {
      LOG_ERROR("pFNglTexDirectVIVMap \n");
      return 1;
    }
    LOG_DEBUG("pFNglTexDirectVIVMap=%p \n",pFNglTexDirectVIVMap);
  }

  return 0;
}

void gl_sink::Close_EGL(void)
{
  /* Destroy all EGL resources. */
  if(eglDisplay != EGL_NO_DISPLAY)
  {
    eglMakeCurrent(eglDisplay, NULL, NULL, NULL);
    if (eglContext != EGL_NO_CONTEXT)
    {
      eglDestroyContext(eglDisplay, eglContext);
      eglContext = EGL_NO_CONTEXT;
    }
    if (eglWindowSurface != EGL_NO_SURFACE)
    {
      eglDestroySurface(eglDisplay, eglWindowSurface);
      eglWindowSurface = EGL_NO_SURFACE;
    }
    eglTerminate(eglDisplay);
    eglDisplay = EGL_NO_DISPLAY;
    LOG_DEBUG("Close EGL \n");
  }
}

void gl_sink::LoadShader()
{
  const GLchar* vsString = 
    "attribute vec4 position;                            \n\
    attribute vec2 texCoord;                            \n\
    uniform   mat4 transMatrix;                         \n\
    varying   vec2 vTexCoord;                           \n\
    void main()                                         \n\
    {                                                   \n\
      gl_Position = transMatrix * position;    \n\
        vTexCoord = texCoord;                           \n\
    }                                                   ";

  const GLchar *fsString = 
    "precision mediump float;                             \n\
    uniform sampler2D yuv_sampler;                       \n\
    varying vec2 vTexCoord;                              \n\
    void main()                                          \n\
    {                                                    \n\
      gl_FragColor = texture2D(yuv_sampler, vTexCoord); \n\
    }                                                   ";

  /* Create and load shader program. */
  if (program == 0)
  {
    vsShader = glCreateShader(GL_VERTEX_SHADER);
    fsShader = glCreateShader(GL_FRAGMENT_SHADER);
    program = glCreateProgram();
    glShaderSource(vsShader, 1, &vsString, NULL );
    glShaderSource(fsShader, 1, &fsString, NULL );
    glCompileShader(vsShader);
    glCompileShader(fsShader);
    glAttachShader(program, vsShader);
    glAttachShader(program, fsShader);
    glLinkProgram(program);
    glUseProgram(program);
    LOG_DEBUG("shader program loaded\n");
  }

  if(glGetError() != GL_NO_ERROR){
    LOG_ERROR("error code = %d, lineNumber = %d \n",  glGetError(), __LINE__);
  }
}

void gl_sink::DeleteShader()
{
    if(program != 0)
    {
        glDeleteShader(vsShader);
        glDeleteShader(fsShader);
        glDeleteProgram(program);
        glUseProgram(0);
        program = 0;
    }
}

void gl_sink::RenderInit()
{
    GLenum ret;
    Open_EGL();

    LoadShader();

    locVertices     = glGetAttribLocation(program, "position");
    locTexcoord     = glGetAttribLocation(program, "texCoord");
    locSampler      = glGetUniformLocation(program, "yuv_sampler");
    locTransformMat = glGetUniformLocation(program, "transMatrix");

    //Init_stream();
    Create_texture(&mTextureInfo);

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    ret = glGetError();

    if (ret != GL_NO_ERROR)
    {
    LOG_ERROR("RenderInit 1 %x \n",ret);
    }
    
    glEnableVertexAttribArray( locVertices );
    glEnableVertexAttribArray( locTexcoord );

    glUniform1i(locSampler, 0);
    glUniformMatrix4fv(locTransformMat, 1, GL_FALSE, transformMatrix);
    LOG_DEBUG("RenderInit end \n");

    ret = glGetError();

    if (ret != GL_NO_ERROR)
    {
    LOG_ERROR("RenderInit 2 %x \n",ret);
    }
}
#define Align(ptr,align)	(((unsigned int64)ptr+(align)-1)/(align)*(align))
#define FRAME_ALIGN (32)
int gl_sink::Calculate_frameInfo()
{

    if(mformat.width <= 0 || mformat.height <= 0)
    {
        return 1;
    }

    mTextureInfo.format = GL_VIV_NV12;
    mTextureInfo.num_planes = 2;
    mTextureInfo.planes_size[0]     = Align(mformat.width, FRAME_ALIGN) * Align(mformat.height, FRAME_ALIGN);
    mTextureInfo.planes_size[1]     = mTextureInfo.planes_size[0] / 2;
    LOG_DEBUG("NV12 w=%d,h=%d\n", mformat.width, mformat.height);

    mTextureInfo.width = mformat.width;
    mTextureInfo.height = mformat.height;

    return 0;
}

int gl_sink::Create_texture(texture_info *texInfo)
{
    GLenum ret;
    int height;
    const GLuint physical= ~0;
    /* Create the texture. */
    glGenTextures(1, &texInfo->textureID);

    return 0;
}

int gl_sink::Map_texture(Buffer *buf)
{

    GLenum ret;
    int64 physical= ~0;

    if(buf == NULL)
        return 1;

    glBindTexture(GL_TEXTURE_2D, mTextureInfo.textureID);

    ret = glGetError();

    if (ret != GL_NO_ERROR)
    {
        LOG_ERROR("Failed to create DirectVIV Map texture. %d,phic=%x\n",ret,physical);
        return 1;
    }

    physical =  (int64)buf->phy_data[0];
    (*pFNglTexDirectVIVMap)(GL_TEXTURE_2D, Align(mTextureInfo.width,FRAME_ALIGN), Align(mTextureInfo.height,FRAME_ALIGN), 
        mTextureInfo.format, (GLvoid**)&mTextureInfo.planes[0],(GLuint *)&physical);
    ret = glGetError();
      
    if (ret != GL_NO_ERROR)
    {
    LOG_ERROR("Failed to create DirectVIV Map texture. %d,phic=%x\n",ret,physical);
    return 1;
    }

    LOG_DEBUG("put_buffer_sink ptr=%p phys=%p\n",mTextureInfo.planes[0],buf->phy_data[0]);

    (*pFNglTexDirectInvalidateVIV)(GL_TEXTURE_2D);
    return 0;
}
void gl_sink::Render()
{  
    EGLBoolean boolRet = EGL_FALSE;

    glClearColor(0.6f, 0.6f, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT| GL_STENCIL_BUFFER_BIT);

    glUniformMatrix4fv(locTransformMat, 1, GL_FALSE, transformMatrix);

    LOG_DEBUG("Render textureID=%d\n",mTextureInfo.textureID);
    glBindTexture(GL_TEXTURE_2D, mTextureInfo.textureID);
    glVertexAttribPointer( locVertices, 4, GL_FLOAT, 0, 0, frontFaceVertexPositions );
    glVertexAttribPointer( locTexcoord, 2, GL_FLOAT, 0, 0, frontFaceCoordPositon );
    glDrawArrays( GL_TRIANGLE_FAN, 0, 4 ); 

#if 0
    //glBindTexture(GL_TEXTURE_2D, fileStream[1].texInfo.textureID);
    glVertexAttribPointer( locVertices, 4, GL_FLOAT, 0, 0, backFaceVertexPositions );
    glVertexAttribPointer( locTexcoord, 2, GL_FLOAT, 0, 0, backFaceCoordPositon );
    glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );  

    //glBindTexture(GL_TEXTURE_2D, fileStream[1].texInfo.textureID);
    glVertexAttribPointer( locVertices, 4, GL_FLOAT, 0, 0, rightFaceVertexPositions );
    glVertexAttribPointer( locTexcoord, 2, GL_FLOAT, 0, 0, rightFaceCoordPositon );
    glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );  

    //glBindTexture(GL_TEXTURE_2D, fileStream[1].texInfo.textureID);
    glVertexAttribPointer( locVertices, 4, GL_FLOAT, 0, 0, leftFaceVertexPositions );
    glVertexAttribPointer( locTexcoord, 2, GL_FLOAT, 0, 0, leftFaceCoordPositon );
    glDrawArrays( GL_TRIANGLE_FAN, 0, 4 ); 
#endif
    boolRet = eglSwapBuffers(eglDisplay, eglWindowSurface);
    //LOG_DEBUG("Render eglDisplay=%p,eglWindowSurface=%p\n",eglDisplay,eglWindowSurface);

    if(EGL_FALSE == boolRet)
        LOG_ERROR("Render eglSwapBuffers FAILED \n");

    usleep(20000);
}

void gl_sink::RenderCleanup()
{
    LOG_DEBUG("RenderCleanup \n");
    glDisableVertexAttribArray(locVertices);
    glDisableVertexAttribArray(locTexcoord);

    DeleteShader();
    Close_EGL();
}

