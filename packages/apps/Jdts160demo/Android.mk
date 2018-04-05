LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(call all-java-files-under, src)
LOCAL_RESOURCE_FILES := $(addprefix $(LOCAL_PATH)/, res)
LOCAL_PACKAGE_NAME := Jdts160demo
LOCAL_CERTIFICATE := platform
LOCAL_STATIC_JAVA_LIBRARIES := android-support-core-utils-api24

include $(BUILD_PACKAGE)