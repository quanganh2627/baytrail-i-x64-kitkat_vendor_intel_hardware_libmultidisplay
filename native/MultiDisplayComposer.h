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

#ifndef __MULTIDISPLAY_COMPOSER_H__
#define __MULTIDISPLAY_COMPOSER_H__

#include <utils/String16.h>
#include <utils/RefBase.h>
#include <utils/Vector.h>
#include <hardware/hwcomposer_defs.h>
#include <display/IMultiDisplayListener.h>
#include <display/IMultiDisplayCallback.h>
#include <display/IMultiDisplayInfoProvider.h>

namespace android {
namespace intel {

/** @brief The display ID */
typedef enum {
    MDS_DISPLAY_PRIMARY   = HWC_DISPLAY_PRIMARY,
    MDS_DISPLAY_EXTERNAL  = HWC_DISPLAY_EXTERNAL,
    MDS_DISPLAY_VIRTUAL   = HWC_NUM_DISPLAY_TYPES,
} MDS_DISPLAY_ID;

static const int overscan_max = 5;

class MultiDisplayListener {
private:
    int   mMsg;
    String16* mName;
    sp<IMultiDisplayListener> mListener;
public:
    MultiDisplayListener(int msg, const char* client, sp<IMultiDisplayListener>);
    ~MultiDisplayListener();
    inline const char16_t* getName() {
        if (mName != NULL)
            return mName->string();
        return NULL;
    }
    inline int getMsg() {
        return mMsg;
    }
    inline sp<IMultiDisplayListener> getListener() {
        return mListener;
    }
    inline bool checkMsg(int msg) {
        return (msg & mMsg) ? true : false;
    }
};

class MultiDisplayVideoSession {
private:
    MDS_VIDEO_STATE mState;
    MDSVideoSourceInfo mInfo;
    bool infoValid;
    // Decoder output
    int32_t mDecoderConfigWidth;
    int32_t mDecoderConfigHeight;
public:

    inline MDS_VIDEO_STATE getState() {
        return mState;
    }
    inline status_t setState(MDS_VIDEO_STATE state) {
        if (state < MDS_VIDEO_PREPARING || state > MDS_VIDEO_UNPREPARED)
            return UNKNOWN_ERROR;
        mState = state;
        return NO_ERROR;
    }
    inline status_t getInfo(MDSVideoSourceInfo* info) {
        if (info == NULL || !infoValid)
            return UNKNOWN_ERROR;
        memcpy(info, &mInfo, sizeof(MDSVideoSourceInfo));
        return NO_ERROR;
    }
    inline status_t setInfo(const MDSVideoSourceInfo& info) {
        memcpy(&mInfo, &info, sizeof(MDSVideoSourceInfo));
        infoValid = true;
        return NO_ERROR;
    }
    inline status_t setDecoderOutputResolution(int32_t width, int32_t height) {
        if (mState < MDS_VIDEO_PREPARING || mState > MDS_VIDEO_UNPREPARED)
            return UNKNOWN_ERROR;
        mDecoderConfigWidth  = width;
        mDecoderConfigHeight = height;
        return NO_ERROR;
    }
    inline status_t getDecoderOutputResolution(int32_t* width, int32_t* height) {
        if (mState < MDS_VIDEO_PREPARING || mState > MDS_VIDEO_UNPREPARED)
            return UNKNOWN_ERROR;
        if (width == NULL || height == NULL)
            return UNKNOWN_ERROR;
        *width  = mDecoderConfigWidth;
        *height = mDecoderConfigHeight;
        return NO_ERROR;
    }
    inline void init() {
        mState = MDS_VIDEO_UNPREPARED;
        memset(&mInfo, 0, sizeof(MDSVideoSourceInfo));
        infoValid = false;
    }
    inline void dump(int index);
};

class MultiDisplayComposer : public RefBase {
public:
    MultiDisplayComposer();
    virtual ~MultiDisplayComposer();

    // Video control
    int allocateVideoSessionId();
    status_t updateVideoState(int, MDS_VIDEO_STATE);
    status_t resetVideoPlayback();
    status_t updateVideoSourceInfo(int, const MDSVideoSourceInfo&);

    // Infomation provider
    int getVideoSessionNumber();
    MDS_VIDEO_STATE getVideoState(int);
    status_t getVideoSourceInfo(int, MDSVideoSourceInfo*);
    MDS_DISPLAY_MODE getDisplayMode(bool);

    // Sink Registrar
    status_t registerListener(const sp<IMultiDisplayListener>&, const char*, int);
    status_t unregisterListener(const sp<IMultiDisplayListener>&);

    // Callback registrar
    status_t registerCallback(const sp<IMultiDisplayCallback>&);
    status_t unregisterCallback(const sp<IMultiDisplayCallback>&);

    // Hdmi control
    status_t setHdmiTiming(const MDSHdmiTiming&);
    int getHdmiTimingCount();
    status_t getHdmiTimingList(int, MDSHdmiTiming**);
    status_t getCurrentHdmiTiming(MDSHdmiTiming*);
    status_t setHdmiTimingByIndex(int);
    int getCurrentHdmiTimingIndex();
    status_t setHdmiScalingType(MDS_SCALING_TYPE);
    status_t setHdmiOverscan(int, int);

    // Display connection state observer
    status_t updateHdmiConnectionStatus(bool);
    status_t updateWidiConnectionStatus(bool);

    // Event monitor
    status_t updateInputState(bool);
    status_t updatePhoneCallState(bool);

    // Decoder configure
    status_t getDecoderOutputResolution(int, int32_t*, int32_t*);
    status_t setDecoderOutputResolution(int, int32_t,  int32_t);

private:
    bool mDrmInit;
    int mMode;
    //int mCapability;
    MultiDisplayVideoSession mVideos[MDS_VIDEO_SESSION_MAX_VALUE];

    MDS_SCALING_TYPE mScaleType;
    uint32_t mHorizontalStep;
    uint32_t mVerticalStep;

    sp<IBinder> mSurfaceComposer;

    sp<IMultiDisplayCallback> mMDSCallback;
    KeyedVector<void *, MultiDisplayListener* > mListeners;
    mutable Mutex mMutex;

    void init();
    status_t notifyHotplugLocked(MDS_DISPLAY_ID, bool);
    void broadcastMessageLocked(int msg, void* value, int size);
    status_t setDisplayScalingLocked(uint32_t mode, uint32_t stepx, uint32_t stepy);
    status_t updateHdmiConnectStatusLocked();
    MultiDisplayVideoSession* getVideoSession_l(int sessionId);
    int getVideoSessionSize_l();
    void initVideoSessions_l();
    bool hasVideoPlaying_l();
    void dumpVideoSession_l();

    inline bool checkMode(int value, int bit) {
        return (value & bit) == bit ? true : false;
    }
};

}; // namespace intel
}; // namespace android

#endif
