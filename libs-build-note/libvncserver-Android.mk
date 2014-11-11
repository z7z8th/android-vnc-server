LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH) \
	$(LOCAL_PATH)/libvncserver \
	$(LOCAL_PATH)/common \
	external/zlib \
	external/libpng \
	libjpeg-turbo \
	external/openssl/include \

LOCAL_SRC_FILES := \
	common/d3des.c \
	common/minilzo.c \
	common/sha1.c \
	common/turbojpeg.c \
	common/vncauth.c \
	common/zywrletemplate.c \
	libvncserver/auth.c \
	libvncserver/cargs.c \
	libvncserver/corre.c \
	libvncserver/cursor.c \
	libvncserver/cutpaste.c \
	libvncserver/draw.c \
	libvncserver/font.c \
	libvncserver/hextile.c \
	libvncserver/httpd.c \
	libvncserver/main.c \
	libvncserver/rfbssl_openssl.c \
	libvncserver/rfbcrypto_openssl.c \
	libvncserver/rfbregion.c \
	libvncserver/rfbserver.c \
	libvncserver/rre.c \
	libvncserver/scale.c \
	libvncserver/selbox.c \
	libvncserver/sockets.c \
	libvncserver/stats.c \
	libvncserver/tight.c \
	libvncserver/tightvnc-filetransfer/filelistinfo.c \
	libvncserver/tightvnc-filetransfer/filetransfermsg.c \
	libvncserver/tightvnc-filetransfer/handlefiletransferrequest.c \
	libvncserver/tightvnc-filetransfer/rfbtightserver.c \
	libvncserver/translate.c \
	libvncserver/ultra.c \
	libvncserver/websockets.c \
	libvncserver/zlib.c \
	libvncserver/zrle.c \
	libvncserver/zrleoutstream.c \
	libvncserver/zrlepalettehelper.c \

LOCAL_CFLAGS := 
LOCAL_MODULE := libvncserver_static

include $(BUILD_STATIC_LIBRARY)
