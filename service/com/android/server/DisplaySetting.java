 /* Copyright (C) 2010 The Android Open Source Project
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

package com.android.server;

import java.io.IOException;
import android.util.Slog;


class DisplaySetting {
    private final String TAG = "MultiDisplay-DisplaySetting";
    private final boolean LOG = true;

    // mode setting policy
    public static final int HDMI_ON_NOT_ALLOWED  = 0;
    public static final int HDMI_ON_ALLOWED = 1;
    public static final int MIPI_OFF_NOT_ALLOWED = 2;
    public static final int MIPI_OFF_ALLOWED = 3;

    // display mode
    // bit 0: MIPI on/off
    // bit 3: HDMI connect status
    // bit 4: HDMI on/off
    public static final int MIPI_MODE_BIT           = 0x1;
    public static final int HDMI_CONNECT_STATUS_BIT = 0x1 << 3;
    public static final int HDMI_MODE_BIT           = 0x1 << 4;

    // MDS message type
    public static final int MDS_MODE_CHANGE = 0;
    public static final int MDS_ORIENTATION_CHANGE = 1;

    // MDS display capability
    public static final int HW_SUPPORT_HDMI = 0x1;
    public static final int HW_SUPPORT_WIDI = 0x1 << 1;


    private static onMdsMessageListener mListener = null;
    private static boolean mInit = false;

    private native boolean native_InitMDSClient();
    private native boolean native_DeInitMDSClient();
    private native boolean native_setModePolicy(int policy);
    private native int     native_getMode();
    private native boolean native_setHdmiPowerOff();
    private native boolean native_notifyHotPlug();
    private native int     native_getHdmiTiming(int width[],
                                                int height[], int refresh[],
                                                int interlace[], int ratio[]);
    private native boolean native_setHdmiTiming(int width, int height,
                            int refresh, int interlace, int ratio);
    private native int     native_getHdmiInfoCount();
    private native boolean native_setHdmiScaleType(int Type);
    private native boolean native_setHdmiScaleStep(int Step, int Orientation);
    private native int     native_getHdmiDeviceChange();
    private native int     native_getDisplayCapability();

    public DisplaySetting() {
        if (mInit == false) {
            if (LOG) Slog.i(TAG, "Create a new DisplaySetting");
            native_InitMDSClient();
            mInit = true;
        }
    }

    public boolean setModePolicy(int policy) {
       return native_setModePolicy(policy);
    }

    public int getMode() {
        return native_getMode();
    }

    public boolean notifyHotPlug() {
        return native_notifyHotPlug();
    }

    public boolean setHdmiPowerOff() {
        return native_setHdmiPowerOff();
    }

    public void onMdsMessage(int event, int value) {
        if (mListener != null) {
            mListener.onMdsMessage(event, value);
        }
    }

    public void setMdsMessageListener(onMdsMessageListener listener) {
        if (mListener == null) {
            mListener = listener;
        }
    }

    public int getHdmiTiming(int width[], int height[],
                             int refresh[], int interlace[], int ratio[]) {
        return native_getHdmiTiming(width, height,
                                    refresh, interlace, ratio);
    }

    public boolean setHdmiTiming(int width, int height,
                        int refresh, int interlace, int ratio) {
        return native_setHdmiTiming(width, height,
                            refresh, interlace, ratio);
    }

    public interface onMdsMessageListener {
        boolean onMdsMessage(int event, int value);
    }

    public int getHdmiInfoCount() {
        return native_getHdmiInfoCount();
    }

    public boolean setHdmiScaleType(int Type) {
        return native_setHdmiScaleType(Type);
    }

    public boolean setHdmiScaleStep(int Step, int Orientation) {
        return native_setHdmiScaleStep(Step, Orientation);
    }

    public int getHdmiDeviceChange() {
        return native_getHdmiDeviceChange();
    }

    public int getDisplayCapability() {
        return native_getDisplayCapability();
    }
}

