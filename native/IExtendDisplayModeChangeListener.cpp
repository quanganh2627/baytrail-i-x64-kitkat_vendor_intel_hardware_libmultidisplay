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
#include <display/IExtendDisplayModeChangeListener.h>
#include <display/MultiDisplayType.h>

using namespace android;

int BpExtendDisplayModeChangeListener::onMdsMessage(int msg, void* value, int size) {
    ALOGV("%s: mode %d", __func__, msg);
    Parcel data, reply;
    data.writeInterfaceToken(IExtendDisplayModeChangeListener::getInterfaceDescriptor());
    if (value == NULL || size < sizeof(int))
        return MDS_ERROR;
    data.writeInt32(msg);
    data.writeInt32(size);
    data.write(value, size);
    if (msg != MDS_SET_TIMING)
        ALOGV("%s: mode %d, 0x%x", __func__, msg, *((int*)value));
    remote()->transact(ON_MDS_EVENT, data, &reply);
    return reply.readInt32();
}

IMPLEMENT_META_INTERFACE(ExtendDisplayModeChangeListener, "com.intel.ExtendDisplayModeChangeListener");

status_t BnExtendDisplayModeChangeListener::onTransact(uint32_t code,
        const Parcel& data,
        Parcel* reply,
        uint32_t flags) {
    switch (code) {
    case ON_MDS_EVENT: {
        ALOGV("%s: ON_MDS_EVENT", __func__);
        CHECK_INTERFACE(IExtendDisplayModeChangeListener, data, replay);
        int32_t msg = data.readInt32();
        int32_t size = data.readInt32();
        void* value = (void*)malloc(size);
        if (size < sizeof(int) || value == NULL)
            return MDS_ERROR;
        data.read(value, size);
        if (msg != MDS_SET_TIMING)
            ALOGV("%s: mode %d, 0x%x", __func__, msg, *((int*)value));
        int32_t ret = onMdsMessage(msg, value, size);
        reply->writeInt32(ret);
        if (value) {
            free(value);
            value = NULL;
        }
        return NO_ERROR;
    }
    break;
    default:
        return BBinder::onTransact(code, data, reply, flags);
    }
}
