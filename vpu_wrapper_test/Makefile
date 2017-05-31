PROGRAM	= zpu
SRC_DIR	= .
OBJ_DIR	= obj
EXEDIR = .

APPL = $(EXEDIR)/mxc_$(PROGRAM)_test.out

CXXFLAGS += $(CXX_DEFINES) $(DEBUG)
CXXFLAGS += -DSUPPORT_LARGE_FILE -D_FILE_OFFSET_BITS=64 

APP_OBJS = $(OBJ_DIR)/camera_input.o \
                     $(OBJ_DIR)/codec_api.o \
					 $(OBJ_DIR)/input.o \
					 $(OBJ_DIR)/detect_device.o \
					 $(OBJ_DIR)/v4l2.o \
					 $(OBJ_DIR)/analysis.o \
					 $(OBJ_DIR)/output.o \
					 $(OBJ_DIR)/sink.o \
					 $(OBJ_DIR)/mxc_zpu_test.o \
					 $(OBJ_DIR)/v4l2_codec.o \
					 $(OBJ_DIR)/file_sink.o \
					 $(OBJ_DIR)/fb_sink.o \
					 $(OBJ_DIR)/file_input.o \
					 $(OBJ_DIR)/process.o

BUILD_VPU_WRAPPER = $(shell if [ -f $(vpu_wrapper.h) ]; then echo "exist"; else echo "not_exist"; fi;)
ifeq ($(BUILD_VPU_WRAPPER), exist)
LD_LIBS += -l_vpu_wrapper
APP_OBJS += $(OBJ_DIR)/vpu_wrapper_decoder.o
endif

ALL_OBJS = $(APP_OBJS)

all: 	$(OBJ_DIR) $(PROGRAM)
	@+echo "--- Test app build-all done ---"

$(OBJ_DIR):
	@echo "Creating directory $(OBJ_DIR)"
	@mkdir $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(PROGRAM): $(APP_OBJS)

	$(CC) -o $(APPL) $(STDLIBDIR)  $(APP_OBJS) -lpthread -lm -ldl -lstdc++ -lg2d $(LD_LIBS)

.PHONY: clean
clean :
	rm -f $(APP_OBJS)
	rm -f $(APPL)

#
# include the Rules
#
include make.rules


