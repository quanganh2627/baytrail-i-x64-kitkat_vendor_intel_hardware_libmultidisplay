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
#include <binder/Parcel.h>
#include <binder/IServiceManager.h>
#include <display/IMultiDisplayComposer.h>
#include <display/MultiDisplayType.h>
#include <display/IExtendDisplayModeChangeListener.h>

using namespace android;

int BpMultiDisplayComposer::getMode(bool wait) {
    Parcel data, reply;
    data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
    data.writeInt32(wait == true ? 1 : 0);
    remote()->transact(MDS_GET_MODE, data, &reply);
    return reply.readInt32();
}

int BpMultiDisplayComposer::setModePolicy(int policy) {
    Parcel data, reply;
    data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
    data.writeInt32(policy);
    remote()->transact(MDS_SET_MODEPOLICY, data, &reply);
    return reply.readInt32();
}

int BpMultiDisplayComposer::notifyWidi(bool on) {
    Parcel data, reply;
    data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
    data.writeInt32( on == true ? 1: 0);
    remote()->transact(MDS_NOTIFY_WIDI, data, &reply);
    return reply.readInt32();
}

int BpMultiDisplayComposer::notifyMipi(bool on) {
    Parcel data, reply;
    data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
    data.writeInt32( on == true ? 1: 0);
    remote()->transact(MDS_NOTIFY_MIPI, data, &reply);
    return reply.readInt32();
}

int BpMultiDisplayComposer::updateVideoInfo(MDSVideoInfo* info) {
    Parcel data, reply;
    data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
    if (info == NULL) return MDS_ERROR;
    int isplaying = (info->isplaying ? 1 : 0);
    int isprotected = (info->isprotected ? 1 : 0);
    int dsw = info->displayW;
    int dsh = info->displayH;
    int framerate = info->frameRate;
    int isinterlace = (info->isinterlace ? 1 : 0);
    data.writeInt32(isplaying);
    data.writeInt32(isprotected);
    data.writeInt32(framerate);
    data.writeInt32(dsw);
    data.writeInt32(dsh);
    data.writeInt32(isinterlace);
    remote()->transact(MDS_UPDATE_VIDEOINFO, data, &reply);
    return reply.readInt32();
}

int BpMultiDisplayComposer::notifyHotPlug() {
    Parcel data, reply;
    data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
    remote()->transact(MDS_NOTIFY_HOTPLUG, data, &reply);
    return reply.readInt32();
}

int BpMultiDisplayComposer::setHdmiPowerOff() {
    Parcel data, reply;
    data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
    remote()->transact(MDS_HDMI_POWER_OFF, data, &reply);
    return reply.readInt32();
}

int BpMultiDisplayComposer::registerModeChangeListener(sp<IExtendDisplayModeChangeListener> listener, void *handle) {
    Parcel data, reply;
    data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
    data.writeStrongBinder(listener->asBinder());
    data.writeIntPtr(reinterpret_cast<intptr_t>(handle));
    remote()->transact(MDS_REGISTER_MODE_CHANGE_LISTENER, data, &reply);
    return reply.readInt32();
}

int BpMultiDisplayComposer::unregisterModeChangeListener(sp<IExtendDisplayModeChangeListener> listener, void *handle) {
    Parcel data, reply;
    data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
    data.writeStrongBinder(listener->asBinder());
    data.writeIntPtr(reinterpret_cast<intptr_t>(handle));
    remote()->transact(MDS_UNREGISTER_MODE_CHANGE_LISTENER, data, &reply);
    return reply.readInt32();
}

int BpMultiDisplayComposer::setHdmiModeInfo(int width, int height, int refresh, int interlace, int ratio) {
    Parcel data, reply;
    data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
    data.writeInt32(width);
    data.writeInt32(height);
    data.writeInt32(refresh);
    data.writeInt32(interlace);
    data.writeInt32(ratio);
    remote()->transact(MDS_SET_HDMIMODE_INFO, data, &reply);
    return reply.readInt32();
}

int BpMultiDisplayComposer::getHdmiModeInfo(int* widthArray, int* heightArray, int* refreshArray, int* interlaceArray,
                                            int* ratioArray) {
    Parcel data, reply;
    data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
    if (widthArray == NULL || heightArray == NULL
        || refreshArray == NULL || interlaceArray == NULL
        || ratioArray == NULL) {
        remote()->transact(MDS_GET_HDMIMODE_INFO_COUNT, data, &reply);
        return reply.readInt32();
    }
    data.writeIntPtr((intptr_t)widthArray);
    data.writeIntPtr((intptr_t)heightArray);
    data.writeIntPtr((intptr_t)refreshArray);
    data.writeIntPtr((intptr_t)interlaceArray);
    data.writeIntPtr((intptr_t)ratioArray);
    remote()->transact(MDS_GET_HDMIMODE_INFO, data, &reply);
    return reply.readInt32();
}

int BpMultiDisplayComposer::setHdmiScaleType(int Type) {
    Parcel data, reply;
    data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
    data.writeInt32(Type);
    remote()->transact(MDS_SET_HDMISCALE_TYPE, data, &reply);
    return reply.readInt32();
}

int BpMultiDisplayComposer::setHdmiScaleStep(int hValue, int vValue) {
    Parcel data, reply;
    data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
    data.writeInt32(hValue);
    data.writeInt32(vValue);
    remote()->transact(MDS_SET_HDMISCALE_STEP, data, &reply);
    return reply.readInt32();
}

int BpMultiDisplayComposer::getHdmiDeviceChange() {
    Parcel data, reply;
    data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
    remote()->transact(MDS_GET_HDMIDEVICE_CHANGE, data, &reply);
    return reply.readInt32();
}


int BpMultiDisplayComposer::getVideoInfo(int* dw, int* dh, int* fps, int* interlace) {
    Parcel data, reply;
    data.writeInterfaceToken(IMultiDisplayComposer::getInterfaceDescriptor());
    if (dw == NULL || dh == NULL
            || fps == NULL || interlace == NULL) {
        LOGE("%s: parameter is null", __func__);
        return -1;
    }
    data.writeIntPtr((intptr_t)dw);
    data.writeIntPtr((intptr_t)dh);
    data.writeIntPtr((intptr_t)fps);
    data.writeIntPtr((intptr_t)interlace);
    remote()->transact(MDS_GET_VIDEO_INFO, data, &reply);
    return reply.readInt32();
}

IMPLEMENT_META_INTERFACE(MultiDisplayComposer,"com.intel.IMultiDisplayComposer");

status_t BnMultiDisplayComposer::onTransact(uint32_t code,
                                            const Parcel& data,
                                            Parcel* reply,
                                            uint32_t flags) {
    switch (code) {
    case MDS_GET_MODE: {
        CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
        reply->writeInt32(getMode((data.readInt32() == 1 ? true : false)));
        return NO_ERROR;
    }
    break;
    case MDS_SET_MODEPOLICY: {
        CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
        reply->writeInt32(setModePolicy(data.readInt32()));
        return NO_ERROR;
    }
    break;
    case MDS_NOTIFY_MIPI: {
        CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
        reply->writeInt32(notifyMipi((data.readInt32() == 1 ? true : false)));
        return NO_ERROR;
    }
    break;
    case MDS_NOTIFY_WIDI: {
        CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
        reply->writeInt32(notifyWidi((data.readInt32() == 1 ? true : false)));
        return NO_ERROR;
    }
    break;
    case MDS_UPDATE_VIDEOINFO: {
        CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
        MDSVideoInfo info;
        info.isplaying = (data.readInt32() == 0 ? false : true);
        info.isprotected = (data.readInt32() == 0 ? false : true);
        info.frameRate = data.readInt32();
        info.displayW = data.readInt32();
        info.displayH = data.readInt32();
        info.isinterlace = (data.readInt32() == 0 ? false : true);
        reply->writeInt32(updateVideoInfo(&info));
        return NO_ERROR;
    }
    break;
    case MDS_NOTIFY_HOTPLUG: {
        CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
        reply->writeInt32(notifyHotPlug());
        return NO_ERROR;
    }
    break;
    case MDS_HDMI_POWER_OFF: {
        CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
        reply->writeInt32(setHdmiPowerOff());
        return NO_ERROR;
    }
    break;
    case MDS_REGISTER_MODE_CHANGE_LISTENER: {
        CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
        sp<IBinder> listener = data.readStrongBinder();
        int ret = registerModeChangeListener(
                    interface_cast<IExtendDisplayModeChangeListener>(listener),
                    reinterpret_cast<void *>(data.readIntPtr()));
        reply->writeInt32(ret);
        return NO_ERROR;
    }
    break;
    case MDS_UNREGISTER_MODE_CHANGE_LISTENER: {
        CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
        sp<IBinder> listener = data.readStrongBinder();
        int ret = unregisterModeChangeListener(
                    interface_cast<IExtendDisplayModeChangeListener>(listener),
                    reinterpret_cast<void *>(data.readIntPtr()));
        reply->writeInt32(ret);
        return NO_ERROR;
    }
    break;
    case MDS_SET_HDMIMODE_INFO: {
        CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
        int width, height, refresh, interlace, ratio, ret;
        width = data.readInt32();
        height = data.readInt32();
        refresh = data.readInt32();
        interlace = data.readInt32();
        ratio = data.readInt32();
        ret = setHdmiModeInfo(width, height, refresh, interlace, ratio);
        reply->writeInt32(ret);
        return NO_ERROR;
    }
    break;
    case MDS_GET_HDMIMODE_INFO: {
        CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
        int *width, *height, *refresh, *interlace, *ratio;
        width = height = refresh = interlace = NULL;
        width = (int*)data.readIntPtr();
        height = (int*)data.readIntPtr();
        refresh = (int*)data.readIntPtr();
        interlace = (int*)data.readIntPtr();
        ratio = (int*)data.readIntPtr();
        int ret = getHdmiModeInfo(width, height, refresh, interlace, ratio);
        reply->writeInt32(ret);
        return NO_ERROR;
    }
    break;
    case MDS_GET_HDMIMODE_INFO_COUNT: {
        CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
        reply->writeInt32(getHdmiModeInfo(NULL, NULL, NULL, NULL, NULL));
        return NO_ERROR;
    }
    break;
    case MDS_SET_HDMISCALE_TYPE: {
        CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
        int type;
        type = data.readInt32();
        int ret = setHdmiScaleType(type);
        reply->writeInt32(ret);
        return NO_ERROR;
    }
    break;
    case MDS_SET_HDMISCALE_STEP: {
        CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
        int hValue, vValue;
        hValue = data.readInt32();
        vValue = data.readInt32();
        int ret = setHdmiScaleStep(hValue, vValue);
        reply->writeInt32(ret);
        return NO_ERROR;
    }
    break;
    case MDS_GET_HDMIDEVICE_CHANGE: {
        CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
        int ret = getHdmiDeviceChange();
        reply->writeInt32(ret);
        return NO_ERROR;
    }
    break;
    case MDS_GET_VIDEO_INFO: {
        CHECK_INTERFACE(IMultiDisplayComposer, data, reply);
        int *width, *height, *fps, *interlace;
        width = height = fps = interlace = NULL;
        width = (int*)data.readIntPtr();
        height = (int*)data.readIntPtr();
        fps = (int*)data.readIntPtr();
        interlace = (int*)data.readIntPtr();
        int ret = getVideoInfo(width, height, fps, interlace);
        reply->writeInt32(ret);
        return NO_ERROR;
    }
    break;

    default:
        return BBinder::onTransact(code, data, reply, flags);
    }
    return NO_ERROR;
}
