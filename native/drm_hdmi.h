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


#ifndef _DRM_HDMI_H
#define _DRM_HDMI_H

#define DRM_MIPI_OFF   (0)
#define DRM_MIPI_ON    (1)

#define DRM_HDMI_CLONE (1)
#define DRM_HDMI_VIDEO_EXT   (2)

#define DRM_HDMI_DISCONNECTED (0)
#define DRM_HDMI_CONNECTED (1)
#define DRM_DVI_CONNECTED (2)

bool drm_init();
bool drm_hdmi_bGetCrtc();
bool drm_hdmi_setHdmiVideoOn();
bool drm_hdmi_setHdmiVideoOff();
bool drm_hdmi_setHdmiPowerOff();
int drm_hdmi_connectStatus();
bool drm_hdmi_getHdmiMode(int *pMode, int *pWidth, int *pHeight);
bool drm_hdmi_setHdmiMode(int mode);
void drm_cleanup();
void drm_mipi_setMode(int mode);
void drm_hdmi_onHdmiDisconnected(void);
int drm_hdmi_getModeInfo(int *pWidth, int* pHeight,
    int *pRefresh, int *pInterlace);
bool drm_hdmi_setModeInfo(int width, int height,
                          int refresh, int interlace, int ratio);
bool drm_hdmi_setScaling(int scale_mode);
int drm_hdmi_getScaling();
bool drm_hdmi_setScaleStep(int hValue, int vValue);
int drm_hdmi_getDeviceChange();
int drm_get_dev_fd();
int drm_hdmi_notify_audio_hotplug(bool plugin);
int drm_get_ioctl_offset();

#endif // _DRM_HDMI_H
