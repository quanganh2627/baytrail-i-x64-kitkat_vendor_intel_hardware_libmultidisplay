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

#include <display/MultiDisplayType.h>

#define HDMI_MODE_OFF          0
#define HDMI_MODE_CLONE        1
#define HDMI_MODE_EXT_VIDEO    2
#define HDMI_MODE_EXT_DESKTOP  3
#define HDMI_MODE_CLONE_ROTATION  4

#define DRM_MODE_SCALE_NONE             0 /* Unmodified timing (display or
software can still scale) */
#define DRM_MODE_SCALE_FULLSCREEN       1 /* Full screen, ignore aspect */
#define DRM_MODE_SCALE_CENTER           2 /* Centered, no scaling */
#define DRM_MODE_SCALE_ASPECT           3 /* Full screen, preserve aspect */

#define CLONE_MODE 1
#define EXT_MODE   2

#define HDMI_STEP_HORVALUE      5
#define HDMI_STEP_VERVALUE      5

#define HDMI_FORCE_VIDEO_ON_OFF 1

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
    drmModeModeInfo *mode_info;
    unsigned int aspect_ratio;
} HDMI_fb_mem_info;

typedef struct _HDMI_config_info_ {
    int setMode;
    int edidChange;
    int setModeIndex;
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
    drmModeConnector *hdmi_connector;
    pthread_mutex_t mtx;
    HDMI_config_info configInfo;//hdmisetting
} drmJNI;

static inline uint32_t align_to(uint32_t arg, uint32_t align)
{
    return ((arg + (align - 1)) & (~(align - 1)));
}

/** Base path of the hal modules */
#define HAL_LIBRARY_PATH1 "/system/lib/hw"
#define HAL_LIBRARY_PATH2 "/vendor/lib/hw"
#define DRM_DEVICE_NAME "/dev/card0"
#define DRM_EDID_FILE	"/data/system/hdmi_edid.dat"

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
static unsigned int select_hdmi_mode(drmModeConnector *hdmi_connector);
static int setScaling(int scaling_val);
static bool setScaleStep(int hValue, int vValue);

/* ---------------- LOCAL FUNCTIONS ---------------------------------- */

/* setup_drm() is a local method that sets up the DRM resources needed */
/* to set or get video modes.  As a side effect encoder and connector  */
/* are set to non-NULL if setup_drm() returns true. */
static bool setup_drm()
{
    bool ret = false;
    union drm_psb_extension_arg video_getparam_arg;
    const char video_getparam_ext[] = "lnc_video_getparam";

    /* Init the variables shared in this module. */
    g_drm.drmFD = -1;

    /* Check for DRM-required resources. */
    g_drm.drmFD = open(DRM_DEVICE_NAME, O_RDWR, 0);
    if (g_drm.drmFD >= 0) {
        strncpy(video_getparam_arg.extension, video_getparam_ext, sizeof(video_getparam_arg.extension));
        ret = drmCommandWriteRead(g_drm.drmFD, DRM_PSB_EXTENSION, &video_getparam_arg,
                                  sizeof(video_getparam_arg));
        if (ret != 0 || !video_getparam_arg.rep.exists) {
            LOGE("%s: fail to getparam %s", __func__, video_getparam_ext);
            g_drm.ioctlOffset = 0;
            ret = false;
        } else {
            g_drm.ioctlOffset = video_getparam_arg.rep.driver_ioctl_offset;
            ret = true;
        }
    } else
        LOGE("%s: fail to open drm device %d", __func__, g_drm.drmFD);

    return ret;
}

drmModeEncoder *get_encoder(int pfd, uint32_t encoder_type)
{
    int i = 0;

    drmModeRes *resources = NULL;
    drmModeEncoder *encoder = NULL;

    resources = drmModeGetResources(pfd);
    if (!resources) {
        LOGE("%s: fail to get resource, error is %s", __func__, strerror(errno));
        return NULL;
    }

    for (i = 0; i < resources->count_encoders; i++) {
        encoder = drmModeGetEncoder(pfd, resources->encoders[i]);

        if (!encoder) {
            LOGE("%s: fail to get encoder %i, error is %s", __func__, resources->encoders[i], strerror(errno));
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
        LOGE("%s: fail to get resource, error is %s", __func__, strerror(errno));
        return NULL;
    }

    for (i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(pfd, resources->connectors[i]);

        if (!connector) {
            LOGE("%s: fail to get connector %d, error is %s", __func__, resources->connectors[i], strerror(errno));
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
            LOGE("%s: fail to get resource , error is %s", __func__, strerror(errno));
            return 0;
        }

        for (i = 0; i < resources->count_crtcs; i++) {
            crtc = drmModeGetCrtc(pfd, resources->crtcs[i]);
            if (!crtc) {
                LOGE("%s: fail to get crtc %d, error is %s", __func__, resources->crtcs[i], strerror(errno));
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
        LOGE("%s: fail to load %s, error is %s", __func__, path, (err_str ? err_str : "unknown"));
        status = -EINVAL;
        goto done;
    }

    /* Get the address of the struct hal_module_info. */
    const char *sym = "HAL_GR_HDMI_SYM";
    ex = (IMG_graphic_hdmi_ex *)dlsym(handle, sym);
    if (ex == NULL) {
        LOGE("%s: fail to find symbol %s", __func__, sym);
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
    if (mode == CLONE_MODE)
        fb_info = &g_drm.cinfo;
    else if (mode == EXT_MODE)
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
    if (mode == CLONE_MODE) {
        uFlags = 0;
        fb_info = &g_drm.cinfo;
    } else if (mode == EXT_MODE) {
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
    if (hdmi_fb_id) {
        hdmi_fb = drmModeGetFB(g_drm.drmFD, hdmi_fb_id);
        if (!hdmi_fb) {
            LOGE("%s: fail to get hdmi fb", __func__);
            return -1;
        }
        if (((uint32_t)width == hdmi_fb->width) && ((uint32_t)height == hdmi_fb->height)) {
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

    length = width > height ? width : height;/*lenght should usually be width*/
    if (mode == CLONE_MODE)
        stride = length * 4;
    else
        stride = width * 4;
    /*stride must align to 64B*/
    stride = align_to(stride, 64);
    if (mode == CLONE_MODE)
        fb_size = stride * length;
    else
        fb_size = stride * height;

    /*buffer size should align to page size*/
    fb_size = align_to(fb_size, getpagesize());

    if (re_use) {
        for (i = 0; i < fb_info->buf_cnt; i++) {
            if (fb_info->psMemInfo[i] && fb_info->psMemInfo[i]->pBase)
                memset(fb_info->psMemInfo[i]->pBase, 0, fb_info->psMemInfo[i]->ui32MemSize);
        }
    } else {
        fb_info->changed = 1;
        fb_info->buf_cnt = fb_info->new_buf_cnt;

        for (i = 0; i < fb_info->new_buf_cnt; i++) {
            if (re_use && fb_info->psMemInfo[i])
                continue;

            fb_info->changed = 1;
            ePVR2DStatus = PVR2DMemAlloc(fb_info->hPVR2DContext,
                                         fb_size,
                                         1,
                                         uFlags,
                                         &fb_info->psMemInfo[i]);
            if (ePVR2DStatus != PVR2D_OK) {
                LOGE("%s: fail to alloc memory", __func__);
                goto error;
            }

            /* HDMI would display black screen, after setting to extended/clone mode. */
            memset(fb_info->psMemInfo[i]->pBase, 0, fb_size);
            ret = drmModeAddFB(g_drm.drmFD, width, height, 24, 32, stride, (uint32_t)(fb_info->psMemInfo[i]->hPrivateMapData), &hdmi_fb_id);
            if (ret) {
                LOGE("%s: fail to add fb, error is %s", __func__, strerror(errno));
                goto error;
            }

            fb_info->fb_id[i] = hdmi_fb_id;
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

/**
 * Since DV10's timing is 1280*800, when in rotation mode, it will change to
 * portrait mode. For some HDMI timing, it will exceed DC's scaling limitation.
 * In this situation, use SGX to do full screen blit.
 */
static void decide_fb_width_height(int *fb_width, int *fb_height,
                                   int hdisplay, int vdisplay)
{
    float dc_limit = 1.5;
    int width = *fb_width;
    int height = *fb_height;

    if ((width > hdisplay * dc_limit) ||
        (height > vdisplay * dc_limit) ||
        (height > hdisplay * dc_limit) ||
        (width > vdisplay * dc_limit)) {
        if (height > vdisplay && height >= width) {
            *fb_height = vdisplay;
            *fb_width = width * vdisplay / height;
        }
    }
}

static void set_hdmi_state(int state)
{
    int ret;
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
    unsigned int mipi_fb_id = 0;
    int index = 0;
    uint16_t hdisplay = 0;
    struct drm_lnc_video_getparam_arg arg;
    int hdmi_mode_width = 0;
    int hdmi_mode_height = 0;
    int hdmi_fb_width = 0;
    int hdmi_fb_height = 0;
    bool bfind_1920_1080_mode = false;
    drmModeFB *mipi_fb = NULL;
    HDMI_config_info hdmiConfig = g_drm.configInfo;

    if (g_drm.drmFD < 0)
        ret = setup_drm();
    else
        ret = true;
    mipi_fb_id = get_fb_id(g_drm.drmFD, DRM_MODE_ENCODER_MIPI);
    mipi_fb = drmModeGetFB(g_drm.drmFD,mipi_fb_id);
    if (g_drm.hdmi_connector == NULL)
        g_drm.hdmi_connector = get_connector(g_drm.drmFD, DRM_MODE_CONNECTOR_DVID);

    if (!g_drm.hdmi_connector) {
        LOGE("%s: fail to get connector, error is %s", __func__, strerror(errno));
        return false;
    }

    hdmi_connector = g_drm.hdmi_connector;

    if (hdmi_connector->connection != DRM_MODE_CONNECTED) {
        LOGE("%s: fail, hdmi is disconnected, error is %s", __func__, strerror(errno));
        return false;
    }

    index = select_hdmi_mode(hdmi_connector);
    if (index == -1) {
        LOGE("%s: fail to select a hdmi mode", __func__);
        return false;
    }
    hdmi_mode_width = hdmi_connector->modes[index].hdisplay;
    hdmi_mode_height = hdmi_connector->modes[index].vdisplay;
    LOGV("%s: w: %d, h: %d, refresh = %d", __func__, hdmi_mode_width, hdmi_mode_height, hdmi_connector->modes[index].vrefresh);

    hdmi_fb_width = hdmi_mode_width > hdmi_mode_height ? hdmi_mode_width : hdmi_mode_height;
    hdmi_fb_height = hdmi_mode_width < hdmi_mode_height ? hdmi_mode_width : hdmi_mode_height;
//    LOGI("%s: hdmi_fb_w = %d, hdmi_fb_h = %d, hdmi_mode_w = %d, hdmi_mode_h = %d", __func__, hdmi_fb_width, hdmi_fb_height, hdmi_mode_width, hdmi_mode_height);

    hdmi_fb_width = align_to(hdmi_fb_width, 16);
    hdmi_fb_height = align_to(hdmi_fb_height, 16);
    LOGI("%s: after align: hdmi_fb_w = %d, hdmi_fb_h = %d", __func__, hdmi_fb_width, hdmi_fb_height);

        LOGI("%s: fw: %d, fh: %d", __func__, hdmi_fb_width, hdmi_fb_height);
        if (0 != setup_hdmi_fb(hdmi_fb_width, hdmi_fb_height, CLONE_MODE)) {
            LOGE("%s: fail to setup hdmi fb", __func__);
            ret = false;
            goto exit;
        }
        hdmi_fb_id = g_drm.cinfo.fb_id[0];

    hdmi_crtc_id = get_crtc_id(g_drm.drmFD, DRM_MODE_ENCODER_TMDS);
    /* Mode setting with the largest resolution (e.g 1920x1080), to set HDMI with clone mode. */
    ret = drmModeSetCrtc(g_drm.drmFD, hdmi_crtc_id, hdmi_fb_id, 0, 0,
                         &hdmi_connector->connector_id, 1, &hdmi_connector->modes[index]);

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
    if (ret) {
        LOGE("%s: fail to set CRTC %d, error is %s", __func__, ret,strerror(errno));
        ret = false;
    } else {
        ret = true;
    }

    set_hdmi_state(HDMI_MODE_CLONE);
exit:
    return ret;
}

static bool set_mode_internal()
{
    unsigned int i = 0;
    bool ret = false;
    drmModeConnector *hdmi_connector = NULL;
    uint32_t hdmi_crtc_id = 0;
    unsigned int hdmi_fb_id = 0;
    struct drm_lnc_video_getparam_arg arg;
    drmModeModeInfoPtr set_mode = g_drm.cinfo.mode_info;
    unsigned int hdmi_fb_width  = 0;
    unsigned int hdmi_fb_height = 0;
    unsigned int hdmi_mode_width = 0;
    unsigned int hdmi_mode_height = 0;
    unsigned int mipi_fb_id = 0;
    drmModeFB *mipi_fb = NULL;

    if (g_drm.drmFD < 0)
        ret = setup_drm();
    else
        ret = true;

    if (g_drm.hdmi_connector == NULL)
        g_drm.hdmi_connector = get_connector(g_drm.drmFD, DRM_MODE_CONNECTOR_DVID);

    if (!g_drm.hdmi_connector) {
        LOGE("%s: fail to get connector", __func__);
        return false;
    }
    hdmi_connector = g_drm.hdmi_connector;

    if (hdmi_connector->connection != DRM_MODE_CONNECTED) {
        LOGE("%s: hdmi is disconnect, error is  %s", __func__, strerror(errno));
        return false;
    }
    hdmi_mode_width = set_mode->hdisplay;
    hdmi_mode_height = set_mode->vdisplay;

    hdmi_fb_width = hdmi_mode_width > hdmi_mode_height ? hdmi_mode_width : hdmi_mode_height;
    hdmi_fb_height = hdmi_mode_width < hdmi_mode_height ? hdmi_mode_width : hdmi_mode_height;
//    LOGI("%s: hdmi_fb_w = %d, hdmi_fb_h = %d, hdmi_mode_w = %d, hdmi_mode_h = %d", __func__, hdmi_fb_width, hdmi_fb_height, hdmi_mode_width, hdmi_mode_height);

    hdmi_fb_width = align_to(hdmi_fb_width, 16);
    hdmi_fb_height = align_to(hdmi_fb_height, 16);
    LOGI("%s: after align: hdmi_fb_w = %d, hdmi_fb_h = %d", __func__, hdmi_fb_width, hdmi_fb_height);

    if (0 != setup_hdmi_fb(hdmi_fb_width, hdmi_fb_height, CLONE_MODE)) {
        LOGE("%s: fail to setup hdmi fb", __func__);
        ret = false;
        goto exit;
    }
    hdmi_fb_id = g_drm.cinfo.fb_id[0];
    hdmi_crtc_id = get_crtc_id(g_drm.drmFD, DRM_MODE_ENCODER_TMDS);
    LOGV("About to call mode set crtc\n");
    int tmp = drmModeSetCrtc(g_drm.drmFD, hdmi_crtc_id, hdmi_fb_id, 0, 0,
                         &hdmi_connector->connector_id, 1, set_mode);

    if (tmp) {
        LOGE("%s: fail to set CRTC %d, error is %s", __func__, ret,strerror(errno));
        ret = false;
    } else
        ret = true;
    /* clear the old fb id */
    if (!g_drm.cinfo.b_old_fb_cleared) {
        for (i = 0; i < g_drm.cinfo.buf_cnt; i++) {
            if (g_drm.cinfo.old_fb_id[i]) {
                drmModeRmFB(g_drm.drmFD, g_drm.cinfo.old_fb_id[i]);
                g_drm.cinfo.old_fb_id[i] = 0;
            }
        }
        g_drm.cinfo.b_old_fb_cleared = 1;
    }

    set_hdmi_state(HDMI_MODE_CLONE);

exit:
    return ret;
}

static unsigned int select_hdmi_mode(drmModeConnector *hdmi_connector)
{
    int index = 0;
    bool bfind_1920_1080_mode = false;
    int hdmi_mode_width = 0;
    int hdmi_mode_height = 0;
    int i = 0;
    if (!hdmi_connector) {
        LOGE("%s: hdmi is disconnect, error is %s", __func__, strerror(errno));
        return index;
    }
    LOGI("mode is %d",g_drm.configInfo.setMode);
    if (g_drm.configInfo.setMode != 0) {
        index = g_drm.configInfo.setModeIndex;
        return index;
    }
    /*mode sort as big --> small, default use index =0, the max one*/
    index = 0;
    hdmi_mode_width = hdmi_connector->modes[index].hdisplay;
    hdmi_mode_height = hdmi_connector->modes[index].vdisplay;
#if 0
#ifndef HDMI_COMPLIANCE_TEST
    /*set to 1920*1080 mode */
    for (i = 0; i < hdmi_connector->count_modes; i++) {
        if (hdmi_connector->modes[i].hdisplay == 1920 &&
            hdmi_connector->modes[i].vdisplay == 1080) {
            hdmi_mode_width = hdmi_connector->modes[i].hdisplay;
            hdmi_mode_height = hdmi_connector->modes[i].vdisplay;
            index = i;
            bfind_1920_1080_mode = true;
        }
    }
#endif
#endif
    if (!bfind_1920_1080_mode) {
        /*set to prefer mode */
        for (i = 0; i < hdmi_connector->count_modes; i++) {
            if (hdmi_connector->modes == NULL) {
                LOGE("%s: fail to get modes", __func__);
                return -1;
            }
            if (hdmi_connector->modes[i].type &
                DRM_MODE_TYPE_PREFERRED) {
                hdmi_mode_width = hdmi_connector->modes[i].hdisplay;
                hdmi_mode_height = hdmi_connector->modes[i].vdisplay;
                index = i;
            }
        }
    }

    if (index == hdmi_connector->count_modes) {
        LOGE("%s: fail to select mode, count is %d", __func__, hdmi_connector->count_modes);
        return -1;
    }
    return index;
}

/*currently, mipi fb is 600*1024,
*for some small mode, such as 640*480
*display controller hw can not downscale bigger than 1.5
*so here ,for those mode default go through hdmi blt
*/
static bool decide_whether_default_go_hdmi_blt()
{
    int i = 0;
    int index = 0;;
    bool ret = false;
    drmModeConnector *hdmi_connector = NULL;
    unsigned int hdmi_fb_id = 0;
    unsigned int mipi_fb_id = 0;
    unsigned int hdmi_mode_width = 0;
    unsigned int hdmi_mode_height = 0;
    drmModeFB *mipi_fb = NULL;
    drmModeModeInfoPtr set_mode = NULL;
    bool bfind_1920_1080_mode = false;
    float dc_downscaling_factor_limit = 1.0f;

    if (g_drm.cinfo.mode_info) {
        free(g_drm.cinfo.mode_info);
        g_drm.cinfo.mode_info = NULL;
        g_drm.cinfo.aspect_ratio = 0;
    }

    if (g_drm.drmFD < 0)
        ret = setup_drm();
    else
        ret = true;
    mipi_fb_id = get_fb_id(g_drm.drmFD, DRM_MODE_ENCODER_MIPI);
    mipi_fb = drmModeGetFB(g_drm.drmFD,mipi_fb_id);

    if (g_drm.hdmi_connector == NULL)
        g_drm.hdmi_connector = get_connector(g_drm.drmFD, DRM_MODE_CONNECTOR_DVID);

    if (!g_drm.hdmi_connector) {
        LOGE("%s: fail to get connector, error is %s", __func__, strerror(errno));
        return false;
    }

    hdmi_connector = g_drm.hdmi_connector;
    if (hdmi_connector->connection != DRM_MODE_CONNECTED) {
        LOGE("%s: fail, hdmi is disconnect, error is %s", __func__, strerror(errno));
        return true;
    }

    index = select_hdmi_mode(hdmi_connector);
    if (index == -1) {
        LOGE("%s: fail to select a hdmi mode", __func__);
        return false;
    }

    hdmi_mode_width = hdmi_connector->modes[index].hdisplay;
    hdmi_mode_height = hdmi_connector->modes[index].vdisplay;
    /*
     * FIXME: need to get the specific Display Controller downscaling factor
     * limitation through the vertical direction.
     */
    if ((float)mipi_fb->height >
            (hdmi_mode_height * dc_downscaling_factor_limit)) {
            set_mode = (drmModeModeInfo*)malloc(sizeof(drmModeModeInfo));
            memcpy(set_mode, &hdmi_connector->modes[index], sizeof(drmModeModeInfo));
            g_drm.cinfo.mode_info = set_mode;
            g_drm.cinfo.aspect_ratio = 2;
            g_drm.ctx->notify_gralloc(CMD_SET_MODE, NULL);
    }
    return ret;
}

static bool set_clone_mode()
{
    int ret = 0;
    if (g_drm.ctx->notify_gralloc) {
        int flag_recursion = 1;
        ret = g_drm.ctx->notify_gralloc(CMD_CLONE_MODE, &flag_recursion);
    }
    if (ret)
        return false;

    if ((g_drm.configInfo.scaleType != DRM_MODE_SCALE_ASPECT) && (g_drm.configInfo.edidChange == 1)) {
        g_drm.configInfo.scaleType = DRM_MODE_SCALE_ASPECT;
        setScaling(DRM_MODE_SCALE_ASPECT);
        LOGI("edidChange: scaleType");
    }

    if (((g_drm.configInfo.horiRatio != HDMI_STEP_HORVALUE) || (g_drm.configInfo.vertRatio != HDMI_STEP_VERVALUE))
        && (g_drm.configInfo.edidChange == 1)) {
        g_drm.configInfo.horiRatio = HDMI_STEP_HORVALUE;
        g_drm.configInfo.vertRatio = HDMI_STEP_VERVALUE;
        setScaleStep(HDMI_STEP_HORVALUE, HDMI_STEP_VERVALUE);
        LOGI("edidChange: stepScale");
    }

    if (g_drm.configInfo.edidChange == 1)
        g_drm.configInfo.edidChange = 2;

    return true;
}

static bool set_ext_video_mode()
{
    int ret = 0;
    if (g_drm.ctx->notify_gralloc) {
        int flag_recursion = 1;
        ret = g_drm.ctx->notify_gralloc(CMD_VIDEO_MODE, &flag_recursion);
    }
    if (ret) {
        LOGE("%s: fail to notify gralloc", __func__);
        return false;
    }
    return true;
}

static bool set_ext_video_mode_internal()
{
    unsigned int i = 0;
    int width = 0;
    int height = 0;
    bool ret = false;
    unsigned int hdmi_fb_id = 0;
    int index = 0;
    uint32_t hdmi_crtc_id = 0;
    drmModeConnector *hdmi_connector = NULL;
    struct drm_lnc_video_getparam_arg arg;
    bool bfind_1920_1080_mode = false;

    if (g_drm.drmFD < 0)
        ret = setup_drm();
    else
        ret = true;

    if (ret) {
        if (g_drm.hdmi_connector == NULL)
            g_drm.hdmi_connector = get_connector(g_drm.drmFD, DRM_MODE_CONNECTOR_DVID);

        if (!g_drm.hdmi_connector) {
            LOGE("%s: fail to get connector, error is %s", __func__, strerror(errno));
            goto error;
        }
        hdmi_connector = g_drm.hdmi_connector;

        if (hdmi_connector->connection != DRM_MODE_CONNECTED) {
            LOGE("%s: hdmi is disconnect, error is %s, %d, %d", __func__, strerror(errno), g_drm.drmFD, errno);
            goto error;
        }

        index = select_hdmi_mode(hdmi_connector);
        if (index == -1) {
            LOGE("%s: select hdmi mode fail, %d", __func__, false);
            return false;
        }
        width = hdmi_connector->modes[index].hdisplay;
        height = hdmi_connector->modes[index].vdisplay;

//        LOGI("%s: w: %d, h: %d", __func__, width, height);
        if (0 != setup_hdmi_fb(width, height, EXT_MODE)) {
            LOGE("%s: setup HDMI fb fail", __func__);
            goto error;
        }
        hdmi_fb_id = g_drm.einfo.fb_id[0];
        hdmi_crtc_id = get_crtc_id(g_drm.drmFD, DRM_MODE_ENCODER_TMDS);

        /* Mode setting with the largest resolution (e.g. 1920x1080) */
        int tmp = drmModeSetCrtc(g_drm.drmFD, hdmi_crtc_id, hdmi_fb_id, 0, 0,
                             &hdmi_connector->connector_id, 1, &hdmi_connector->modes[index]);
        if (tmp) {
            LOGE("%s: fail to set CRTC", __func__);
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
    }

    set_hdmi_state(HDMI_MODE_EXT_VIDEO);
    return ret;

error:
    cleanup_hdmi_fb(g_drm.drmFD, EXT_MODE);
    ret = false;
    return ret;
}

static int hdmi_rm_notifier_handler(int cmd, void *data)
{
    bool ret = false;
    unsigned int i = 0;

    HDMI_mem_info_t *hdmi_info = (HDMI_mem_info_t *)data;

    if (!hdmi_info->flag_recursion) {
        pthread_mutex_lock(&g_drm.mtx);
    }

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
        /*clear ext video mode*/
        cleanup_hdmi_fb(g_drm.drmFD, EXT_MODE);
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
        /*clear ext video mode*/
        cleanup_hdmi_fb(g_drm.drmFD, EXT_MODE);
        break;
    }


    if (!hdmi_info->flag_recursion) {
        pthread_mutex_unlock(&g_drm.mtx);
    }

    if (!ret) {
        LOGE("%s: fail to notify handler", __func__);
        return -1;
    }
    return 0;
}

static int setModeIndex( int index ) {
    int scale = 0;
    int ret = 0;

    LOGV("%s: set mode index %d", __func__, index);

    /* notify gralloc to use set scale step*/
    ret = g_drm.ctx->notify_gralloc(CMD_SET_MODE_INDEX, &index);

    LOGI("%s: %d", __func__, ret);
    return ret;
}

static int setScaling(int scaling_val)
{
    int scale = 0;
    int ret = 0;

    g_drm.configInfo.scaleType = scaling_val;

    scale = g_drm.configInfo.scaleType;
    LOGV("%s: scale mode %d", __func__, scale);

    /* notify gralloc to use set scale step*/
    ret = g_drm.ctx->notify_gralloc(CMD_SET_SCALE_TYPE, &scale);

    return ret;

}

static int getScaling()
{
    int i = 0;
    int ret = 0;

    return g_drm.configInfo.scaleType;
}

static bool setScaleStep(int hValue, int vValue)
{
    int ret = 0;
    int i = 0;
    int value = 0;
    int scale = 0;

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

    /* notify gralloc to use set scale step*/
    ret = g_drm.ctx->notify_gralloc(CMD_SET_SCALE_STEP, &value);

    LOGI("%s: %d", __func__, ret);
    return ret;
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
    g_drm.configInfo.setMode = 0;
    g_drm.configInfo.edidChange = 0;
    g_drm.configInfo.setModeIndex = -1;
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
    LOGI("%s: %d", __func__, ret);
    pthread_mutex_unlock(&g_drm.mtx);
    return ret;
}

void drm_cleanup()
{
    pthread_mutex_lock(&g_drm.mtx);
    if (g_drm.drmFD >= 0) {
        cleanup_hdmi_fb(g_drm.drmFD, EXT_MODE);
        cleanup_hdmi_fb(g_drm.drmFD, CLONE_MODE);
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


/* drm_hdmi_bGetCrtc() returns TRUE if the drmModeGetCrtc() returns non-zero */
/* which indicates that HDMI is on, otherwise returns FALSE which means that */
/* HDMI is off. */
bool drm_hdmi_bGetCrtc()
{
    int i = 0;
    bool ret = false;
    drmModeConnector *hdmi_connector = NULL;
    drmModeCrtc *hdmi_crtc = NULL;
    drmModePropertyPtr hdmi_props = NULL;

    pthread_mutex_lock(&g_drm.mtx);
    if (g_drm.drmFD < 0)
        ret = setup_drm();
    else
        ret = true;
    if (ret) {
        if (g_drm.hdmi_connector == NULL)
            g_drm.hdmi_connector = get_connector(g_drm.drmFD, DRM_MODE_CONNECTOR_DVID);

        if (!g_drm.hdmi_connector) {
            LOGE("%s: fail to get connector", __func__);
            ret = false;
            goto End;
        }

        hdmi_connector = g_drm.hdmi_connector;
        if (hdmi_connector->connection != DRM_MODE_CONNECTED) {
            LOGE("%s: HDMI is disconnect, error is %s", __func__, strerror(errno));
            ret = true;
            goto End;
        }

        for (i = 0, ret = false ;
             (i < hdmi_connector->count_props) && (ret == false) ; i++) {
            hdmi_props = drmModeGetProperty(g_drm.drmFD, hdmi_connector->props[i]);
            if (!strcmp(hdmi_props->name, "DPMS")) {
                if (hdmi_connector->prop_values[i] == DRM_MODE_DPMS_OFF) {
                    ret = false;
                    LOGV("%s: fail to get dpms %d,0x%llx", __func__, i, hdmi_connector->prop_values[i]);
                } else {
                    hdmi_crtc = get_crtc(g_drm.drmFD, DRM_MODE_ENCODER_TMDS);
                    if (hdmi_crtc) {
                        ret = true;
                        drmModeFreeCrtc(hdmi_crtc);
                    } else {
                        ret = false;
                        LOGV("%s: fail to get crtc 0x%p", __func__, hdmi_crtc);
                    }
                }
            }
            drmModeFreeProperty(hdmi_props);
        }
    }
End:
    LOGI("%s: %d", __func__, ret);
    pthread_mutex_unlock(&g_drm.mtx);
    return ret;
}

/* drm_hdmi_setHdmiVideoOn() returns TRUE if HDMI was found and turns on HDMI. */
bool drm_hdmi_setHdmiVideoOn()
{
    int i = 0;
    bool ret = false;
    struct drm_psb_disp_ctrl dp_ctrl;

    pthread_mutex_lock(&g_drm.mtx);

    if (g_drm.drmFD < 0)
        ret = setup_drm();
    else
        ret = true;

    if (ret) {
        ret = false;
        /* set HDMI display off */
        memset(&dp_ctrl, 0, sizeof(dp_ctrl));
        dp_ctrl.cmd = DRM_PSB_DISP_PLANEB_ENABLE;
        dp_ctrl.u.data = HDMI_FORCE_VIDEO_ON_OFF;
        ret = drmCommandWriteRead(g_drm.drmFD, DRM_PSB_HDMI_FB_CMD, &dp_ctrl, sizeof(dp_ctrl));
        if (ret)
            LOGE("HDMI set display on failed : %d", ret);
        else
            ret = true;
    }

    LOGI("%s: %d", __func__, ret);
    pthread_mutex_unlock(&g_drm.mtx);
    return ret;
}

/* drm_hdmi_setHdmiPowerOff() returns TRUE if HDMI power was successfully turned off */
bool drm_hdmi_setHdmiPowerOff()
{
    bool ret = false;
    struct drm_psb_disp_ctrl dp_ctrl;

    pthread_mutex_lock(&g_drm.mtx);
    if (g_drm.drmFD < 0)
        ret = setup_drm();
    else
        ret = true;

    if (ret) {
        ret = false;
        /*when hdmi disconnected , set the power state lsland down */
        memset(&dp_ctrl, 0, sizeof(dp_ctrl));
        dp_ctrl.cmd = DRM_PSB_HDMI_OSPM_ISLAND_DOWN;
        ret = drmCommandWriteRead(g_drm.drmFD, DRM_PSB_HDMI_FB_CMD, &dp_ctrl, sizeof(dp_ctrl));
        if (ret)
            LOGE("%s: fail to set power state lsland down, %d", __func__, ret);
        else
            ret = true;
    }

    LOGI("%s: %d", __func__, ret);
    pthread_mutex_unlock(&g_drm.mtx);

    return ret;
}

/* drm_hdmi_setHdmiVideoOff() returns TRUE if HDMI was found and turns off HDMI. */
bool drm_hdmi_setHdmiVideoOff()
{
    bool ret = false;
    struct drm_lnc_video_getparam_arg arg;
    struct drm_psb_disp_ctrl dp_ctrl;

    pthread_mutex_lock(&g_drm.mtx);
    if (g_drm.drmFD < 0)
        ret = setup_drm();
    else
        ret = true;

    if (ret) {
        ret = false;
        /* set HDMI display on */
        memset(&dp_ctrl, 0, sizeof(dp_ctrl));
        dp_ctrl.cmd = DRM_PSB_DISP_PLANEB_DISABLE;
        dp_ctrl.u.data = HDMI_FORCE_VIDEO_ON_OFF;
        ret = drmCommandWriteRead(g_drm.drmFD, DRM_PSB_HDMI_FB_CMD, &dp_ctrl, sizeof(dp_ctrl));
        if (ret)
            LOGE("HDMI set display off failed : %d", ret);
        else
            ret = true;
    }

    LOGI("%s: %d", __func__, ret);
    pthread_mutex_unlock(&g_drm.mtx);

    return ret;
}

#define EDID_BLOCK_SIZE         128

int drm_hdmi_connectStatus()
{
    drmModeConnector *hdmi_connector = NULL;
    int ret = 0;
    static char last_product_inf[8] ={0,0,0,0,0,0,0,0};

    pthread_mutex_lock(&g_drm.mtx);
    if (g_drm.drmFD < 0) {
        ret = setup_drm();
        if (ret == 0) {
            ret = 0;
            goto End;
        }
    }

    /* release hdmi_connector before getting connectStatus */
    if (g_drm.hdmi_connector)
        drmModeFreeConnector(g_drm.hdmi_connector);

    g_drm.hdmi_connector = NULL;

    if (g_drm.hdmi_connector == NULL)
        g_drm.hdmi_connector = get_connector(g_drm.drmFD, DRM_MODE_CONNECTOR_DVID);

    if (!g_drm.hdmi_connector) {
        LOGE("%s: fail to get connector", __func__);
        ret = 0;
        goto End;
    }

    hdmi_connector = g_drm.hdmi_connector;

    if (hdmi_connector->connection != DRM_MODE_CONNECTED)
        ret = 0;
    else {
        // Read EDID, and check whether it's HDMI or DVI interface
        int i, j, k;
        drmModePropertyPtr props = NULL;
        uint64_t* edid = NULL;
        drmModePropertyBlobPtr edidBlob = NULL;
        char* edid_binary = NULL;

        for (i = 0; i < hdmi_connector->count_props; i++) {
            props = drmModeGetProperty(g_drm.drmFD, hdmi_connector->props[i]);
            if (!props)
                continue;

            if (props->name && !strcmp(props->name, "EDID")) {
                ret = 2; //DVI
                edid = &hdmi_connector->prop_values[i];
                edidBlob = drmModeGetPropertyBlob(g_drm.drmFD, *edid);
                if (edidBlob == NULL || edidBlob->length < EDID_BLOCK_SIZE) {
                    LOGE("drmModeGetPropertyBlob returns invalid EDID Blob.");
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
                        g_drm.configInfo.edidChange =1;
                    }
                }

                /*search VSDB in extend  edid */
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
        LOGI("EDID Product Info edidChange: %d\n", g_drm.configInfo.edidChange);
    }
End:
    pthread_mutex_unlock(&g_drm.mtx);
    LOGI("%s: %d", __func__, ret);

    if (g_drm.configInfo.edidChange == 1) {
        g_drm.configInfo.setMode = 0;
        g_drm.configInfo.setModeIndex = -1;
    }

    return ret;
}


bool drm_hdmi_getHdmiMode(int *pMode, int *pWidth, int *pHeight)
{
    int i = 0;
    drmModeConnector *hdmi_connector = NULL;
    drmModePropertyPtr hdmi_props = NULL;
    drmModeCrtc *hdmi_crtc = NULL;
    bool ret = false;

    if (!pMode) return false;
    pthread_mutex_lock(&g_drm.mtx);
    if (g_drm.drmFD < 0)
        ret = setup_drm();
    else
        ret = true;

    if (ret) {
        if (g_drm.hdmi_connector == NULL)
            g_drm.hdmi_connector = get_connector(g_drm.drmFD, DRM_MODE_CONNECTOR_DVID);

        if (!g_drm.hdmi_connector) {
            LOGE("%s: fail to get connector", __func__);
            ret = false;
            goto End;
        }

        hdmi_connector = g_drm.hdmi_connector;

        if (hdmi_connector->connection != DRM_MODE_CONNECTED) {
            *pMode = HDMI_MODE_OFF;
            ret = false;
            goto End;
        }

        for (i = 0, ret = false ;
             (i < hdmi_connector->count_props) && (ret == false) ; i++) {
            hdmi_props = drmModeGetProperty(g_drm.drmFD, hdmi_connector->props[i]);
            if (!strcmp(hdmi_props->name, "DPMS")) {
                if (hdmi_connector->prop_values[i] == DRM_MODE_DPMS_OFF) {
                    *pMode = HDMI_MODE_OFF;
                } else {
                    hdmi_crtc = get_crtc(g_drm.drmFD, DRM_MODE_ENCODER_TMDS);
                    if (!hdmi_crtc) {
                        /* No CRTC attached to HDMI. */
                        *pMode = HDMI_MODE_OFF;
                        drmModeFreeProperty(hdmi_props);
                        break;
                    }

                    *pMode = g_drm.curMode;

                    *pWidth = hdmi_crtc->mode.hdisplay;
                    *pHeight = hdmi_crtc->mode.vdisplay;

                    drmModeFreeCrtc(hdmi_crtc);
                    break;
                }
            }
            drmModeFreeProperty(hdmi_props);
        }
    }

End:
    pthread_mutex_unlock(&g_drm.mtx);
    LOGI("%s: %d", __func__, ret);
    return ret;
}

void drm_hdmi_onHdmiDisconnected(void)
{
    pthread_mutex_lock(&g_drm.mtx);
    /*notify disonnect message*/
    if (g_drm.ctx->notify_gralloc)
        g_drm.ctx->notify_gralloc(CMD_HDMI_DISCONNECTED, NULL);
    /*clear memory*/
    cleanup_hdmi_fb(g_drm.drmFD, EXT_MODE);
    cleanup_hdmi_fb(g_drm.drmFD, CLONE_MODE);
    /* release hdmi_connector when disconnected */
    if (g_drm.hdmi_connector)
        drmModeFreeConnector(g_drm.hdmi_connector);
    g_drm.hdmi_connector = NULL;
    pthread_mutex_unlock(&g_drm.mtx);

}

bool drm_hdmi_setHdmiMode(int mode)
{
    int i = 0;
    bool ret = false;
    pthread_mutex_lock(&g_drm.mtx);

    switch (mode) {
    case HDMI_MODE_CLONE:
        ret = set_clone_mode();
        g_drm.curMode = mode;
        break;

    case HDMI_MODE_EXT_VIDEO:
        ret = set_ext_video_mode();
        g_drm.curMode = mode;
        break;

    case HDMI_MODE_EXT_DESKTOP:
        LOGE("%s: not support mode %d", __func__, mode);
        ret = false;
        break;

    default:
        LOGE("%s: not support mode %d", __func__, mode);
        ret = false;
        break;
    }
    pthread_mutex_unlock(&g_drm.mtx);
    LOGI("%s: %d %d", __func__, ret, mode);
    return ret;
}

void drm_mipi_setMode(int mode)
{
    int i;

    drmModeEncoder *mipi_encoder = NULL;
    drmModeConnector *mipi_connector = NULL;
    drmModePropertyPtr props = NULL;

    pthread_mutex_lock(&g_drm.mtx);

    mipi_connector = get_connector(g_drm.drmFD, DRM_MODE_CONNECTOR_MIPI);
    if (!mipi_connector) {
        LOGE("%s: fail to get connector", __func__);
        goto exit;
    }

    if (mipi_connector->connection != DRM_MODE_CONNECTED) {
        LOGE("%s: hdmi is disconnect %d %d", __func__,
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
exit:
    if (mipi_connector)
        drmModeFreeConnector(mipi_connector);
    pthread_mutex_unlock(&g_drm.mtx);
    LOGI("%s", __func__);
    return;
}

int drm_hdmi_getModeInfo(int *pWidth, int *pHeight, int *pRefresh, int *pInterlace)
{
    int i = 0, j = 0, iCount = 0;
    int valid_mode_count = 0;
    bool ret = false;
    drmModeConnector *hdmi_connector = NULL;

    pthread_mutex_lock(&g_drm.mtx);
    if (g_drm.drmFD < 0)
        ret = setup_drm();
    else
        ret = true;

    if (ret) {
        ret = false;
        if (g_drm.hdmi_connector == NULL)
            g_drm.hdmi_connector = get_connector(g_drm.drmFD, DRM_MODE_CONNECTOR_DVID);

        if (!g_drm.hdmi_connector) {
            iCount = -1;
            goto End;
        }
        hdmi_connector = g_drm.hdmi_connector;
    }

    if (!hdmi_connector) {
        LOGE("failed to get hdmi connector");
        iCount = -1;
        goto End;
    }

    if (hdmi_connector->connection != DRM_MODE_CONNECTED) {
        LOGE("%s: fail, hdmi is disconnected, error is %s", __func__, strerror(errno));
        iCount = -1;
        goto End;
    }

    iCount = hdmi_connector->count_modes;

    /* get resolution of each mode */
    for (i = 0; i < iCount; i++) {
        unsigned int temp_hdisplay = hdmi_connector->modes[i].hdisplay;
        unsigned int temp_vdisplay = hdmi_connector->modes[i].vdisplay;
        unsigned int temp_refresh = hdmi_connector->modes[i].vrefresh;
        unsigned int temp_flags = hdmi_connector->modes[i].flags;

        /* re-traverse the connector mode list to see if there is
         * same resolution and refresh. The same mode will not be
         * counted into valid mode.
         */
        j = i;

        while ((j--) >= 0) {
            if(temp_hdisplay == hdmi_connector->modes[j].hdisplay
                && temp_vdisplay == hdmi_connector->modes[j].vdisplay
                && temp_refresh == hdmi_connector->modes[j].vrefresh)
                break;
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
            }

            valid_mode_count++;
        }
    }

    iCount = valid_mode_count;

End:
    pthread_mutex_unlock(&g_drm.mtx);
    // return number of timing info except mipi.
    return iCount;
}

#ifdef HDMI_COMPLIANCE
struct vicInfo {
    int vic;
    int width;
    int height;
    int refresh;
    int interlace;
};
#define MAX_VIC_ENTRY 8
static struct vicInfo vicTable[MAX_VIC_ENTRY] = {
    {34, 1920, 1080, 30, 0},
    {4, 1280, 720, 60, 0},
    {19, 1280, 720, 50, 0},
    {2, 720, 480, 60, 0},
    {3, 720, 480, 60, 0},
    {17, 720, 576, 50, 0},
    {18, 720, 576, 50, 0},
    {1, 640, 480, 60, 0}
};
#endif

bool drm_hdmi_setModeInfo(int width, int height, int refresh, int interlace, int ratio)
{
    int i = 0, index = 0;
    bool ret = false;
    drmModeConnector *hdmi_connector = NULL;
    unsigned int hdmi_fb_id = 0;
    unsigned int mipi_fb_id = 0;
    uint32_t hdmi_crtc_id = 0;
    drmModeModeInfoPtr set_mode = NULL;

    LOGV("Entered drm_hdmi_setModeInfo with width = %d, height = %d, refresh = %d\n", width, height, refresh);

    pthread_mutex_lock(&g_drm.mtx);
    if (g_drm.cinfo.mode_info) {
        free(g_drm.cinfo.mode_info);
        g_drm.cinfo.mode_info = NULL;
    }
    if (g_drm.drmFD < 0)
        ret = setup_drm();
    else
        ret = true;

#ifdef HDMI_COMPLIANCE
    struct drm_lnc_video_getparam_arg arg;
    if (g_drm.ioctlOffset) {
      /* Record the hdmi vic requested in kernel.
       * Do this even if its not a vic request, so that
       * any previous vic state used for compliance is
       * cleared
       */
      arg.key = OTM_HDMI_SET_HDMI_MODE_VIC;
      arg.value = (uint64_t)(width);
      drmCommandWriteRead(g_drm.drmFD, g_drm.ioctlOffset, &arg, sizeof(arg));
    }

    /* check if the information passed is VIC value */
    if (height == 0 && width != 0) {
        /* Now mode set per the vic value info */
        int j = 0;
        for (j = 0; j < MAX_VIC_ENTRY; ++j) {
            if (vicTable[j].vic == width)
                break;
        }

        if (j == MAX_VIC_ENTRY) {
            LOGE("No VIC mode found for requested vic = %d\n", width);
            ret = false;
            goto End;
        }

        width = vicTable[j].width;
        height = vicTable[j].height;
        refresh = vicTable[j].refresh;
        interlace = vicTable[j].interlace;
        LOGV("Found VIC mode %d: %dx%d@%d\n", vicTable[j].vic, width, height, refresh);
    }
#endif

    if(g_drm.configInfo.setMode == 0)
        g_drm.configInfo.setMode = 1;
    mipi_fb_id = get_fb_id(g_drm.drmFD, DRM_MODE_ENCODER_MIPI);
    if (ret) {
        if (g_drm.hdmi_connector == NULL)
            g_drm.hdmi_connector = get_connector(g_drm.drmFD, DRM_MODE_CONNECTOR_DVID);

        if (!g_drm.hdmi_connector) {
            ret = false;
            goto End;
        }
        hdmi_connector = g_drm.hdmi_connector;
    }
    if (!hdmi_connector) {
        LOGE("failed to get hdmi connector");
        return -1;
    }
    if (hdmi_connector->connection != DRM_MODE_CONNECTED) {
        LOGV("%s: hdmi is disconnect, error is %s", __func__, strerror(errno));
        ret = true;
        goto End;
    }

    hdmi_fb_id = mipi_fb_id;
    hdmi_crtc_id = get_crtc_id(g_drm.drmFD, DRM_MODE_ENCODER_TMDS);

    /* find seting info in edid */
    set_mode = (drmModeModeInfo*)malloc(sizeof(drmModeModeInfo));
    for (; i < hdmi_connector->count_modes; i++) {
        bool temp_flags = false;
        if (hdmi_connector->modes[i].flags & DRM_MODE_FLAG_INTERLACE)
            temp_flags = 1;
        else
            temp_flags = 0;
        LOGV("Mode avail: %dx%d@%d\n", hdmi_connector->modes[i].hdisplay, hdmi_connector->modes[i].vdisplay, hdmi_connector->modes[i].vrefresh);
        /* match width, hdisplay, interlace*/
        if ((width == hdmi_connector->modes[i].hdisplay) && (temp_flags == interlace) &&
            (height == hdmi_connector->modes[i].vdisplay)) {
            g_drm.configInfo.setModeIndex = i;
            /* hardcode setting refresh is 30Hz */
            if (refresh == 30) {
                memcpy(set_mode, &hdmi_connector->modes[i], sizeof(drmModeModeInfo));
                set_mode->vrefresh = 30;
                float fRatio = (float)30/(float)hdmi_connector->modes[i].vrefresh;
                set_mode->clock *= fRatio;
                g_drm.cinfo.mode_info = set_mode;
                break;
            }
            /* edit setting */
            else if ((uint32_t)refresh == hdmi_connector->modes[i].vrefresh) {
                memcpy(set_mode, &hdmi_connector->modes[i], sizeof(drmModeModeInfo));
                g_drm.cinfo.mode_info = set_mode;
                break;
            }
        }
    }

    LOGV("Mode Selected: %dx%d@%d\n", set_mode->hdisplay, set_mode->vdisplay, set_mode->vrefresh);
    /* if hardcode resolution not exit in edid, hardcode fill */
    if (!g_drm.cinfo.mode_info) {
        g_drm.cinfo.mode_info = set_mode;
        if (width == 1920 && height == 1080) {
            set_mode->clock = 145000/2;
            set_mode->hdisplay = 1920;
            set_mode->hsync_start = 2008;
            set_mode->hsync_end = 2052;
            set_mode->htotal = 2200;
            set_mode->hskew = 0;
            set_mode->vdisplay = 1080;
            set_mode->vsync_start = 1084;
            set_mode->vsync_end = 1089;
            set_mode->vtotal = 1125;
            set_mode->vscan = 0;
            set_mode->vrefresh = 30;
            set_mode->flags = 0x5;
            set_mode->type = 0x40;
            strcpy(set_mode->name, "1920*1080");
        } else if (width == 1280 && height == 720) {
            set_mode->clock = 74250/2;
            set_mode->hdisplay = 1280;
            set_mode->hsync_start = 1390;
            set_mode->hsync_end = 1430;
            set_mode->htotal = 1650;
            set_mode->hskew = 0;
            set_mode->vdisplay = 720;
            set_mode->vsync_start = 725;
            set_mode->vsync_end = 730;
            set_mode->vtotal = 750;
            set_mode->vscan = 0;
            set_mode->vrefresh = 30;
            set_mode->flags = 0x5;
            set_mode->type = 0x40;
            strcpy(set_mode->name, "1280*720");
        } else if (width == 720 && height ==480) {
            set_mode->clock = 27000/2;
            set_mode->hdisplay = 720;
            set_mode->hsync_start = 736;
            set_mode->hsync_end = 798;
            set_mode->htotal = 858;
            set_mode->hskew = 0;
            set_mode->vdisplay = 480;
            set_mode->vsync_start = 489;
            set_mode->vsync_end = 492;
            set_mode->vtotal = 525;
            set_mode->vscan = 0;
            set_mode->vrefresh = 30;
            set_mode->flags = 0xa;
            set_mode->type = 0x40;
            strcpy(set_mode->name, "720*480");
        } else {
            free(set_mode);
            set_mode = NULL;
            g_drm.cinfo.mode_info = NULL;
            LOGE("%s: wrong timing %dx%d", __func__, width, height);
            ret = false;
            goto End;
        }
    }

    g_drm.cinfo.aspect_ratio = ratio;
    LOGV("%s: <clock: %d, flags: %d, hdisplay: %d, vdisplay: %d, vrefresh: %d, ratio: %d>",
         __func__, set_mode->clock, set_mode->flags,
         set_mode->hdisplay, set_mode->vdisplay, set_mode->vrefresh,ratio);

    /* notify gralloc to use set mode */
    if (g_drm.ctx->notify_gralloc) {
        //g_drm.ctx->notify_gralloc(CMD_SET_MODE, NULL);
        int flag_recursion = 1;
        ret = g_drm.ctx->notify_gralloc(CMD_CLONE_MODE, &flag_recursion);
    }
    if (ret) {
        ret = false;
        free(set_mode);
        set_mode = NULL;
        g_drm.cinfo.mode_info = NULL;
    } else
        ret = true;

End:
    pthread_mutex_unlock(&g_drm.mtx);
    LOGI("%s: %d", __func__, ret);
    return ret;
}

bool drm_hdmi_setScaling(int scaling_val)
{
    int ret = 0;
    HDMI_config_info hdmiConfig = g_drm.configInfo;

    pthread_mutex_lock(&g_drm.mtx);
    hdmiConfig.scaleType = scaling_val;
    ret = setScaling(scaling_val);
    pthread_mutex_unlock(&g_drm.mtx);
    LOGI("%s: %d", __func__, ret);

    return ret == 1 ? true : false;
}

int drm_hdmi_getScaling()
{
    int ret = 0;
    pthread_mutex_lock(&g_drm.mtx);
    ret = getScaling();
    pthread_mutex_unlock(&g_drm.mtx);
    LOGI("%s: %d", __func__, ret);
    return ret;
}

bool drm_hdmi_setScaleStep(int hValue, int vValue)
{
    int ret = 0;
    pthread_mutex_lock(&g_drm.mtx);
    setScaleStep(hValue, vValue);
    pthread_mutex_unlock(&g_drm.mtx);
    LOGI("%s: %d", __func__, ret);
    return ret;

}

int drm_hdmi_getDeviceChange()
{
    int ret = 0;
    if (g_drm.configInfo.edidChange != 0 ) {
        ret = 1;
        g_drm.configInfo.edidChange = 0;
    }

    return ret;
}

int drm_hdmi_notify_audio_hotplug(bool plugin)
{
    bool ret = false;
    int tmp;
    struct drm_psb_disp_ctrl dp_ctrl;

    pthread_mutex_lock(&g_drm.mtx);

    if (g_drm.drmFD < 0)
        ret = setup_drm();
    else
        ret = true;

    if (ret) {
        ret = false;
        /* set HDMI display off */
        memset(&dp_ctrl, 0, sizeof(dp_ctrl));
        dp_ctrl.cmd = DRM_PSB_HDMI_NOTIFY_HOTPLUG_TO_AUDIO;
        dp_ctrl.u.data = (plugin == true ? 1 : 0);
        tmp = drmCommandWriteRead(g_drm.drmFD, DRM_PSB_HDMI_FB_CMD, &dp_ctrl, sizeof(dp_ctrl));
        if (tmp)
            LOGE("HDMI notify audio plug event failed : %d", ret);
        else
            ret = true;
    }

    LOGI("%s: %d", __func__, ret);
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

