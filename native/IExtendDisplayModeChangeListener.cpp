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

using namespace android;

void BpExtendDisplayModeChangeListener::onModeChange(int mode) {
    LOGV("%s: mode %d", __func__, mode);
    Parcel data, reply;
    data.writeInterfaceToken(IExtendDisplayModeChangeListener::getInterfaceDescriptor());
    data.writeInt32(mode);
    remote()->transact(ON_MODE_CHANGE, data, &reply);
}

IMPLEMENT_META_INTERFACE(ExtendDisplayModeChangeListener, "com.intel.ExtendDisplayModeChangeListener");

status_t BnExtendDisplayModeChangeListener::onTransact(uint32_t code,
        const Parcel& data,
        Parcel* reply,
        uint32_t flags) {
    switch (code) {
    case ON_MODE_CHANGE: {
        LOGV("%s: ON_MODE_CHANGE", __func__);
        CHECK_INTERFACE(IExtendDisplayModeChangeListener, data, replay);
        onModeChange(data.readInt32());
        return NO_ERROR;
    }
    break;
    default:
        return BBinder::onTransact(code, data, reply, flags);
    }
}