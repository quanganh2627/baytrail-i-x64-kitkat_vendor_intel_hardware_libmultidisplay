/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef ANDROID_MULTIDISPLAYTYPE_H
#define ANDROID_MULTIDISPLAYTYPE_H

//#include <binder/IBinder.h>
//using namespace android;

enum {
    MDS_GET_MODE = 1,//IBinder::FIRST_CALL_TRANSACTION,
    MDS_SET_MODEPOLICY,
    MDS_NOTIFY_WIDI,
    MDS_NOTIFY_MIPI,
    MDS_NOTIFY_HOTPLUG,
    MDS_HDMI_POWER_OFF,
    MDS_UPDATE_VIDEOINFO,
    MDS_GET_HDMIMODE_INFO_COUNT,
    MDS_GET_HDMIMODE_INFO,
    MDS_SET_HDMIMODE_INFO,
    MDS_REGISTER_MODE_CHANGE_LISTENER,
    MDS_UNREGISTER_MODE_CHANGE_LISTENER,
    MDS_SET_HDMISCALE_TYPE,
    MDS_SET_HDMISCALE_STEP,
    MDS_GET_HDMIDEVICE_CHANGE,
    MDS_GET_VIDEO_INFO,
};

typedef struct _MDSVideoInfo {
    bool isplaying;
    bool isprotected;
    int  frameRate;
    int  displayW;
    int  displayH;
    bool isinterlace;
} MDSVideoInfo;

typedef enum _MDSMode {
    MDS_MIPI_ON        = 0x1,

    MDS_HDMI_CONNECTED = 0x1 << 3,
    MDS_HDMI_ON        = 0x1 << 4,
    MDS_HDMI_CLONE     = 0x1 << 5,
    MDS_HDMI_VIDEO_EXT = 0x1 << 6,
    MDS_WIDI_ON        = 0x1 << 7,

    MDS_VIDEO_PLAYING  = 0x1 << 23,
    MDS_HDCP_ON        = 0x1 << 24,
    MDS_OVERLAY_OFF    = 0x1 << 25,
} MDSMode;

enum {
    MDS_HDMI_ON_NOT_ALLOWED  = 0,
    MDS_HDMI_ON_ALLOWED      = 1,
    MDS_MIPI_OFF_NOT_ALLOWED = 2,
    MDS_MIPI_OFF_ALLOWED     = 3
};

#define MDS_NO_ERROR (0)
#define MDS_ERROR    (-1)

#endif
