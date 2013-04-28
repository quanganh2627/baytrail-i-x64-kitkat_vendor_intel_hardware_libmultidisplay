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
 */

#define LOG_TAG "MultiDisplay"

#include "JNIHelp.h"
#include "jni.h"
#include <android_runtime/AndroidRuntime.h>

#include <utils/Log.h>
#include <utils/Errors.h>
#include <utils/threads.h>

#include <binder/Parcel.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>

#include <display/MultiDisplayClient.h>
#include <display/IMultiDisplayListener.h>

namespace android {

#define CLASS_PATH_NAME  "com/intel/multidisplay/DisplaySetting"
static sp<class JNIMDSListener> gListener = NULL;
static MultiDisplayClient* gMDClient = NULL;
static Mutex gMutex;


class JNIMDSListener : public BnMultiDisplayListener
{
public:
    JNIMDSListener(JNIEnv* env, jobject thiz, jobject serviceObj);
    ~JNIMDSListener();
    status_t onMdsMessage(MDS_MESSAGE msg, void* value, int size);

private:
    JNIMDSListener(); // private constructor
    jobject mServiceObj; // reference to DisplaySetting Java object to call back
    jmethodID mOnMdsMessageMethodID; // onMdsMessage method id
};

JNIMDSListener::JNIMDSListener(JNIEnv* env, jobject thiz, jobject serviceObj)
{
    LOGI("%s: Creating MDS listener.", __func__);
    jclass clazz = env->FindClass(CLASS_PATH_NAME);
    mOnMdsMessageMethodID = NULL;
    if (clazz == NULL) {
        LOGE("%s: Fail to find class %s", __func__, CLASS_PATH_NAME);
    } else {
        mOnMdsMessageMethodID = env->GetMethodID(clazz, "onMdsMessage", "(II)V");
        if (mOnMdsMessageMethodID == NULL) {
            LOGE("%s: Fail to find onMdsMessage method.", __func__);
        }
    }

    mServiceObj  = env->NewGlobalRef(serviceObj);
    if (!mServiceObj) {
        LOGE("%s: Fail to reference serviceObj!", __func__);
    }
}

JNIMDSListener::~JNIMDSListener() {
    LOGI("%s: Releasing MDS listener.", __func__);
    JNIEnv *env = AndroidRuntime::getJNIEnv();
    if (env) {
        // remove global reference
        env->DeleteGlobalRef(mServiceObj);
    }
}

status_t JNIMDSListener::onMdsMessage(MDS_MESSAGE msg, void* value, int size)
{
    LOGD("Entering %s", __func__);

    JNIEnv *env = AndroidRuntime::getJNIEnv();
    if (env == NULL) {
        LOGE("%s: Faild to get JNI Env.", __func__);
        return NO_INIT;
    }

    if (!mServiceObj || !mOnMdsMessageMethodID) {
        LOGE("%s: Invalid service object or method ID", __func__);
        return NO_INIT;
    }

    if (msg == MDS_MSG_MODE_CHANGE ||
        msg == MDS_MSG_HOT_PLUG) {
        LOGD("%s: Get message from MDS, %d, 0x%x", __func__, msg, *((int*)value));
        env->CallVoidMethod(mServiceObj, mOnMdsMessageMethodID, (int)msg, *((int*)value));
    }

    if (env->ExceptionCheck()) {
        LOGW("%s: Exception occurred while posting message.", __func__);
        env->ExceptionClear();
    }

    LOGD("Leaving %s", __func__);
    return NO_ERROR;
}

static jboolean MDS_InitMDSClient(JNIEnv* env, jobject thiz, jobject serviceObj)
{
    AutoMutex _l(gMutex);
    LOGI("%s: creating MultiDisplay JNI client.", __func__);
    if (gMDClient) {
        LOGW("%s: MultiDisplay JNI client has been created.", __func__);
        return true;
    }

    if (env == NULL || thiz == NULL || serviceObj == NULL) {
        LOGE("%s: Invalid input parameters.", __func__);
        return false;
    }

    gMDClient = new MultiDisplayClient();
    if (gMDClient == NULL) {
        LOGE("%s: Failed to create MultiDisplayClient instance.", __func__);
        return false;
    }

    gListener = new JNIMDSListener(env, thiz, serviceObj);
    if (gListener == NULL) {
        LOGE("%s: Failed to create JNIMDSListener instance.", __func__);
        delete gMDClient;
        gMDClient = NULL;
        return false;
    }

    gMDClient->registerListener(gListener, "DisplaySetting",
            (MDS_MESSAGE)(MDS_MSG_MODE_CHANGE | MDS_MSG_HOT_PLUG));
    return true;
}

static jboolean MDS_DeInitMDSClient(JNIEnv* env, jobject obj)
{
    AutoMutex _l(gMutex);
    LOGI("%s: Releasing MultiDisplay JNI client.", __func__);
    if (gListener != NULL && gMDClient != NULL) {
        gMDClient->unregisterListener();
        gListener = NULL;
        delete gMDClient;
        gMDClient = NULL;
    }
    return true;
}

static jint MDS_getMode(JNIEnv* env, jobject obj)
{
    if (gMDClient == NULL) return 0;
    AutoMutex _l(gMutex);
    return gMDClient->getDisplayMode(true);
}

/*
static jboolean MDS_setModePolicy(JNIEnv* env, jobject obj, jint policy)
{
    if (gMDClient == NULL) return false;
    AutoMutex _l(gMutex);
    int ret = gMDClient->setModePolicy(policy);
    return (ret == MDS_NO_ERROR ? true : false);
}

static jboolean MDS_notifyHotPlug(JNIEnv* env, jobject obj)
{
    if (gMDClient == NULL) return false;
    AutoMutex _l(gMutex);
    int ret = gMDClient->notifyHotPlug();
    return (ret == MDS_NO_ERROR ? true : false);
}

static jboolean MDS_setHdmiPowerOff(JNIEnv* env, jobject obj)
{
    if (gMDClient == NULL) return false;
    AutoMutex _l(gMutex);
    int ret = gMDClient->setHdmiPowerOff();
    return (ret == 0 ? true : false);
}
*/

static jint MDS_getHdmiTiming(
    JNIEnv* env,
    jobject obj,
    jintArray width,
    jintArray height,
    jintArray refresh,
    jintArray interlace,
    jintArray ratio)
{
    if (gMDClient == NULL) return 0;
    AutoMutex _l(gMutex);
    int32_t* pWidth = env->GetIntArrayElements(width, NULL);
    int32_t* pHeight = env->GetIntArrayElements(height, NULL);
    int32_t* pRefresh = env->GetIntArrayElements(refresh, NULL);
    int32_t* pInterlace = env->GetIntArrayElements(interlace, NULL);
    int32_t* pRatio = env->GetIntArrayElements(ratio, NULL);
    // The total supported timing count
    jint iCount = gMDClient->getDisplayTimingCount(MDS_DISPLAY_EXTERNAL);

    if (iCount > 0) {
        jint i;
        MDSDisplayTiming list[iCount];
        gMDClient->getDisplayTimingList(MDS_DISPLAY_EXTERNAL, &list[0]);
        for (i = 0; i < iCount; i++) {
            pRatio[i]     = list[i].ratio;
            pWidth[i]     = list[i].width;
            pHeight[i]    = list[i].height;
            pRefresh[i]   = list[i].refresh;
            pInterlace[i] = list[i].interlace;
        }
    }

    env->ReleaseIntArrayElements(width, pWidth, 0);
    env->ReleaseIntArrayElements(height, pHeight, 0);
    env->ReleaseIntArrayElements(refresh, pRefresh, 0);
    env->ReleaseIntArrayElements(interlace, pInterlace, 0);
    env->ReleaseIntArrayElements(ratio, pRatio, 0);
    return iCount;
}

static jboolean MDS_setHdmiTiming(
    JNIEnv* env,
    jobject obj,
    jint width,
    jint height,
    jint refresh,
    jint interlace,
    jint ratio)
{
    if (gMDClient == NULL) return false;
    AutoMutex _l(gMutex);

    MDSDisplayTiming timing;
    timing.ratio = ratio;
    timing.width = width;
    timing.height = height;
    timing.refresh = refresh;
    timing.interlace = interlace;

    status_t ret = gMDClient->setDisplayTiming(MDS_DISPLAY_EXTERNAL, &timing);
    return (ret == NO_ERROR ? true : false);
}

static jint MDS_getHdmiInfoCount(JNIEnv* env, jobject obj)
{
    if (gMDClient == NULL) return 0;
    AutoMutex _l(gMutex);
    return gMDClient->getDisplayTimingCount(MDS_DISPLAY_EXTERNAL);
}

static jboolean MDS_HdmiScaleType(JNIEnv* env, jobject obj, jint type)
{
    if (gMDClient == NULL) return false;
    AutoMutex _l(gMutex);

    status_t ret = gMDClient->setScalingType(
            MDS_DISPLAY_EXTERNAL, (MDS_SCALING_TYPE)type);
    return (ret == NO_ERROR ? true : false);
}

static jboolean MDS_HdmiOverscan(JNIEnv* env, jobject obj,jint hValue,jint vValue)
{
    if (gMDClient == NULL) return false;
    AutoMutex _l(gMutex);
    status_t ret = gMDClient->setOverscan(
            MDS_DISPLAY_EXTERNAL, hValue, vValue);
    return (ret == NO_ERROR ? true : false);
}

static jint MDS_getHdmiDeviceChange(JNIEnv* env, jobject obj)
{
    if (gMDClient == NULL) return 0;
    AutoMutex _l(gMutex);
    bool ret = gMDClient->getDisplayDeviceChange(MDS_DISPLAY_EXTERNAL);
    return ret ? 1 : 0;
}

static jint MDS_getDisplayCapability(JNIEnv* env, jobject obj)
{
    if (gMDClient == NULL) return 0;
    AutoMutex _l(gMutex);
    return gMDClient->getPlatformCapability();
}

static jint MDS_setPhoneState(JNIEnv* env, jobject obj, jint state)
{
    if (gMDClient == NULL) return 0;
    AutoMutex _l(gMutex);
    return gMDClient->setPhoneState((MDS_PHONE_STATE)state);
}

static JNINativeMethod sMethods[] = {
    /* name, signature, funcPtr */
    {"native_InitMDSClient", "(Lcom/intel/multidisplay/DisplaySetting;)Z", (void*)MDS_InitMDSClient},
    {"native_DeInitMDSClient", "()Z", (void*)MDS_DeInitMDSClient},
    {"native_getMode", "()I", (void*)MDS_getMode},
    // {"native_setModePolicy", "(I)Z", (void*)MDS_setModePolicy},
    // {"native_notifyHotPlug", "()Z", (void*)MDS_notifyHotPlug},
    // {"native_setHdmiPowerOff", "()Z", (void*)MDS_setHdmiPowerOff},
    {"native_setHdmiTiming", "(IIIII)Z", (void*)MDS_setHdmiTiming},
    {"native_getHdmiTiming", "([I[I[I[I[I)I", (void*)MDS_getHdmiTiming},
    {"native_getHdmiInfoCount", "()I", (void*)MDS_getHdmiInfoCount},
    {"native_setHdmiScaleType", "(I)Z", (void*)MDS_HdmiScaleType},
    {"native_setHdmiOverscan", "(II)Z", (void*)MDS_HdmiOverscan},
    {"native_getHdmiDeviceChange", "()I", (void*)MDS_getHdmiDeviceChange},
    {"native_getDisplayCapability", "()I", (void*)MDS_getDisplayCapability},
    {"native_setPhoneState", "(I)I", (void*)MDS_setPhoneState},
};


int register_intel_multidisplay_DisplaySetting(JNIEnv* env)
{
    LOGD("Entering %s", __func__);
    jclass clazz = env->FindClass(CLASS_PATH_NAME);
    if (clazz == NULL) {
        LOGE("%s: Fail to find class %s", __func__, CLASS_PATH_NAME);
        return -1;
    }
    int ret = jniRegisterNativeMethods(env, CLASS_PATH_NAME, sMethods, NELEM(sMethods));
    LOGD("Leaving %s, return = %d", __func__, ret);
    return ret;
}

} /* namespace android */

