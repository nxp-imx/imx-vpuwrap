
PROGRAM=test_dec_arm_elinux
ENC_PROGRAM=test_enc_arm_elinux
ENC_AUTO_TEST=enc_auto_test
LIB=lib_vpu_wrapper
LIBRARY=../../release/lib/$(LIB)
SQLITE_LIBRARY=./sqlite/libsqlite3

VERSION = .1.0

ifeq ($(BUILD),CROSSBUILD)

TOOLS_DIR=/opt/freescale/usr/local/gcc-4.4.4-glibc-2.11.1-multilib-1.0/arm-fsl-linux-gnueabi
CC=$(TOOLS_DIR)/bin/arm-none-linux-gnueabi-gcc
LN=$(TOOLS_DIR)/bin/arm-none-linux-gnueabi-gcc

CFLAGS=-mcpu=arm1136j-s
CFLAGS+=-O2 -Wall
#CFLAGS+=-g
AFLAGS=

LFLAGS=-L$(TOOLS_DIR)/lib/gcc/arm-fsl-linux-gnueabi/4.4.4 -lpthread -lm -ldl
LFLAGS+=-L../../../bsp_51/lib -lvpu -lipu
LFLAGS_LIB=-L../../../bsp_51/lib -lvpu

else

CC=gcc
LN=gcc
AS=as
AR=ar

CFLAGS= -O2 -Wall
#CFLAGS+=-g
AFLAGS=

LFLAGS=-L/usr/lib -lvpu -lipu -lpthread -lm -ldl
LFLAGS_LIB=-L/usr/lib -lvpu

endif

INCLUDES= -I. -I../../ghdr -I../../../bsp_51/include -I/usr/include -I./sqlite


#CFLAGS+=-DUSE_VPU_WRAPPER_TIMER
#CFLAGS+=-DVPU_WRAPPER_DEBUG
CFLAGS+=-DDEC_STREAM_DEBUG
CFLAGS+=-DENC_STREAM_DEBUG
CFLAGS+=-DAPP_DEBUG
CFLAGS+=-DFB_RENDER_DEBUG

#CFLAGS+=-DUSER_SPECIFY_BINARY_VER -DSTR_USER_SPECIFY_BINARY_VER=\"binary version specified by user\"

LIB_OBJS=vpu_wrapper_timer.o
#ifeq ($(PLATFORM),IMX6)
#LIB_OBJS+=vpu_wrapper_imx6.o
#else
LIB_OBJS+=vpu_wrapper.o
#endif

APP_OBJS=test_dec_arm_elinux.o decode_stream.o
#APP_OBJS+=vpu_general_lib.o
APP_OBJS+=fb_render.o

ENC_APP_OBJS=test_enc_arm_elinux.o encode_stream.o
ENC_AUTO_OBJS=enc_auto_test.o encode_stream.o decode_stream.o fb_render.o sqlite_wrapper.o

all: EXE ENC_EXE ENC_AUTO_TEST
	@echo "--- Build-all done for vpu wrapper ---"

LIBRARY: $(LIB_OBJS)
	$(AR) -r $(LIBRARY).a $(LIB_OBJS)
	$(LN) -o $(LIBRARY).so $(LFLAGS_LIB) --shared -Wl,-soname,$(LIB).so$(VERSION) -fpic $(LIB_OBJS)

EXE: $(APP_OBJS) LIBRARY
	$(LN) -o $(PROGRAM) $(LFLAGS) $(APP_OBJS) $(LIBRARY).a

ENC_EXE: $(ENC_APP_OBJS) LIBRARY
	$(LN) -o $(ENC_PROGRAM) $(LFLAGS) $(ENC_APP_OBJS) $(LIBRARY).a

ENC_AUTO_TEST: $(ENC_AUTO_OBJS) LIBRARY
	$(LN) -o $(ENC_AUTO_TEST) $(LFLAGS) $(ENC_AUTO_OBJS) $(LIBRARY).a $(SQLITE_LIBRARY).a

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES)  -c  -o $@ $<

#%.o: %.s
#	$(AS) $(AFLAGS) -o $@ $<


clean:
	rm -rf $(LIB_OBJS)
	rm -rf $(APP_OBJS)	
	rm -rf $(ENC_APP_OBJS)
	rm -rf $(ENC_AUTO_OBJS)
	rm -rf $(LIBRARY).a
	rm -rf $(LIBRARY).so
	rm -rf $(PROGRAM)
	rm -rf $(ENC_PROGRAM)	
	rm -rf $(ENC_AUTO_TEST)

	
