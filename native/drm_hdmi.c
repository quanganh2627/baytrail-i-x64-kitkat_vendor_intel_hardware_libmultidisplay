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
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include "linux/psb_drm.h"
#include "drm_hdmi.h"
#include "xf86drm.h"
#include "xf86drmMode.h"


#define HDMI_FORCE_VIDEO_ON_OFF 1
#define EDID_BLOCK_SIZE         128


typedef struct _HDMI_config_info_ {
    int edidChange;
} HDMI_config_info;

typedef struct _drmJNI {
    int drmFD;
    int ioctlOffset;
    bool hasHdmi;
    drmModeConnectorPtr hdmi_connector;
    pthread_mutex_t mtx;
    HDMI_config_info configInfo;//hdmisetting
    int modeIndex;
    int cloneModeIndex;
} drmJNI;

/** Base path of the hal modules */
#define DRM_DEVICE_NAME     "/dev/card0"
#define DRM_EDID_FILE       "/data/system/hdmi_edid.dat"

static drmJNI g_drm;
static drmModeConnectorPtr getHdmiConnector();

#define CHECK_DRM_FD(ERROR_CODE) \
    do { \
        if (g_drm.drmFD < 0) { \
            LOGE("%s: Invalid drm device descriptor", __func__); \
            return (ERROR_CODE); \
        } \
    } while(0)

#define CHECK_HW_SUPPORT_HDMI(ERROR_CODE) \
    do { \
        if (g_drm.hasHdmi == false) { \
            LOGE("%s: HW platform doesn't support HDMI", __func__); \
            return (ERROR_CODE); \
        } \
    } while(0)

#define CHECK_COMMAND_WR_ERR() \
    do { \
        if (cmdWRret != 0) { \
            LOGE("%s: Failed to call drm command write/read", __func__); \
            ret = false; \
        } \
    } while(0)

#define CHECK_CONNECTOR_STATUS_ERR_RETURN(ERROR_CODE) \
    do { \
        if (connector->connection != DRM_MODE_CONNECTED) { \
            LOGE("%s: Failed to get a connected connector", __func__); \
            return ERROR_CODE; \
        } \
    } while(0)

#define CHECK_CONNECTOR_STATUS_ERR_GOTOEND() \
    do { \
        if (connector->connection != DRM_MODE_CONNECTED) { \
            LOGE("%s: Failed to get a connected connector", __func__); \
            goto End; \
        } \
    } while(0)

/* ---------------- LOCAL FUNCTIONS ---------------------------------- */

/* setup_drm() is a local method that sets up the DRM resources needed */
/* to set or get video modes.  As a side effect encoder and connector  */
/* are set to non-NULL if setup_drm() returns true. */
static bool setup_drm()
{
    bool ret = true;
    union drm_psb_extension_arg video_getparam_arg;
    const char video_getparam_ext[] = "lnc_video_getparam";
    /* Check for DRM-required resources. */
    if ((g_drm.drmFD = open(DRM_DEVICE_NAME, O_RDWR, 0)) < 0)
        return false;
    strncpy(video_getparam_arg.extension,
            video_getparam_ext, sizeof(video_getparam_arg.extension));
    int cmdWRret = drmCommandWriteRead(g_drm.drmFD, DRM_PSB_EXTENSION,
            &video_getparam_arg, sizeof(video_getparam_arg));
    CHECK_COMMAND_WR_ERR();
    if (cmdWRret == 0)
        g_drm.ioctlOffset = video_getparam_arg.rep.driver_ioctl_offset;
    return ret;
}

static drmModeRes* get_resource(int fd)
{
    drmModeRes *res = drmModeGetResources(fd);
    if (res == NULL)
        LOGE("%s: Failed to get DRM resource", __func__);
    return res;
}

static drmModeEncoder *get_encoder(int pfd, uint32_t encoder_type)
{
    int i = 0;
    drmModeRes *resources = NULL;
    drmModeEncoder *encoder = NULL;

    if ((resources = get_resource(pfd)) == NULL)
        return NULL;

    for (i = 0; i < resources->count_encoders; i++) {
        encoder = drmModeGetEncoder(pfd, resources->encoders[i]);

        if (!encoder) {
            LOGE("%s: Failed to get encoder %i, error is %s",
                    __func__, resources->encoders[i], strerror(errno));
            continue;
        }

        /* Get the specific encoder (DRM_MODE_ENCODER_TMDS or DRM_MODE_ENCODER_MIPI). */
        if (encoder->encoder_type == encoder_type)
            break;

        drmModeFreeEncoder(encoder);
        encoder = NULL;
    }
    drmModeFreeResources(resources);

    return encoder;
}

static drmModeConnector *get_connector(int pfd, uint32_t connector_type)
{
    int i = 0;
    drmModeRes *resources = NULL;
    drmModeConnector *connector = NULL;

    if ((resources = get_resource(pfd)) == NULL)
        return NULL;
    for (i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(pfd, resources->connectors[i]);
        if (connector == NULL)
            continue;
        if (connector->connector_type == connector_type)
            break;

        drmModeFreeConnector(connector);
        connector = NULL;
    }
    drmModeFreeResources(resources);
    if (connector == NULL)
        LOGE("%s: Failed to get a connected conector", __func__);
    return connector;
}

static uint32_t get_crtc_id(int pfd, uint32_t encoder_type)
{
    drmModeEncoder *encoder = NULL;
    drmModeRes *resources = NULL;
    drmModeCrtc *crtc = NULL;
    uint32_t crtc_id = 0;
    int i = 0;

    if ((encoder = get_encoder(pfd, encoder_type)) == NULL)
        return 0;
    crtc_id = encoder->crtc_id;
    drmModeFreeEncoder(encoder);

    if (crtc_id == 0) {
        /* Query an available crtc to use */
        if ((resources = get_resource(pfd)) == NULL)
            return 0;

        for (i = 0; i < resources->count_crtcs; i++) {
            crtc = drmModeGetCrtc(pfd, resources->crtcs[i]);
            if (!crtc) {
                LOGE("%s: Failed to get crtc %d, error is %s",
                        __func__, resources->crtcs[i], strerror(errno));
                continue;
            }
            if (crtc->buffer_id == 0) {
                crtc_id = crtc->crtc_id;
                drmModeFreeCrtc(crtc);
                break;
            }
            drmModeFreeCrtc(crtc);
        }
    }

    return crtc_id;
}

static drmModeCrtc *get_crtc(int pfd, uint32_t encoder_type)
{
    uint32_t crtc_id = 0;

    crtc_id = get_crtc_id(pfd, encoder_type);
    if (!crtc_id)
        return NULL;
    return drmModeGetCrtc(pfd, crtc_id);
}

static unsigned int get_fb_id(int pfd, uint32_t encoder_type)
{
    uint32_t crtc_id = 0;
    unsigned int fb_id = 0;
    drmModeCrtc *crtc = NULL;

    if ((crtc_id = get_crtc_id(pfd, encoder_type)) == 0)
        return 0;
    if ((crtc = drmModeGetCrtc(pfd, crtc_id)) == NULL)
        return 0;
    fb_id = crtc->buffer_id;
    drmModeFreeCrtc(crtc);

    return fb_id;
}

static bool checkHwSupportHdmi() {
    //TODO: also can check DRM_MODE_ENCODER_TMDS
    if (get_connector(g_drm.drmFD, DRM_MODE_CONNECTOR_DVID) == NULL) {
        LOGI("%s: HW platform doesn't support HDMI", __func__);
        return false;
    }
    return true;
}

static drmModeConnectorPtr getHdmiConnector()
{
    if (g_drm.hdmi_connector == NULL)
        g_drm.hdmi_connector = get_connector(g_drm.drmFD, DRM_MODE_CONNECTOR_DVID);
    if (g_drm.hdmi_connector == NULL)
        return NULL;
    if (g_drm.hdmi_connector->modes == NULL) {
        LOGE("%s: Failed to get HDMI connector's modes", __func__);
        return NULL;
    }
    return g_drm.hdmi_connector;
}

static int checkHdmiTiming(int index) {
    drmModeConnectorPtr connector = NULL;
    if ((connector = getHdmiConnector()) == NULL)
        return -1;
    CHECK_CONNECTOR_STATUS_ERR_RETURN(-1);
    if (index > connector->count_modes)
        return -1;
    return index;
}

static int initHdmiTiming(drmModeConnectorPtr connector)
{
    int i = 0;
    int index = -1;
    int preferred = -1;
    int max = 0;
    for (i = 0; i < connector->count_modes; i++) {
        //Get the preferred timing
        if (connector->modes[i].type & DRM_MODE_TYPE_PREFERRED)
            preferred = i;
        //Get the max timing
        if ((connector->modes[i].hdisplay > connector->modes[max].hdisplay) &&
            (connector->modes[i].vdisplay > connector->modes[max].vdisplay)) {
            max = i;
        } else if (
            (connector->modes[i].hdisplay == connector->modes[max].hdisplay) &&
            (connector->modes[i].vdisplay == connector->modes[max].vdisplay) &&
            (connector->modes[i].vrefresh > connector->modes[max].vrefresh)) {
            max = i;
        }
        ALOGV("%dx%d@%dHz, %d, %d, %d",
                connector->modes[i].hdisplay,
                connector->modes[i].vdisplay,
                connector->modes[i].vrefresh, i, preferred, max);
    }
    if (preferred != -1) {
        //Select the max if the preferred < the max
        if (connector->modes[preferred].hdisplay < connector->modes[max].hdisplay ||
            connector->modes[preferred].vdisplay < connector->modes[max].vdisplay ||
            connector->modes[preferred].vrefresh < connector->modes[max].vrefresh)
            index = max;
        else
            index = preferred;
    } else
        index = max;
    LOGD("Get the timing index, %d, %d, %d", preferred, max, index);
    return index;
}

static int getMatchingHdmiTiming(drmModeConnectorPtr connector, MDSHDMITiming* in) {
    int i = 0;
    int index = -1;
    uint32_t temp_flags = 0;
    if (in->interlace)
        temp_flags |= DRM_MODE_FLAG_INTERLACE;
    if (in->ratio == 1)
        temp_flags |= DRM_MODE_FLAG_PAR16_9;
    else if (in->ratio == 2)
        temp_flags |= DRM_MODE_FLAG_PAR4_3;

    LOGD("%s: temp_flags = 0x%x, in->ratio = %d\n", __func__, temp_flags, in->ratio);

    for (i = 0; i < connector->count_modes; i++) {
        /* Extract relevant flags to compare */
        uint32_t compare_flags = connector->modes[i].flags &
          (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_PAR16_9 | DRM_MODE_FLAG_PAR4_3);

        /* Don't compare aspect ratio bits if input has no ratio information
         * This is especially true for video case, where we may not be given
         * this information.
         */
        if (temp_flags == 0)
            compare_flags &= ~(DRM_MODE_FLAG_PAR16_9 | DRM_MODE_FLAG_PAR4_3);

        LOGD("Mode avail: %dx%d@%d flags = 0x%x, compare_flags = 0x%x, 0x%x",
             connector->modes[i].hdisplay, connector->modes[i].vdisplay,
             connector->modes[i].vrefresh, connector->modes[i].flags,
             compare_flags, connector->modes[i].type);

        if (in->width == connector->modes[i].hdisplay &&
            in->height == connector->modes[i].vdisplay &&
            in->refresh == connector->modes[i].vrefresh &&
            temp_flags == compare_flags) {
            index = i;
            break;
        }
    }
    if (index >= 0 && index < connector->count_modes) {
        LOGD("%s, Find a matching timing, %d, %dx%d@%dHzx0x%x",
                __func__, index,
                connector->modes[index].hdisplay,
                connector->modes[index].vdisplay,
                connector->modes[index].vrefresh,
                connector->modes[index].flags);
    }
    return index;
}

static int getHdmiModeInfo(int *pWidth, int *pHeight, int *pRefresh, int *pInterlace, int *pRatio)
{
    int i = 0, j = 0, iCount = -1;
    int valid_mode_count = 0;
    bool ret = false;
    drmModeConnectorPtr connector = NULL;

    if ((connector = getHdmiConnector()) == NULL)
        goto End;
    CHECK_CONNECTOR_STATUS_ERR_GOTOEND();

    iCount = connector->count_modes;
    if (iCount < 0 || iCount > HDMI_TIMING_MAX)
        return -1;
    /* get resolution of each mode */
    for (i = 0; i < iCount; i++) {
        unsigned int temp_hdisplay = connector->modes[i].hdisplay;
        unsigned int temp_vdisplay = connector->modes[i].vdisplay;
        unsigned int temp_refresh = connector->modes[i].vrefresh;
        /* Only extract the required flags for comparison */
        unsigned int temp_flags = connector->modes[i].flags &
          (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_PAR16_9 | DRM_MODE_FLAG_PAR4_3);
        unsigned int compare_flags = 0;
        /* re-traverse the connector mode list to see if there is
         * same resolution and refresh. The same mode will not be
         * counted into valid mode.
         */
        j = i;

        while ((--j) >= 0) {

            compare_flags = connector->modes[j].flags &
              (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_PAR16_9 |
               DRM_MODE_FLAG_PAR4_3);

            if(temp_hdisplay == connector->modes[j].hdisplay
                && temp_vdisplay == connector->modes[j].vdisplay
                && temp_refresh == connector->modes[j].vrefresh
               && temp_flags == compare_flags) {
                  LOGD("Found duplicate mode: %dx%d@%d with flags = 0x%x\n",
                       temp_hdisplay, temp_vdisplay, temp_refresh, temp_flags);

                  break;
            }
        }

        /* if j<0, no found of same mode, valid_mode_count++ */
        if (j < 0) {
            /* record the valid mode info into mode array */
            if (pWidth && pHeight && pRefresh && pInterlace) {
                pWidth[valid_mode_count] = temp_hdisplay;
                pHeight[valid_mode_count] = temp_vdisplay;
                pRefresh[valid_mode_count] = temp_refresh;
                if (temp_flags & DRM_MODE_FLAG_INTERLACE)
                    pInterlace[valid_mode_count] = 1;
                else
                    pInterlace[valid_mode_count] = 0;
                if (temp_flags & DRM_MODE_FLAG_PAR16_9)
                    pRatio[valid_mode_count] = 1;
                else if (temp_flags & DRM_MODE_FLAG_PAR4_3)
                    pRatio[valid_mode_count] = 2;
                else
                    pRatio[valid_mode_count] = 0;
            }

            LOGV("Adding mode[%d]: %dx%d@%d with flags = 0x%x\n", valid_mode_count,
                 temp_hdisplay, temp_vdisplay, temp_refresh, temp_flags);
            valid_mode_count++;
        }
    }
    iCount = valid_mode_count;
End:
    return iCount;
}

static int get_hdmi_preferedTimingIndex(drmModeConnector *hdmi_connector)
{
    int i = 0;
    /*mode sort as big --> small, default use index =0, the max one*/
    for (i = 0; i < hdmi_connector->count_modes; i++) {
        /*set to prefer mode */
        if (hdmi_connector->modes[i].type &
                        DRM_MODE_TYPE_PREFERRED) {
            LOGD("%s: Get preferred timing index %d, type is 0x%x, ",
                    __func__, i, hdmi_connector->modes[i].type);
            break;
        }
    }
    if (i == hdmi_connector->count_modes)
        return 0;

    return i;
}

static void get_hdmi_get_TimingByIndex(drmModeConnectorPtr connector, int index, MDSHDMITiming* info)
{
    info->width = connector->modes[index].hdisplay;
    info->height = connector->modes[index].vdisplay;
    info->refresh = connector->modes[index].vrefresh;
}




/* ---------------- GLOBAL FUNCTIONS --------------------------------- */
bool drm_init()
{
    bool ret = false;
    memset(&g_drm,0,sizeof(drmJNI));
    pthread_mutex_init(&g_drm.mtx,NULL);
    pthread_mutex_lock(&g_drm.mtx);
    g_drm.drmFD = -1;
    g_drm.hdmi_connector = NULL;
    g_drm.cloneModeIndex = -1;
    g_drm.modeIndex = -1;
    g_drm.configInfo.edidChange = 0;
    g_drm.hasHdmi = false;

    ret = setup_drm();
    if (!ret)
        goto End;
    g_drm.hasHdmi = checkHwSupportHdmi();
End:
    pthread_mutex_unlock(&g_drm.mtx);
    return ret;
}

void drm_cleanup()
{
    pthread_mutex_lock(&g_drm.mtx);
    if (g_drm.drmFD >= 0)
        drmClose(g_drm.drmFD);
    if (g_drm.hdmi_connector)
        drmModeFreeConnector(g_drm.hdmi_connector);
    g_drm.hasHdmi = false;
    g_drm.hdmi_connector = NULL;
    pthread_mutex_unlock(&g_drm.mtx);
    pthread_mutex_destroy(&g_drm.mtx);
    memset(&g_drm,0,sizeof(drmJNI));
    g_drm.drmFD = -1;
}

bool drm_check_hw_supportHdmi() {
    return g_drm.hasHdmi;
}

/*
 * returns TRUE if HDMI was found and turns on HDMI.
 */
bool drm_hdmi_setHdmiVideoOn()
{
    int i = 0;
    bool ret = true;
    struct drm_psb_disp_ctrl dp_ctrl;

    CHECK_HW_SUPPORT_HDMI(false);
    CHECK_DRM_FD(false);

    pthread_mutex_lock(&g_drm.mtx);
    /* set HDMI display off */
    memset(&dp_ctrl, 0, sizeof(dp_ctrl));
    dp_ctrl.cmd = DRM_PSB_DISP_PLANEB_ENABLE;
    dp_ctrl.u.data = HDMI_FORCE_VIDEO_ON_OFF;
    int cmdWRret = drmCommandWriteRead(g_drm.drmFD,
                DRM_PSB_HDMI_FB_CMD, &dp_ctrl, sizeof(dp_ctrl));
    CHECK_COMMAND_WR_ERR();
    pthread_mutex_unlock(&g_drm.mtx);
    return ret;
}

/* drm_hdmi_setHdmiPowerOff() returns TRUE if HDMI power was successfully turned off */
bool drm_hdmi_setHdmiPowerOff()
{
    bool ret = true;
    struct drm_psb_disp_ctrl dp_ctrl;
    CHECK_HW_SUPPORT_HDMI(false);
    CHECK_DRM_FD(false);

    pthread_mutex_lock(&g_drm.mtx);
    /*when hdmi disconnected , set the power state lsland down */
    memset(&dp_ctrl, 0, sizeof(dp_ctrl));
    dp_ctrl.cmd = DRM_PSB_HDMI_OSPM_ISLAND_DOWN;
    int cmdWRret = drmCommandWriteRead(g_drm.drmFD,
            DRM_PSB_HDMI_FB_CMD, &dp_ctrl, sizeof(dp_ctrl));
    CHECK_COMMAND_WR_ERR();
    pthread_mutex_unlock(&g_drm.mtx);
    return ret;
}

/* drm_hdmi_setHdmiVideoOff() returns TRUE if HDMI was found and turns off HDMI. */
bool drm_hdmi_setHdmiVideoOff()
{
    bool ret = true;
    struct drm_lnc_video_getparam_arg arg;
    struct drm_psb_disp_ctrl dp_ctrl;

    CHECK_HW_SUPPORT_HDMI(false);
    CHECK_DRM_FD(false);

    pthread_mutex_lock(&g_drm.mtx);
    /* set HDMI display on */
    memset(&dp_ctrl, 0, sizeof(dp_ctrl));
    dp_ctrl.cmd = DRM_PSB_DISP_PLANEB_DISABLE;
    dp_ctrl.u.data = HDMI_FORCE_VIDEO_ON_OFF;
    int cmdWRret = drmCommandWriteRead(g_drm.drmFD,
            DRM_PSB_HDMI_FB_CMD, &dp_ctrl, sizeof(dp_ctrl));
    CHECK_COMMAND_WR_ERR();
    pthread_mutex_unlock(&g_drm.mtx);
    return ret;
}

int drm_hdmi_connectStatus()
{
    drmModeConnector *connector = NULL;
    int ret = 0;
    char last_product_inf[8] ={0,0,0,0,0,0,0,0};

    CHECK_HW_SUPPORT_HDMI(false);
    CHECK_DRM_FD(0);

    pthread_mutex_lock(&g_drm.mtx);
    /* release hdmi_connector before getting connectStatus */
    if (g_drm.hdmi_connector)
        drmModeFreeConnector(g_drm.hdmi_connector);
    g_drm.hdmi_connector = NULL;

    if ((connector = getHdmiConnector()) == NULL)
        goto End;
    CHECK_CONNECTOR_STATUS_ERR_GOTOEND();
    // Read EDID, and check whether it's HDMI or DVI interface
    int i, j, k;
    drmModePropertyPtr props = NULL;
    uint64_t* edid = NULL;
    drmModePropertyBlobPtr edidBlob = NULL;
    char* edid_binary = NULL;
    FILE* fp;

    fp = fopen(DRM_EDID_FILE, "rb");
    if (fp != NULL) {
        fread(last_product_inf, 1, sizeof(last_product_inf), fp);
        fclose(fp);
    } else {
        LOGW("Failed to open file %s to read data", DRM_EDID_FILE);
    }

    for (i = 0; i < connector->count_props; i++) {
        props = drmModeGetProperty(g_drm.drmFD, connector->props[i]);
        if (!props)
            continue;

        if (props->name && !strcmp(props->name, "EDID")) {
            ret = 2; //DVI
            edid = &connector->prop_values[i];
            edidBlob = drmModeGetPropertyBlob(g_drm.drmFD, *edid);
            if (edidBlob == NULL ||
                edidBlob->data == NULL ||
                edidBlob->length < EDID_BLOCK_SIZE) {
                LOGE("%s: Invalid EDID Blob.", __func__);
                drmModeFreeProperty(props);
                continue;
            }

            edid_binary = (char *)edidBlob->data;
            if (edid_binary[126] == 0) {
                ret = 2;
                break;
            }

            for (k = 8; k <= 15; k++) {
                LOGV("EDID Product Info %d: 0x%x", k, edid_binary[k]);
                if (last_product_inf[k-8] != edid_binary[k]) {
                    last_product_inf[k-8] = edid_binary[k];
                    g_drm.configInfo.edidChange = 1;
                }
            }

            /*search VSDB in extend edid */
            for (j = 0; j <= EDID_BLOCK_SIZE - 3; j++) {
                int n;
                n = EDID_BLOCK_SIZE + j;
                if (edid_binary[n] == 0x03 && edid_binary[n+1] == 0x0c
                    && edid_binary[n+2] == 0x00) {
                    ret = 1; //HDMI
                    break;
                }
            }
            drmModeFreeProperty(props);
            break;
        } else {
            drmModeFreeProperty(props);
        }
    }
    LOGV("EDID Product Info edidChange: %d", g_drm.configInfo.edidChange);
    if (g_drm.configInfo.edidChange == 1) {
        fp = fopen(DRM_EDID_FILE, "wb");
        if (fp != NULL) {
            fwrite(last_product_inf, 1, sizeof(last_product_inf), fp);
            fclose(fp);
        } else {
            LOGW("Failed to open file %s to write data.", DRM_EDID_FILE);
        }
    }
End:
    if (g_drm.configInfo.edidChange == 1) {
        g_drm.modeIndex = -1;
        g_drm.cloneModeIndex = -1;
    }

    pthread_mutex_unlock(&g_drm.mtx);

    LOGI("%s: Leaving, connect status is %d", __func__, ret);
    return ret;
}


bool drm_hdmi_onHdmiDisconnected(void)
{
    CHECK_HW_SUPPORT_HDMI(false);
    CHECK_DRM_FD(false);

    pthread_mutex_lock(&g_drm.mtx);
    /* release hdmi_connector when disconnected */
    if (g_drm.hdmi_connector)
        drmModeFreeConnector(g_drm.hdmi_connector);
    g_drm.hdmi_connector = NULL;
    pthread_mutex_unlock(&g_drm.mtx);
    return true;
}

bool drm_mipi_setMode(int mode)
{
    bool ret = false;
    int i = 0;
    drmModeEncoder *mipi_encoder = NULL;
    drmModeConnector *connector = NULL;
    drmModePropertyPtr props = NULL;
    CHECK_DRM_FD(false);
    pthread_mutex_lock(&g_drm.mtx);
    //TODO: MIPI connector couldn't be modified,
    //there is no need to get MIPI connector everytime.
    if ((connector = get_connector(g_drm.drmFD, DRM_MODE_CONNECTOR_MIPI)) == NULL)
        goto End;
    if (connector->connection != DRM_MODE_CONNECTED)
        goto End;
    /* Set MIPI On/Off */
    for (i = 0; i < connector->count_props; i++) {
        props = drmModeGetProperty(g_drm.drmFD, connector->props[i]);
        if (!props) continue;

        if (!strcmp(props->name, "DPMS")) {
            LOGV("%s: %s %u", __func__,
                  (mode == DRM_MIPI_ON) ? "On" : "Off",
                  connector->connector_id);
            drmModeConnectorSetProperty(g_drm.drmFD,
                                        connector->connector_id,
                                        props->prop_id,
                                        (mode == DRM_MIPI_ON)
                                        ? DRM_MODE_DPMS_ON : DRM_MODE_DPMS_OFF);
            drmModeFreeProperty(props);
            break;
        }

        drmModeFreeProperty(props);
    }
    ret = true;
End:
    if (connector)
        drmModeFreeConnector(connector);
    pthread_mutex_unlock(&g_drm.mtx);
    LOGI("%s %s", __func__, (ret == true ? "success" : "fail"));
    return ret;
}

int drm_hdmi_getModeInfo(int *pWidth, int *pHeight, int *pRefresh, int *pInterlace, int *pRatio)
{
    int ret = -1;
    CHECK_HW_SUPPORT_HDMI(MDS_ERROR);
    CHECK_DRM_FD(MDS_ERROR);
    pthread_mutex_lock(&g_drm.mtx);
    ret = getHdmiModeInfo(pWidth, pHeight, pRefresh, pInterlace, pRatio);
    pthread_mutex_unlock(&g_drm.mtx);
    return ret;
}

int drm_hdmi_getDeviceChange()
{
    int ret = 1;
    CHECK_HW_SUPPORT_HDMI(0);
    CHECK_DRM_FD(0);
    pthread_mutex_lock(&g_drm.mtx);
    if (g_drm.configInfo.edidChange == 0)
        ret = 0;
    g_drm.configInfo.edidChange = 0;
    pthread_mutex_unlock(&g_drm.mtx);
    return ret;
}

int drm_hdmi_notify_audio_hotplug(bool plugin)
{
    bool ret = true;
    struct drm_psb_disp_ctrl dp_ctrl;

    CHECK_HW_SUPPORT_HDMI(MDS_ERROR);
    CHECK_DRM_FD(MDS_ERROR);
    pthread_mutex_lock(&g_drm.mtx);

    /* set HDMI display off */
    memset(&dp_ctrl, 0, sizeof(dp_ctrl));
    dp_ctrl.cmd = DRM_PSB_HDMI_NOTIFY_HOTPLUG_TO_AUDIO;
    dp_ctrl.u.data = (plugin == true ? 1 : 0);
    int cmdWRret = drmCommandWriteRead(g_drm.drmFD,
            DRM_PSB_HDMI_FB_CMD, &dp_ctrl, sizeof(dp_ctrl));
    CHECK_COMMAND_WR_ERR();
    pthread_mutex_unlock(&g_drm.mtx);
    return (ret == true ? MDS_NO_ERROR : MDS_ERROR);
}

int drm_hdmi_saveMode(int mode, int index)
{
    CHECK_HW_SUPPORT_HDMI(MDS_ERROR);
    pthread_mutex_lock(&g_drm.mtx);
    if (index >= 0) {
        if (mode == DRM_HDMI_CLONE)
            g_drm.cloneModeIndex = g_drm.modeIndex = index;
        else if (mode == DRM_HDMI_VIDEO_EXT)
            g_drm.modeIndex = index;
    }
    LOGD("%s: %d, %d", __func__, g_drm.modeIndex, g_drm.cloneModeIndex);
    pthread_mutex_unlock(&g_drm.mtx);
    return MDS_NO_ERROR;
}

int drm_get_dev_fd()
{
    return g_drm.drmFD;
}

int drm_get_ioctl_offset()
{
    return g_drm.ioctlOffset;
}

int drm_hdmi_checkTiming(int mode, MDSHDMITiming* info)
{
    int index = 0;
    if (info == NULL)
        return MDS_ERROR;
    LOGI("%s: HDMI timing is %dx%d@%dHzx%d, %d, %d", __func__,
            info->width, info->height, info->refresh,
            info->interlace, g_drm.cloneModeIndex, g_drm.modeIndex);

    CHECK_HW_SUPPORT_HDMI(MDS_ERROR);
    drmModeConnectorPtr connector = NULL;
    if ((connector = getHdmiConnector()) == NULL)
        return -1;
    CHECK_CONNECTOR_STATUS_ERR_RETURN(-1);

    pthread_mutex_lock(&g_drm.mtx);
    if (mode == DRM_HDMI_VIDEO_EXT) {
        if (g_drm.cloneModeIndex == -1)
            g_drm.cloneModeIndex = initHdmiTiming(connector);
        g_drm.modeIndex = g_drm.cloneModeIndex;
    } else if (mode == DRM_HDMI_CLONE) {
        if (info->width == 0) {
            if (g_drm.cloneModeIndex == -1)
                g_drm.cloneModeIndex = initHdmiTiming(connector);
            g_drm.modeIndex = g_drm.cloneModeIndex;
        } else
            g_drm.modeIndex = getMatchingHdmiTiming(connector, info);
    }
    if (g_drm.modeIndex == -1) {
        if (g_drm.cloneModeIndex != -1)
            g_drm.modeIndex = g_drm.cloneModeIndex;
        else {
            g_drm.modeIndex = get_hdmi_preferedTimingIndex(connector);
            g_drm.cloneModeIndex = g_drm.modeIndex;
        }
    }
    get_hdmi_get_TimingByIndex(connector, g_drm.modeIndex, info);
    LOGD("%s, Switching to Timing, %dx%d@%dHzx%dx%d, %d",
            __func__, info->width, info->height,
            info->refresh, info->ratio, info->interlace, g_drm.modeIndex);
    pthread_mutex_unlock(&g_drm.mtx);
    return g_drm.modeIndex;
}

int drm_initHdmiMode()
{
    CHECK_HW_SUPPORT_HDMI(MDS_ERROR);
    int index = -1;
    drmModeConnectorPtr connector = NULL;
    if ((connector = getHdmiConnector()) == NULL)
        return -1;
    CHECK_CONNECTOR_STATUS_ERR_RETURN(-1);
    pthread_mutex_lock(&g_drm.mtx);
    index = initHdmiTiming(connector);
    pthread_mutex_unlock(&g_drm.mtx);
    return index;
}

