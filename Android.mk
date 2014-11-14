LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	fbvncserver.c \

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH) \
	libvncserver \
	external/zlib \
	libjpeg-turbo

LOCAL_SHARED_LIBRARIES := libssl libcrypto libpng libz libc
#LOCAL_SHARED_LIBRARIES := libcutils libc
LOCAL_STATIC_LIBRARIES := libvncserver_static libjpeg_turbo_static \
#  libssl_static libcrypto_static libpng libz #libcutils  libc

#LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE:= android-vnc-server
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
