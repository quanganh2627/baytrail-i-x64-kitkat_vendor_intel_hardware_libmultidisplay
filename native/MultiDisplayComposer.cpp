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

//#define LOG_NDEBUG 0

#include <utils/Log.h>
#include <binder/IServiceManager.h>
#include <binder/IPCThreadState.h>
#include <display/MultiDisplayType.h>
#include <display/IMultiDisplayComposer.h>
#include <display/MultiDisplayComposer.h>

extern "C" {
#include "drm_hdmi.h"
#include "drm_hdcp.h"
}

using namespace android;

MultiDisplayComposer::MultiDisplayComposer() {
    mDrmInit = false;
    mMode = 0;
    mMipiPolicy = MDS_MIPI_OFF_NOT_ALLOWED;
    mHdmiPolicy = MDS_HDMI_ON_ALLOWED;
    memset((void*)(&mVideo), 0, sizeof(MDSVideoInfo));
    mVideo.isplaying = false;
    mVideo.isprotected = false;
    mVideo.isinterlace = false;
    if (!drm_init()) {
        LOGE("%s: drm_init fails", __func__);
        return;
    }
    // default setting
    setModePolicy_l(MDS_MIPI_OFF_NOT_ALLOWED);
    setModePolicy_l(MDS_HDMI_ON_ALLOWED);
    mMipiOn = true;
    mWidiVideoExt = false;
    mMipiReq = NO_MIPI_REQ;
    // start mipi listener
    run("MIPIListener", PRIORITY_URGENT_DISPLAY);
    LOGI("%s: mMode: 0x%x", __func__, mMode);
    mDrmInit = true;
}

MultiDisplayComposer::~MultiDisplayComposer() {
    unsigned int i = 0;
    mDrmInit = false;
    mMode = 0;
    mHdmiPolicy = MDS_HDMI_ON_ALLOWED;
    mMipiPolicy = MDS_MIPI_OFF_NOT_ALLOWED;
    mMipiOn = false;
    mWidiVideoExt = false;
    mVideo.isprotected = false;
    mMipiReq = NO_MIPI_REQ;
    drm_cleanup();
    if (!mMCListenerVector.isEmpty()) {
        mMCListenerVector.clear();
    }
    memset((void*)(&mVideo), 0, sizeof(MDSVideoInfo));
    LOGV("%s: mMode: 0x%x", __func__, mMode);
}

int MultiDisplayComposer::getMode(bool wait) {
    if (mDrmInit == false) {
        LOGE("%s: drm_init fails", __func__);
        return MDS_ERROR;
    }
    if (wait)
        mLock.lock();
    else {
        if (mLock.tryLock() == -EBUSY) {
            //LOGW("%s: couldn't hold lock", __func__);
            return MDS_ERROR;
        }
    }
    int mode = mMode;
    mLock.unlock();
    return mode;
}

int MultiDisplayComposer::notifyWidi(bool on) {
    if (mDrmInit == false) {
        LOGE("%s: drm_init fails", __func__);
        return MDS_ERROR;
    }
    // It should hold mLock here instead. It is safe not holding any lock however.
    //Mutex::Autolock _l(mMipiLock);
    mWidiVideoExt = on;
    if (mWidiVideoExt)
        mMode |= MDS_WIDI_ON;
    else
        mMode &= ~MDS_WIDI_ON;
    return MDS_NO_ERROR;
}

int MultiDisplayComposer::notifyMipi(bool on) {
    if (mDrmInit == false) {
        LOGE("%s: drm_init fails", __func__);
        return MDS_ERROR;
    }
    Mutex::Autolock _l(mMipiLock);
    mMipiReq = on ? MIPI_ON_REQ : MIPI_OFF_REQ;
    mMipiCon.signal();
    return MDS_NO_ERROR;
}

int MultiDisplayComposer::setHdmiPowerOff() {
    if (mDrmInit == false) {
        LOGE("%s: drm_init fails", __func__);
        return MDS_ERROR;
    }
    Mutex::Autolock _l(mLock);
    drm_hdmi_setHdmiPowerOff();
    return MDS_NO_ERROR;
}

int MultiDisplayComposer::updateVideoInfo(MDSVideoInfo* info) {
    if (mDrmInit == false) {
        LOGE("%s: drm_init fails", __func__);
        return MDS_ERROR;
    }
    if (info == NULL) {
        LOGE("%s: video info is null", __func__);
        return MDS_ERROR;
    }
    Mutex::Autolock _l(mLock);
    LOGV("%s: \
        \n mode: 0x%x, \
        \n isplaying: %d, \
        \n protected Content: %d, \
        \n displayW: %d, \
        \n displayH: %d, \
        \n frameRate: %d, \
        \n isinterlace: %d",
        __func__, mMode, info->isplaying, info->isprotected,
        info->displayW, info->displayH, info->frameRate, info->isinterlace);

    memcpy(&mVideo, info, sizeof(MDSVideoInfo));

    return setHdmiMode_l();
}


int MultiDisplayComposer::setHdmiMode_l() {
    MDSHDMITiming timing;
    LOGV("Entering %s, current mode = %#x", __func__, mMode);
    // Common case, update video status
    if (mVideo.isplaying) {
        mMode |= MDS_VIDEO_PLAYING;
    } else {
        mMode &= ~MDS_VIDEO_PLAYING;
    }

    int connectStatus = getHdmiPlug_l();
#if !defined(DVI_SUPPORTED)
    if (connectStatus == DRM_DVI_CONNECTED) {
        LOGE("%s: DVI is connected but is not supported for now.", __func__);
        broadcastModeChange_l(mMode);
        return MDS_ERROR;
    }
#endif
    // Common case, turn on MIPI if necessary
    if (connectStatus == DRM_HDMI_DISCONNECTED ||
            mHdmiPolicy == MDS_HDMI_ON_NOT_ALLOWED ||
            mVideo.isplaying == false) {
        mMipiPolicy = MDS_MIPI_OFF_NOT_ALLOWED;
        if (!checkMode(mMode, MDS_MIPI_ON)) {
            LOGI("Turn on MIPI.");
            drm_mipi_setMode(DRM_MIPI_ON);
            mMode |= MDS_MIPI_ON;
            mMipiOn = true;
        }
    }

    if (connectStatus == DRM_HDMI_DISCONNECTED) {
        LOGI("HDMI is disconnected.");
        if (!checkMode(mMode, MDS_HDMI_CONNECTED)) {
            LOGW("HDMI is already in disconnected state.");
            broadcastModeChange_l(mMode);
            return MDS_NO_ERROR;
        }

        LOGI("Notify HDMI audio driver hot unplug event.");
        drm_hdmi_notify_audio_hotplug(false);
        drm_hdcp_disable_hdcp(false);
        mMode &= ~MDS_HDMI_CONNECTED;
        mMode &= ~MDS_HDMI_ON;
        mMode &= ~MDS_HDCP_ON;
        mMode &= ~MDS_HDMI_CLONE;
        mMode &= ~MDS_HDMI_VIDEO_EXT;
        broadcastModeChange_l(mMode);
        drm_hdmi_onHdmiDisconnected();
        return MDS_NO_ERROR;
    }

    LOGI("HDMI is connected.");
    bool notify_audio_hotplug = (mMode & MDS_HDMI_CONNECTED) == 0;
    mMode |= MDS_HDMI_CONNECTED;

    // Check HDMI policy first
    if (mHdmiPolicy == MDS_HDMI_ON_NOT_ALLOWED) {
        LOGI("HDMI on is not allowed. Turning off HDMI...");
        if (!checkMode(mMode, MDS_HDMI_ON)) {
            LOGW("HDMI is already in off state.");
            broadcastModeChange_l(mMode);
            return MDS_NO_ERROR;
        }
        if (checkMode(mMode, MDS_HDCP_ON)) {
            drm_hdcp_disable_hdcp(true);
        }
        mMode &= ~MDS_HDCP_ON;
        mMode &= ~MDS_HDMI_ON;
        mMode &= ~MDS_HDMI_CLONE;
        mMode &= ~MDS_HDMI_VIDEO_EXT;
        broadcastModeChange_l(mMode);
        drm_hdmi_setHdmiVideoOff();
        return MDS_NO_ERROR;
    }

    // Common case, turn on HDMI if necessary
    if (!checkMode(mMode, MDS_HDMI_ON)) {
        LOGI("Turn on HDMI...");
        if (!drm_hdmi_setHdmiVideoOn()) {
            LOGE("Fail to turn on HDMI.");
            mMode &= ~MDS_HDCP_ON;
            mMode &= ~MDS_HDMI_CLONE;
            mMode &= ~MDS_HDMI_VIDEO_EXT;
            broadcastModeChange_l(mMode);
            return MDS_ERROR;
        }
        mMode |= MDS_HDMI_ON;
    }

    // Turn off overlay temporarily during mode transition.
    // Make sure overlay is turned on when this function exits.
    // Turn off Overlay immediately when video playback is over.
    int dmode = mMode;
    if (mVideo.isplaying == false) {
        dmode &= ~MDS_HDMI_CLONE;
        dmode &= ~MDS_HDMI_VIDEO_EXT;
    }
    dmode |= MDS_OVERLAY_OFF;
    broadcastModeChange_l(dmode);

    if (mVideo.isplaying == true) {
        LOGI("Video is in playing state. Mode = %#x", mMode);
        if (checkMode(mMode, MDS_HDMI_VIDEO_EXT)) {
            LOGW("HDMI is already in Video Extended mode.");
            broadcastModeChange_l(mMode);
            return MDS_NO_ERROR;
        }
        timing.width = mVideo.displayW;
        timing.height = mVideo.displayH;
        timing.refresh = mVideo.frameRate;
        timing.interlace = 0;
        if (!(drm_hdmi_setMode(DRM_HDMI_VIDEO_EXT, &timing))) {
            LOGE("Fail to set HDMI extended mode");
            broadcastModeChange_l(mMode);
            return MDS_ERROR;
        }

        mMode |= MDS_HDMI_VIDEO_EXT;
        mMode &= ~MDS_HDMI_CLONE;
        mMipiPolicy = MDS_MIPI_OFF_ALLOWED;
        if (mVideo.isprotected) {
            LOGI("Turning on HDCP...");
            if (drm_hdcp_enable_hdcp() == false) {
                LOGE("Fail to enable HDCP.");
                // Continue mode setting as it may be recovered, unless HDCP is not supported.
                // If HDCP is not supported, user will have to unplug the cable to restore video to phone.
            }
            mMode |= MDS_HDCP_ON;
        }
        broadcastModeChange_l(mMode);
    } else {
        LOGI("Video is not in playing state. Mode = %#x", mMode);
        if (checkMode(mMode, MDS_HDMI_CLONE)) {
            LOGW("HDMI is already in cloned state.");
            broadcastModeChange_l(mMode);
            return MDS_NO_ERROR;
        }
        if (checkMode(mMode, MDS_HDCP_ON)) {
            drm_hdcp_disable_hdcp(true);
        }
        mMode &= ~MDS_HDCP_ON;

        if (!(drm_hdmi_setMode(DRM_HDMI_CLONE, NULL))) {
            LOGE("Fail to set HDMI clone mode");
            broadcastModeChange_l(mMode);
            return MDS_ERROR;
        }

        mMode &= ~MDS_HDMI_VIDEO_EXT;
        mMode |= MDS_HDMI_CLONE;
        broadcastModeChange_l(mMode);
    }

    if (notify_audio_hotplug && mDrmInit) {
        // Do not need to notify HDMI audio driver about hotplug during startup.
        LOGI("Notify HDMI audio drvier hot plug event.");
        drm_hdmi_notify_audio_hotplug(true);
    }

    LOGV("Leaving %s, new mode is %#x", __func__, mMode);
    return MDS_NO_ERROR;
}

void MultiDisplayComposer::broadcastModeChange_l(int mode) {
   for (unsigned int index = 0; index < mMCListenerVector.size(); index++) {
        if (mMCListenerVector.valueAt(index) != NULL) {
            mMCListenerVector.valueAt(index)->onModeChange(mode);
        }
    }
}

int MultiDisplayComposer::setMipiMode_l(bool on) {
    Mutex::Autolock _l(mLock);

    if (mMipiOn == on)
        return MDS_NO_ERROR;

    if (on) {
        drm_mipi_setMode(DRM_MIPI_ON);
        mMode |= MDS_MIPI_ON;
    } else {
        if(!mWidiVideoExt) {
            if (!checkMode(mMode, MDS_HDMI_VIDEO_EXT)) {
                LOGW("%s: Attempt to turn off Mipi while not in extended video mode.", __func__);
                broadcastModeChange_l(mMode);
                return MDS_ERROR;
            }
            if (mMipiPolicy == MDS_MIPI_OFF_ALLOWED) {
                drm_mipi_setMode(DRM_MIPI_OFF);
                mMode &= ~MDS_MIPI_ON;
            }
        } else {
            drm_mipi_setMode(DRM_MIPI_OFF);
            mMode &= ~MDS_MIPI_ON;
        }
    }
    mMipiOn = on;
    return MDS_NO_ERROR;
}

int MultiDisplayComposer::notifyHotPlug() {
    if (mDrmInit == false) {
        LOGE("%s: drm_init fails", __func__);
        return MDS_ERROR;
    }
    LOGV("%s: mipi policy: %d, hdmi policy: %d, mode: 0x%x", __func__, mMipiPolicy, mHdmiPolicy, mMode);
    Mutex::Autolock _l(mLock);
    return setHdmiMode_l();
}

int MultiDisplayComposer::setModePolicy(int policy) {
    if (mDrmInit == false) {
        LOGE("%s: drm_init fails", __func__);
        return MDS_ERROR;
    }
    Mutex::Autolock _l(mLock);
    return setModePolicy_l(policy);
}

int MultiDisplayComposer::setModePolicy_l(int policy) {
    int ret = 0;
    unsigned int index = 0;
    LOGI("%s: policy %d, mHdmiPolicy 0x%x, mMipiPolicy 0x%x, mMode: 0x%x",
            __func__, policy, mHdmiPolicy, mMipiPolicy, mMode);
    switch (policy) {
        case MDS_HDMI_ON_NOT_ALLOWED:
        case MDS_HDMI_ON_ALLOWED:
            mHdmiPolicy = policy;
            return setHdmiMode_l();

        case MDS_MIPI_OFF_NOT_ALLOWED:
            mMipiPolicy = policy;
            if (!checkMode(mMode, MDS_MIPI_ON)) {
                drm_mipi_setMode(DRM_MIPI_ON);
            }
            mMode |= MDS_MIPI_ON;
            mMipiOn = true;
            break;
        case MDS_MIPI_OFF_ALLOWED:
            mMipiPolicy = policy;
            if (checkMode(mMode, MDS_MIPI_ON)) {
                drm_mipi_setMode(DRM_MIPI_OFF);
            }
            mMode &= ~MDS_MIPI_ON;
            mMipiOn = false;
            break;
        default:
            return MDS_ERROR;
    }
    broadcastModeChange_l(mMode);
    return MDS_NO_ERROR;
}

int MultiDisplayComposer::registerModeChangeListener(
                sp<IExtendDisplayModeChangeListener> listener, void *handle) {
    unsigned int i = 0;
    if (mDrmInit == false) {
        LOGE("%s: drm_init fails", __func__);
        return MDS_ERROR;
    }
    Mutex::Autolock _l(mLock);
    LOGV("%s: mMode: 0x%x %x %d", __func__, mMode, (unsigned)handle, mMCListenerVector.size());
    for (i = 0; i < mMCListenerVector.size(); i++) {
        if (mMCListenerVector.keyAt(i) == handle) {
            LOGE("%s register error!", __func__);
            return MDS_ERROR;
        }
    }
    mMCListenerVector.add(handle, listener);
    return MDS_NO_ERROR;
}

int MultiDisplayComposer::unregisterModeChangeListener(
                sp<IExtendDisplayModeChangeListener> listener, void *handle) {
    unsigned int i = 0;
    if (mDrmInit == false) {
        LOGE("%s: drm_init fails", __func__);
        return MDS_ERROR;
    }
    Mutex::Autolock _l(mLock);
    LOGV("%s: mMode: 0x%x %x %d", __func__, mMode, (unsigned)handle, mMCListenerVector.size());
    for (i = 0; i < mMCListenerVector.size(); i++) {
        if (mMCListenerVector.keyAt(i) == handle) {
            mMCListenerVector.removeItem(handle);
            break;
        }
    }
    return MDS_NO_ERROR;
}

int MultiDisplayComposer::getHdmiPlug_l() {
    return drm_hdmi_connectStatus();
}

int MultiDisplayComposer::getHdmiModeInfo(int* pWidth, int* pHeight,
                                          int* pRefresh, int* pInterlace,
                                          int *pRatio) {
    if (mDrmInit == false) {
        LOGE("%s: drm_init fails", __func__);
        return MDS_ERROR;
    }
    Mutex::Autolock _l(mLock);
    LOGV("%s: mMode: 0x%x", __func__, mMode);
    if (pWidth == NULL || pHeight == NULL ||
            pRefresh == NULL || pInterlace == NULL) {
      return drm_hdmi_getModeInfo(NULL, NULL, NULL, NULL, NULL);
    }
    return drm_hdmi_getModeInfo(pWidth, pHeight, pRefresh, pInterlace, pRatio);
}

int MultiDisplayComposer::setHdmiModeInfo(int width, int height,
                            int refresh, int interlace, int ratio) {
    bool ret = false;
    MDSHDMITiming timing;
    if (mDrmInit == false) {
        LOGE("%s: drm_init fails", __func__);
        return MDS_ERROR;
    }
    Mutex::Autolock _l(mLock);
    LOGV("%s: \
        \n  mMode: %d, \
        \n  width: %d, \
        \n  height: %d, \
        \n  refresh: %d, \
        \n  ratio: %d, \
        \n  interlace: %d",
         __func__, mMode, width, height, refresh, ratio, interlace);
    timing.width = width;
    timing.height = height;
    timing.refresh = refresh;
    timing.interlace = interlace;
    timing.ratio = ratio;

    ret = drm_hdmi_setMode(DRM_HDMI_CLONE, &timing);
    broadcastModeChange_l(mMode);
    return (ret == true ? MDS_NO_ERROR : MDS_ERROR);
}

int MultiDisplayComposer::setHdmiScaleType(int type) {
    if (mDrmInit == false) {
        LOGE("%s: drm_init fails", __func__);
        return MDS_ERROR;
    }
    Mutex::Autolock _l(mLock);
    bool ret = false;
    LOGV("%s: type: %d", __func__, type);
    ret = drm_hdmi_setScaling(type);
    return (ret == true ? 0 : -1);
}

int MultiDisplayComposer::setHdmiScaleStep(int hValue, int vValue) {
    if (mDrmInit == false) {
        LOGE("%s: drm_init fails", __func__);
        return MDS_ERROR;
    }
    Mutex::Autolock _l(mLock);
    bool ret = false;
    LOGV("%s: hValue: %d, vValue: %d", __func__, hValue, vValue);
    ret = drm_hdmi_setScaleStep(hValue, vValue);
    return (ret == true ? 0 : -1);
}

int MultiDisplayComposer::getHdmiDeviceChange() {
    if (mDrmInit == false) {
        LOGE("%s: drm_init fails", __func__);
        return MDS_ERROR;
    }
    Mutex::Autolock _l(mLock);
    return drm_hdmi_getDeviceChange();
}

int MultiDisplayComposer::getVideoInfo(int* dw, int* dh, int* fps, int* interlace) {
    if (mDrmInit == false) {
        LOGE("%s: drm_init fails", __func__);
        return MDS_ERROR;
    }
    //TODO: here don't need to lock mLock
    if (!mVideo.isplaying) {
        LOGE("%s: Video player is not in playing state", __func__);
        return MDS_ERROR;
    }
    if (dw)
        *dw = mVideo.displayH;
    if (dh)
        *dh = mVideo.displayH;
    if (fps)
        *fps = mVideo.frameRate;
    if (interlace)
        *interlace = mVideo.isinterlace;
    return MDS_NO_ERROR;
}

bool MultiDisplayComposer::threadLoop() {
    bool mipiOn;
    while(true) {
        {
            Mutex::Autolock _l(mMipiLock);
            if (mMipiReq == NO_MIPI_REQ)
                mMipiCon.wait(mMipiLock);
            mipiOn = (mMipiReq == MIPI_ON_REQ) ? true : false;
            mMipiReq = NO_MIPI_REQ;
            LOGI("%s: receive a mipi message, %d", __func__, mipiOn);
        }
        setMipiMode_l(mipiOn);
    }
    return MDS_NO_ERROR;
}
