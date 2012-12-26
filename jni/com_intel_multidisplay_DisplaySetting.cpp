/*
 * Copyright (C) 2008 The Android Open Source Project
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

#define LOG_TAG "MultiDisplay-Jni"

#include "JNIHelp.h"
#include "jni.h"
#include <android_runtime/AndroidRuntime.h>
#include <utils/Log.h>
#include <utils/threads.h>
#include <binder/Parcel.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>
#include <display/MultiDisplayClient.h>
#include <display/IExtendDisplayListener.h>

namespace android {

class JNIMDSListener : public BnExtendDisplayListener {
public:
    JNIMDSListener();
    ~JNIMDSListener();
    int onMdsMessage(int msg, void* value, int size);
    void setEnv(JNIEnv*, jobject*);
private:
    JNIEnv* mEnv;
    jobject* mObj;
    inline bool checkMode(int value, int bit) {
        if ((value & bit) == bit)
            return true;
        return false;
    }
};

JNIMDSListener::JNIMDSListener() {
    mEnv = NULL;
    mObj = NULL;
}

void JNIMDSListener::setEnv(JNIEnv* env, jobject* obj) {
    mEnv = env;
    mObj = obj;
}

JNIMDSListener::~JNIMDSListener() {
    LOGI("%s: release a mds listener", __func__);
    mEnv = NULL;
    mObj = NULL;
}

static JNIMDSListener* mListener = NULL;
static MultiDisplayClient* mMDClient = NULL;
static Mutex gMutex;
static JavaVM* gJvm = NULL;

int JNIMDSListener::onMdsMessage(int msg, void* value, int size) {
    jmethodID mid = NULL;
    if (mEnv != NULL && mObj != NULL &&
            gJvm != NULL && msg == MDS_MODE_CHANGE && size == sizeof(int)) {
        LOGD("%s: Get message from MDS, %d, 0x%x", __func__, msg, *((int*)value));
        void* eenv = NULL;
        jint ret = gJvm->GetEnv(&eenv, JNI_VERSION_1_4);
        JNIEnv* env = (JNIEnv*)eenv;
        if (env != mEnv) {
            LOGE("%s: invalid java env, ignore onMdsMessage callback", __func__);
            mEnv = NULL;
            mObj = NULL;
            return MDS_ERROR;
        }
        jclass clazz = mEnv->GetObjectClass(*mObj);
        mid = mEnv->GetMethodID(clazz, "onMdsMessage", "(II)V");
        if (mid == NULL) {
            LOGE("%s: fail to get onMdsMessage", __func__);
            mEnv = NULL;
            mObj = NULL;
            return MDS_ERROR;
        }
        mEnv->CallVoidMethod(*mObj, mid, msg, *((int*)value));
        mEnv = NULL;
        mObj = NULL;
    }
    return MDS_NO_ERROR;
}

static void initMDC() {
    LOGI("%s: create a MultiDisplay jni client", __func__);
    if (mMDClient == NULL) {
        mMDClient = new MultiDisplayClient();
    }
    if (mListener == NULL) {
        mListener = new JNIMDSListener();
        mMDClient->registerListener(mListener, "DisplaySetting", MDS_MODE_CHANGE);
    }
    mListener->setEnv(NULL, NULL);
}

static void DeInitMDC() {
    LOGI("%s: release a MultiDisplay JNI client", __func__);
    if (mMDClient != NULL) {
        if (mListener != NULL) {
            gJvm = NULL;
            mListener->setEnv(NULL, NULL);
            mMDClient->unregisterListener(mListener);
            delete mListener;
            mListener = NULL;
        }
        delete mMDClient;
        mMDClient = NULL;
    }
}

static jboolean intel_multidisplayDisplaySetting_InitMDSClient(JNIEnv* env, jobject thiz) {
    AutoMutex _l(gMutex);
    initMDC();
    return true;
}

static jboolean intel_multidisplayDisplaySetting_DeInitMDSClient(JNIEnv* env, jobject obj) {
    AutoMutex _l(gMutex);
    DeInitMDC();
    return true;
}

static jint intel_multidisplayDisplaySetting_getMode(JNIEnv* env, jobject obj) {
    if (mMDClient == NULL) return -1;
    AutoMutex _l(gMutex);
    return mMDClient->getMode(true);
}

static jboolean intel_multidisplayDisplaySetting_setModePolicy(JNIEnv* env, jobject obj, jint policy) {
    if (mMDClient == NULL) return false;
    AutoMutex _l(gMutex);
    mListener->setEnv(env, &obj);
    env->GetJavaVM(&gJvm);
    int ret = mMDClient->setModePolicy(policy);
    return (ret == MDS_NO_ERROR ? true : false);
}

static jboolean intel_multidisplayDisplaySetting_notifyHotPlug(JNIEnv* env, jobject obj) {
    if (mMDClient == NULL) return false;
    AutoMutex _l(gMutex);
    mListener->setEnv(env, &obj);
    env->GetJavaVM(&gJvm);
    int ret = mMDClient->notifyHotPlug();
    return (ret == MDS_NO_ERROR ? true : false);
}

static jboolean intel_multidisplayDisplaySetting_setHdmiPowerOff(JNIEnv* env, jobject obj) {
    if (mMDClient == NULL) return false;
    AutoMutex _l(gMutex);
    int ret = mMDClient->setHdmiPowerOff();
    return (ret == 0 ? true : false);
}

static jint intel_multidisplayDisplaySetting_getHdmiTiming(JNIEnv* env, jobject obj,
                                                        jintArray width, jintArray height,
                                                        jintArray refresh, jintArray interlace,
                                                        jintArray ratio) {
    if (mMDClient == NULL) return false;
    AutoMutex _l(gMutex);
    int32_t* pWidth = env->GetIntArrayElements(width, NULL);
    int32_t* pHeight = env->GetIntArrayElements(height, NULL);
    int32_t* pRefresh = env->GetIntArrayElements(refresh, NULL);
    int32_t* pInterlace = env->GetIntArrayElements(interlace, NULL);
    int32_t* pRatio = env->GetIntArrayElements(ratio, NULL);
    jint iCount = mMDClient->getHdmiModeInfo(pWidth, pHeight, pRefresh, pInterlace, pRatio);
    env->ReleaseIntArrayElements(width, pWidth, 0);
    env->ReleaseIntArrayElements(height, pHeight, 0);
    env->ReleaseIntArrayElements(refresh, pRefresh, 0);
    env->ReleaseIntArrayElements(interlace, pInterlace, 0);
    env->ReleaseIntArrayElements(ratio, pRatio, 0);
    return iCount;
}

static jboolean intel_multidisplayDisplaySetting_setHdmiTiming(JNIEnv* env, jobject obj,
             jint width, jint height, jint refresh, jint interlace, jint ratio) {
    if (mMDClient == NULL) return false;
    AutoMutex _l(gMutex);
    mListener->setEnv(env, &obj);
    env->GetJavaVM(&gJvm);
    int ret = mMDClient->setHdmiModeInfo(width, height, refresh, interlace, ratio);
    return (ret == MDS_NO_ERROR ? true : false);
}

static jint intel_multidisplayDisplaySetting_getHdmiInfoCount(JNIEnv* env, jobject obj) {
    if (mMDClient == NULL) return -1;
    AutoMutex _l(gMutex);
    return mMDClient->getHdmiModeInfo(NULL,NULL, NULL, NULL, NULL);
}

static jboolean intel_multidisplayDisplaySetting_HdmiScaleType(JNIEnv* env, jobject obj,jint Type)
{
    if (mMDClient == NULL) return false;
    AutoMutex _l(gMutex);
    return mMDClient->setHdmiScaleType(Type);
}

static jboolean intel_multidisplayDisplaySetting_HdmiScaleStep(JNIEnv* env, jobject obj,jint hValue,jint vValue)
{
    if (mMDClient == NULL) return false;
    AutoMutex _l(gMutex);
    return mMDClient->setHdmiScaleStep(hValue,vValue);
}

static jint intel_multidisplayDisplaySetting_getHdmiDeviceChange(JNIEnv* env, jobject obj) {
    if (mMDClient == NULL) return -1;
    AutoMutex _l(gMutex);
    return mMDClient->getHdmiDeviceChange();
}

static jint intel_multidisplayDisplaySetting_getDisplayCapability(JNIEnv* env, jobject obj) {
    if (mMDClient == NULL) return -1;
    AutoMutex _l(gMutex);
    return mMDClient->getDisplayCapability();
}

static jint intel_multidisplayDisplaySetting_setPlayInBackground(JNIEnv* env, jobject thiz, jboolean value, jint playerId)
{
    if (mMDClient == NULL) {
        LOGE("%s: mMDClient NULL", __func__);
        return -1;
    }
    AutoMutex _l(gMutex);
    return mMDClient->setPlayInBackground(value, playerId);
}

static jint intel_multidisplay_DisplaySetting_setHdcpStatus(JNIEnv* env, jobject thiz, jint value)
{
    if (mMDClient == NULL) {
        LOGE("%s: mMDClient NULL", __func__);
        return -1;
    }
    AutoMutex _l(gMutex);
    return mMDClient->setHdcpStatus(value);
}

static jint intel_multidisplay_DisplaySetting_notifyScreenOff(JNIEnv* env, jobject obj) {
    if (mMDClient == NULL) return -1;
    AutoMutex _l(gMutex);
    return mMDClient->notifyScreenOff();
}

static JNINativeMethod sMethods[] = {
    /* name, signature, funcPtr */
    {"native_InitMDSClient", "()Z", (void*)intel_multidisplayDisplaySetting_InitMDSClient},
    {"native_DeInitMDSClient", "()Z", (void*)intel_multidisplayDisplaySetting_DeInitMDSClient},
    {"native_getMode", "()I", (void*)intel_multidisplayDisplaySetting_getMode},
    {"native_setModePolicy", "(I)Z", (void*)intel_multidisplayDisplaySetting_setModePolicy},
    {"native_notifyHotPlug", "()Z", (void*)intel_multidisplayDisplaySetting_notifyHotPlug},
    {"native_setHdmiPowerOff", "()Z", (void*)intel_multidisplayDisplaySetting_setHdmiPowerOff},
    {"native_setHdmiTiming", "(IIIII)Z", (void*)intel_multidisplayDisplaySetting_setHdmiTiming},
    {"native_getHdmiTiming", "([I[I[I[I[I)I", (void*)intel_multidisplayDisplaySetting_getHdmiTiming},
    {"native_getHdmiInfoCount", "()I", (void*)intel_multidisplayDisplaySetting_getHdmiInfoCount},
    {"native_setHdmiScaleType", "(I)Z", (void*)intel_multidisplayDisplaySetting_HdmiScaleType},
    {"native_setHdmiScaleStep", "(II)Z", (void*)intel_multidisplayDisplaySetting_HdmiScaleStep},
    {"native_getHdmiDeviceChange", "()I", (void*)intel_multidisplayDisplaySetting_getHdmiDeviceChange},
    {"native_getDisplayCapability", "()I", (void*)intel_multidisplayDisplaySetting_getDisplayCapability},
    {"native_setPlayInBackground", "(ZI)I", (void*)intel_multidisplayDisplaySetting_setPlayInBackground},
    {"native_setHdcpStatus", "(I)I", (void*)intel_multidisplay_DisplaySetting_setHdcpStatus},
    {"native_notifyScreenOff", "()I", (void*)intel_multidisplay_DisplaySetting_notifyScreenOff},
};


int register_intel_multidisplay_DisplaySetting(JNIEnv* env) {
    jclass clazz = env->FindClass("com/intel/multidisplay/DisplaySetting");
    if (clazz == NULL) {
        LOGE("%s: Fail to find DisplaySetting class", __func__);
        return -1;
    }
    return jniRegisterNativeMethods(env, "com/intel/multidisplay/DisplaySetting", sMethods, NELEM(sMethods));
}

} /* namespace android */

