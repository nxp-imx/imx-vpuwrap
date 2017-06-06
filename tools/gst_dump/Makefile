ifeq ($(BUILD),ARM11YOCTO_HW)
#. /opt/fsl-imx-internal-x11/1.0+snapshot/environment-setup-cortexa9hf-vfp-neon-freescale-linux-gnueabi
#. /opt/fsl-imx-x11/4.1.30-3.0.0/environment-setup-aarch64-poky-linux
endif
ifeq ($(BUILD),UNIX)
	TGT_OS_BIN 	= x86_unix
	CC 		= gcc
	AR 		= ar
	LD 		= ld
	CC_DEFINES  += -DPLATFORM_X86_UNIX
endif

DEBUG_MODE = ON
ifeq ($(DEBUG_MODE),ON)
	DEBUG = -g
else
	OPTIMIZE = -O2
endif

PROGRAM		= gst_dump
SRC_DIR		= .
OBJ_DIR		= obj
EXEDIR = .

APPL = $(EXEDIR)/$(PROGRAM).out

CCFLAGS += $(CC_DEFINES) $(DEBUG) -O2 -Wall -ansi `pkg-config gstreamer-1.0 --cflags`
CCFLAGS += -DSUPPORT_LARGE_FILE -D_FILE_OFFSET_BITS=64
STDLIBDIR += `pkg-config gstreamer-1.0 --libs` -lgstvideo-1.0

APP_OBJS = $(OBJ_DIR)/gst_dump.o 

ALL_OBJS = $(APP_OBJS)

all: 	$(OBJ_DIR) $(PROGRAM)
	@+echo "--- Test app build-all done ---"

$(OBJ_DIR):
	@echo "Creating directory $(OBJ_DIR)"
	@mkdir $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CCFLAGS) -c -o $@ $<

$(PROGRAM): $(APP_OBJS)

	$(CC) -o $(APPL) $(STDLIBDIR) $(APP_OBJS)

clean:
	rm -f $(ALL_OBJS)
	rm -f $(APPL)
