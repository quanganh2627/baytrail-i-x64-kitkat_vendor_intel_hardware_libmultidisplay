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

#ifndef __MULTIDISPLAY_CLIENT_H__
#define __MULTIDISPLAY_CLIENT_H__
#include <binder/Parcel.h>
#include <binder/IServiceManager.h>
#include <display/MultiDisplayType.h>
#include <display/IMultiDisplayComposer.h>
#include <display/IExtendDisplayModeChangeListener.h>

using namespace android;

class MultiDisplayClient {
private:
    void initService();
    sp<IMultiDisplayComposer> mIMDComposer;
public:
    MultiDisplayClient();
    ~MultiDisplayClient();
    /*
     * set display mode policy
     * param: please refer MultiDisplayType.h
     * return:
     *       0: on success
     *     !=0: on failure
     */
    int setModePolicy(int policy);
    /*
     * notify the MDS that widi is turned on/off
     * return:
     *       0: on success
     *     !=0: on failure
     */
    int notifyWidi(bool on);
     /*
     * notify the MDS that mipi should be turned on/off
     * param:
     * return:
     *       0: on success
     *     !=0: on failure
     */
    int notifyMipi(bool on);
    /*
     * get display mode
     * param:
     *      wait: "ture" means this interface will be blocked
     *            until getting an accurate mode,
     *            "false" means it will return immediatly,
     *            but couldn't ensure the mode is right,
     *            maybe return MDS_ERROR;
     * return:
     *     !=MDS_ERROR: on success
     *      =MDS_ERROR: on failure
     */
    int getMode(bool wait);
    /*
     * update video playback info
     * param: struct MDSVideoInfo
     * return:
     *       0: on success
     *     !=0: on failure
     */
    int updateVideoInfo(MDSVideoInfo* info);
    /*
     * notify HDMI is plugged in/off
     * param:
     * return:
     *       0: on success
     *     !=0: on failure
     */
    int notifyHotPlug();
    int setHdmiPowerOff();
    /*
     * register mode change listener
     * param:
     * return:
     *       0: on success
     *     !=0: on failure
     */
    int registerModeChangeListener(sp<IExtendDisplayModeChangeListener>);
    /*
     * unregister mode change listener
     * param:
     * return:
     *       0: on success
     *     !=0: on failure
     */
    int unregisterModeChangeListener(sp<IExtendDisplayModeChangeListener>);
    /*
     * set HDMI Timing info
     * param:
     * return:
     *       0: on success
     *     !=0: on failure
     */
    int setHdmiModeInfo(int width, int height, int refreshrate, int interlace, int ratio);
    /*
     * get HDMI Timing info
     * param:
     * return: HDMI Timing info number
     */
    int getHdmiModeInfo(int* pWidth, int* pHeight, int* pRefresh, int* pInterlace, int *pRatio);
    /*
     * set HDMI scale type
     * param:
     * return:
     *       0: on success
     *     !=0: on failure
     */
    int setHdmiScaleType(int type);
    /*
     * set HDMI scale step
     * param:
     * return:
     *       0: on success
     *     !=0: on failure
     */
    int setHdmiScaleStep(int hValue, int vValue);
    /*
     * get HDMI Device Change Status
     * param:
     * return: HDMI Device Change Status
     */
    int getHdmiDeviceChange();

    /*
     * get video info
     * input param:
     *              displayW: video display width, ensure it not null
     *              displayH: video display height, ensure it not null
     *                   fps: video fps, ensure it not null
     *           isinterlace: video is interlace?, ensure it not null
     * return:
     *       MDS_ERROR: on failure
     *    MDS_NO_ERROR: on success
     */
    int getVideoInfo(int* displayW, int* displayH, int* fps, int* isinterlace);
};

#endif

