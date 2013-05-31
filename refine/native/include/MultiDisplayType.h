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

#ifndef __MULTIDISPLAYTYPE_H__
#define __MULTIDISPLAYTYPE_H__

#include <hardware/hwcomposer_defs.h>

/** @brief The max HDMI timing number */
#define HDMI_TIMING_MAX (128)

/** @brief The display ID */
typedef enum {
    MDS_DISPLAY_PRIMARY   = HWC_DISPLAY_PRIMARY,
    MDS_DISPLAY_EXTERNAL  = HWC_DISPLAY_EXTERNAL,
    MDS_DISPLAY_VIRTUAL   = HWC_NUM_DISPLAY_TYPES,
    MDS_DISPLAY_MAX_NUM,
} MDS_DISPLAY_ID;

/** @brief The state of display */
typedef enum {
    MDS_DISPLAY_ENABLE   = 0,
    MDS_DISPLAY_DISABLE,
    MDS_DISPLAY_SUSPEND,
    MDS_DISPLAY_PAUSE,
    MDS_DISPLAY_STATE_MAX_NUM
} MDS_DISPLAY_STATE;

/** @brief The state of the telephone call from DisplayObserver */
typedef enum {
    MDS_PHONE_STATE_OFF = 0,
    MDS_PHONE_STATE_ON  = 1,
} MDS_PHONE_STATE;

/** @brief The messages MDS broadcasts to listeners */
typedef enum {
    MDS_MSG_DISPLAY_STATE_CHANGE       = 1,
    MDS_MSG_VIDEO_SOURCE_INFO          = 1 << 1,
    MDS_MSG_MODE_CHANGE                = 1 << 2,
    MDS_MSG_HOT_PLUG                   = 1 << 3,
} MDS_MESSAGE;

/** @brief Video source info from video players */
typedef struct {
    bool isplaying;   /**< kept for CTP only */
    bool isprotected; /**< protected content */
    int  frameRate;   /**< frame rate */
    int  displayW;    /**< displayed width */
    int  displayH;    /**< displayed height */
    bool isinterlace; /**< true: interlaced false: progressive */
} MDSVideoSourceInfo;

/** @brief HDMI timing structure */
typedef struct {
    uint32_t  refresh;    /**< refresh rate */
    int       width;
    int       height;
    int       interlace;  /**< 1:interlaced 0:progressive */
    int       ratio;      /**< aspect ratio */
} MDSDisplayTiming;

/** @brief The display capbiltity of platform */
typedef enum {
    MDS_DISPLAY_CAP_UNKNOWN       = 0,
    MDS_SUPPORT_PRIMARY_DISPLAY   = 1 << MDS_DISPLAY_PRIMARY,
    MDS_SUPPORT_EXTERNAL_DISPLAY  = 1 << MDS_DISPLAY_EXTERNAL,
    MDS_SUPPORT_VIRTUAL_DISPLAY   = 1 << MDS_DISPLAY_VIRTUAL,
}MDS_DISPLAY_CAP;

/** @brief The state of video playback */
typedef enum {
    MDS_VIDEO_STATE_UNKNOWN = -1,
    MDS_VIDEO_PREPARING   = 1,
    MDS_VIDEO_PREPARED    = 2,
    MDS_VIDEO_UNPREPARING = 3,
    MDS_VIDEO_UNPREPARED  = 4,
} MDS_VIDEO_STATE;

/** @brief The scaling type @see SurfaceFligner.h */
typedef enum {
    MDS_SCALING_NONE        = 0,
    MDS_SCALING_FULL_SCREEN = 1,
    MDS_SCALING_CENTER      = 2,
    MDS_SCALING_ASPECT      = 3,
} MDS_SCALING_TYPE;

/**
 * @brief The display related mode info: \n
 * '1' means the bit is valid \n
 * '0' means the bit is invalid
 */
typedef enum {
    MDS_MODE_NONE      = 0,       /**< No bit is set */
    MDS_DVI_CONNECTED  = 1,       /**< DVI is connected */
    MDS_HDMI_CONNECTED = 1 << 1,  /**< HDMI is connected */
    MDS_HDMI_VIDEO_EXT = 1 << 2,  /**< Extended video mode */
    MDS_WIDI_ON        = 1 << 3,  /**< Widi is working */
} MDS_DISPLAY_MODE;

#endif
