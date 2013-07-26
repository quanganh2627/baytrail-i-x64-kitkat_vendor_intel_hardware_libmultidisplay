
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

#ifndef __MULTIDISPLAY_CLIENT_H__
#define __MULTIDISPLAY_CLIENT_H__

#include <display/MultiDisplayType.h>
#include <display/IMultiDisplayListener.h>
#include <display/IMultiDisplayCallback.h>
#include <display/IMultiDisplayComposer.h>

using namespace android;

class MultiDisplayClient {
private:
    void init();

    /// Binder proxy handle for Multi Display Composer
    sp<IMultiDisplayComposer> mMDSComposer;
public:
    MultiDisplayClient();
    ~MultiDisplayClient();

    /**
     * @brief Notify display hotplug event to MDS
     *
     * @param dpyID:     @see MDS_DISPLAY_ID
     * @param connected: "true" indicate receiving a plug-in event @
     *                    "false" indicates receiving a plug-out event
     * @return @see status_t in <utils/Errors.h>
     */
    status_t notifyHotPlug(MDS_DISPLAY_ID dpyID, bool connected);

    /**
     * @brief Allocate a unique id for video player
     * @return: a sessionId
     */
    int allocateVideoSessionId();

    /**
     * @brief Reset video playback
     * @return @see status_t in <utils/Errors.h>
     */
    status_t resetVideoPlayback();

    /**
     * @brief Set the state of video playback
     * @param state @see MDS_VIDEO_STATE in MultiDisplayType.h
     * @return @see status_t in <utils/Errors.h>
     */
    status_t setVideoState(int sessionId, MDS_VIDEO_STATE state);

    /**
     * @brief Get the state of video playback
     * @param sessionId, a unique id for every video player
     * @return @see MDS_VIDEO_STATE in MultiDisplayType.h
     */
    MDS_VIDEO_STATE getVideoState(int sessionID);

    /**
     * @brief Update the video playback info
     * @param sessionId, a unique id for every video player
     * @param info @see MDSVideoSourceInfo in the MultiDisplayType.h
     * @return @see status_t in <utils/Errors.h>
     */
    status_t setVideoSourceInfo(int sessionId, MDSVideoSourceInfo* info);

    /**
     * @brief Get video source info
     * @param sessionId, a unique id for every video player
     * @param info @see MDSVideoSourceInfo in the MultiDisplayType.h
     * @return @see status_t in <utils/Errors.h>
     */
    status_t getVideoSourceInfo(int sessionId, MDSVideoSourceInfo* info);

    /**
     * @brief Update the state of phone call
     * @param state @see MDS_PHONE_STATE in the MultiDisplayType.h
     * @return @see status_t in <utils/Errors.h>
     */
    status_t setPhoneState(MDS_PHONE_STATE state);

    /**
     * @brief Register display listener, those modules which need to get
     * info from MDS can register a listener to get info, avoid to use
     * polling method.
     * @param listener: inherit and implement IMultiDisplayListener.
     * @param name: client name, ensure it is not a null pointer
     * @param msg: messge type, @see MDS_MESSAGE in MultiDisplayType.h
     * @return @see status_t in <utils/Errors.h>
     */
    status_t registerListener(sp<IMultiDisplayListener> listener,
        const char* name, MDS_MESSAGE msg);

    /**
     * @brief Unregister the display listener
     * @param
     * @return @see status_t in <utils/Errors.h>
     */
    status_t unregisterListener();

    /**
     * @brief Register a display callback handler to handle those
     * "drm write" operations.
     * @param cbk The registered callback handle.
     * @return @see status_t in <utils/Errors.h>
     */
    status_t registerCallback(sp<IMultiDisplayCallback> cbk);

    /**
     * @brief Unregister display listener
     * @param
     * return: @see status_t in <utils/Errors.h>
     */
    status_t unregisterCallback();

    /**
     * @brief set the state of display
     * @param dypID The display device id @see MDS_DISPLAY_ID
     * @param state @see MDS_DISPLAY_STATE
     * @return @see status_t in <utils/Errors.h>
     */
    status_t setDisplayState(MDS_DISPLAY_ID dpyID, MDS_DISPLAY_STATE state);

    /**
     * @brief Set the timing of display according to the input timing parameter
     * @param dypID The display device id @see MDS_DISPLAY_ID
     * @param timing The timing which is going to be set. @see MDSDisplayTiming
     * @return @see status_t in <utils/Errors.h>
     */
    status_t setDisplayTiming(MDS_DISPLAY_ID dpyID, MDSDisplayTiming* timing);

    /**
     * @brief Get the timing count of display, only for HDMI
     * @param dypID The display device id @see MDS_DISPLAY_ID
     * @return The total timing count supported by display device
     */
    int getDisplayTimingCount(MDS_DISPLAY_ID dpyID);

    /**
     * @brief Get the timing list of display, only for HDMI
     * @param dypID The display device id @see MDS_DISPLAY_ID
     * @param count the total timing count, must equal to the value from \
     *        @see getDisplayTimingCount
     * @param list  the timing list
     * return: @see status_t in <utils/Errors.h>
     */
    status_t getDisplayTimingList(MDS_DISPLAY_ID dpyID,
            const int count, MDSDisplayTiming** list);

    /**
     * @brief Get the timing which is used now, only for HDMI
     * @param dypID The display device id @see MDS_DISPLAY_ID
     * @param timing The current timing in use
     * @return: @see status_t in <utils/Errors.h>
     */
    status_t getCurrentDisplayTiming(MDS_DISPLAY_ID dpyID, MDSDisplayTiming* timing);

    /**
     * @brief Set the timing of display according to the input timing index
     * @param dypID The display device id @see MDS_DISPLAY_ID
     * @param index The index in the timing list.
     * @return: @see status_t in <utils/Errors.h>
     */
    status_t setDisplayTimingByIndex(MDS_DISPLAY_ID dpyID, int index);

    /**
     * @brief Get the timing index which is used now, only for HDMI
     * @param dypID The display device id @see MDS_DISPLAY_ID
     * @return current timing index in the list
     */
    int getCurrentDisplayTimingIndex(MDS_DISPLAY_ID dpyID);

    /**
     * @brief Set the scale of display, only for HDMI
     * @param dypID The display device id @see MDS_DISPLAY_ID
     * @param type  @see MDS_SCALING_TYPE in MultiDisplayType.h
     * @return @see status_t in <utils/Errors.h>
     */
    status_t setScalingType(MDS_DISPLAY_ID dpyID, MDS_SCALING_TYPE type);

    /**
     * @brief Set the overscan compensation for displayp, only for HDMI
     * @param dypID The display device id @see MDS_DISPLAY_ID
     * @param hValue
     * @param vValue
     * @return @see status_t in <utils/Errors.h>
     */
    status_t setOverscan(MDS_DISPLAY_ID dpyID, int hValue, int vValue);

    /**
     * @brief Get external display Sink device Change Status, only for HDMI
     * @param dypID The display device id @see MDS_DISPLAY_ID
     * @return if false: external display sink device isn't changed
     *         if true: external display sink device was changed
     */
    bool getDisplayDeviceChange(MDS_DISPLAY_ID dpyID);

    /**
     * @brief Get the display capability of platform, support HDMI or WIDI?
     * @param
     * @return The display capability of platform.
     * @see MDS_DISPLAY_CAP in MultiDisplayType.h
     */
    MDS_DISPLAY_CAP getPlatformCapability();

    /**
     * @brife Get display mode
     * @param wait "ture" means this interface will be blocked
     *             until getting an accurate mode,
     *             "false" means it will return immediatly,
     *             but couldn't ensure the mode is right.
     * @return: @see MDS_DISPLAY_MODE in MultiDisplayType.h
     */
    MDS_DISPLAY_MODE getDisplayMode(bool wait);
};
#endif

