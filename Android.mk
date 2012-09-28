LOCAL_PATH:= $(call my-dir)

ifeq ($(TARGET_HAS_MULTIPLE_DISPLAY),true)

include $(CLEAR_VARS)
LOCAL_COPY_HEADERS_TO := display
LOCAL_COPY_HEADERS := \
    native/include/IExtendDisplayModeChangeListener.h \
    native/include/IMultiDisplayComposer.h \
    native/include/MultiDisplayClient.h \
    native/include/MultiDisplayComposer.h \
    native/include/MultiDisplayType.h \
    native/MultiDisplayService.h

include $(BUILD_COPY_HEADERS)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= \
    native/MultiDisplayService.cpp \
    native/MultiDisplayClient.cpp \
    native/IMultiDisplayComposer.cpp \
    native/MultiDisplayComposer.cpp \
    native/IExtendDisplayModeChangeListener.cpp

LOCAL_MODULE:= libmultidisplay
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := \
    libui libcutils libutils libbinder libsepdrm libgui libstagefright_foundation
LOCAL_CFLAGS := -DLOG_TAG=\"MultiDisplay\"

ifeq ($(ENABLE_IMG_GRAPHICS),true)
    LOCAL_SRC_FILES += \
        native/drm_hdmi.c \
        native/drm_hdcp.c

    LOCAL_C_INCLUDES = \
        $(TARGET_OUT_HEADERS)/drm \
        $(TARGET_OUT_HEADERS)/libdrm \
        $(TARGET_OUT_HEADERS)/pvr/pvr2d \
        $(TARGET_OUT_HEADERS)/libttm \
        $(TARGET_OUT_HEADERS)/libspedrm \
        $(TOP)/hardware/intel/linux-2.6/drivers/staging/intel_media/common

    LOCAL_SHARED_LIBRARIES += \
         libdrm \
         libsrv_um \
         libpvr2d

    LOCAL_CFLAGS += -DENABLE_DRM
    LOCAL_CFLAGS += -DDVI_SUPPORTED
    LOCAL_SHARED_LIBRARIES += libdl
endif
include $(BUILD_SHARED_LIBRARY)

# Build JNI library
include $(CLEAR_VARS)

LOCAL_SRC_FILES := service/jni/com_android_server_DisplaySetting.cpp

LOCAL_MODULE:= libdisplayobserverjni
LOCAL_MODULE_TAGS:=optional

LOCAL_SHARED_LIBRARIES := \
     libcutils \
     libutils \
     libbinder \
     libandroid_runtime \
     libmultidisplay \
     libsystem_server \
     libnativehelper

LOCAL_C_INCLUDES := \
     $(JNI_H_INCLUDE) \
     $(TOP)/frameworks/base/include \
     $(TARGET_OUT_HEADERS)/display

include $(BUILD_STATIC_LIBRARY)

# ============================================================
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
            $(call all-subdir-java-files)

LOCAL_MODULE:= displayobserver
LOCAL_MODULE_TAGS:=optional

LOCAL_JNI_SHARED_LIBRARIES := libdisplayobserverjni

LOCAL_NO_EMMA_INSTRUMENT := true
LOCAL_NO_EMMA_COMPILE := true

include $(BUILD_STATIC_JAVA_LIBRARY)

include $(BUILD_DROIDDOC)

endif
