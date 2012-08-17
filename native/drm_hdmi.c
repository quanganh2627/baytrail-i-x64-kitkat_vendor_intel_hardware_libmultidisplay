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


#define LOG_NDEBUG 0

#include <utils/Log.h>
#include <errno.h>
#include <sys/types.h>
#include <string.h>
#include <fcntl.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <cutils/properties.h>
#include <hardware/gralloc.h>
#include "pvr2d.h"
#include "linux/psb_drm.h"
#include "pvr_android.h"
#include "drm_hdmi.h"
#include "xf86drm.h"
#include "xf86drmMode.h"


#define DRM_MODE_SCALE_NONE             0 /* Unmodified timing (display or
software can still scale) */
#define DRM_MODE_SCALE_FULLSCREEN       1 /* Full screen, ignore aspect */
#define DRM_MODE_SCALE_CENTER           2 /* Centered, no scaling */
#define DRM_MODE_SCALE_ASPECT           3 /* Full screen, preserve aspect */

#define HDMI_STEP_HORVALUE      5
#define HDMI_STEP_VERVALUE      5

#define HDMI_FORCE_VIDEO_ON_OFF 1
#define EDID_BLOCK_SIZE         128


typedef enum  _ROTATION_ {
    ROTATION_NONE = 0,
    ROTATION_90,
    ROTATION_180,
    ROTATION_270
} ROTATION_DEGREE;

typedef struct _HDMI_fb_mem_info_ {
    PVR2DCONTEXTHANDLE hPVR2DContext;
    PVR2DMEMINFO *psMemInfo[PSB_HDMI_FLIP_MAX_SIZE];
    unsigned int fb_id[PSB_HDMI_FLIP_MAX_SIZE];
    unsigned int old_fb_id[PSB_HDMI_FLIP_MAX_SIZE];
    unsigned int buf_cnt;
    unsigned int new_buf_cnt;
    unsigned int changed;
    bool b_old_fb_cleared;
} HDMI_fb_mem_info;

typedef struct _HDMI_config_info_ {
    int edidChange;
    int scaleType;
    int horiRatio;
    int vertRatio;
} HDMI_config_info;

typedef struct _drmJNI {
    int drmFD;
    int curMode;
    int ioctlOffset;
    IMG_graphic_hdmi_ex* ctx;
    HDMI_fb_mem_info cinfo;//clone
    HDMI_fb_mem_info einfo;//ext
    drmModeConnectorPtr hdmi_connector;
    pthread_mutex_t mtx;
    HDMI_config_info configInfo;//hdmisetting
    int modeIndex;
    int cloneModeIndex;
} drmJNI;

static inline uint32_t align_to(uint32_t arg, uint32_t align)
{
    return ((arg + (align - 1)) & (~(align - 1)));
}

/** Base path of the hal modules */
#define HAL_LIBRARY_PATH1   "/system/lib/hw"
#define HAL_LIBRARY_PATH2   "/vendor/lib/hw"
#define DRM_DEVICE_NAME     "/dev/card0"
#define DRM_EDID_FILE       "/data/system/hdmi_edid.dat"

static drmJNI g_drm;

static const char *variant_keys[] = {
    "ro.hardware",  /* This goes first so that it can pick up a different
                       file on the emulator. */
    "ro.product.board",
    "ro.board.platform",
    "ro.arch"
};

static const int HAL_VARIANT_KEYS_COUNT =
    (sizeof(variant_keys)/sizeof(variant_keys[0]));

static int hdmi_rm_notifier_handler(int cmd, void *data);
static int select_hdmi_mode(drmModeConnector *hdmi_connector);
static int setScaling(int scaling_val);
static bool setScaleStep(int value);
static drmModeConnectorPtr getHdmiConnector();
static int hdmi_get_timinginfo(int cmd, MDSHDMITiming* info);

#define CHECK_DRM_FD(ERROR_CODE) \
    do { \
        if (g_drm.drmFD < 0) { \
            LOGE("%s: Invalid drm device descriptor", __func__); \
            return (ERROR_CODE); \
        } \
    } while(0)

/* ---------------- LOCAL FUNCTIONS ---------------------------------- */

/* setup_drm() is a local method that sets up the DRM resources needed */
/* to set or get video modes.  As a side effect encoder and connector  */
/* are set to non-NULL if setup_drm() returns true. */
static bool setup_drm()
{
    int ret = 0;
    union drm_psb_extension_arg video_getparam_arg;
    const char video_getparam_ext[] = "lnc_video_getparam";
    if (g_drm.drmFD > 0)
        close(g_drm.drmFD);
    /* Check for DRM-required resources. */
    if ((g_drm.drmFD = open(DRM_DEVICE_NAME, O_RDWR, 0)) < 0)
        return false;
    strncpy(video_getparam_arg.extension,
            video_getparam_ext, sizeof(video_getparam_arg.extension));
    ret = drmCommandWriteRead(g_drm.drmFD, DRM_PSB_EXTENSION,
            &video_getparam_arg, sizeof(video_getparam_arg));
    if (ret != 0 || !video_getparam_arg.rep.exists) {
        LOGE("%s: Failed to get param %s", __func__, video_getparam_ext);
        g_drm.ioctlOffset = 0;
        return false;
    } else {
        g_drm.ioctlOffset = video_getparam_arg.rep.driver_ioctl_offset;
        return true;
    }
}

drmModeEncoder *get_encoder(int pfd, uint32_t encoder_type)
{
    int i = 0;
    drmModeRes *resources = NULL;
    drmModeEncoder *encoder = NULL;

    resources = drmModeGetResources(pfd);
    if (!resources) {
        LOGE("%s: Failed to get resource, error is %s", __func__, strerror(errno));
        return NULL;
    }

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

drmModeConnector *get_connector(int pfd, uint32_t connector_type)
{
    int i = 0;
    drmModeRes *resources = NULL;
    drmModeConnector *connector = NULL;

    resources = drmModeGetResources(pfd);
    if (!resources) {
        LOGE("%s: Failed to get resource, error is %s", __func__, strerror(errno));
        return NULL;
    }

    for (i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(pfd, resources->connectors[i]);

        if (!connector) {
            LOGE("%s: Failed to get connector %d, error is %s",
                    __func__, resources->connectors[i], strerror(errno));
        }

        if (connector->connector_type == connector_type)
            break;

        drmModeFreeConnector(connector);
        connector = NULL;
    }
    drmModeFreeResources(resources);

    return connector;
}

static uint32_t get_crtc_id(int pfd, uint32_t encoder_type)
{
    drmModeEncoder *encoder = NULL;
    drmModeRes *resources = NULL;
    drmModeCrtc *crtc = NULL;
    uint32_t crtc_id = 0;
    int i = 0;

    encoder = get_encoder(pfd, encoder_type);
    if (!encoder) {
        return 0;
    }
    crtc_id = encoder->crtc_id;
    drmModeFreeEncoder(encoder);

    if (crtc_id == 0) {
        /* Query an available crtc to use */
        resources = drmModeGetResources(pfd);
        if (!resources) {
            LOGE("%s: Failed to get resource , error is %s", __func__, strerror(errno));
            return 0;
        }

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
    if (!crtc_id) {
        return NULL;
    }

    return drmModeGetCrtc(pfd, crtc_id);
}

static unsigned int get_fb_id(int pfd, uint32_t encoder_type)
{
    uint32_t crtc_id = 0;
    unsigned int fb_id = 0;
    drmModeCrtc *crtc = NULL;

    crtc_id = get_crtc_id(pfd, encoder_type);
    if (!crtc_id) {
        return 0;
    }

    crtc = drmModeGetCrtc(pfd, crtc_id);
    if (!crtc) {
        return 0;
    }

    fb_id = crtc->buffer_id;
    drmModeFreeCrtc(crtc);

    return fb_id;
}

/**
 * Load the file defined by the variant and if successful
 * return the dlopen handle and the hmi.
 * @return 0 = success, !0 = failure.
 */
static int load(const char *id,
                const char *path,
                const IMG_graphic_hdmi_ex **pex)
{
    int status;
    void *handle;
    IMG_graphic_hdmi_ex *ex;

    /*
     * load the symbols resolving undefined symbols before
     * dlopen returns. Since RTLD_GLOBAL is not or'd in with
     * RTLD_NOW the external symbols will not be global
     */
    handle = dlopen(path, RTLD_NOW);
    if (handle == NULL) {
        char const *err_str = dlerror();
        LOGE("%s: Failed to load %s, error is %s",
                __func__, path, (err_str ? err_str : "unknown"));
        status = -EINVAL;
        goto done;
    }

    /* Get the address of the struct hal_module_info. */
    const char *sym = "HAL_GR_HDMI_SYM";
    ex = (IMG_graphic_hdmi_ex *)dlsym(handle, sym);
    if (ex == NULL) {
        LOGE("%s: Failed to find symbol %s", __func__, sym);
        status = -EINVAL;
        goto done;
    }
    /* success */
    status = 0;

done:
    if (status != 0) {
        ex = NULL;
        if (handle != NULL) {
            dlclose(handle);
            handle = NULL;
        }
    } else {
        LOGV("%s: loaded HAL id=%s path=%s hmi=%p handle=%p",
             __func__, id, path, *pex, handle);
    }

    *pex = ex;

    return status;
}

static int get_gralloc_ex(IMG_graphic_hdmi_ex **pex)
{
    int status;
    int i;
    char prop[PATH_MAX];
    char path[PATH_MAX];
    char *id = GRALLOC_HARDWARE_MODULE_ID;

    /*
     * Here we rely on the fact that calling dlopen multiple times on
     * the same .so will simply increment a refcount (and not load
     * a new copy of the library).
     * We also assume that dlopen() is thread-safe.
     */

    /* Loop through the configuration variants looking for a module */
    for (i=0 ; i<HAL_VARIANT_KEYS_COUNT+1 ; i++) {
        if (i < HAL_VARIANT_KEYS_COUNT) {
            if (property_get(variant_keys[i], prop, NULL) == 0) {
                continue;
            }
            snprintf(path, sizeof(path), "%s/%s.%s.so",
                     HAL_LIBRARY_PATH1, id, prop);
            if (access(path, R_OK) == 0) break;

            snprintf(path, sizeof(path), "%s/%s.%s.so",
                     HAL_LIBRARY_PATH2, id, prop);
            if (access(path, R_OK) == 0) break;
        } else {
            snprintf(path, sizeof(path), "%s/%s.default.so",
                     HAL_LIBRARY_PATH1, id);
            if (access(path, R_OK) == 0) break;
        }
    }

    status = -ENOENT;
    if (i < HAL_VARIANT_KEYS_COUNT+1) {
        /* load the module, if this fails, we're doomed, and we should not try
         * to load a different variant. */
        status = load(id, path, (const IMG_graphic_hdmi_ex **)pex);
    }

    return status;
}



static void cleanup_hdmi_fb(int pfd, int mode)
{
    unsigned int hdmi_fb_id = 0, mipi_fb_id = 0;
    int ret = 0;
    unsigned int i = 0;
    HDMI_fb_mem_info *fb_info = NULL;
    if (mode == DRM_HDMI_CLONE)
        fb_info = &g_drm.cinfo;
    else if (mode == DRM_HDMI_VIDEO_EXT)
        fb_info = &g_drm.einfo;
    else {
        LOGE("%s: invalide mode", __func__);
        return;
    }
    hdmi_fb_id = fb_info->fb_id[0];
    mipi_fb_id = get_fb_id(pfd, DRM_MODE_ENCODER_MIPI);
    if (hdmi_fb_id && hdmi_fb_id != mipi_fb_id) {
        for (i = 0; i < fb_info->buf_cnt; i++) {
            if (fb_info->fb_id[i]) {
                drmModeRmFB(pfd, fb_info->fb_id[i]);
                fb_info->fb_id[i] = 0;
            }
            if (fb_info->psMemInfo[i]) {
                PVR2DMemFree(fb_info->hPVR2DContext, fb_info->psMemInfo[i]);
                fb_info->psMemInfo[i] = NULL;
            }
        }
    }
}


static int setup_hdmi_fb(int width, int height, int mode)
{
    int stride = 0, ret = 0;
    uint32_t fb_size = 0;
    unsigned int hdmi_fb_id = 0, re_use = 0, mipi_fb_id = 0, length = 0;
    drmModeFBPtr hdmi_fb = 0;
    PVR2DERROR ePVR2DStatus;
    PVR2D_ULONG uFlags = 0;
    HDMI_fb_mem_info *fb_info = NULL;
    unsigned int i = 0;
    if (mode == DRM_HDMI_CLONE) {
        uFlags = 0;
        fb_info = &g_drm.cinfo;
    } else if (mode == DRM_HDMI_VIDEO_EXT) {
        uFlags |= PVR2D_MEM_NO_GPU_ADDR;
        fb_info = &g_drm.einfo;
    } else {
        LOGE("%s: Invalid mode %d", __func__, mode);
        return -1;
    }
    if (!fb_info->hPVR2DContext) {
        LOGE("%s: Invalid 2D mem context", __func__);
        return -1;
    }
    if (fb_info->new_buf_cnt > PSB_HDMI_FLIP_MAX_SIZE) {
        LOGE("%s: wrong count %d", __func__, fb_info->new_buf_cnt);
        return -1;
    }

    /*default to unchanged*/
    fb_info->changed = 0;
    fb_info->b_old_fb_cleared = 1;
    hdmi_fb_id = fb_info->fb_id[0];
    mipi_fb_id = get_fb_id(g_drm.drmFD, DRM_MODE_ENCODER_MIPI);
    LOGI("%s: %d, %d, %d, %d, %d, %d, %d, %d", __func__, __LINE__,
            hdmi_fb_id, mipi_fb_id,
            stride, length, width, height, fb_size);
    if (hdmi_fb_id) {
        hdmi_fb = drmModeGetFB(g_drm.drmFD, hdmi_fb_id);
        if (!hdmi_fb) {
            LOGE("%s: Failed to get hdmi fb", __func__);
            return -1;
        }
        LOGI("%s: %d, %d, %d, %d, %d", __func__, __LINE__, width, height, hdmi_fb->width, hdmi_fb->height);
        if (((uint32_t)width == hdmi_fb->width) &&
                ((uint32_t)height == hdmi_fb->height)) {
            re_use = 1;
        } else if (hdmi_fb_id && (hdmi_fb_id != mipi_fb_id)) {
            for (i = 0; i < fb_info->buf_cnt; i++) {
                if (fb_info->fb_id[i])
                    fb_info->old_fb_id[i] = fb_info->fb_id[i];
                if (fb_info->psMemInfo[i]) {
                    PVR2DMemFree(fb_info->hPVR2DContext, fb_info->psMemInfo[i]);
                    fb_info->psMemInfo[i] = NULL;
                }
            }
            fb_info->b_old_fb_cleared = 0;
        }
        drmModeFreeFB(hdmi_fb);
    }

    length = width > height ? width : height;
    if (mode == DRM_HDMI_CLONE)
        stride = length * 4;
    else
        stride = width * 4;
    /*stride must align to 64B*/
    stride = align_to(stride, 64);
    if (mode == DRM_HDMI_CLONE)
        fb_size = stride * length;
    else
        fb_size = stride * height;
    /*buffer size should align to page size*/
    fb_size = align_to(fb_size, getpagesize());
    LOGV("%s: %d, %d, %d, %d, %d, %d", __func__,
            stride, length, width, height, fb_size, re_use);

    if (re_use) {
        for (i = 0; i < fb_info->buf_cnt; i++) {
            if (fb_info->psMemInfo[i] && fb_info->psMemInfo[i]->pBase)
                memset(fb_info->psMemInfo[i]->pBase, 0, fb_info->psMemInfo[i]->ui32MemSize);
        }
    } else {
        fb_info->changed = 1;
        fb_info->buf_cnt = fb_info->new_buf_cnt;

        for (i = 0; i < fb_info->new_buf_cnt; i++) {
            ePVR2DStatus = PVR2DMemAlloc(fb_info->hPVR2DContext,
                                         fb_size,
                                         1,
                                         uFlags,
                                         &fb_info->psMemInfo[i]);
            if (ePVR2DStatus != PVR2D_OK) {
                LOGE("%s: Failed to alloc memory", __func__);
                goto error;
            }

            /* HDMI would display black screen, after setting to extended/clone mode. */
            memset(fb_info->psMemInfo[i]->pBase, 0, fb_size);
            ret = drmModeAddFB(g_drm.drmFD, width, height, 24, 32,
                    stride, (uint32_t)(fb_info->psMemInfo[i]->hPrivateMapData), &hdmi_fb_id);
            if (ret) {
                LOGE("%s: Failed to add fb, error is %s", __func__, strerror(errno));
                goto error;
            }

            fb_info->fb_id[i] = hdmi_fb_id;
            LOGV("%s: %d, %d, %d, %d, %p, %ld", __func__, __LINE__,
                    i, hdmi_fb_id, fb_info->buf_cnt,
                    fb_info->psMemInfo[i]->pBase, fb_info->psMemInfo[i]->ui32MemSize);
        }
    }
    return 0;

error:
    for (i = 0; i < fb_info->buf_cnt; i++) {
        if (fb_info->fb_id[i])
            drmModeRmFB(g_drm.drmFD, fb_info->fb_id[i]);
        if (fb_info->psMemInfo[i]) {
            PVR2DMemFree(fb_info->hPVR2DContext, fb_info->psMemInfo[i]);
            fb_info->psMemInfo[i] = NULL;
        }
    }
    return -1;
}

static void set_hdmi_state(int state)
{
    int ret = 0;
    if (g_drm.ioctlOffset <= 0 || g_drm.drmFD <= 0) {
        LOGE("Failed to set HDMI state. DRM file descriptor or IOCTL offset is invalid.");
        return;
    }

    struct drm_lnc_video_getparam_arg arg;
    /* Record the hdmi state in kernel */
    arg.key = IMG_VIDEO_SET_HDMI_STATE;
    arg.value = (uint64_t)state;
    ret = drmCommandWriteRead(g_drm.drmFD, g_drm.ioctlOffset, &arg, sizeof(arg));
    if (ret != 0) {
        LOGE("Failed to set HDMI mode. Error = %d", ret);
    }
}

static bool set_clone_mode_internal(unsigned int  degree)
{
    unsigned int i = 0;
    bool ret = false;
    drmModeConnector *hdmi_connector = NULL;
    uint32_t hdmi_crtc_id = 0;
    unsigned int hdmi_fb_id = 0;
    int index = 0;
    uint16_t hdisplay = 0;
    int hdmi_mode_width = 0;
    int hdmi_mode_height = 0;
    int hdmi_fb_width = 0;
    int hdmi_fb_height = 0;
    drmModeModeInfoPtr mode = NULL;

    if ((hdmi_connector = getHdmiConnector()) == NULL) {
        LOGE("%s: Failed to get connector", __func__);
        return false;
    }
    if (hdmi_connector->connection != DRM_MODE_CONNECTED) {
        LOGE("%s: Failed to get a connected HDMI connector", __func__);
        return false;
    }
    index = select_hdmi_mode(hdmi_connector);
    if (index == -1) {
        LOGE("%s: Failed to select a hdmi mode", __func__);
        return false;
    } else {
        mode = &(hdmi_connector->modes[index]);
        hdmi_mode_width = mode->hdisplay;
        hdmi_mode_height = mode->vdisplay;
    }
    hdmi_fb_width = hdmi_mode_width > hdmi_mode_height ? hdmi_mode_width : hdmi_mode_height;
    hdmi_fb_height = hdmi_mode_width < hdmi_mode_height ? hdmi_mode_width : hdmi_mode_height;

    hdmi_fb_width = align_to(hdmi_fb_width, 16);
    hdmi_fb_height = align_to(hdmi_fb_height, 16);
    //LOGV("%s: Clone mode timing is %dx%d@%dHz, %dx%d",
    //        __func__, hdmi_fb_width, hdmi_fb_height,
    //        mode->vrefresh, hdmi_mode_width, hdmi_mode_height);

    if (0 != setup_hdmi_fb(hdmi_fb_width, hdmi_fb_height, DRM_HDMI_CLONE)) {
        LOGE("%s: Failed to setup hdmi fb", __func__);
        ret = false;
        goto exit;
    }
    hdmi_fb_id = g_drm.cinfo.fb_id[0];
    hdmi_crtc_id = get_crtc_id(g_drm.drmFD, DRM_MODE_ENCODER_TMDS);

    LOGV("%s: Clone mode timing is %dx%d@%dHz, %dx%d, %d", __func__,
                hdmi_fb_width, hdmi_fb_height, mode->vrefresh,
                hdmi_mode_width, hdmi_mode_height, hdmi_fb_id);
    /* Mode setting with the largest resolution (e.g 1920x1080), to set HDMI with clone mode. */
    int tmp = drmModeSetCrtc(g_drm.drmFD, hdmi_crtc_id, hdmi_fb_id, 0, 0,
                         &hdmi_connector->connector_id, 1, mode);

    /*clear the old fb id*/
    if (!g_drm.cinfo.b_old_fb_cleared) {
        for (i = 0; i < g_drm.cinfo.buf_cnt; i++) {
            if (g_drm.cinfo.old_fb_id[i]) {
                drmModeRmFB(g_drm.drmFD, g_drm.cinfo.old_fb_id[i]);
                g_drm.cinfo.old_fb_id[i] = 0;
            }
        }
        g_drm.cinfo.b_old_fb_cleared = 1;
    }
    if (tmp) {
        LOGE("%s: Failed to set CRTC %d, error is %s", __func__, ret,strerror(errno));
        ret = false;
    } else {
        ret = true;
    }
    if (ret)
        g_drm.cloneModeIndex = index;
exit:
    return ret;
}

static int select_hdmi_mode(drmModeConnector *hdmi_connector)
{
    int index = 0;
    int hdmi_mode_width = 0;
    int hdmi_mode_height = 0;
    int i = 0;
    if (!hdmi_connector) {
        LOGE("%s: Failed to get valid connector", __func__);
        return -1;
    }
    LOGI("%s: mode index is %d", __func__, g_drm.modeIndex);
    if (g_drm.modeIndex != -1) {
        return g_drm.modeIndex;
    }
    /*mode sort as big --> small, default use index =0, the max one*/
    index = 0;
    hdmi_mode_width = hdmi_connector->modes[index].hdisplay;
    hdmi_mode_height = hdmi_connector->modes[index].vdisplay;
    /*set to prefer mode */
    for (i = 0; i < hdmi_connector->count_modes; i++) {
        if (hdmi_connector->modes == NULL) {
            LOGE("%s: Failed to get modes", __func__);
            return -1;
        }
        if (hdmi_connector->modes[i].type &
                        DRM_MODE_TYPE_PREFERRED) {
            hdmi_mode_width = hdmi_connector->modes[i].hdisplay;
            hdmi_mode_height = hdmi_connector->modes[i].vdisplay;
            index = i;
        }
    }
    if (index == hdmi_connector->count_modes) {
        LOGE("%s: Failed to select mode, count is %d",
                __func__, hdmi_connector->count_modes);
        return -1;
    }
    return index;
}

static bool set_clone_mode(MDSHDMITiming* info)
{
    int ret = 0;
    if (g_drm.ctx->notify_gralloc) {
        ret = g_drm.ctx->notify_gralloc(CMD_CLONE_MODE, info);
    }
    if (ret) {
        LOGE("%s: Failed to notify gralloc", __func__);
        return false;
    }
    return true;
}

static bool set_ext_video_mode(MDSHDMITiming* info)
{
    int ret = 0;
    if (g_drm.ctx->notify_gralloc) {
        ret = g_drm.ctx->notify_gralloc(CMD_VIDEO_MODE, info);
    }
    if (ret) {
        LOGE("%s: Failed to notify gralloc", __func__);
        return false;
    }
    return true;
}

static bool set_ext_video_mode_internal()
{
    unsigned int i = 0;
    int width = 0;
    int height = 0;
    unsigned int hdmi_fb_id = 0;
    int index = 0;
    uint32_t hdmi_crtc_id = 0;
    drmModeConnectorPtr connector = NULL;
    drmModeModeInfoPtr mode = NULL;

    if ((connector = getHdmiConnector()) == NULL) {
        LOGE("%s: Failed to get HDMI connector", __func__);
        return false;
    }
    if (connector->connection != DRM_MODE_CONNECTED) {
        LOGE("%s: Failed to get a connected HDMI connector", __func__);
        return false;
    }
    index = select_hdmi_mode(connector);
    if (index == -1) {
        LOGE("%s: Failed to select a hdmi mode", __func__);
        return false;
    } else {
        mode = &(connector->modes[index]);
        width = mode->hdisplay;
        height = mode->vdisplay;
    }
    width = width > height ? width : height;
    height = width < height ? width : height;
    width = align_to(width, 16);
    height = align_to(height, 16);

    LOGV("%s: Extended Mode timing is %dx%d@%dHz",
            __func__, width, height, mode->vrefresh);
    if (0 != setup_hdmi_fb(width, height, DRM_HDMI_VIDEO_EXT)) {
        LOGE("%s: Failed to setup HDMI fb", __func__);
        goto error;
    }
    hdmi_fb_id = g_drm.einfo.fb_id[0];
    hdmi_crtc_id = get_crtc_id(g_drm.drmFD, DRM_MODE_ENCODER_TMDS);
    /* Mode setting with the largest resolution (e.g. 1920x1080) */
    int tmp = drmModeSetCrtc(g_drm.drmFD, hdmi_crtc_id, hdmi_fb_id, 0, 0,
                         &connector->connector_id, 1, mode);
    if (tmp) {
        LOGE("%s: Failed to set CRTC", __func__);
        goto error;
    }

    if (!g_drm.einfo.b_old_fb_cleared) {
        for (i = 0; i < g_drm.einfo.buf_cnt; i++) {
            if (g_drm.einfo.old_fb_id[i]) {
                drmModeRmFB(g_drm.drmFD, g_drm.einfo.old_fb_id[i]);
                g_drm.einfo.old_fb_id[i] = 0;
            }
        }
        g_drm.einfo.b_old_fb_cleared = 1;
    }
    set_hdmi_state(DRM_HDMI_VIDEO_EXT);
    return true;

error:
    cleanup_hdmi_fb(g_drm.drmFD, DRM_HDMI_VIDEO_EXT);
    return false;
}

static int hdmi_rm_notifier_handler(int cmd, void *data)
{
    bool ret = false;
    unsigned int i = 0;

    HDMI_mem_info_t *hdmi_info = (HDMI_mem_info_t *)data;

    pthread_mutex_lock(&g_drm.mtx);

    hdmi_get_timinginfo(cmd, hdmi_info->TimingInfo);

    switch (cmd) {
    case CMD_ROTATION_MODE:
        g_drm.cinfo.hPVR2DContext = hdmi_info->h2DCtx;
        g_drm.cinfo.new_buf_cnt = hdmi_info->size;
        ret = set_clone_mode_internal(hdmi_info->Orientation);
        if (ret) {
            for (i = 0; i < hdmi_info->size; i++) {
                hdmi_info->fb_id[i] = g_drm.cinfo.fb_id[i];
                hdmi_info->pHDMIMemInfo[i] = g_drm.cinfo.psMemInfo[i];
            }
            hdmi_info->changed = g_drm.cinfo.changed;
        } else
            hdmi_info->changed = 0;
        g_drm.curMode = CMD_CLONE_MODE;
        /*clear ext video mode*/
        cleanup_hdmi_fb(g_drm.drmFD, DRM_HDMI_VIDEO_EXT);
        break;
    case CMD_VIDEO_MODE:
        g_drm.einfo.hPVR2DContext = hdmi_info->h2DCtx;
        g_drm.einfo.new_buf_cnt = hdmi_info->size;
        ret = set_ext_video_mode_internal();
        if (ret) {
            for (i = 0; i < hdmi_info->size; i++) {
                hdmi_info->fb_id[i] = g_drm.einfo.fb_id[i];
                hdmi_info->pHDMIMemInfo[i] = g_drm.einfo.psMemInfo[i];
            }
            hdmi_info->changed = g_drm.einfo.changed;
        } else
            hdmi_info->changed = 0;
        g_drm.curMode = CMD_VIDEO_MODE;
        break;
    case CMD_NON_ROTATION_MODE:
        g_drm.cinfo.hPVR2DContext = hdmi_info->h2DCtx;
        g_drm.cinfo.new_buf_cnt = hdmi_info->size;
        ret = set_clone_mode_internal(hdmi_info->Orientation);
        if (ret) {
            for (i = 0; i < hdmi_info->size; i++) {
                hdmi_info->fb_id[i] = g_drm.cinfo.fb_id[i];
                hdmi_info->pHDMIMemInfo[i] = g_drm.cinfo.psMemInfo[i];
            }
            hdmi_info->changed = g_drm.cinfo.changed;
        } else
            hdmi_info->changed = 0;
        g_drm.curMode = CMD_CLONE_MODE;
        /*clear ext video mode*/
        cleanup_hdmi_fb(g_drm.drmFD, DRM_HDMI_VIDEO_EXT);
        break;
    }

    pthread_mutex_unlock(&g_drm.mtx);

    if (!ret) {
        LOGE("%s: Failed to notify handler", __func__);
        return -1;
    }
    return 0;
}

static int setScaling(int scaling_val)
{
    LOGV("%s: scale type is %d", __func__, scaling_val);
    /* notify gralloc to use set scale step*/
    return g_drm.ctx->notify_gralloc(CMD_SET_SCALE_TYPE, &scaling_val);
}

static int getScaling()
{
    return g_drm.configInfo.scaleType;
}

static bool setScaleStep(int value)
{
    int ret = 0;
    LOGV("%s: scale mode %x", __func__, value);

    /* notify gralloc to use set scale step*/
    ret = g_drm.ctx->notify_gralloc(CMD_SET_SCALE_STEP, &value);

    LOGI("%s: %d", __func__, ret);
    return ret;
}

static drmModeConnectorPtr getHdmiConnector()
{
    drmModeConnectorPtr con = NULL;
    if (g_drm.hdmi_connector == NULL)
        g_drm.hdmi_connector = get_connector(g_drm.drmFD, DRM_MODE_CONNECTOR_DVID);
    if (!g_drm.hdmi_connector) {
        LOGE("%s: Failed to get HDMI connector", __func__);
        return NULL;
    }
    con = g_drm.hdmi_connector;
    if (con->modes == NULL) {
        LOGE("%s: Failed to get HDMI connector's modes", __func__);
        return NULL;
    }
    return con;
}

static bool setHdmiMode(int mode, MDSHDMITiming* info)
{
    int i = 0;
    bool ret = false;
    switch (mode) {
    case DRM_HDMI_CLONE:
        ret = set_clone_mode(info);
        break;
    case DRM_HDMI_VIDEO_EXT:
        ret = set_ext_video_mode(info);
        break;
    default:
        LOGE("%s: not support mode %d", __func__, mode);
        ret = false;
        break;
    }
    return ret;
}

static void dumpHdmiTiming() {
    int index = 0;
    drmModeConnectorPtr con = NULL;
    if ((con = getHdmiConnector()) == NULL) {
        LOGE("%s: Failed to get HDMI connector", __func__);
        return;
    }
    if (con->connection != DRM_MODE_CONNECTED) {
        LOGE("%s: Failed to get a connected HDMI connector", __func__);
        return;
    }
    for (; index < con->count_modes; index++) {
        LOGD("%d, %dx%d@%dHzx0x%x", index,
                con->modes[index].hdisplay, con->modes[index].vdisplay,
                con->modes[index].vrefresh, con->modes[index].flags);
    }
}

static int checkHdmiTiming(int index) {
    drmModeConnectorPtr con = NULL;
    if ((con = getHdmiConnector()) == NULL) {
        LOGE("%s: Failed to get HDMI connector", __func__);
        return -1;
    }
    if (con->connection != DRM_MODE_CONNECTED) {
        LOGE("%s: Failed to get a connected HDMI connector", __func__);
        return -1;
    }
    if (index > con->count_modes)
        return -1;
    return index;
}

static int getMatchingHdmiTiming(int mode, MDSHDMITiming* in) {
    int index = 0;
    int firstRefreshMatchIndex = -1;
    int totalMatchIndex = -1;
    drmModeConnectorPtr con = NULL;
    if ((con = getHdmiConnector()) == NULL) {
        LOGE("%s: Failed to get HDMI connector", __func__);
        return -1;
    }
    if (con->connection != DRM_MODE_CONNECTED) {
        LOGE("%s: Failed to get a connected HDMI connector", __func__);
        return -1;
    }

    uint32_t temp_flags = 0;
    if (in->interlace)
        temp_flags |= DRM_MODE_FLAG_INTERLACE;
    if (in->ratio == 1)
        temp_flags |= DRM_MODE_FLAG_PAR16_9;
    else if (in->ratio == 2)
        temp_flags |= DRM_MODE_FLAG_PAR4_3;

    LOGD("%s: temp_flags = 0x%x, in->ratio = %d\n", __func__, temp_flags, in->ratio);

    for (index = 0; index < con->count_modes; index++) {
        /* Extract relevant flags to compare */
        uint32_t compare_flags = con->modes[index].flags &
          (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_PAR16_9 | DRM_MODE_FLAG_PAR4_3);

        LOGD("Mode avail: %dx%d@%d flags = 0x%x, compare_flags = 0x%x\n",
             con->modes[index].hdisplay, con->modes[index].vdisplay,
             con->modes[index].vrefresh, con->modes[index].flags,
             compare_flags);
        if (mode == DRM_HDMI_VIDEO_EXT &&
                firstRefreshMatchIndex == -1 &&
                temp_flags == compare_flags &&
                in->refresh == con->modes[index].vrefresh) {
            firstRefreshMatchIndex = index;
        }
        if (in->refresh == con->modes[index].vrefresh &&
                in->width == con->modes[index].hdisplay &&
                in->height == con->modes[index].vdisplay &&
                temp_flags == compare_flags) {
            totalMatchIndex = index;
            break;
        }
    }
    LOGD("%s: %d, %d, %d", __func__, index, firstRefreshMatchIndex, totalMatchIndex);
    index = -1;
    if (totalMatchIndex != -1)
        index = totalMatchIndex;
    else if (index == -1 && firstRefreshMatchIndex != -1)
        index = firstRefreshMatchIndex;
    if (index != -1) {
        LOGD("%s, Find a matching timing, %dx%d@%dHzx0x%x, %d", __func__,
                con->modes[index].hdisplay, con->modes[index].vdisplay,
                con->modes[index].vrefresh, con->modes[index].flags, index);
    }
    return index;
}

static int getHdmiModeInfo(int *pWidth, int *pHeight, int *pRefresh, int *pInterlace, int *pRatio)
{
    int i = 0, j = 0, iCount = 0;
    int valid_mode_count = 0;
    bool ret = false;
    drmModeConnectorPtr con = NULL;

    if ((con = getHdmiConnector()) == NULL) {
        LOGE("%s: Failed to get HDMI connector", __func__);
        iCount = -1;
        goto End;
    }
    if (con->connection != DRM_MODE_CONNECTED) {
        LOGE("%s: Failed to get a connected HDMI connector", __func__);
        iCount = -1;
        goto End;
    }

    iCount = con->count_modes;
    /* get resolution of each mode */
    for (i = 0; i < iCount; i++) {
        unsigned int temp_hdisplay = con->modes[i].hdisplay;
        unsigned int temp_vdisplay = con->modes[i].vdisplay;
        unsigned int temp_refresh = con->modes[i].vrefresh;
        /* Only extract the required flags for comparison */
        unsigned int temp_flags = con->modes[i].flags &
          (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_PAR16_9 | DRM_MODE_FLAG_PAR4_3);
        unsigned int compare_flags = 0;
        /* re-traverse the connector mode list to see if there is
         * same resolution and refresh. The same mode will not be
         * counted into valid mode.
         */
        j = i;

        while ((j--) >= 0) {

            compare_flags = con->modes[j].flags &
              (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_PAR16_9 |
               DRM_MODE_FLAG_PAR4_3);

            if(temp_hdisplay == con->modes[j].hdisplay
                && temp_vdisplay == con->modes[j].vdisplay
                && temp_refresh == con->modes[j].vrefresh
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

            LOGD("Adding mode[%d]: %dx%d@%d with flags = 0x%x\n", valid_mode_count,
                 temp_hdisplay, temp_vdisplay, temp_refresh, temp_flags);
            valid_mode_count++;
        }
    }
    iCount = valid_mode_count;
End:
    return iCount;
}

static int hdmi_get_timinginfo(int cmd, MDSHDMITiming* info)
{
    int index = 0;
    int mode = 0;
    if (cmd == CMD_VIDEO_MODE)
        mode = DRM_HDMI_VIDEO_EXT;
    else if ((cmd == CMD_ROTATION_MODE) || (cmd == CMD_ROTATION_MODE))
        mode = DRM_HDMI_CLONE;

    if ((mode != DRM_HDMI_CLONE && mode != DRM_HDMI_VIDEO_EXT)
       || (mode == DRM_HDMI_VIDEO_EXT && info == NULL)) {
       LOGE("%s: Failed to get invalid parameters", __func__);
       return -1;
    }

    if (info != NULL)
        LOGI("%s: HDMI timing is %dx%d@%dHzx%d", __func__,
                info->width, info->height, info->refresh, info->interlace);

    if (mode == DRM_HDMI_VIDEO_EXT) {
        g_drm.modeIndex = getMatchingHdmiTiming(DRM_HDMI_VIDEO_EXT, info);
    } else if (mode == DRM_HDMI_CLONE){
        if (info != NULL) {
            g_drm.modeIndex = getMatchingHdmiTiming(DRM_HDMI_CLONE, info);
        } else {
            g_drm.modeIndex = checkHdmiTiming(g_drm.cloneModeIndex);
        }
    }

    LOGD("%s: Switching to mode %d", __func__, g_drm.modeIndex);
    return 0;
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
    g_drm.configInfo.edidChange = 0;
    g_drm.cloneModeIndex = -1;
    g_drm.modeIndex = -1;
    g_drm.configInfo.scaleType = DRM_MODE_SCALE_ASPECT;
    g_drm.configInfo.horiRatio = HDMI_STEP_HORVALUE;
    g_drm.configInfo.vertRatio = HDMI_STEP_VERVALUE;

    ret = setup_drm();
    if (!ret)
        goto exit;
    if (get_gralloc_ex(&g_drm.ctx)) {
        ret = false;
        goto exit;
    } else
        ret = true;
    if (g_drm.ctx->register_notify_func)
        g_drm.ctx->register_notify_func(hdmi_rm_notifier_handler);
    if (getScaling() != DRM_MODE_SCALE_ASPECT)
        setScaling(DRM_MODE_SCALE_ASPECT);
exit:
    pthread_mutex_unlock(&g_drm.mtx);
    return ret;
}

void drm_cleanup()
{
    pthread_mutex_lock(&g_drm.mtx);
    if (g_drm.drmFD >= 0) {
        cleanup_hdmi_fb(g_drm.drmFD, DRM_HDMI_VIDEO_EXT);
        cleanup_hdmi_fb(g_drm.drmFD, DRM_HDMI_CLONE);
        drmClose(g_drm.drmFD);
    }
    if (g_drm.hdmi_connector)
        drmModeFreeConnector(g_drm.hdmi_connector);
    g_drm.hdmi_connector = NULL;
    pthread_mutex_unlock(&g_drm.mtx);
    pthread_mutex_destroy(&g_drm.mtx);
    memset(&g_drm,0,sizeof(drmJNI));
    g_drm.drmFD = -1;
}

/*
 * returns TRUE if HDMI was found and turns on HDMI.
 */
bool drm_hdmi_setHdmiVideoOn()
{
    int i = 0;
    bool ret = true;
    struct drm_psb_disp_ctrl dp_ctrl;

    CHECK_DRM_FD(false);

    pthread_mutex_lock(&g_drm.mtx);
    /* set HDMI display off */
    memset(&dp_ctrl, 0, sizeof(dp_ctrl));
    dp_ctrl.cmd = DRM_PSB_DISP_PLANEB_ENABLE;
    dp_ctrl.u.data = HDMI_FORCE_VIDEO_ON_OFF;
    int tmp = drmCommandWriteRead(g_drm.drmFD,
                DRM_PSB_HDMI_FB_CMD, &dp_ctrl, sizeof(dp_ctrl));
    if (tmp != 0) {
        LOGE("%s: Failed to turn on HDMI", __func__);
        ret = false;
    }
    pthread_mutex_unlock(&g_drm.mtx);
    return ret;
}

/* drm_hdmi_setHdmiPowerOff() returns TRUE if HDMI power was successfully turned off */
bool drm_hdmi_setHdmiPowerOff()
{
    bool ret = true;
    struct drm_psb_disp_ctrl dp_ctrl;
    CHECK_DRM_FD(false);

    pthread_mutex_lock(&g_drm.mtx);
    /*when hdmi disconnected , set the power state lsland down */
    memset(&dp_ctrl, 0, sizeof(dp_ctrl));
    dp_ctrl.cmd = DRM_PSB_HDMI_OSPM_ISLAND_DOWN;
    int tmp = drmCommandWriteRead(g_drm.drmFD,
            DRM_PSB_HDMI_FB_CMD, &dp_ctrl, sizeof(dp_ctrl));
    if (tmp != 0) {
        LOGE("%s: Failed to set power state lsland down, %d", __func__, ret);
        ret = false;
    }
    pthread_mutex_unlock(&g_drm.mtx);
    return ret;
}

/* drm_hdmi_setHdmiVideoOff() returns TRUE if HDMI was found and turns off HDMI. */
bool drm_hdmi_setHdmiVideoOff()
{
    bool ret = true;
    struct drm_lnc_video_getparam_arg arg;
    struct drm_psb_disp_ctrl dp_ctrl;

    CHECK_DRM_FD(false);

    pthread_mutex_lock(&g_drm.mtx);
    /* set HDMI display on */
    memset(&dp_ctrl, 0, sizeof(dp_ctrl));
    dp_ctrl.cmd = DRM_PSB_DISP_PLANEB_DISABLE;
    dp_ctrl.u.data = HDMI_FORCE_VIDEO_ON_OFF;
    int tmp = drmCommandWriteRead(g_drm.drmFD,
            DRM_PSB_HDMI_FB_CMD, &dp_ctrl, sizeof(dp_ctrl));
    if (tmp != 0) {
        LOGE("Failed to turn off HDMI, %d", ret);
        ret = false;
    }
    pthread_mutex_unlock(&g_drm.mtx);
    return ret;
}

int drm_hdmi_connectStatus()
{
    drmModeConnector *hdmi_connector = NULL;
    int ret = 0;
    char last_product_inf[8] ={0,0,0,0,0,0,0,0};

    CHECK_DRM_FD(0);

    pthread_mutex_lock(&g_drm.mtx);
    /* release hdmi_connector before getting connectStatus */
    if (g_drm.hdmi_connector)
        drmModeFreeConnector(g_drm.hdmi_connector);
    g_drm.hdmi_connector = NULL;

    if ((hdmi_connector = getHdmiConnector()) == NULL) {
        LOGE("%s: Failed to get connector", __func__);
        ret = 0;
        goto End;
    }
    if (hdmi_connector->connection != DRM_MODE_CONNECTED) {
        ret = 0;
        goto End;
    }
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

    for (i = 0; i < hdmi_connector->count_props; i++) {
        props = drmModeGetProperty(g_drm.drmFD, hdmi_connector->props[i]);
        if (!props)
            continue;

        if (props->name && !strcmp(props->name, "EDID")) {
            ret = 2; //DVI
            edid = &hdmi_connector->prop_values[i];
            edidBlob = drmModeGetPropertyBlob(g_drm.drmFD, *edid);
            if (edidBlob == NULL || edidBlob->length < EDID_BLOCK_SIZE) {
                LOGE("%s: Invalid EDID Blob.", __func__);
                drmModeFreeProperty(props);
                continue;
            }

            edid_binary = (char*)&edidBlob->data[0];
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

    if ((g_drm.configInfo.scaleType != DRM_MODE_SCALE_ASPECT) &&
        (g_drm.configInfo.edidChange == 1)) {
        g_drm.configInfo.scaleType = DRM_MODE_SCALE_ASPECT;
        setScaling(DRM_MODE_SCALE_ASPECT);
    }

    if (((g_drm.configInfo.horiRatio != HDMI_STEP_HORVALUE) ||
        (g_drm.configInfo.vertRatio != HDMI_STEP_VERVALUE))
        && (g_drm.configInfo.edidChange == 1)) {
        g_drm.configInfo.horiRatio = HDMI_STEP_HORVALUE;
        g_drm.configInfo.vertRatio = HDMI_STEP_VERVALUE;
        setScaleStep(0);
    }

    pthread_mutex_unlock(&g_drm.mtx);

    LOGI("%s: Leaving, connect status is %d", __func__, ret);
    return ret;
}


bool drm_hdmi_onHdmiDisconnected(void)
{
    CHECK_DRM_FD(false);

    pthread_mutex_lock(&g_drm.mtx);
    /*notify disonnect message*/
    if (g_drm.ctx->notify_gralloc)
        g_drm.ctx->notify_gralloc(CMD_HDMI_DISCONNECTED, NULL);
    /*clear memory*/
    cleanup_hdmi_fb(g_drm.drmFD, DRM_HDMI_VIDEO_EXT);
    cleanup_hdmi_fb(g_drm.drmFD, DRM_HDMI_CLONE);
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
    drmModeConnector *mipi_connector = NULL;
    drmModePropertyPtr props = NULL;
    CHECK_DRM_FD(false);
    pthread_mutex_lock(&g_drm.mtx);
    //TODO: MIPI connector couldn't be modified,
    //there is no need to get MIPI connector everytime.
    mipi_connector = get_connector(g_drm.drmFD, DRM_MODE_CONNECTOR_MIPI);
    if (!mipi_connector) {
        LOGE("%s: Failed to get MIPI connector", __func__);
        goto exit;
    }

    if (mipi_connector->connection != DRM_MODE_CONNECTED) {
        LOGE("%s: Failed to get a connected MIPI, %d %d", __func__,
              mipi_connector->connector_id,
              mipi_connector->connector_type);
        goto exit;
    }

    /* Set MIPI On/Off */
    for (i = 0; i < mipi_connector->count_props; i++) {
        props = drmModeGetProperty(g_drm.drmFD, mipi_connector->props[i]);
        if (!props) continue;

        if (!strcmp(props->name, "DPMS")) {
            LOGV("%s: %s %u", __func__,
                  (mode == DRM_MIPI_ON) ? "On" : "Off",
                  mipi_connector->connector_id);
            drmModeConnectorSetProperty(g_drm.drmFD,
                                        mipi_connector->connector_id,
                                        props->prop_id,
                                        (mode == DRM_MIPI_ON)
                                        ? DRM_MODE_DPMS_ON : DRM_MODE_DPMS_OFF);
            drmModeFreeProperty(props);
            break;
        }

        drmModeFreeProperty(props);
    }
    ret = true;
exit:
    if (mipi_connector)
        drmModeFreeConnector(mipi_connector);
    pthread_mutex_unlock(&g_drm.mtx);
    LOGI("%s %s", __func__, (ret == true ? "success" : "fail"));
    return ret;
}

int drm_hdmi_getModeInfo(int *pWidth, int *pHeight, int *pRefresh, int *pInterlace, int *pRatio)
{
    int ret = -1;
    CHECK_DRM_FD(false);
    pthread_mutex_lock(&g_drm.mtx);
    ret = getHdmiModeInfo(pWidth, pHeight, pRefresh, pInterlace, pRatio);
    pthread_mutex_unlock(&g_drm.mtx);
    return ret;
}

bool drm_hdmi_setScaling(int scaling_val)
{
    int ret = 0;
    CHECK_DRM_FD(false);

    pthread_mutex_lock(&g_drm.mtx);
    g_drm.configInfo.scaleType = scaling_val;
    ret = setScaling(scaling_val);
    pthread_mutex_unlock(&g_drm.mtx);
    return (ret == 1 ? true : false);
}

int drm_hdmi_getScaling()
{
    int ret = 0;
    CHECK_DRM_FD(0);
    pthread_mutex_lock(&g_drm.mtx);
    ret = getScaling();
    pthread_mutex_unlock(&g_drm.mtx);
    return ret;
}

bool drm_hdmi_setScaleStep(int hValue, int vValue)
{
    bool ret = false;
    int i = 0;
    int value = 0;
    int scale = 0;
    CHECK_DRM_FD(false);
    pthread_mutex_lock(&g_drm.mtx);

    g_drm.configInfo.horiRatio = hValue;
    g_drm.configInfo.vertRatio = vValue;

    if(hValue < 6)
        hValue = 5 - hValue;
        hValue &= 0x0F;

    if(vValue < 6)
        vValue = 5 - vValue;
        vValue &= 0x0F;

    scale = g_drm.configInfo.scaleType;
    scale &= 0x0F;

    value = (hValue << 4)|0xF;
    value = (vValue << 8)|value;
    LOGV("%s: scale mode %x", __func__, value);

    ret = setScaleStep(value);
    pthread_mutex_unlock(&g_drm.mtx);
    return ret;
}

int drm_hdmi_getDeviceChange()
{
    int ret = 0;
    CHECK_DRM_FD(0);
    pthread_mutex_lock(&g_drm.mtx);
    if (g_drm.configInfo.edidChange != 0 ) {
        ret = 1;
        g_drm.configInfo.edidChange = 0;
    }
    pthread_mutex_unlock(&g_drm.mtx);
    return ret;
}

int drm_hdmi_notify_audio_hotplug(bool plugin)
{
    bool ret = true;
    struct drm_psb_disp_ctrl dp_ctrl;

    CHECK_DRM_FD(MDS_ERROR);
    pthread_mutex_lock(&g_drm.mtx);

    /* set HDMI display off */
    memset(&dp_ctrl, 0, sizeof(dp_ctrl));
    dp_ctrl.cmd = DRM_PSB_HDMI_NOTIFY_HOTPLUG_TO_AUDIO;
    dp_ctrl.u.data = (plugin == true ? 1 : 0);
    int tmp = drmCommandWriteRead(g_drm.drmFD,
            DRM_PSB_HDMI_FB_CMD, &dp_ctrl, sizeof(dp_ctrl));
    if (tmp != 0) {
        LOGE("%s: Failed to notify audio plug event, %d", __func__, ret);
        ret = false;
    }

    pthread_mutex_unlock(&g_drm.mtx);
    return (ret == true ? MDS_NO_ERROR : MDS_ERROR);
}

int drm_get_dev_fd()
{
    return g_drm.drmFD;
}

int drm_get_ioctl_offset()
{
    return g_drm.ioctlOffset;
}

bool drm_hdmi_setMode(int mode, MDSHDMITiming* info) {
    int index = 0;
    if ((mode != DRM_HDMI_CLONE && mode != DRM_HDMI_VIDEO_EXT) ||
            (mode == DRM_HDMI_VIDEO_EXT && info == NULL)) {
        LOGE("%s: Failed to get invalid parameters", __func__);
        return false;
    }
    if (info != NULL)
        LOGI("%s: HDMI timing is %dx%d@%dHzx%d", __func__,
                info->width, info->height, info->refresh, info->interlace);
    CHECK_DRM_FD(false);

    return setHdmiMode(mode, info);
}
