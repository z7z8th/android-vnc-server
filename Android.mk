LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	fbvncserver.c \

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH) \
	libvncserver \
	external/zlib \
	libjpeg-turbo

LOCAL_SHARED_LIBRARIES := libz libpng libssl libcrypto
LOCAL_STATIC_LIBRARIES := libvncserver_static libjpeg_turbo_static

LOCAL_MODULE:= android-vnc-server

include $(BUILD_EXECUTABLE)
