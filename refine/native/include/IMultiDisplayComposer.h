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

#ifndef __I_MULTIDISPLAY_COMPOSER_H__
#define __I_MULTIDISPLAY_COMPOSER_H__

#include <stdint.h>
#include <sys/types.h>

#include <utils/Errors.h>
#include <utils/RefBase.h>

#include <binder/IInterface.h>

#include <display/MultiDisplayType.h>
#include <display/IMultiDisplayListener.h>
#include <display/IMultiDisplayCallback.h>

namespace android {

class IMultiDisplayComposer: public IInterface {
public:
    DECLARE_META_INTERFACE(MultiDisplayComposer);

    virtual status_t notifyHotPlug(MDS_DISPLAY_ID, bool connected) = 0;

    virtual status_t setVideoState(MDS_VIDEO_STATE) = 0;
    virtual MDS_VIDEO_STATE getVideoState() = 0;

    virtual status_t setVideoSourceInfo(MDSVideoSourceInfo*) = 0;
    virtual status_t getVideoSourceInfo(MDSVideoSourceInfo*) = 0;

    virtual status_t setPhoneState(MDS_PHONE_STATE) = 0;

    virtual status_t registerListener(sp<IMultiDisplayListener>,
            void *, const char*, MDS_MESSAGE) = 0;
    virtual status_t unregisterListener(void *) = 0;

    virtual status_t registerCallback(sp<IMultiDisplayCallback>) = 0;
    virtual status_t unregisterCallback() = 0;

    virtual status_t setDisplayState(MDS_DISPLAY_ID, MDS_DISPLAY_STATE) = 0;

    virtual int getDisplayTimingCount(MDS_DISPLAY_ID) = 0;
    virtual status_t setDisplayTiming(MDS_DISPLAY_ID, MDSDisplayTiming*) = 0;
    virtual status_t getDisplayTimingList(MDS_DISPLAY_ID, MDSDisplayTiming*) = 0;
    virtual status_t getCurrentDisplayTiming(MDS_DISPLAY_ID, MDSDisplayTiming*) = 0;
    virtual status_t setDisplayTimingByIndex(MDS_DISPLAY_ID, int) = 0;
    virtual int getCurrentDisplayTimingIndex(MDS_DISPLAY_ID) = 0;

    virtual status_t setScalingType(MDS_DISPLAY_ID, MDS_SCALING_TYPE) = 0;

    virtual status_t setOverscan(MDS_DISPLAY_ID, int, int) = 0;

    virtual bool getDisplayDeviceChange(MDS_DISPLAY_ID) = 0;

    virtual MDS_DISPLAY_CAP getPlatformCapability() = 0;

    virtual MDS_DISPLAY_MODE getDisplayMode(bool) = 0;
};

class BnMultiDisplayComposer:public BnInterface<IMultiDisplayComposer> {
public:
    virtual status_t onTransact(uint32_t code,
                                const Parcel& data,
                                Parcel* replay,
                                uint32_t flags = 0);
};

}; // android namespace
#endif // __I_MULTIDISPLAY_COMPOSER_H__

