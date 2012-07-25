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
#include <binder/IServiceManager.h>
#include <binder/IPCThreadState.h>
#include <display/MultiDisplayType.h>
#include <utils/Log.h>
#include "MultiDisplayService.h"

namespace android {

MultiDisplayService::MultiDisplayService() {
    mMDC = new MultiDisplayComposer();
}

MultiDisplayService::~MultiDisplayService() {
    if (mMDC != NULL) {
        delete mMDC;
        mMDC = NULL;
    }
}

int MultiDisplayService::getMode(bool wait) {
    if (mMDC == NULL) return MDS_ERROR;
    return mMDC->getMode(wait);
}

int MultiDisplayService::notifyWidi(bool on) {
    if (mMDC == NULL) return MDS_ERROR;
    return mMDC->notifyWidi(on);
}

int MultiDisplayService::notifyMipi(bool on) {
    if (mMDC == NULL) return MDS_ERROR;
    return mMDC->notifyMipi(on);
}

int MultiDisplayService::updateVideoInfo(MDSVideoInfo* info) {
    if (mMDC == NULL) return MDS_ERROR;
    return mMDC->updateVideoInfo(info);
}

int MultiDisplayService::notifyHotPlug() {
    if (mMDC == NULL) return MDS_ERROR;
    return mMDC->notifyHotPlug();
}

int MultiDisplayService::setHdmiPowerOff() {
    if (mMDC == NULL) return MDS_ERROR;
    return mMDC->setHdmiPowerOff();
}

int MultiDisplayService::setModePolicy(int policy) {
    if (mMDC == NULL) return MDS_ERROR;
    return mMDC->setModePolicy(policy);
}

int MultiDisplayService::registerModeChangeListener(
                            sp<IExtendDisplayModeChangeListener> listener, void *handle) {
    if (mMDC == NULL) return MDS_ERROR;
    return mMDC->registerModeChangeListener(listener, handle);
}

int MultiDisplayService::unregisterModeChangeListener(
                            sp<IExtendDisplayModeChangeListener> listener, void *handle) {
    if (mMDC == NULL) return MDS_ERROR;
    return mMDC->unregisterModeChangeListener(listener, handle);
}

int MultiDisplayService::getHdmiModeInfo(int* pWidth, int* pHeight,
                                    int* pRefresh, int* pInterlace) {
    if (mMDC == NULL) return MDS_ERROR;
    return mMDC->getHdmiModeInfo(pWidth, pHeight, pRefresh, pInterlace);
}

int MultiDisplayService::setHdmiModeInfo(int width, int height, int refresh, int interlace, int ratio) {
    if (mMDC == NULL) return MDS_ERROR;
    return mMDC->setHdmiModeInfo(width, height, refresh, interlace, ratio);
}
int MultiDisplayService::setHdmiScaleType(int type) {
    if (mMDC == NULL) return MDS_ERROR;
    return mMDC->setHdmiScaleType(type);
}

int MultiDisplayService::setHdmiScaleStep(int hValue, int vValue) {
    if (mMDC == NULL) return MDS_ERROR;
    return mMDC->setHdmiScaleStep(hValue, vValue);
}

int MultiDisplayService::getHdmiDeviceChange() {
    if (mMDC == NULL) return MDS_ERROR;
    return mMDC->getHdmiDeviceChange();
}

int MultiDisplayService::getVideoInfo(int* dw, int* dh, int* fps, int* interlace) {
    if (mMDC == NULL) return MDS_ERROR;
    return mMDC->getVideoInfo(dw, dh, fps, interlace);
}
}
