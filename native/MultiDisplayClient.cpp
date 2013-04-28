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
#include <display/MultiDisplayType.h>
#include <display/MultiDisplayClient.h>
#include <display/IMultiDisplayComposer.h>

#define MDC_CHECK_IMDC() \
do { \
    if (mIMDComposer == NULL) { \
        LOGE("%s: IMDSComposer is null", __func__); \
        return MDS_ERROR; \
    } \
} while(0)

MultiDisplayClient::MultiDisplayClient() {
    mIMDComposer = NULL;
    sp<IServiceManager> sm = defaultServiceManager();
    if (sm == NULL) {
        LOGE("%s: Fail to get service manager", __func__);
        return;
    }
    sp<IBinder> service = sm->checkService(String16("MultiDisplay"));
    if (service == NULL) {
        LOGE("%s: Fail to get MultiDisplay service", __func__);
        return;
    } else
        mIMDComposer = interface_cast<IMultiDisplayComposer>(service);

}

MultiDisplayClient::~MultiDisplayClient() {
    LOGI("%s: MDSClient is destroyed", __func__);
    mIMDComposer = NULL;
}

int MultiDisplayClient::setModePolicy(int policy) {
    MDC_CHECK_IMDC();
    return mIMDComposer->setModePolicy(policy);
}

int MultiDisplayClient::getMode(bool wait) {
    MDC_CHECK_IMDC();
    return mIMDComposer->getMode(wait);
}

int MultiDisplayClient::notifyWidi(bool on) {
    MDC_CHECK_IMDC();
    return mIMDComposer->notifyWidi(on);
}

int MultiDisplayClient::notifyMipi(bool on) {
    MDC_CHECK_IMDC();
    return mIMDComposer->notifyMipi(on);
}

int MultiDisplayClient::isMdsSurface(int* nw) {
    MDC_CHECK_IMDC();
    return mIMDComposer->isMdsSurface(nw);
}

int MultiDisplayClient::prepareForVideo(int status) {
    MDC_CHECK_IMDC();
    return mIMDComposer->prepareForVideo(status);
}

int MultiDisplayClient::updateVideoInfo(MDSVideoInfo* info) {
    MDC_CHECK_IMDC();
    return mIMDComposer->updateVideoInfo(info);
}

int MultiDisplayClient::notifyHotPlug() {
    MDC_CHECK_IMDC();
    return mIMDComposer->notifyHotPlug();
}

int MultiDisplayClient::setHdmiPowerOff() {
    MDC_CHECK_IMDC();
    return mIMDComposer->setHdmiPowerOff();
}

int MultiDisplayClient::registerListener(
        sp<IExtendDisplayListener> listener, char* client, int msg) {
    MDC_CHECK_IMDC();
    return mIMDComposer->registerListener(listener,
            static_cast<void *>(this), client, msg);
}

int MultiDisplayClient::unregisterListener() {
    MDC_CHECK_IMDC();
    return mIMDComposer->unregisterListener(static_cast<void *>(this));
}


int MultiDisplayClient::getHdmiModeInfo(int* width, int* height, int* refresh, int* interlace, int *ratio) {
    MDC_CHECK_IMDC();
    return mIMDComposer->getHdmiModeInfo(width, height, refresh, interlace, ratio);
}

int MultiDisplayClient::setHdmiModeInfo(int width, int height, int refresh, int interlace, int ratio) {
    MDC_CHECK_IMDC();
    return mIMDComposer->setHdmiModeInfo(width, height, refresh, interlace, ratio);
}

int MultiDisplayClient::setHdmiScaleType(int type) {
    MDC_CHECK_IMDC();
    return mIMDComposer->setHdmiScaleType(type);
}

int MultiDisplayClient::setHdmiScaleStep(int hValue, int vValue) {
    MDC_CHECK_IMDC();
    return mIMDComposer->setHdmiScaleStep(hValue, vValue);
}

int MultiDisplayClient::getHdmiDeviceChange() {
    MDC_CHECK_IMDC();
    return mIMDComposer->getHdmiDeviceChange();
}

int MultiDisplayClient::getVideoInfo(int* displayW, int* displayH, int* fps, int* interlace) {
    MDC_CHECK_IMDC();
    return mIMDComposer->getVideoInfo(displayW, displayH, fps, interlace);
}

int MultiDisplayClient::getDisplayCapability() {
    MDC_CHECK_IMDC();
    return mIMDComposer->getDisplayCapability();
}

sp<ANativeWindow> MultiDisplayClient::createNewVideoSurface(int width, int height, int pixelFormat, int playerId) {
    LOGV("%s: %d, %d, %d, %d", __func__, width, height, pixelFormat, playerId);
    // Check if mIMDComposer is NULL
    // Not using macro MDC_CHECK_IMDC() here since return type for this function is sp<ANativeWindow>
    if (mIMDComposer == NULL) {
        LOGE("%s: MDSComposer is null", __func__);
        return NULL;
    }
    bool bEnabled = false;
    int backgroundPlayerId = 0;
    bEnabled = (mIMDComposer->isPlayInBackgroundEnabled() == 1 ? true : false);
    if (bEnabled) {
        backgroundPlayerId = mIMDComposer->getBackgroundPlayerId();
        if (playerId == backgroundPlayerId) {
            mComposerClient = new SurfaceComposerClient;
            CHECK_EQ(mComposerClient->initCheck(), (status_t)OK);

            mSurfaceControl = mComposerClient->createSurface(String8("BackgroungPlaySurface"), width, height, pixelFormat, 0);
            CHECK(mSurfaceControl != NULL);
            CHECK(mSurfaceControl->isValid());

            SurfaceComposerClient::openGlobalTransaction();
            //set the layer to be max value to avoid render contention with foreground surface.
            CHECK_EQ(mSurfaceControl->setLayer(INT_MAX), (status_t)OK);
            mSurfaceControl->setAlpha(255);
            SurfaceComposerClient::closeGlobalTransaction();

            mANW =  mSurfaceControl->getSurface();
            CHECK(mANW != NULL);

            mIMDComposer->setNativeSurface((int*)mANW.get());
        } else {
            return NULL;
        }
    } else {
        return NULL;
    }

    return mANW;
}

void MultiDisplayClient::destroyVideoSurface() {
    LOGV("%s", __func__);
    if (mANW != NULL) {
        LOGV("%s: clear native window", __func__);
        mANW.clear();
    }
    if (mSurfaceControl != NULL) {
        LOGV("%s: clear surface controller", __func__);
        mSurfaceControl.clear();
    }
    if (mComposerClient != NULL) {
        LOGV("%s: clear surface composer client", __func__);
        mComposerClient.clear();
    }
}

int MultiDisplayClient::setPlayInBackground(bool on, int playerId) {
    LOGV("%s: param %d, %d", __func__, on, playerId);
    MDC_CHECK_IMDC();
    return mIMDComposer->enablePlayInBackground(on, playerId);
}
