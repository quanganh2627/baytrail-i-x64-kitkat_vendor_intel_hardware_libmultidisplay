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

#ifndef ANDROID_MULTIDISPLAYSERVICE_H
#define ANDROID_MULTIDISPLAYSERVICE_H

#include <utils/RefBase.h>

#include <binder/IInterface.h>
#include <binder/Parcel.h>
#include <binder/BinderService.h>

#include <display/IMultiDisplayListener.h>
#include <display/IMultiDisplayCallback.h>
#include <display/IMultiDisplayComposer.h>
#include <display/MultiDisplayType.h>
#include <display/MultiDisplayComposer.h>

namespace android {

class MultiDisplayService:
    public BinderService<MultiDisplayService>,
    public BnMultiDisplayComposer {
private:
    MultiDisplayComposer* mMDSComposer;
public:
    static char* const getServiceName() { return "MultiDisplay"; }
    static void instantiate();

    MultiDisplayService();
    ~MultiDisplayService();

    status_t notifyHotPlug(MDS_DISPLAY_ID, bool);

    status_t setVideoState(MDS_VIDEO_STATE);
    MDS_VIDEO_STATE getVideoState();

    status_t setVideoSourceInfo(MDSVideoSourceInfo*);
    status_t getVideoSourceInfo(MDSVideoSourceInfo*);

    status_t setPhoneState(MDS_PHONE_STATE);

    status_t registerListener(sp<IMultiDisplayListener>, void *, const char*, MDS_MESSAGE);
    status_t unregisterListener(void *);
    status_t registerCallback(sp<IMultiDisplayCallback>);
    status_t unregisterCallback();

    status_t setDisplayState(MDS_DISPLAY_ID, MDS_DISPLAY_STATE);

    status_t setDisplayTiming(MDS_DISPLAY_ID, MDSDisplayTiming*);
    int getDisplayTimingCount(MDS_DISPLAY_ID);
    status_t getDisplayTimingList(MDS_DISPLAY_ID, MDSDisplayTiming*);
    status_t getCurrentDisplayTiming(MDS_DISPLAY_ID, MDSDisplayTiming*);
    status_t setDisplayTimingByIndex(MDS_DISPLAY_ID, int);
    int getCurrentDisplayTimingIndex(MDS_DISPLAY_ID);

    status_t setScalingType(MDS_DISPLAY_ID, MDS_SCALING_TYPE);
    status_t setOverscan(MDS_DISPLAY_ID, int, int);
    bool getDisplayDeviceChange(MDS_DISPLAY_ID);
    MDS_DISPLAY_CAP getPlatformCapability();
    MDS_DISPLAY_MODE getDisplayMode(bool);
};

}; // namespace android
#endif

