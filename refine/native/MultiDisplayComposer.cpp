/*
 * Copyright (c) 2012-2013, Intel Corporation. All rights reserved.
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

#define LOG_NDEBUG 0

#include <utils/Log.h>
#include <binder/IServiceManager.h>
#include <binder/IPCThreadState.h>
#include <display/MultiDisplayComposer.h>

extern "C" {
#include "drm_hdmi.h"
}

using namespace android;

#define MDC_CHECK_INIT() \
do { \
    if (mDrmInit == false) { \
        LOGE("%s: drm_init fails", __func__); \
        return NO_INIT; \
    } \
} while(0)

MultiDisplayListener::MultiDisplayListener(int msg,
        const char* client, sp<IMultiDisplayListener> listener) {
    mMsg = msg;
    int len = strlen(client) + 1;
    mName = new char[len];
    strncpy(mName, client, len - 1);
    *(mName + len - 1) = '\0';
    LOGD("%s: Register a new %s client, 0x%x", __func__, client, msg);
    mListener = listener;
}

MultiDisplayListener::~MultiDisplayListener() {
    LOGD("%s: Unregister client %s", __func__, mName);
    mMsg = 0;
    delete [] mName;
    mName = NULL;
    mListener = NULL;
}

MultiDisplayComposer::MultiDisplayComposer() :
    mDrmInit(false),
    mMode(MDS_MODE_NONE),
    mCapability(MDS_DISPLAY_CAP_UNKNOWN),
    mVideoState(MDS_VIDEO_UNPREPARED),
    mScaleType(MDS_SCALING_NONE),
    mHorizontalStep(0),
    mVerticalStep(0),
    mSurfaceComposer(NULL),
    mMDSCallback(NULL)
{
    init();
}

MultiDisplayComposer::~MultiDisplayComposer() {
    drm_cleanup();

    // Remove all the listeners.
    size_t size = mListeners.size();
    if (size > 0) {
        MultiDisplayListener* listener = NULL;
        for (size_t i = 0; i < size; i++) {
            listener = mListeners.valueAt(i);
            delete listener;
        }
        mListeners.clear();
    }

    mSurfaceComposer = NULL;
    mMDSCallback = NULL;
}

void MultiDisplayComposer::init() {
    if (!drm_init()) {
        LOGE("%s: drm_init fails.", __func__);
        return;
    }

    mDrmInit = true;

    // TODO: read the capability from a system config file.
    mCapability = MDS_SUPPORT_PRIMARY_DISPLAY |
                  MDS_SUPPORT_VIRTUAL_DISPLAY;

    if (drm_hdmi_isSupported()) {
        mCapability |= MDS_SUPPORT_EXTERNAL_DISPLAY;
    }

    memset((void*)(&mVideoInfo), 0, sizeof(MDSVideoSourceInfo));
    updateHdmiConnectStatusLocked();
}

status_t MultiDisplayComposer::updateHdmiConnectStatusLocked() {
    MDC_CHECK_INIT();

    int connectStatus = drm_hdmi_getConnectionStatus();
    if (connectStatus == DRM_HDMI_CONNECTED) {
        mMode |= MDS_HDMI_CONNECTED;
    } else {
        // TODO: check dvi connection
        mMode &= ~MDS_HDMI_CONNECTED;
        drm_hdmi_onHdmiDisconnected();
    }
    return NO_ERROR;
}

status_t MultiDisplayComposer::registerCallback(sp<IMultiDisplayCallback> cbk) {
    Mutex::Autolock lock(mMutex);
    if (mMDSCallback != NULL) {
        mMDSCallback = NULL;
    }
    ALOGV("%s ", __func__);
    mMDSCallback = cbk;

    // Make sure the hdmi status is aligned
    // between MDS and hwc.
    updateHdmiConnectStatusLocked();
    return NO_ERROR;
}

status_t MultiDisplayComposer::unregisterCallback() {
    Mutex::Autolock lock(mMutex);
    ALOGV("%s ", __func__);
    mMDSCallback = NULL;
    return NO_ERROR;
}

status_t MultiDisplayComposer::notifyHotPlug(
        MDS_DISPLAY_ID dispId, bool connected) {
    Mutex::Autolock lock(mMutex);
    ALOGV("%s id:%d connect:%d", __func__, dispId, connected);
    if (dispId != MDS_DISPLAY_EXTERNAL) {
        ALOGW("%s:support external display only.", __func__);
        return NO_ERROR;
    }

    // Notify hotplug and switch audio
    int mode = mMode;
    if (connected && (mVideoState == MDS_VIDEO_PREPARED))
        mMode |= MDS_HDMI_VIDEO_EXT;
    else
        mMode &= ~MDS_HDMI_VIDEO_EXT;

    updateHdmiConnectStatusLocked();

    if (mode != mMode) {
        int connection = connected ? 1 : 0;
        broadcastMessageLocked(MDS_MSG_HOT_PLUG, &connection, sizeof(connection));
        broadcastMessageLocked(MDS_MSG_MODE_CHANGE, &mMode, sizeof(mMode));
        drm_hdmi_notify_audio_hotplug(connected);
    }

    if (drm_hdmi_isDeviceChanged(false) && connected)
        setDisplayScalingLocked(0, 0, 0);

    return NO_ERROR;
}

status_t MultiDisplayComposer::setVideoState(MDS_VIDEO_STATE state) {
    Mutex::Autolock lock(mMutex);

    mVideoState = state;
    ALOGV("%s state:%d", __func__, state);

    status_t result = NO_ERROR;
    if (mMDSCallback != NULL) {
        result = mMDSCallback->setVideoState(state);
    }

    int mode = mMode;
    if (state == MDS_VIDEO_PREPARED && (mMode & MDS_HDMI_CONNECTED))
        mMode |= MDS_HDMI_VIDEO_EXT;
    else
        mMode &= ~MDS_HDMI_VIDEO_EXT;

    if (mode != mMode)
        broadcastMessageLocked(MDS_MSG_MODE_CHANGE, &mMode, sizeof(mMode));

    return result;
}

MDS_VIDEO_STATE MultiDisplayComposer::getVideoState() {
    Mutex::Autolock lock(mMutex);
    ALOGV("%s state:%d", __func__, mVideoState);
    return mVideoState;
}

status_t MultiDisplayComposer::setVideoSourceInfo(MDSVideoSourceInfo* info) {
    MDC_CHECK_INIT();
    if (info == NULL) {
        ALOGE("%s: video info is null", __func__);
        return BAD_VALUE;
    }
    Mutex::Autolock lock(mMutex);
    ALOGV("%s:\nmode[0x%x]playing[%d]protected[%d]w[%d]h[%d]fps[%d]interlace[%d]",
        __func__, mMode, info->isplaying, info->isprotected,
        info->displayW, info->displayH, info->frameRate, info->isinterlace);

    memcpy(&mVideoInfo, info, sizeof(MDSVideoSourceInfo));
    // TODO: for widi case
    // broadcastMessageLocked(MDS_MSG_VIDEO_SOURCE_INFO, &mMode, sizeof(mMode));

    return NO_ERROR;
}

status_t MultiDisplayComposer::getVideoSourceInfo(MDSVideoSourceInfo *info) {
    if (info == NULL) {
        ALOGE("%s: null pointer", __func__);
        return BAD_VALUE;
    }
    Mutex::Autolock lock(mMutex);
    memcpy(info, &mVideoInfo, sizeof(MDSVideoSourceInfo));
    return NO_ERROR;
}

status_t MultiDisplayComposer::setPhoneState(MDS_PHONE_STATE state) {
    Mutex::Autolock lock(mMutex);
    ALOGV("%s state:%d", __func__, state);
    if (mMDSCallback == NULL)
        return NO_INIT;

    return mMDSCallback->setPhoneState(state);
}

status_t MultiDisplayComposer::setDisplayState(
        MDS_DISPLAY_ID dispId,
        MDS_DISPLAY_STATE state) {
    Mutex::Autolock lock(mMutex);
    ALOGV("%s id:%d state:%d", __func__, dispId, state);
    if (mMDSCallback == NULL)
        return NO_INIT;

    return mMDSCallback->setDisplayState(dispId, state);
}

status_t MultiDisplayComposer::setDisplayTiming(
        MDS_DISPLAY_ID dispId, MDSDisplayTiming* timing) {
    Mutex::Autolock lock(mMutex);

    if (mMDSCallback == NULL)
        return NO_INIT;

    return mMDSCallback->setDisplayTiming(dispId, timing);
}

int MultiDisplayComposer::getDisplayTimingCount(MDS_DISPLAY_ID id) {
    Mutex::Autolock lock(mMutex);

    return drm_hdmi_getModeInfo(NULL, NULL, NULL, NULL, NULL);
}

status_t MultiDisplayComposer::getDisplayTimingList(
        MDS_DISPLAY_ID dispId, const int count, MDSDisplayTiming **list) {
    Mutex::Autolock lock(mMutex);

    int width[count];
    int height[count];
    int refresh[count];
    int interlace[count];
    int ratio[count];
    memset(width, 0, sizeof(width));
    memset(height, 0, sizeof(height));
    memset(refresh, 0, sizeof(refresh));
    memset(interlace, 0, sizeof(interlace));
    memset(ratio, 0, sizeof(ratio));

    int ret = drm_hdmi_getModeInfo(width, height, refresh, interlace, ratio);
    if (ret != count) {
        ALOGE("%s: get timing list error!", __func__);
        return UNKNOWN_ERROR;
    }

    for (int i = 0; i < count; i++) {
        list[i]->refresh   = refresh[i];
        list[i]->width     = width[i];
        list[i]->height    = height[i];
        list[i]->ratio     = ratio[i];
        list[i]->interlace = interlace[i];
    }

    return NO_ERROR;
}

status_t MultiDisplayComposer::getCurrentDisplayTiming(
        MDS_DISPLAY_ID dispId, MDSDisplayTiming* timing) {
    Mutex::Autolock lock(mMutex);

    return NO_ERROR;
}

status_t MultiDisplayComposer::setDisplayTimingByIndex(
        MDS_DISPLAY_ID dispId, int index) {
    Mutex::Autolock lock(mMutex);

    return NO_ERROR;
}

int MultiDisplayComposer::getCurrentDisplayTimingIndex(
        MDS_DISPLAY_ID dispId) {
    Mutex::Autolock lock(mMutex);
    return 0;
}

status_t MultiDisplayComposer::setScalingType(
        MDS_DISPLAY_ID dispId, MDS_SCALING_TYPE type) {
    Mutex::Autolock lock(mMutex);
    mScaleType = type;

    //TODO: implement in callback.
#if 0
    if (mMDSCallback == NULL)
        return NO_INIT;
    return mMDSCallback->setScalingType(dispId, type);
#endif

    status_t result = setDisplayScalingLocked((uint32_t)mScaleType,
            mHorizontalStep, mVerticalStep);
    return result;
}

status_t MultiDisplayComposer::setOverscan(
        MDS_DISPLAY_ID dispId, int hVal, int vVal) {
    Mutex::Autolock lock(mMutex);
    status_t result;

    //TODO: implement in callback.
#if 0
    if (mMDSCallback == NULL) {
        result = setDisplayScalingLocked((uint32_t)mScaleType, hVal, vVal);
    } else {
        result = mMDSCallback->setOverscan(dispId, hVal, vVal);
    }
#endif
    mHorizontalStep = (hVal > 5) ? 0: (5 - hVal);
    mVerticalStep = (vVal > 5) ? 0: (5 - vVal);
    ALOGD("%s h_val:%d v_val:%d", __func__, hVal, vVal);

    result = setDisplayScalingLocked((uint32_t)mScaleType,
            mHorizontalStep, mVerticalStep);
    return result;
}

bool MultiDisplayComposer::getDisplayDeviceChange(MDS_DISPLAY_ID dispId) {
    MDC_CHECK_INIT();
    Mutex::Autolock lock(mMutex);

    if (dispId == MDS_DISPLAY_EXTERNAL)
        return drm_hdmi_isDeviceChanged(true);

    return false;
}

MDS_DISPLAY_CAP MultiDisplayComposer::getPlatformCapability() {
    Mutex::Autolock lock(mMutex);
    return (MDS_DISPLAY_CAP)mCapability;
}

MDS_DISPLAY_MODE MultiDisplayComposer::getDisplayMode(bool wait) {
    if (wait) {
        mMutex.lock();
    } else {
        if (mMutex.tryLock() == -EBUSY) {
            return MDS_MODE_NONE;
        }
    }
    MDS_DISPLAY_MODE mode = (MDS_DISPLAY_MODE)mMode;
    mMutex.unlock();
    return mode;
}

status_t MultiDisplayComposer::registerListener(
        sp<IMultiDisplayListener> listener, void* handle,
        const char* name, MDS_MESSAGE msg) {
    if (name == NULL || msg == 0) {
        ALOGE("%s: intent to register a empty name or message", __func__);
        return BAD_VALUE;
    }

    Mutex::Autolock _l(mMutex);

    for (size_t i = 0; i < mListeners.size(); i++) {
        if (mListeners.keyAt(i) == handle) {
            ALOGE("%s %x is already registered!", __func__, (uint32_t)handle);
            return UNKNOWN_ERROR;
        }
    }

    MultiDisplayListener* tlistener =
        new MultiDisplayListener(msg, name, listener);
    mListeners.add(handle, tlistener);
    return NO_ERROR;
}

status_t MultiDisplayComposer::unregisterListener(void *handle) {
    Mutex::Autolock _l(mMutex);

    for (size_t i = 0; i < mListeners.size(); i++) {
        if (mListeners.keyAt(i) == handle) {
            MultiDisplayListener* tlistener = mListeners.valueAt(i);
            mListeners.removeItem(handle);
            delete tlistener;
            tlistener = NULL;
            break;
        }
    }
    return NO_ERROR;
}

void MultiDisplayComposer::broadcastMessageLocked(
        MDS_MESSAGE msg, void* value, int size) {
    if (mListeners.size() == 0)
        return;

    for (size_t index = 0; index < mListeners.size(); index++) {
        MultiDisplayListener* listener = mListeners.valueAt(index);
        if (listener == NULL)
            continue;

        if (listener->getName() != NULL)
            ALOGI("Broadcast message 0x%x to %s, 0x%x",
                    msg, listener->getName(), *((int*)value));

        if (listener->checkMsg(msg)) {
            sp<IMultiDisplayListener> ielistener = listener->getListener();
            if (ielistener != NULL)
                ielistener->onMdsMessage(msg, value, size);
        }
    }
}

status_t MultiDisplayComposer::setDisplayScalingLocked(uint32_t mode,
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
