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
#include <binder/IServiceManager.h>

#include <display/IMultiDisplayComposer.h>

namespace android {

enum {
    MDS_SERVER_NOTIFY_HOT_PLUG = IBinder::FIRST_CALL_TRANSACTION,
    MDS_SERVER_ALLOCATE_VIDEO_SESSIONID,
    MDS_SERVER_RESET_VIDEO_PLAYBACK,
    MDS_SERVER_SET_VIDEO_STATE,
    MDS_SERVER_GET_VIDEO_STATE,
    MDS_SERVER_SET_VIDEO_SOURCE_INFO,
    MDS_SERVER_GET_VIDEO_SOURCE_INFO,
    MDS_SERVER_SET_PHONE_CALL_STATE,
    MDS_SERVER_REGISTER_LISTENER,
    MDS_SERVER_UNREGISTER_LISTENER,
    MDS_SERVER_REGISTER_CALLBACK,
    MDS_SERVER_UNREGISTER_CALLBACK,
    MDS_SERVER_SET_DISPLAY_STATE,
    MDS_SERVER_SET_DISPLAY_TIMING,
    MDS_SERVER_GET_DISPLAY_TIMING_COUNT,
    MDS_SERVER_GET_DISPLAY_TIMING_LIST,
    MDS_SERVER_SET_DISPLAY_TIMING_BY_INDEX,
    MDS_SERVER_GET_CURRENT_DISPLAY_TIMING,
    MDS_SERVER_GET_CURRENT_DISPLAY_TIMING_INDEX,
    MDS_SERVER_SET_SCALING_TYPE,
    MDS_SERVER_SET_OVER_SCAN,
    MDS_SERVER_GET_DISPLAY_DEVICE_CHANGE,
    MDS_SERVER_GET_PLATFORM_DISPLAY_CAP,
    MDS_SERVER_GET_DISPLAY_MODE,
};

class BpMultiDisplayComposer:public BpInterface<IMultiDisplayComposer> {
public:
    BpMultiDisplayComposer(const sp<IBinder>& impl)
        : BpInterface<IMultiDisplayComposer>(impl)
    {
    }

    virtual status_t notifyHotPlug(MDS_DISPLAY_ID dpyID, bool connected) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        data.writeInt32(dpyID);
        data.writeInt32((connected ? 1 : 0));
        status_t result = remote()->transact(
                MDS_SERVER_NOTIFY_HOT_PLUG, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t resetVideoPlayback() {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        status_t result = remote()->transact(
                MDS_SERVER_RESET_VIDEO_PLAYBACK, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual int allocateVideoSessionId() {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        status_t result = remote()->transact(
                MDS_SERVER_ALLOCATE_VIDEO_SESSIONID, data, &reply);
        if (result != NO_ERROR) {
            return -1;
        }
        return reply.readInt32();
    }

    virtual status_t setVideoState(int sessionId, MDS_VIDEO_STATE state) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        data.writeInt32(sessionId);
        data.writeInt32(state);
        status_t result = remote()->transact(
                MDS_SERVER_SET_VIDEO_STATE, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual MDS_VIDEO_STATE getVideoState(int sessionId) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        data.writeInt32(sessionId);
        status_t result = remote()->transact(
                MDS_SERVER_GET_VIDEO_STATE, data, &reply);
        if (result != NO_ERROR) {
            return MDS_VIDEO_STATE_UNKNOWN;
        }
        MDS_VIDEO_STATE state = (MDS_VIDEO_STATE)reply.readInt32();
        return state;
    }

    virtual status_t setVideoSourceInfo(int sessionId, MDSVideoSourceInfo* info) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        if (info == NULL) {
            return BAD_VALUE;
        }
        data.writeInt32(sessionId);
        data.write((const void *)info, sizeof(MDSVideoSourceInfo));
        status_t result = remote()->transact(
                MDS_SERVER_SET_VIDEO_SOURCE_INFO, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t getVideoSourceInfo(int sessionId, MDSVideoSourceInfo* info) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        if (info == NULL) {
            return BAD_VALUE;
        }
        data.writeInt32(sessionId);
        status_t result = remote()->transact(
                MDS_SERVER_GET_VIDEO_SOURCE_INFO, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        reply.read((void *)info, sizeof(MDSVideoSourceInfo));
        result = reply.readInt32();
        return result;
    }

    virtual status_t setPhoneState(MDS_PHONE_STATE state) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        data.writeInt32(state);
        status_t result = remote()->transact(
                MDS_SERVER_SET_PHONE_CALL_STATE, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t registerListener(sp<IMultiDisplayListener> listener,
            void *handle, const char* name, MDS_MESSAGE msg) {
        Parcel data, reply;
        if (handle == NULL || name == NULL) {
            return BAD_VALUE;
        }
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        data.writeStrongBinder(listener->asBinder());
        data.writeIntPtr(reinterpret_cast<intptr_t>(handle));
        data.writeCString(name);
        data.writeInt32(msg);
        status_t result = remote()->transact(
                MDS_SERVER_REGISTER_LISTENER, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t unregisterListener(void *handle) {
        Parcel data, reply;
        if (handle == NULL) {
            return BAD_VALUE;
        }
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        data.writeIntPtr(reinterpret_cast<intptr_t>(handle));
        status_t result = remote()->transact(
                MDS_SERVER_UNREGISTER_LISTENER, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t registerCallback(sp<IMultiDisplayCallback> cbk) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        data.writeStrongBinder(cbk->asBinder());
        status_t result = remote()->transact(
                MDS_SERVER_REGISTER_CALLBACK, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t unregisterCallback() {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        status_t result = remote()->transact(
                MDS_SERVER_UNREGISTER_CALLBACK, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t setDisplayState(MDS_DISPLAY_ID dpyId, MDS_DISPLAY_STATE state) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        data.writeInt32(dpyId);
        data.writeInt32(state);
        status_t result = remote()->transact(
                MDS_SERVER_SET_DISPLAY_STATE, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t setDisplayTiming(
            MDS_DISPLAY_ID dpyId, MDSDisplayTiming* timing) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        if (timing == NULL) {
            return BAD_VALUE;
        }
        data.writeInt32(dpyId);
        data.write((void *)timing, sizeof(MDSDisplayTiming));
        status_t result = remote()->transact(
                MDS_SERVER_SET_DISPLAY_TIMING, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual int getDisplayTimingCount(MDS_DISPLAY_ID dpyId) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        data.writeInt32(dpyId);
        status_t result = remote()->transact(
                MDS_SERVER_GET_DISPLAY_TIMING_COUNT, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        int count = reply.readInt32();
        return count;
    }

    virtual status_t getDisplayTimingList(MDS_DISPLAY_ID dpyId,
            int timingCount, MDSDisplayTiming** list) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        if (list == NULL || timingCount <= 0 || timingCount > HDMI_TIMING_MAX) {
            return BAD_VALUE;
        }
        data.writeInt32(dpyId);
        data.writeInt32(timingCount);
        status_t result = remote()->transact(
                MDS_SERVER_GET_DISPLAY_TIMING_LIST, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        for (int i = 0; i < timingCount; i++) {
            if (list[i] == NULL)
                return BAD_VALUE;

            reply.read((void*)list[i], sizeof(MDSDisplayTiming));
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t getCurrentDisplayTiming(
            MDS_DISPLAY_ID dpyId, MDSDisplayTiming* timing) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        if (timing == NULL) {
            return BAD_VALUE;
        }
        data.writeInt32(dpyId);
        status_t result = remote()->transact(
                MDS_SERVER_GET_CURRENT_DISPLAY_TIMING, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        reply.read((void *)timing, sizeof(MDSDisplayTiming));
        result = reply.readInt32();
        return result;
    }

    virtual status_t setDisplayTimingByIndex(MDS_DISPLAY_ID dpyId, int index) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        data.writeInt32(dpyId);
        data.writeInt32(index);
        status_t result = remote()->transact(
                MDS_SERVER_SET_DISPLAY_TIMING_BY_INDEX, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual int getCurrentDisplayTimingIndex(MDS_DISPLAY_ID dpyId) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        data.writeInt32(dpyId);
        status_t result = remote()->transact(
                MDS_SERVER_GET_CURRENT_DISPLAY_TIMING_INDEX, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        int32_t index = reply.readInt32();
        return index;
    }

    virtual status_t setScalingType(MDS_DISPLAY_ID dpyId, MDS_SCALING_TYPE type) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        data.writeInt32(dpyId);
        data.writeInt32(type);
        status_t result = remote()->transact(
                MDS_SERVER_SET_SCALING_TYPE, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t setOverscan(MDS_DISPLAY_ID dpyId, int hValue, int vValue) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        data.writeInt32(dpyId);
        data.writeInt32(hValue);
        data.writeInt32(vValue);
        status_t result = remote()->transact(
                MDS_SERVER_SET_OVER_SCAN, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual bool getDisplayDeviceChange(MDS_DISPLAY_ID dpyId) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        data.writeInt32(dpyId);
        status_t result = remote()->transact(
                MDS_SERVER_GET_DISPLAY_DEVICE_CHANGE, data, &reply);
        if (result != NO_ERROR) {
            return result;
        }
        bool changed = (reply.readInt32() == 0) ? false : true;
        return changed;
    }

    virtual MDS_DISPLAY_CAP getPlatformCapability() {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        status_t result = remote()->transact(
                MDS_SERVER_GET_PLATFORM_DISPLAY_CAP, data, &reply);
        if (result != NO_ERROR) {
            return MDS_DISPLAY_CAP_UNKNOWN;
        }
        MDS_DISPLAY_CAP capability = (MDS_DISPLAY_CAP)reply.readInt32();
        return capability;
    }

    virtual MDS_DISPLAY_MODE getDisplayMode(bool wait) {
        Parcel data, reply;
        data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
        data.writeInt32(wait ? 1 : 0);
        status_t result = remote()->transact(
                MDS_SERVER_GET_DISPLAY_MODE, data, &reply);
        if (result != NO_ERROR) {
            return MDS_MODE_NONE;
        }
        MDS_DISPLAY_MODE mode = (MDS_DISPLAY_MODE)reply.readInt32();
        return mode;
    }
};

IMPLEMENT_META_INTERFACE(MultiDisplayComposer,"com.intel.IMultiDisplayComposer");

status_t BnMultiDisplayComposer::onTransact(
    uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
{
    switch (code) {
        case MDS_SERVER_NOTIFY_HOT_PLUG: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            MDS_DISPLAY_ID dpyID = (MDS_DISPLAY_ID)data.readInt32();
            bool connected = (data.readInt32() == 1 ? true : false);
            status_t ret = notifyHotPlug(dpyID, connected);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SERVER_ALLOCATE_VIDEO_SESSIONID: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            int32_t sessionId = allocateVideoSessionId();
            reply->writeInt32(sessionId);
            return NO_ERROR;
            } break;
        case MDS_SERVER_RESET_VIDEO_PLAYBACK: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            status_t ret = resetVideoPlayback();
            reply->writeInt32(ret);
            return NO_ERROR;
            } break;
        case MDS_SERVER_SET_VIDEO_STATE: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            int32_t sessionId = data.readInt32();
            MDS_VIDEO_STATE state = (MDS_VIDEO_STATE)data.readInt32();
            status_t ret = setVideoState(sessionId, state);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SERVER_GET_VIDEO_STATE: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            int32_t sessionId = data.readInt32();
            MDS_VIDEO_STATE ret = getVideoState(sessionId);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SERVER_SET_VIDEO_SOURCE_INFO: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            int32_t sessionId = data.readInt32();
            MDSVideoSourceInfo info;
            data.read((void *)&info, sizeof(MDSVideoSourceInfo));
            status_t ret = setVideoSourceInfo(sessionId, &info);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SERVER_GET_VIDEO_SOURCE_INFO: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            MDSVideoSourceInfo info;
            int32_t sessionId = data.readInt32();
            status_t ret = getVideoSourceInfo(sessionId, &info);
            reply->write((const void *)&info, sizeof(MDSVideoSourceInfo));
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SERVER_SET_PHONE_CALL_STATE: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            MDS_PHONE_STATE state = (MDS_PHONE_STATE)data.readInt32();
            status_t ret = setPhoneState(state);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SERVER_REGISTER_LISTENER: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            sp<IBinder> listener = data.readStrongBinder();
            void* handle = reinterpret_cast<void *>(data.readIntPtr());
            const char* client = data.readCString();
            MDS_MESSAGE msg = (MDS_MESSAGE)data.readInt32();
            status_t ret = registerListener(
                interface_cast<IMultiDisplayListener>(listener), handle, client, msg);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SERVER_UNREGISTER_LISTENER: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            status_t ret = unregisterListener(
                reinterpret_cast<void *>(data.readIntPtr()));
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SERVER_REGISTER_CALLBACK: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            sp<IBinder> callback = data.readStrongBinder();
            status_t ret = registerCallback(
                interface_cast<IMultiDisplayCallback>(callback));
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SERVER_UNREGISTER_CALLBACK: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            status_t ret = unregisterCallback();
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SERVER_SET_DISPLAY_STATE: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            MDS_DISPLAY_ID dpyId = (MDS_DISPLAY_ID)data.readInt32();
            MDS_DISPLAY_STATE state = (MDS_DISPLAY_STATE)data.readInt32();
            status_t ret = setDisplayState(dpyId, state);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SERVER_SET_DISPLAY_TIMING: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            MDS_DISPLAY_ID dpyId = (MDS_DISPLAY_ID)data.readInt32();
            MDSDisplayTiming timing;
            data.read((void *)&timing, sizeof(MDSVideoSourceInfo));
            status_t ret = setDisplayTiming(dpyId, &timing);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SERVER_GET_DISPLAY_TIMING_COUNT: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            MDS_DISPLAY_ID dpyId = (MDS_DISPLAY_ID)data.readInt32();
            int32_t ret = getDisplayTimingCount(dpyId);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SERVER_GET_DISPLAY_TIMING_LIST: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            MDS_DISPLAY_ID dispId = (MDS_DISPLAY_ID)data.readInt32();
            const int count = data.readInt32();
            MDSDisplayTiming *list[count];
            memset(list, 0, count * sizeof(MDSDisplayTiming *));
            for (int i = 0; i < count; i++) {
                list[i] = (MDSDisplayTiming *)malloc(sizeof(MDSDisplayTiming));
                if (list[i] == NULL) {
                    for (int j = 0; j < i; j++)
                        if (list[j]) free(list[j]);
                    return NO_ERROR;
                }
                memset(list[i], 0, sizeof(MDSDisplayTiming));
            }

            int ret = getDisplayTimingList(
                    dispId, count, (MDSDisplayTiming **)list);

            for (int i = 0; i < count; i++) {
                reply->write((const void*)list[i], sizeof(MDSDisplayTiming));
                free(list[i]);
                list[i] = NULL;
            }
            reply->writeInt32(ret);
        } break;
        case MDS_SERVER_GET_CURRENT_DISPLAY_TIMING: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            MDS_DISPLAY_ID dpyId = (MDS_DISPLAY_ID)data.readInt32();
            MDSDisplayTiming timing;
            status_t ret = getCurrentDisplayTiming(dpyId, &timing);
            reply->write((const void *)&timing, sizeof(MDSDisplayTiming));
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SERVER_SET_DISPLAY_TIMING_BY_INDEX: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            MDS_DISPLAY_ID dpyId = (MDS_DISPLAY_ID)data.readInt32();
            int32_t index = data.readInt32();
            status_t ret = setDisplayTimingByIndex(dpyId, index);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SERVER_GET_CURRENT_DISPLAY_TIMING_INDEX: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            MDS_DISPLAY_ID dpyId = (MDS_DISPLAY_ID)data.readInt32();
            int32_t index = getCurrentDisplayTimingIndex(dpyId);
            reply->writeInt32(index);
            return NO_ERROR;
        } break;
        case MDS_SERVER_SET_SCALING_TYPE: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            MDS_DISPLAY_ID dpyId = (MDS_DISPLAY_ID)data.readInt32();
            MDS_SCALING_TYPE type = (MDS_SCALING_TYPE)data.readInt32();
            status_t ret = setScalingType(dpyId, type);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SERVER_SET_OVER_SCAN: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            MDS_DISPLAY_ID dpyId = (MDS_DISPLAY_ID)data.readInt32();
            int32_t hValue = data.readInt32();
            int32_t vValue = data.readInt32();
            status_t ret = setOverscan(dpyId, hValue, vValue);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
        case MDS_SERVER_GET_DISPLAY_DEVICE_CHANGE: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            MDS_DISPLAY_ID dpyId = (MDS_DISPLAY_ID)data.readInt32();
            bool ret = getDisplayDeviceChange(dpyId);
            reply->writeInt32(ret ? 1 : 0);
            return NO_ERROR;
        } break;
        case MDS_SERVER_GET_PLATFORM_DISPLAY_CAP: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            MDS_DISPLAY_CAP capability = getPlatformCapability();
            reply->writeInt32(capability);
            return NO_ERROR;
        } break;
        case MDS_SERVER_GET_DISPLAY_MODE: {
            CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
            bool wait = (data.readInt32() == 0 ? false : true);
            MDS_DISPLAY_MODE ret = getDisplayMode(wait);
            reply->writeInt32(ret);
            return NO_ERROR;
        } break;
    } // switch
    return BBinder::onTransact(code, data, reply, flags);
}

}; // namespace android
