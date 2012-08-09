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
#include <display/MultiDisplayType.h>
#include <display/MultiDisplayClient.h>
#include <display/IMultiDisplayComposer.h>


MultiDisplayClient::MultiDisplayClient() {
    mIMDComposer = NULL;
    sp<IServiceManager> sm = defaultServiceManager();
    if (sm == NULL) {
        LOGE("%s: fail to get service manager", __func__);
        return;
    }
    sp<IBinder> service = sm->checkService(String16("MultiDisplay"));
    if (service == NULL) {
        LOGE("%s: fail to get MultiDisplay service", __func__);
        return;
    } else
        mIMDComposer = interface_cast<IMultiDisplayComposer>(service);
}

MultiDisplayClient::~MultiDisplayClient() {
    LOGI("%s: MDSClient is destroyed", __func__);
    mIMDComposer = NULL;
}

int MultiDisplayClient::setModePolicy(int policy) {
    if (mIMDComposer == NULL) {
        LOGE("%s: MDSComposer is null", __func__);
        return MDS_ERROR;
    }
    return mIMDComposer->setModePolicy(policy);
}

int MultiDisplayClient::getMode(bool wait) {
    if (mIMDComposer == NULL) {
        LOGE("%s: MDSComposer is null", __func__);
        return MDS_ERROR;
    }
    return mIMDComposer->getMode(wait);
}

int MultiDisplayClient::notifyWidi(bool on) {
    if (mIMDComposer == NULL) {
        LOGE("%s: MDSComposer is null", __func__);
        return MDS_ERROR;
    }
    return mIMDComposer->notifyWidi(on);
}

int MultiDisplayClient::notifyMipi(bool on) {
    if (mIMDComposer == NULL) {
        LOGE("%s: MDSComposer is null", __func__);
        return MDS_ERROR;
    }
    return mIMDComposer->notifyMipi(on);
}

int MultiDisplayClient::updateVideoInfo(MDSVideoInfo* info) {
    if (mIMDComposer == NULL) {
        LOGE("%s: MDSComposer is null", __func__);
        return MDS_ERROR;
    }
    return mIMDComposer->updateVideoInfo(info);
}

int MultiDisplayClient::notifyHotPlug() {
    if (mIMDComposer == NULL) {
        LOGE("%s: MDSComposer is null", __func__);
        return MDS_ERROR;
    }
    return mIMDComposer->notifyHotPlug();
}

int MultiDisplayClient::setHdmiPowerOff() {
    if (mIMDComposer == NULL) {
        LOGE("%s: MDSComposer is null", __func__);
        return MDS_ERROR;
    }
    return mIMDComposer->setHdmiPowerOff();
}

int MultiDisplayClient::registerModeChangeListener(sp<IExtendDisplayModeChangeListener> listener) {
    if (mIMDComposer == NULL) {
        LOGE("%s: MDSComposer is null", __func__);
        return MDS_ERROR;
    }
    return mIMDComposer->registerModeChangeListener(listener, static_cast<void *>(this));
}

int MultiDisplayClient::unregisterModeChangeListener(sp<IExtendDisplayModeChangeListener> listener) {
    if (mIMDComposer == NULL) {
        LOGE("%s: MDSComposer is null", __func__);
        return MDS_ERROR;
    }
    return mIMDComposer->unregisterModeChangeListener(listener, static_cast<void *>(this));
}


int MultiDisplayClient::getHdmiModeInfo(int* width, int* height, int* refresh, int* interlace, int *ratio) {
    if (mIMDComposer == NULL) {
        LOGE("%s: MDSComposer is null", __func__);
        return MDS_ERROR;
    }
    return mIMDComposer->getHdmiModeInfo(width, height, refresh, interlace, ratio);
}

int MultiDisplayClient::setHdmiModeInfo(int width, int height, int refresh, int interlace, int ratio) {
    if (mIMDComposer == NULL) {
        LOGE("%s: MDSComposer is null", __func__);
        return MDS_ERROR;
    }
    return mIMDComposer->setHdmiModeInfo(width, height, refresh, interlace, ratio);
}

int MultiDisplayClient::setHdmiScaleType(int type) {
    if (mIMDComposer == NULL) {
        LOGE("%s: MDSComposer is null", __func__);
        return MDS_ERROR;
    }
    return mIMDComposer->setHdmiScaleType(type);
}

int MultiDisplayClient::setHdmiScaleStep(int hValue, int vValue) {
    if (mIMDComposer == NULL) {
        LOGE("%s: MDSComposer is null", __func__);
        return MDS_ERROR;
    }
    return mIMDComposer->setHdmiScaleStep(hValue, vValue);
}

int MultiDisplayClient::getHdmiDeviceChange() {
    if (mIMDComposer == NULL) {
        LOGE("%s: MDSComposer is null", __func__);
        return MDS_ERROR;
    }
    return mIMDComposer->getHdmiDeviceChange();
}

int MultiDisplayClient::getVideoInfo(int* displayW, int* displayH, int* fps, int* interlace) {
    if (mIMDComposer == NULL) {
        LOGE("%s: MDSComposer is null", __func__);
        return MDS_ERROR;
    }
    return mIMDComposer->getVideoInfo(displayW, displayH, fps, interlace);
}
