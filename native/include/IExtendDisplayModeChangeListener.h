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

#ifndef __IEXTEND_DISPLAY_MODE_CHANGE_LISTENER_H__
#define __IEXTEND_DISPLAY_MODE_CHANGE_LISTENER_H__

#include <utils/RefBase.h>
#include <binder/IInterface.h>
#include <binder/Parcel.h>


namespace android {

class IExtendDisplayModeChangeListener : public IInterface {
public:
    enum {
        ON_MDS_EVENT = IBinder::FIRST_CALL_TRANSACTION,
    };
    DECLARE_META_INTERFACE(ExtendDisplayModeChangeListener);
    virtual int onMdsMessage(int msg, void* value, int size) = 0;
};
class BpExtendDisplayModeChangeListener : public BpInterface<IExtendDisplayModeChangeListener> {
public:
    BpExtendDisplayModeChangeListener(const sp<IBinder>& impl)
        : BpInterface<IExtendDisplayModeChangeListener>(impl) {}
    virtual int onMdsMessage(int msg, void* value, int size);
};

class BnExtendDisplayModeChangeListener : public BnInterface<IExtendDisplayModeChangeListener> {
public:
    virtual status_t onTransact(uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags = 0);
};

}; // namespace android

#endif /* __IEXTEND_DISPLAY_MODE_CHANGE_LISTENER_H__ */
