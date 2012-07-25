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

#define LOG_TAG "MultiDisplay-DisplaySetting-Jni"

#include "JNIHelp.h"
#include "jni.h"
#include <android_runtime/AndroidRuntime.h>
#include <utils/Log.h>
#include <utils/threads.h>
#include <binder/Parcel.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>
#include <display/MultiDisplayClient.h>
#include <display/IExtendDisplayModeChangeListener.h>

namespace android {

class JNIMDSListener : public BnExtendDisplayModeChangeListener {
public:
    JNIMDSListener();
    ~JNIMDSListener();
    void onModeChange(int mode);
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

void JNIMDSListener::onModeChange(int mode) {
    jmethodID mid = NULL;
    if (mEnv != NULL && mObj != NULL && gJvm != NULL) {
        void* eenv = NULL;
        jint ret = gJvm->GetEnv(&eenv, JNI_VERSION_1_4);
        JNIEnv* env = (JNIEnv*)eenv;
        if (env != mEnv) {
            LOGE("%s: invalid java env, ignore onModeChange callback", __func__);
            mEnv = NULL;
            mObj = NULL;
            return;
        }
        jclass clazz = mEnv->GetObjectClass(*mObj);
        mid = mEnv->GetMethodID(clazz, "onModeChange", "(I)V");
        if (mid == NULL) {
            LOGE("%s: fail to get onModeChange", __func__);
            mEnv = NULL;
            mObj = NULL;
            return;
        }
        mEnv->CallVoidMethod(*mObj, mid, mode);
        mEnv = NULL;
        mObj = NULL;
    } else
        LOGI("%s: No need to call onModeChange, 0x%x", __func__, mode);
}

static void initMDC() {
    LOGI("%s: create a MultiDisplay jni client", __func__);
    if (mMDClient == NULL) {
        mMDClient = new MultiDisplayClient();
    }
    if (mListener == NULL) {
        mListener = new JNIMDSListener();
        mMDClient->registerModeChangeListener(mListener);
    }
    mListener->setEnv(NULL, NULL);
}

static void DeInitMDC() {
    LOGI("%s: release a MultiDisplay JNI client", __func__);
    if (mMDClient != NULL) {
        if (mListener != NULL) {
            gJvm = NULL;
            mListener->setEnv(NULL, NULL);
            mMDClient->unregisterModeChangeListener(mListener);
            delete mListener;
            mListener = NULL;
        }
        delete mMDClient;
        mMDClient = NULL;
    }
}

static String16 get_interface_name(sp<IBinder> service) {
    if (service != NULL) {
        Parcel data, reply;
        status_t err = service->transact(IBinder::INTERFACE_TRANSACTION, data, &reply);
        if (err == NO_ERROR) {
            return reply.readString16();
        }
    }
    return String16();
}

static sp<IBinder> getService(String16 sname) {
    int err;
    sp<IServiceManager> sm = defaultServiceManager();
    if (sm == NULL) {
        LOGE("%s: fail to getservice manager", __func__);
        return NULL;
    }

    sp<IBinder> service = sm->checkService(sname);
    if (service == NULL) {
        LOGE("%s: fail to get surfacefinger service", __func__);
        return NULL;
    }
    return service;
}

static int updateSurface() {
    int err = -1;
    sp<IBinder> service = getService(String16("SurfaceFlinger"));
    String16 ifName = get_interface_name(service);
    android::Parcel data, reply;
    data.writeInterfaceToken(ifName);
    err = service->transact(1004, data, &reply, 0);
    if (err != 0) {
        LOGE("%s: fail to transact", __func__);
        return -1;
    }
    return 0;
}

static jboolean android_server_DisplaySetting_InitMDSClient(JNIEnv* env, jobject thiz) {
    AutoMutex _l(gMutex);
    initMDC();
    return true;
}

static jboolean android_server_DisplaySetting_DeInitMDSClient(JNIEnv* env, jobject obj) {
    AutoMutex _l(gMutex);
    DeInitMDC();
    return true;
}

static jint android_server_DisplaySetting_getMode(JNIEnv* env, jobject obj) {
    if (mMDClient == NULL) return -1;
    AutoMutex _l(gMutex);
    return mMDClient->getMode(true);
}

static jboolean android_server_DisplaySetting_setModePolicy(JNIEnv* env, jobject obj, jint policy) {
    if (mMDClient == NULL) return false;
    AutoMutex _l(gMutex);
    mListener->setEnv(env, &obj);
    env->GetJavaVM(&gJvm);
    int ret = mMDClient->setModePolicy(policy);
    if (ret == 0)
        updateSurface();
    return (ret == 0 ? true : false);
}

static jboolean android_server_DisplaySetting_notifyHotPlug(JNIEnv* env, jobject obj) {
    if (mMDClient == NULL) return false;
    AutoMutex _l(gMutex);
    mListener->setEnv(env, &obj);
    env->GetJavaVM(&gJvm);
    int ret = mMDClient->notifyHotPlug();
    if (ret == 0)
        updateSurface();
    return (ret == 0 ? true : false);
}

static jboolean android_server_DisplaySetting_setHdmiPowerOff(JNIEnv* env, jobject obj) {
    if (mMDClient == NULL) return false;
    AutoMutex _l(gMutex);
    int ret = mMDClient->setHdmiPowerOff();
    return (ret == 0 ? true : false);
}

static jint android_server_DisplaySetting_getHdmiTiming(JNIEnv* env, jobject obj,
             jintArray width, jintArray height, jintArray refresh, jintArray interlace) {
    if (mMDClient == NULL) return false;
    AutoMutex _l(gMutex);
    int32_t* pWidth = env->GetIntArrayElements(width, NULL);
    int32_t* pHeight = env->GetIntArrayElements(height, NULL);
    int32_t* pRefresh = env->GetIntArrayElements(refresh, NULL);
    int32_t* pInterlace = env->GetIntArrayElements(interlace, NULL);
    jint iCount = mMDClient->getHdmiModeInfo(pWidth, pHeight, pRefresh, pInterlace);
    env->ReleaseIntArrayElements(width, pWidth, 0);
    env->ReleaseIntArrayElements(height, pHeight, 0);
    env->ReleaseIntArrayElements(refresh, pRefresh, 0);
    env->ReleaseIntArrayElements(interlace, pInterlace, 0);
    return iCount;
}

static jboolean android_server_DisplaySetting_setHdmiTiming(JNIEnv* env, jobject obj,
             jint width, jint height, jint refresh, jint interlace, jint ratio) {
    if (mMDClient == NULL) return false;
    AutoMutex _l(gMutex);
    mListener->setEnv(env, &obj);
    env->GetJavaVM(&gJvm);
    return mMDClient->setHdmiModeInfo(width, height, refresh, interlace, ratio);
}

static jint android_server_HDMIObserver_getHdmiInfoCount(JNIEnv* env, jobject obj) {
    if (mMDClient == NULL) return -1;
    AutoMutex _l(gMutex);
    return mMDClient->getHdmiModeInfo(NULL,NULL, NULL, NULL);
}

static jboolean android_server_HDMIObserver_HdmiScaleType(JNIEnv* env, jobject obj,jint Type)
{
    if (mMDClient == NULL) return false;
    AutoMutex _l(gMutex);
    return mMDClient->setHdmiScaleType(Type);
}

static jboolean android_server_HDMIObserver_HdmiScaleStep(JNIEnv* env, jobject obj,jint hValue,jint vValue)
{
    if (mMDClient == NULL) return false;
    AutoMutex _l(gMutex);
    return mMDClient->setHdmiScaleStep(hValue,vValue);
}

static jint android_server_HDMIObserver_getHdmiDeviceChange(JNIEnv* env, jobject obj) {
    if (mMDClient == NULL) return -1;
    AutoMutex _l(gMutex);
    return mMDClient->getHdmiDeviceChange();
}

static JNINativeMethod sMethods[] = {
    /* name, signature, funcPtr */
    {"native_InitMDSClient", "()Z", (void*)android_server_DisplaySetting_InitMDSClient},
    {"native_DeInitMDSClient", "()Z", (void*)android_server_DisplaySetting_DeInitMDSClient},
    {"native_getMode", "()I", (void*)android_server_DisplaySetting_getMode},
    {"native_setModePolicy", "(I)Z", (void*)android_server_DisplaySetting_setModePolicy},
    {"native_notifyHotPlug", "()Z", (void*)android_server_DisplaySetting_notifyHotPlug},
    {"native_setHdmiPowerOff", "()Z", (void*)android_server_DisplaySetting_setHdmiPowerOff},
    {"native_setHdmiTiming", "(IIIII)Z", (void*)android_server_DisplaySetting_setHdmiTiming},
    {"native_getHdmiTiming", "([I[I[I[I)I", (void*)android_server_DisplaySetting_getHdmiTiming},
    {"native_getHdmiInfoCount", "()I", (void*)android_server_HDMIObserver_getHdmiInfoCount},
    {"native_setHdmiScaleType", "(I)Z", (void*)android_server_HDMIObserver_HdmiScaleType},
    {"native_setHdmiScaleStep", "(II)Z", (void*)android_server_HDMIObserver_HdmiScaleStep},
    {"native_getHdmiDeviceChange", "()I", (void*)android_server_HDMIObserver_getHdmiDeviceChange},
};


int register_android_server_DisplaySetting(JNIEnv* env) {
    jclass clazz = env->FindClass("com/android/server/DisplaySetting");
    if (clazz == NULL) {
        LOGE("%s: fail to find DisplaySetting class", __func__);
        return -1;
    }
    return jniRegisterNativeMethods(env, "com/android/server/DisplaySetting", sMethods, NELEM(sMethods));
}

} /* namespace android */

