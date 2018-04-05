# building path
LOCAL_PATH := $(call my-dir)

ifneq ($(TARGET_PRODUCT),sim)
# modules` buildings follow one after another in a merged .mk file
# this is why it is needed to clear the parameters beforehand
# except LOCAL_PATH
include $(CLEAR_VARS)

LOCAL_PRELINK_MODULE := false
# this is where it will be in a loaded OS
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
# libs to build
LOCAL_SHARED_LIBRARIES := liblog libcutils libhardware
# source split with spaces
LOCAL_SRC_FILES := sensor_jdts_temperature.c
# output name (.default postfix matter. there are several 
# types of postfixes, which are recofgnized at loading)
LOCAL_MODULE := techartmsjdts.default
LOCAL_MODULE_TAGS := debug

include $(BUILD_SHARED_LIBRARY)

endif