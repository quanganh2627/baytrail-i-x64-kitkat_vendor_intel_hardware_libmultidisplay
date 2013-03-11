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
 * Author: tianyang.zhu@intel.com
 */

//#define LOG_NDEBUG 0

#include <utils/Log.h>
#include <binder/IServiceManager.h>
#include <binder/IPCThreadState.h>
#include <display/IMultiDisplayComposer.h>
#include <display/MultiDisplayComposer.h>

extern "C" {
#include "drm_hdmi.h"
#ifdef ENABLE_HDCP
#include "drm_hdcp.h"
#endif
}

using namespace android;

#define MDC_CHECK_INIT() \
do { \
    if (mDrmInit == false) { \
        LOGE("%s: drm_init fails", __func__); \
        return MDS_ERROR; \
    } \
} while(0)

MultiDisplayListener::MultiDisplayListener(int msg,
        const char* client, sp<IExtendDisplayListener> listener) {
    mMsg = msg;
    int len = strlen(client) + 1;
    mName = new char[len];
    strncpy(mName, client, len - 1);
    *(mName + len - 1) = '\0';
    LOGD("%s: Register a new %s client, 0x%x", __func__, client, msg);
    mIEListener = listener;
}

MultiDisplayListener::~MultiDisplayListener() {
    LOGD("%s: Unregister client %s", __func__, mName);
    mMsg = 0;
    delete [] mName;
    mName = NULL;
    mIEListener = NULL;
}

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
    mEnablePlayInBackground = false;
    mBackgroundPlayerId = 0;
    mNativeSurface = NULL;
    mDrmInit = true;
    mHdcpStatus = 0;

    initDisplayCapability_l();
    LOGI("%s: mMode: 0x%x", __func__, mMode);

    mSurfaceComposer = NULL;
    mScaleMode = 0;
    mScaleStepX = 0;
    mScaleStepY = 0;
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
    if (!mListener.isEmpty()) {
        mListener.clear();
    }
    memset((void*)(&mVideo), 0, sizeof(MDSVideoInfo));
    LOGV("%s: mMode: 0x%x", __func__, mMode);
}

int MultiDisplayComposer::getMode(bool wait) {
    MDC_CHECK_INIT();
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
#if 0
    MDC_CHECK_INIT();
    // It should hold mLock here instead. It is safe not holding any lock however.
    //Mutex::Autolock _l(mMipiLock);
    mWidiVideoExt = on;
    if (mWidiVideoExt)
        mMode |= MDS_WIDI_ON;
    else
        mMode &= ~MDS_WIDI_ON;

    drm_widi_notify(on, this, (void *)widi_rm_notifier_handler);
#endif
    return MDS_NO_ERROR;
}

int MultiDisplayComposer::notifyMipi(bool on) {
    MDC_CHECK_INIT();
    Mutex::Autolock _l(mMipiLock);
    mMipiReq = on ? MIPI_ON_REQ : MIPI_OFF_REQ;
    mMipiCon.signal();
    return MDS_NO_ERROR;
}

int MultiDisplayComposer::isMdsSurface(int* nw) {
    Mutex::Autolock _l(mBackgroundPlayLock);
    return(nw == mNativeSurface ? 1 : 0 );
}

int MultiDisplayComposer::setHdmiPowerOff() {
    MDC_CHECK_INIT();
    Mutex::Autolock _l(mLock);
    drm_hdmi_setHdmiPowerOff();
    return MDS_NO_ERROR;
}

int MultiDisplayComposer::updateVideoInfo(MDSVideoInfo* info) {
    MDC_CHECK_INIT();
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

    memset(&timing, 0, sizeof(MDSHDMITiming));
    LOGI("Entering %s, current mode = %#x", __func__, mMode);
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
        broadcastMessage_l(MDS_MODE_CHANGE, &mMode, sizeof(mMode));
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
            broadcastMessage_l(MDS_MODE_CHANGE, &mMode, sizeof(mMode));
            return MDS_NO_ERROR;
        }

        LOGI("Notify HDMI audio driver hot unplug event.");
        drm_hdmi_notify_audio_hotplug(false);
#ifdef ENABLE_HDCP
        drm_hdcp_disable_hdcp(false);
#endif
        mMode &= ~MDS_HDMI_CONNECTED;
        mMode &= ~MDS_HDMI_ON;
        mMode &= ~MDS_HDCP_ON;
        mMode &= ~MDS_HDMI_CLONE;
        mMode &= ~MDS_HDMI_VIDEO_EXT;
        mMode &= ~MDS_OVERLAY_OFF;
        broadcastMessage_l(MDS_MODE_CHANGE, &mMode, sizeof(mMode));
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
            broadcastMessage_l(MDS_MODE_CHANGE, &mMode, sizeof(mMode));
            return MDS_NO_ERROR;
        }
#ifdef ENABLE_HDCP
        if (checkMode(mMode, MDS_HDCP_ON)) {
            drm_hdcp_disable_hdcp(true);
        }
        mMode &= ~MDS_HDCP_ON;
#endif
        mMode &= ~MDS_HDMI_ON;
        mMode &= ~MDS_HDMI_CLONE;
        mMode &= ~MDS_HDMI_VIDEO_EXT;
        mMode &= ~MDS_OVERLAY_OFF;
        broadcastMessage_l(MDS_MODE_CHANGE, &mMode, sizeof(mMode));
        drm_hdmi_setHdmiVideoOff();
        return MDS_NO_ERROR;
    }
    // Turn off overlay temporarily during mode transition.
    // Make sure overlay is turned on when this function exits.
    // Transition mode starts with standalone local mipi mode (no cloned, no video extended).
    int transitionalMode = mMode;
    transitionalMode &= ~MDS_HDMI_CLONE;
    transitionalMode &= ~MDS_HDMI_VIDEO_EXT;
    transitionalMode |= MDS_OVERLAY_OFF;

    if (mVideo.isplaying == true) {
        LOGI("Video is in playing state. Mode = %#x", mMode);
        if (checkMode(mMode, MDS_HDMI_VIDEO_EXT)) {
            LOGW("HDMI is already in Video Extended mode.");
            broadcastMessage_l(MDS_MODE_CHANGE, &mMode, sizeof(mMode));
            return MDS_NO_ERROR;
        }
        broadcastMessage_l(MDS_MODE_CHANGE, &transitionalMode, sizeof(transitionalMode));

        timing.width = mVideo.displayW;
        timing.height = mVideo.displayH;
        timing.refresh = mVideo.frameRate;
        timing.interlace = 0;
        timing.ratio = 0;

        //TODO: Need check here to avoid send timing setting message to HWC
        //if new timing == old timing
        int index = drm_hdmi_checkTiming(DRM_HDMI_VIDEO_EXT, &timing);
        int ret = setHdmiTiming_l((void*)&timing, sizeof(MDSHDMITiming));
        if (ret < MDS_NO_ERROR) {
            LOGE("Fail to set HDMI extended mode");
            broadcastMessage_l(MDS_MODE_CHANGE, &mMode, sizeof(mMode));
            return MDS_ERROR;
        } else {
            drm_hdmi_saveMode(DRM_HDMI_VIDEO_EXT, ret);
            if (ret != index)
                LOGW("%s: Using HDMI mode %d to instead of %d", __func__, ret, index);
        }
#ifdef ENABLE_HDCP
        if ((mVideo.isprotected) || mHdcpStatus) {
            LOGI("Turning on HDCP...");
            if (drm_hdcp_enable_hdcp() == false) {
                LOGE("Fail to enable HDCP.");
                // Continue mode setting as it may be recovered, unless HDCP is not supported.
                // If HDCP is not supported, user will have to unplug the cable to restore video to phone.
            }
            mMode |= MDS_HDCP_ON;
        }
#endif
        mMode |= MDS_HDMI_VIDEO_EXT;
        mMode &= ~MDS_HDMI_CLONE;
        mMipiPolicy = MDS_MIPI_OFF_ALLOWED;
        //prolong overlay off time
        mMode |= MDS_OVERLAY_OFF;
        broadcastMessage_l(MDS_MODE_CHANGE, &mMode, sizeof(mMode));
    } else {
        LOGI("Video is not in playing state. Mode = %#x", mMode);
        if (checkMode(mMode, MDS_HDMI_CLONE)) {
            LOGW("HDMI is already in cloned state.");
            broadcastMessage_l(MDS_MODE_CHANGE, &mMode, sizeof(mMode));
            return MDS_NO_ERROR;
        }

        broadcastMessage_l(MDS_MODE_CHANGE, &transitionalMode, sizeof(transitionalMode));
#ifdef ENABLE_HDCP
        if (checkMode(mMode, MDS_HDCP_ON)) {
            drm_hdcp_disable_hdcp(true);
        }
        mMode &= ~MDS_HDCP_ON;
#endif

        //TODO: Need check here to avoid send timing setting message to HWC
        //if new timing == old timing
        int index = -1;
        int ret = -1;
        if (isHwcSetUp_l()) {
            index = drm_hdmi_checkTiming(DRM_HDMI_CLONE, &timing);
            ret = setHdmiTiming_l((void*)&timing, sizeof(MDSHDMITiming));
            if (ret < MDS_NO_ERROR) {
                LOGE("Fail to set HDMI clone mode");
                broadcastMessage_l(MDS_MODE_CHANGE, &mMode, sizeof(mMode));
                return MDS_ERROR;
            }
        } else
            ret = drm_initHdmiMode();
        drm_hdmi_saveMode(DRM_HDMI_CLONE, ret);
        if (ret != index)
            LOGW("%s: Using HDMI mode %d to instead of %d", __func__, ret, index);

        mMode &= ~MDS_HDMI_VIDEO_EXT;
        mMode |= MDS_HDMI_CLONE;
        //prolong overlay off time
        mMode |= MDS_OVERLAY_OFF;
        broadcastMessage_l(MDS_MODE_CHANGE, &mMode, sizeof(mMode));
    }

    // Common case, turn on HDMI if necessary
    if (!checkMode(mMode, MDS_HDMI_ON)) {
        LOGI("Turn on HDMI...");
        if (!drm_hdmi_setHdmiVideoOn()) {
            LOGI("Fail to turn on HDMI.");
            mMode &= ~MDS_HDCP_ON;
            mMode &= ~MDS_HDMI_CLONE;
            mMode &= ~MDS_HDMI_VIDEO_EXT;
            broadcastMessage_l(MDS_MODE_CHANGE, &mMode, sizeof(mMode));
            return MDS_ERROR;
        }
    }

    mMode |= MDS_HDMI_ON;
    if (checkMode(mMode, MDS_HDMI_VIDEO_EXT)) {
        //Enable overlay lastly
        mMode &= ~MDS_OVERLAY_OFF;
    }

    broadcastMessage_l(MDS_MODE_CHANGE, &mMode, sizeof(mMode));

    if (notify_audio_hotplug && mDrmInit) {
        // Do not need to notify HDMI audio driver about hotplug during startup.
        LOGI("Notify HDMI audio drvier hot plug event.");
        drm_hdmi_notify_audio_hotplug(true);
    }

    LOGI("Leaving %s, new mode is %#x", __func__, mMode);
    return MDS_NO_ERROR;
}

void MultiDisplayComposer::broadcastMessage_l(int msg, void* value, int size) {
    for (unsigned int index = 0; index < mListener.size(); index++) {
        MultiDisplayListener* listener = mListener.valueAt(index);
        if (listener == NULL)
            continue;
        if (listener->getName() != NULL)
            LOGI("%s: Broadcast message 0x%x to %s, 0x%x", __func__, msg, listener->getName(), *((int*)value));
        if (listener->checkMsg(msg)) {
            sp<IExtendDisplayListener> ielistener = listener->getIEListener();
            if (ielistener != NULL)
                ielistener->onMdsMessage(msg, value, size);
        }
    }
}

int MultiDisplayComposer::isHwcSetUp_l() {
    bool hasHwc = false;
    for (unsigned int index = 0; index < mListener.size(); index++) {
        MultiDisplayListener* listener = mListener.valueAt(index);
        if (listener == NULL)
            continue;
        char* client = listener->getName();
        if (client && !strcmp(client, "HWComposer")) {
            hasHwc = true;
            break;
        }
    }
    ALOGD("HWC is %s", (hasHwc ? "set up" :"not set up"));
    return hasHwc;
}

int MultiDisplayComposer::setHdmiTiming_l(void* value, int size) {
    int ret = MDS_ERROR;
    sp<IExtendDisplayListener> ielistener = NULL;
    bool hasHwc = false;
    for (unsigned int index = 0; index < mListener.size(); index++) {
        MultiDisplayListener* listener = mListener.valueAt(index);
        if (listener == NULL)
            continue;
        char* client = listener->getName();
        if (client && !strcmp(client, "HWComposer")) {
            hasHwc = true;
            if (listener->checkMsg(MDS_SET_TIMING)) {
                LOGV("%s: Set HDMI timing through HWC", __func__);
                ielistener = listener->getIEListener();
                break;
            }
        }
    }
    if (ielistener != NULL)
        ret = ielistener->onMdsMessage(MDS_SET_TIMING, value, size);
    else if (!hasHwc)
        ret = MDS_NO_ERROR;
    return ret;
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
                broadcastMessage_l(MDS_MODE_CHANGE, &mMode, sizeof(mMode));
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
    MDC_CHECK_INIT();
    LOGV("%s: mipi policy: %d, hdmi policy: %d, mode: 0x%x", __func__, mMipiPolicy, mHdmiPolicy, mMode);
    Mutex::Autolock _l(mLock);
    return setHdmiMode_l();
}

int MultiDisplayComposer::setModePolicy(int policy) {
    MDC_CHECK_INIT();
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
    broadcastMessage_l(MDS_MODE_CHANGE, &mMode, sizeof(mMode));
    return MDS_NO_ERROR;
}

int MultiDisplayComposer::registerListener(
                sp<IExtendDisplayListener> listener,
                void *handle, const char* name, int msg) {
    unsigned int i = 0;
    MDC_CHECK_INIT();
    if (name == NULL || msg == 0) {
        LOGE("%s: Failed to register a no-name or no-message client", __func__);
        return MDS_ERROR;
    }
    Mutex::Autolock _l(mLock);
    for (i = 0; i < mListener.size(); i++) {
        if (mListener.keyAt(i) == handle) {
            LOGE("%s register error!", __func__);
            return MDS_ERROR;
        }
    }
    MultiDisplayListener* tlistener = new  MultiDisplayListener(msg, name, listener);
    mListener.add(handle, tlistener);
    return MDS_NO_ERROR;
}

int MultiDisplayComposer::unregisterListener(void *handle) {
    unsigned int i = 0;
    MDC_CHECK_INIT();
    Mutex::Autolock _l(mLock);
    for (i = 0; i < mListener.size(); i++) {
        if (mListener.keyAt(i) == handle) {
            MultiDisplayListener* tlistener = mListener.valueAt(i);
            mListener.removeItem(handle);
            delete tlistener;
            tlistener = NULL;
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
    MDC_CHECK_INIT();
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
    MDSHDMITiming timing;
    MDC_CHECK_INIT();
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

    //TODO: Need check here to avoid send timing setting message to HWC
    //if new timing == old timing
    int index = drm_hdmi_checkTiming(DRM_HDMI_CLONE, &timing);
    int ret = setHdmiTiming_l((void*)&timing, sizeof(MDSHDMITiming));
    if (ret < MDS_NO_ERROR) {
        LOGE("%s: Failed to set HDMI clone mode", __func__);
        broadcastMessage_l(MDS_MODE_CHANGE, &mMode, sizeof(mMode));
        return MDS_ERROR;
    } else {
        drm_hdmi_saveMode(DRM_HDMI_CLONE, ret);
        if (ret != index)
            LOGW("%s: Using HDMI mode %d to instead of %d", __func__, ret, index);
        ret = MDS_NO_ERROR;
    }
    broadcastMessage_l(MDS_MODE_CHANGE, &mMode, sizeof(mMode));
    return ret;
}

int MultiDisplayComposer::setDisplayScalingLocked(uint32_t mode,
         uint32_t stepx, uint32_t stepy) {
    if (mSurfaceComposer == NULL) {
        const sp<IServiceManager> sm = defaultServiceManager();
        const String16 name("SurfaceFlinger");
        mSurfaceComposer = sm->getService(name);
        if (mSurfaceComposer == NULL) {
            return -1;
        }
    }

    uint32_t scale;
    Parcel data, reply;
    const String16 token("android.ui.ISurfaceComposer");

    scale = mode | stepx << 16 | stepy << 24;
    data.writeInterfaceToken(token);
    data.writeInt32(scale);
    mSurfaceComposer->transact(1014, data, &reply);
    return reply.readInt32();
}

int MultiDisplayComposer::setHdmiScaleType(int type) {
    MDC_CHECK_INIT();
    Mutex::Autolock _l(mLock);

    mScaleMode = type;
    return setDisplayScalingLocked(mScaleMode, mScaleStepX, mScaleStepY);
}

int MultiDisplayComposer::setHdmiScaleStep(int hValue, int vValue) {
    MDC_CHECK_INIT();
    Mutex::Autolock _l(mLock);

    mScaleStepX = (hValue > 5) ? 0: (5 - hValue);
    mScaleStepY = (vValue > 5) ? 0: (5 - vValue);
    return setDisplayScalingLocked(mScaleMode, mScaleStepX, mScaleStepY);
}

int MultiDisplayComposer::getHdmiDeviceChange() {
    MDC_CHECK_INIT();
    Mutex::Autolock _l(mLock);
    return drm_hdmi_getDeviceChange();
}

int MultiDisplayComposer::getVideoInfo(int* dw, int* dh, int* fps, int* interlace) {
    MDC_CHECK_INIT();
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

void MultiDisplayComposer::initDisplayCapability_l()
{
    // Default value
    mDisplayCapability = MDS_HW_SUPPORT_WIDI;
    if (drm_check_hw_supportHdmi())
        mDisplayCapability |= MDS_HW_SUPPORT_HDMI;
    return;
}

int MultiDisplayComposer::getDisplayCapability()
{
    return mDisplayCapability;
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

void MultiDisplayComposer::setWidiOrientationInfo(int orientation) {
#if 0

    Mutex::Autolock _l(mLock);
    broadcastMessage_l(MDS_ORIENTATION_CHANGE, &orientation, sizeof(orientation));
#endif
}

int MultiDisplayComposer::widi_rm_notifier_handler(void* cookie, int cmd, int data) {
    int ret = 0;
#if 0
    MultiDisplayComposer* mdsComposerObj = (MultiDisplayComposer *)cookie;
    switch(cmd) {
        case CMD_ROTATION_MODE:
            LOGV("MultiDisplayComposer::widi_rm_notifier_handler CMD_ROTATION_MODE");
            mdsComposerObj->setWidiOrientationInfo(data);
            ret = 1;
            break;
        default:
            LOGE("MultiDisplayComposer::widi_rm_notifier_handler DEFAULT !!!");
            break;
    }
#endif
    return ret;
}

int MultiDisplayComposer::enablePlayInBackground(bool on, int playerId) {
    Mutex::Autolock _l(mBackgroundPlayLock);
    mEnablePlayInBackground = on;
    mBackgroundPlayerId = playerId;
    int playInBgEnabled = (mEnablePlayInBackground == true ? 1 : 0);
    broadcastMessage_l(MDS_SET_BACKGROUND_VIDEO_MODE, &playInBgEnabled, sizeof(playInBgEnabled));
    return MDS_NO_ERROR;
}

int MultiDisplayComposer::setNativeSurface(int* surface) {
    if (surface == NULL)
        return MDS_ERROR;
    Mutex::Autolock _l(mBackgroundPlayLock);
    mNativeSurface = surface;
    return MDS_NO_ERROR;
}

int MultiDisplayComposer::isPlayInBackgroundEnabled() {
    Mutex::Autolock _l(mBackgroundPlayLock);
    int playInBgEnabled = (mEnablePlayInBackground == true ? 1 : 0);
    return playInBgEnabled;
}

int MultiDisplayComposer::getBackgroundPlayerId() {
    Mutex::Autolock _l(mBackgroundPlayLock);
    return mBackgroundPlayerId;
}

int MultiDisplayComposer::setHdcpStatus(int value) {
    MDC_CHECK_INIT();
    Mutex::Autolock _l(mLock);
    LOGV("%s: HDCP Status: %d", __func__, value);
    mHdcpStatus = value;
    return MDS_NO_ERROR;
}
