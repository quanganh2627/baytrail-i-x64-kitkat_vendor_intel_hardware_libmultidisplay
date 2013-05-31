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

#include <binder/Parcel.h>

#include <display/IMultiDisplayCallback.h>

namespace android {

enum {
    MDS_SET_PHONE_STATE = IBinder::FIRST_CALL_TRANSACTION,
    MDS_SET_VIDEO_PLAYBACK_STATE,
    MDS_SET_DISPLAY_TIMING,
    MDS_SET_DISPLAY_STATE,
    MDS_SET_SCALING_TYPE,
    MDS_SET_OVERSCAN,
};

class BpMultiDisplayCallback : public BpInterface<IMultiDisplayCallback>
{
public:
    BpMultiDisplayCallback(const sp<IBinder>& impl)
        : BpInterface<IMultiDisplayCallback>(impl)
    {
    }

    virtual status_t setPhoneState(MDS_PHONE_STATE state) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayCallback::getInterfaceDescriptor());
        data.writeInt32(state);
        status_t result = remote()->transact(
                MDS_SET_PHONE_STATE, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t setVideoState(MDS_VIDEO_STATE state) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayCallback::getInterfaceDescriptor());
        data.writeInt32(state);
        status_t result = remote()->transact(
                MDS_SET_VIDEO_PLAYBACK_STATE, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t setDisplayTiming(
            MDS_DISPLAY_ID dpyID, MDSDisplayTiming* timing) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayCallback::getInterfaceDescriptor());
        data.writeInt32(dpyID);
        if (timing == NULL || dpyID != MDS_DISPLAY_EXTERNAL) {
            return BAD_VALUE;
        }

        data.write((const void *)timing, sizeof(MDSDisplayTiming));
        status_t result = remote()->transact(
                MDS_SET_DISPLAY_TIMING, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t setDisplayState(
            MDS_DISPLAY_ID dpyID, MDS_DISPLAY_STATE state) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayCallback::getInterfaceDescriptor());
        data.writeInt32(dpyID);
        data.writeInt32(state);
        status_t result = remote()->transact(
                MDS_SET_DISPLAY_STATE, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t setScalingType(
            MDS_DISPLAY_ID dpyID, MDS_SCALING_TYPE type) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayCallback::getInterfaceDescriptor());
        data.writeInt32(dpyID);
        data.writeInt32(type);
        status_t result = remote()->transact(
                MDS_SET_SCALING_TYPE, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t setOverscan(
            MDS_DISPLAY_ID dpyID, int hValue, int vValue) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayCallback::getInterfaceDescriptor());
        data.writeInt32(dpyID);
        data.writeInt32(hValue);
        data.writeInt32(vValue);
        status_t result = remote()->transact(
                MDS_SET_OVERSCAN, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }
};

IMPLEMENT_META_INTERFACE(MultiDisplayCallback, "com.intel.MultiDisplayCallback");

status_t BnMultiDisplayCallback::onTransact(
    uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
{
    switch (code) {
        case MDS_SET_PHONE_STATE: {
            CHECK_INTERFACE(IMultiDisplayCallback, data, reply);
            int32_t state = data.readInt32();
            ALOGV("%s: Set phone state %d", __func__, state);
            int32_t ret = setPhoneState((MDS_PHONE_STATE)state);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SET_VIDEO_PLAYBACK_STATE: {
            CHECK_INTERFACE(IMultiDisplayCallback, data, reply);
            int32_t state = data.readInt32();
            ALOGV("%s: Set video playback state %d", __func__, state);
            int32_t ret = setVideoState((MDS_VIDEO_STATE)state);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SET_DISPLAY_TIMING: {
            CHECK_INTERFACE(IMultiDisplayCallback, data, reply);
            int32_t dpyId = data.readInt32();
            if (dpyId != MDS_DISPLAY_EXTERNAL) {
                ALOGE("%s: Only support HDMI", __func__);
                return BAD_VALUE;
            }
            MDSDisplayTiming timing;
            data.read((void *)&timing, sizeof(MDSDisplayTiming));
            ALOGV("%s: set HDMI timing, %dx%d@%dx%dx%d", __func__,
                    timing->width, timing->height, timing->refresh, timing->ratio);
            int32_t ret = setDisplayTiming((MDS_DISPLAY_ID)dpyId, &timing);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SET_DISPLAY_STATE: {
            CHECK_INTERFACE(IMultiDisplayCallback, data, reply);
            int32_t dpyId = data.readInt32();
            int32_t state = data.readInt32();
            ALOGV("%s: Set display[%d] state to:%d", __func__, dpyId, state);
            int32_t ret = setDisplayState((MDS_DISPLAY_ID)dpyId, (MDS_DISPLAY_STATE)state);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SET_SCALING_TYPE: {
            CHECK_INTERFACE(IMultiDisplayCallback, data, reply);
            int32_t dpyId = data.readInt32();
            int32_t type = data.readInt32();
            ALOGV("%s: Set display[%d] scaling type to:%d", __func__, dpyId, type);
            int32_t ret = setScalingType((MDS_DISPLAY_ID)dpyId, (MDS_SCALING_TYPE)type);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SET_OVERSCAN: {
            CHECK_INTERFACE(IMultiDisplayCallback, data, reply);
            int32_t dpyId = data.readInt32();
            int32_t hValue = data.readInt32();
            int32_t vValue = data.readInt32();
            ALOGV("%s: Set display[%d] overscan h:%d v:%d",
                    __func__, dpyId, hValue, vValue);
            int32_t ret = setOverscan((MDS_DISPLAY_ID)dpyId, hValue, vValue);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
    }
    return BBinder::onTransact(code, data, reply, flags);
}

}; // namespace android
