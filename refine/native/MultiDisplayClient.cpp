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


//#define LOG_NDEBUG 0
#include <utils/Log.h>
#include <utils/RefBase.h>

#include <binder/IInterface.h>
#include <binder/IServiceManager.h>

#include <display/MultiDisplayType.h>
#include <display/MultiDisplayClient.h>

#define MDC_CHECK_IMDC(ERR) \
do { \
    if (mMDSComposer == NULL) { \
        ALOGE("%s: IMDSComposer is null", __func__); \
        return ERR; \
    } \
} while(0)

#define IMPLEMENT_CLIENT_API_0(INTERFACE, RETURN, ERR)               \
    RETURN MultiDisplayClient::INTERFACE() {                         \
        MDC_CHECK_IMDC(ERR);                                         \
        return mMDSComposer->INTERFACE();                            \
    }

#define IMPLEMENT_CLIENT_API_1(INTERFACE, PARAM0, RETURN, ERR)       \
    RETURN MultiDisplayClient::INTERFACE(PARAM0 p0) {                \
        MDC_CHECK_IMDC(ERR);                                         \
        return mMDSComposer->INTERFACE(p0);                          \
    }

#define IMPLEMENT_CLIENT_API_2(INTERFACE, PARAM0, PARAM1, RETURN, ERR)          \
    RETURN MultiDisplayClient::INTERFACE(PARAM0 p0, PARAM1 p1) {                \
        MDC_CHECK_IMDC(ERR);                                                    \
        return mMDSComposer->INTERFACE(p0, p1);                                 \
    }

#define IMPLEMENT_CLIENT_API_3(INTERFACE, PARAM0, PARAM1, PARAM2, RETURN, ERR)  \
    RETURN MultiDisplayClient::INTERFACE(PARAM0 p0, PARAM1 p1, PARAM2 p2) {     \
        MDC_CHECK_IMDC(ERR);                                                    \
        return mMDSComposer->INTERFACE(p0, p1, p2);                             \
    }

MultiDisplayClient::MultiDisplayClient() {
    mMDSComposer = NULL;

    sp<IServiceManager> sm = defaultServiceManager();
    if (sm == NULL) {
        LOGE("%s: Fail to get service manager", __func__);
        return;
    }
    sp<IBinder> service = sm->checkService(String16("MultiDisplay"));
    if (service == NULL) {
        LOGE("%s: Fail to get MultiDisplay service", __func__);
        return;
    }

    mMDSComposer = interface_cast<IMultiDisplayComposer>(service);
}

MultiDisplayClient::~MultiDisplayClient() {
    LOGI("%s: MDSClient is destroyed", __func__);
    mMDSComposer = NULL;
}

void MultiDisplayClient::init() {
    mMDSComposer = NULL;

    sp<IServiceManager> sm = defaultServiceManager();
    if (sm == NULL) {
        LOGE("%s: Fail to get service manager", __func__);
        return;
    }
    sp<IBinder> service = sm->checkService(String16("MultiDisplay"));
    if (service == NULL) {
        LOGE("%s: Fail to get MultiDisplay service", __func__);
        return;
    }

    mMDSComposer = interface_cast<IMultiDisplayComposer>(service);
}

/// Public method implementation
IMPLEMENT_CLIENT_API_0(resetVideoPlayback,           status_t,        NO_INIT)
IMPLEMENT_CLIENT_API_0(allocateVideoSessionId,       int,             -1)
IMPLEMENT_CLIENT_API_0(getPlatformCapability,        MDS_DISPLAY_CAP, MDS_DISPLAY_CAP_UNKNOWN)
IMPLEMENT_CLIENT_API_0(unregisterCallback,           status_t,        NO_INIT)

IMPLEMENT_CLIENT_API_1(getVideoState,                int,          MDS_VIDEO_STATE, MDS_VIDEO_STATE_UNKNOWN)
IMPLEMENT_CLIENT_API_1(setPhoneState,                MDS_PHONE_STATE,           status_t, NO_INIT)
IMPLEMENT_CLIENT_API_1(registerCallback,             sp<IMultiDisplayCallback>, status_t, NO_INIT)
IMPLEMENT_CLIENT_API_1(getDisplayTimingCount,        MDS_DISPLAY_ID,            int,      0)
IMPLEMENT_CLIENT_API_1(getDisplayDeviceChange,       MDS_DISPLAY_ID,            bool,     false)
IMPLEMENT_CLIENT_API_1(getCurrentDisplayTimingIndex, MDS_DISPLAY_ID,            int,      -1)
IMPLEMENT_CLIENT_API_1(getDisplayMode,               bool,         MDS_DISPLAY_MODE, MDS_MODE_NONE)

IMPLEMENT_CLIENT_API_2(setVideoState,           int,                MDS_VIDEO_STATE,           status_t, NO_INIT)
IMPLEMENT_CLIENT_API_2(setVideoSourceInfo,      int,                MDSVideoSourceInfo*,       status_t, NO_INIT)
IMPLEMENT_CLIENT_API_2(getVideoSourceInfo,      int,                MDSVideoSourceInfo*,       status_t, NO_INIT)
IMPLEMENT_CLIENT_API_2(notifyHotPlug,           MDS_DISPLAY_ID,     bool,              status_t, NO_INIT)
IMPLEMENT_CLIENT_API_2(setDisplayState,         MDS_DISPLAY_ID,     MDS_DISPLAY_STATE, status_t, NO_INIT)
IMPLEMENT_CLIENT_API_2(setDisplayTiming,        MDS_DISPLAY_ID,     MDSDisplayTiming*, status_t, NO_INIT)
IMPLEMENT_CLIENT_API_2(getCurrentDisplayTiming, MDS_DISPLAY_ID,     MDSDisplayTiming*, status_t, NO_INIT)
IMPLEMENT_CLIENT_API_2(setDisplayTimingByIndex, MDS_DISPLAY_ID,     int,               status_t, NO_INIT)
IMPLEMENT_CLIENT_API_2(setScalingType,          MDS_DISPLAY_ID,     MDS_SCALING_TYPE,  status_t, NO_INIT)

IMPLEMENT_CLIENT_API_3(setOverscan,          MDS_DISPLAY_ID, int,       int,                status_t, NO_INIT)
IMPLEMENT_CLIENT_API_3(getDisplayTimingList, MDS_DISPLAY_ID, const int, MDSDisplayTiming**, status_t, NO_INIT)

status_t MultiDisplayClient::registerListener(
        sp<IMultiDisplayListener> listener, const char* client, MDS_MESSAGE msg) {
    MDC_CHECK_IMDC(NO_INIT);
    return mMDSComposer->registerListener(listener, static_cast<void *>(this), client, msg);
}

status_t MultiDisplayClient::unregisterListener() {
    MDC_CHECK_IMDC(NO_INIT);
    return mMDSComposer->unregisterListener(static_cast<void *>(this));
}

