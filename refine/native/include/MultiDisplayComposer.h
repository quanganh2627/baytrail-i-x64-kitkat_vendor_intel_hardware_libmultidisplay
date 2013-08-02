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

#include <utils/Vector.h>
#include <display/MultiDisplayType.h>
#include <display/IMultiDisplayListener.h>
#include <display/IMultiDisplayCallback.h>
#include <display/IMultiDisplayComposer.h>

using namespace android;

class MultiDisplayListener {
private:
    int   mMsg;
    char* mName;
    sp<IMultiDisplayListener> mListener;
public:
    MultiDisplayListener(int msg, const char* client, sp<IMultiDisplayListener>);
    ~MultiDisplayListener();
    inline char* getName() {
        return mName;
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
    inline status_t setInfo(MDSVideoSourceInfo* info) {
        if (info == NULL)
            return UNKNOWN_ERROR;
        memcpy(&mInfo, info, sizeof(MDSVideoSourceInfo));
        infoValid = true;
        return NO_ERROR;
    }
    inline void init() {
        mState = MDS_VIDEO_UNPREPARED;
        memset(&mInfo, 0, sizeof(MDSVideoSourceInfo));
        infoValid = false;
    }
    inline void dump(int index);
};

class MultiDisplayComposer {
public:
    MultiDisplayComposer();
    ~MultiDisplayComposer();

    status_t notifyHotPlug(MDS_DISPLAY_ID, bool);

    int allocateVideoSessionId();
    status_t setVideoState(int, MDS_VIDEO_STATE);
    status_t resetVideoPlayback();
    MDS_VIDEO_STATE getVideoState(int);

    status_t setVideoSourceInfo(int, MDSVideoSourceInfo*);
    status_t getVideoSourceInfo(int, MDSVideoSourceInfo*);

    status_t setPhoneState(MDS_PHONE_STATE);

    status_t registerListener(sp<IMultiDisplayListener>, void*, const char*, MDS_MESSAGE);
    status_t unregisterListener(void *);

    status_t registerCallback(sp<IMultiDisplayCallback>);
    status_t unregisterCallback();

    status_t setDisplayState(MDS_DISPLAY_ID, MDS_DISPLAY_STATE);

    status_t setDisplayTiming(MDS_DISPLAY_ID, MDSDisplayTiming*);
    int getDisplayTimingCount(MDS_DISPLAY_ID);
    status_t getDisplayTimingList(MDS_DISPLAY_ID, const int, MDSDisplayTiming**);
    status_t getCurrentDisplayTiming(MDS_DISPLAY_ID, MDSDisplayTiming*);
    status_t setDisplayTimingByIndex(MDS_DISPLAY_ID, int);
    int getCurrentDisplayTimingIndex(MDS_DISPLAY_ID);

    status_t setScalingType(MDS_DISPLAY_ID, MDS_SCALING_TYPE);
    status_t setOverscan(MDS_DISPLAY_ID, int, int);
    bool getDisplayDeviceChange(MDS_DISPLAY_ID);
    MDS_DISPLAY_CAP getPlatformCapability();
    MDS_DISPLAY_MODE getDisplayMode(bool);

private:
    bool mDrmInit;
    int mMode;
    int mCapability;
    MultiDisplayVideoSession mVideos[MDS_VIDEO_SESSION_MAX_VALUE];

    MDS_SCALING_TYPE mScaleType;
    uint32_t mHorizontalStep;
    uint32_t mVerticalStep;

    sp<IBinder> mSurfaceComposer;

    sp<IMultiDisplayCallback> mMDSCallback;
    KeyedVector<void *, MultiDisplayListener* > mListeners;
    mutable Mutex mMutex;

    void init();
    void broadcastMessageLocked(MDS_MESSAGE msg, void* value, int size);
    status_t setDisplayScalingLocked(uint32_t mode, uint32_t stepx, uint32_t stepy);
    status_t updateHdmiConnectStatusLocked();
    MultiDisplayVideoSession* getVideoSession_l(int sessionId);
    int getVideoSessionSize_l();
    void initVideoSessions_l();
    bool enableVideoExtMode_l();
    void dumpVideoSession_l();

    inline bool checkMode(int value, int bit) {
        return (value & bit) == bit ? true : false;
    }
};


#endif

