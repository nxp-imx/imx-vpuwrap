ifeq ($(HAVE_FSL_IMX_CODEC),true)
ifneq ($(PREBUILT_FSL_IMX_OMX),true)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

## ifeq ($(TARGET_BOARD_PLATFORM), imx6)
## LOCAL_SRC_FILES := vpu_wrapper_imx6.c
## else
LOCAL_SRC_FILES := vpu_wrapper.c
## endif

LOCAL_CFLAGS += $(FSL_OMX_CFLAGS)

LOCAL_C_INCLUDES += $(FSL_OMX_INCLUDES) \
		    $(LOCAL_PATH)/hdr \
		    $(LOCAL_PATH)/../fsl_watermark/hdr

LOCAL_MODULE:= lib_vpu_wrapper


LOCAL_SHARED_LIBRARIES := libutils libc libm libstdc++ libvpu \
    			  lib_omx_osal_v2_arm11_elinux

LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_TAGS := eng
include $(BUILD_SHARED_LIBRARY)


endif
endif
