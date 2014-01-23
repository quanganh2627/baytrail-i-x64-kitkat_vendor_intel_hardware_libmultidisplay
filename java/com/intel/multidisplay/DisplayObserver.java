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

package com.intel.multidisplay;

import android.app.ActivityManager;
import android.app.ActivityManager.RunningTaskInfo;

import android.content.BroadcastReceiver;
import android.content.IntentFilter;
import android.os.Bundle;
import android.app.ActivityManagerNative;
import android.content.Context;
import android.content.Intent;
import android.os.Handler;
import android.os.Message;
import android.os.PowerManager;
import android.os.PowerManager.WakeLock;
import android.os.UEventObserver;
import android.util.Slog;
import android.media.AudioManager;
import android.telephony.TelephonyManager;
import android.provider.Settings;
import android.content.ContentResolver;
import android.database.ContentObserver;

import android.hardware.display.DisplayManager;
import android.hardware.display.WifiDisplayStatus;
import java.io.FileReader;
import java.io.FileNotFoundException;
import java.util.List;
import com.intel.multidisplay.DisplaySetting;

import android.view.MotionEvent;
import android.view.WindowManagerPolicy;
import android.view.WindowManagerPolicy.PointerEventListener;

/**
 * <p>DisplayObserver.
 */
public class DisplayObserver {
    private static final String TAG = "MultiDisplay";
    private static final boolean LOG = true;

    // Assuming unplugged (i.e. 0) for initial state, assign initial state in init() below.
    private final int ROUTE_TO_SPEAKER = 0;
    private final int ROUTE_TO_HDMI    = 1;
    private final int HDMI_HOTPLUG     = 2;
    private int mAudioRoute =  ROUTE_TO_SPEAKER;
    private int mPreAudioRoute = -1;
    private String mHDMIName;
    private int mEdidChange = 0;
    private int mDisplayBoot = 1;
    private int mHoriRatio = 5;
    private int mVertRatio = 5;

    // indicate HDMI connect state
    private int mHDMIConnected = 0;
    private int mHDMIPlugEvent = 0;

    private Context mContext;
    private WakeLock mWakeLock;  // held while there is a pending route change
    private boolean mHasIncomingCall = false;
    private boolean mInCallScreenFinished = true;
    private DisplaySetting mDs;

    // WIDI
    private boolean mWidiConnected = false;

    //Message need to handle
    private final int HDMI_STATE_CHANGE = 0;
    private final int MSG_INPUT_TIMEOUT = 3;
    private final int MSG_START_MONITORING_INPUT = 4;
    private final int MSG_STOP_MONITORING_INPUT = 5;
    private final int INPUT_TIMEOUT_MSEC = 5000;

    private static final String HDMI_GET_INFO = "android.hdmi.GET_HDMI_INFO";
    private static final String HDMI_SET_INFO = "android.hdmi.SET_HDMI_INFO";
    private static final String HDMI_SERVER_GET_INFO = "HdmiObserver.GET_HDMI_INFO";
    private static final String HDMI_SET_SCALE= "android.hdmi.SET.HDMI_SCALE";
    private static final String HDMI_SET_STEP_SCALE= "android.hdmi.SET.HDMI_STEP_SCALE";
    private static final String HDMI_Get_DisplayBoot = "android.hdmi.GET_HDMI_Boot";
    private static final String HDMI_Set_DisplayBoot = "HdmiObserver.SET_HDMI_Boot";

    // Broadcast receiver for device connections intent broadcasts
    private final BroadcastReceiver mReceiver = new DisplayObserverBroadcastReceiver();

    private final class DisplayObserverTouchEventListener implements PointerEventListener {
        private DisplayObserver mObserver;

        public DisplayObserverTouchEventListener(DisplayObserver observer) {
            mObserver = observer;
        }

        @Override
        public void onPointerEvent(MotionEvent motionEvent) {
            mObserver.onInputEvent();
        }
    }

    private DisplayObserverTouchEventListener mTouchEventListener = null;
    private WindowManagerPolicy.WindowManagerFuncs mWindowManagerFuncs;

    public void onInputEvent() {
        if (!mHandler.hasMessages(MSG_INPUT_TIMEOUT)) {
            logv("input is active");
            mDs.updateInputState(true);
        } else {
            mHandler.removeMessages(MSG_INPUT_TIMEOUT);
        }
        mHandler.sendEmptyMessageDelayed(MSG_INPUT_TIMEOUT, INPUT_TIMEOUT_MSEC);
    }

    private void startMonitoringInput() {
        if (mTouchEventListener != null)
            return;
        if (mWindowManagerFuncs == null) {
            logv("invalid WindowManagerFuncs");
            return;
        }

        logv("start monitoring input");
        mTouchEventListener =
            new DisplayObserverTouchEventListener(this);
        if (mTouchEventListener == null) {
           logv("invalid input event receiver.");
           return;
        }
        mWindowManagerFuncs.registerPointerEventListener(
                mTouchEventListener);

        // start "input idle" count down
        mHandler.sendEmptyMessageDelayed(MSG_INPUT_TIMEOUT, INPUT_TIMEOUT_MSEC);
    }

    private void stopMonitoringInput() {
        if (mWindowManagerFuncs == null ||
                mTouchEventListener == null)
            return;

        logv("stop monitoring input");
        mWindowManagerFuncs.unregisterPointerEventListener(
                mTouchEventListener);
        mTouchEventListener = null;

        if (mHandler.hasMessages(MSG_INPUT_TIMEOUT)) {
            mHandler.removeMessages(MSG_INPUT_TIMEOUT);
        }
    }

    public DisplayObserver(Context context,
            WindowManagerPolicy.WindowManagerFuncs funcs) {
        if (funcs == null) {
            logv("invalid WindowManagerFuns, MDS couldn't handle input envent");
        }

        mContext = context;
        mWindowManagerFuncs = funcs;
        mDs = new DisplaySetting();
        IntentFilter intentFilter = new IntentFilter(TelephonyManager.ACTION_PHONE_STATE_CHANGED);
        intentFilter.addAction(HDMI_GET_INFO);
        intentFilter.addAction(HDMI_SET_INFO);
        intentFilter.addAction(HDMI_SET_SCALE);
        intentFilter.addAction(HDMI_SET_STEP_SCALE);
        intentFilter.addAction(HDMI_Get_DisplayBoot);
        intentFilter.addAction(DisplayManager.ACTION_WIFI_DISPLAY_STATUS_CHANGED);

        mContext.registerReceiver(mReceiver, intentFilter);
        PowerManager pm = (PowerManager)context.getSystemService(Context.POWER_SERVICE);
        mWakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "DisplayObserver");
        mWakeLock.setReferenceCounted(false);
        mDs.setMdsMessageListener(mListener);
        if ((mDs.getMode() & mDs.HDMI_CONNECTED_BIT) == mDs.HDMI_CONNECTED_BIT) {
            mHDMIConnected = 1;
            update("HOTPLUG", ROUTE_TO_HDMI);
        } else {
            mHDMIConnected = 0;
            update("HOTPLUG", ROUTE_TO_SPEAKER);
        }
    }

    protected void finalize() throws Throwable {
        stopMonitoringInput();
        mContext.unregisterReceiver(mReceiver);
        super.finalize();
    }

    private void logv(String s) {
        if (LOG) {
            Slog.v(TAG, s);
        }
    }

    DisplaySetting.onMdsMessageListener mListener =
                        new DisplaySetting.onMdsMessageListener() {
        public boolean onMdsMessage(int msg, int value) {
            if (msg == mDs.MDS_MSG_MODE_CHANGE) {
                boolean isHdmiConnected =
                    ((value & mDs.HDMI_CONNECTED_BIT) != 0);

                // start monitor input only when HDMI/Wireless Display
                // is connected AND the video is being played.
                if ((isHdmiConnected || (value & mDs.WIDI_CONNECTED_BIT) != 0) &&
                        ((value & mDs.VIDEO_ON_BIT) != 0)) {
                    mHandler.sendEmptyMessage(MSG_START_MONITORING_INPUT);
                } else {
                    mHandler.sendEmptyMessage(MSG_STOP_MONITORING_INPUT);
                }

                if ((mHDMIConnected == 0) && isHdmiConnected)
                    mHDMIConnected = 1;
                else if ((mHDMIConnected == 1) && !isHdmiConnected)
                    mHDMIConnected = 0;
                else
                    return true;
                if (mHDMIConnected == 1) {
                    if ((value & mDs.NEW_HDMI_DEVICE) != 0) {
                        mEdidChange = 1;
                    }
                }
                /// audio switch
                postNotifyHotplug(mHDMIConnected);
            }
            return true;
        };
    };

/*
    @Override
    public synchronized void onUEvent(UEventObserver.UEvent event) {
        if (event.toString().contains("HOTPLUG")) {
            logv("HDMI UEVENT: " + event.toString());
            int delay = 0;
            if (event.toString().contains("HOTPLUG_IN")) {
                delay = 0;
                mHDMIPlugEvent = 1;
            } else if (event.toString().contains("HOTPLUG_OUT")) {
                delay = 200;
                mHDMIPlugEvent = 0;
            } else
                return;
            mHandler.removeMessages(HDMI_HOTPLUG);
            Message msg = mHandler.obtainMessage(HDMI_HOTPLUG, mHDMIPlugEvent, 0);
            mHandler.sendMessageDelayed(msg, delay);
        }
    }
*/

    private synchronized void update(String newName, int newState) {
        // Retain only relevant bits
        int delay = 0;
        if (newState == mAudioRoute) {
            //logv("Same Audio output, Don't set");
            return;
        }
        logv("Audio output switches from " +
                (mAudioRoute == ROUTE_TO_SPEAKER ? "SPEAKER":"HDMI") +
                " to " + (newState == ROUTE_TO_SPEAKER ? "SPEAKER":"HDMI"));

        mHDMIName = newName;
        mPreAudioRoute = mAudioRoute;
        mAudioRoute = newState;
        if (newState == ROUTE_TO_SPEAKER)
            delay = 500;

        if (newState == ROUTE_TO_SPEAKER) {
            Intent intent = new Intent(AudioManager.ACTION_AUDIO_BECOMING_NOISY);
            mContext.sendBroadcast(intent);
        }
        mWakeLock.acquire();
        mHandler.sendMessageDelayed(mHandler.obtainMessage(HDMI_STATE_CHANGE,
                                    mAudioRoute,
                                    mPreAudioRoute,
                                    mHDMIName),
                                    delay);
    }

    private synchronized final void sendIntents(int cur, int prev, String name) {
        if (cur == ROUTE_TO_HDMI || (prev == ROUTE_TO_HDMI))
            sendIntent(1, cur, prev, name);
    }

    private final void sendIntent(int hdmi, int State, int prev, String Name) {
        //logv("State:" + State + " prev:" + prev + " Name:" + Name);
        //  Pack up the values and broadcast them to everyone
        Intent intent = new Intent(Intent.ACTION_HDMI_AUDIO_PLUG);
        intent.addFlags(Intent.FLAG_RECEIVER_REGISTERED_ONLY);
        intent.putExtra("state", State);
        intent.putExtra("name", Name);

        // Should we require a permission?
        ActivityManagerNative.broadcastStickyIntent(intent, Name, 0);
    }

    private final void preNotifyHotplug(int event) {
            /* set HDMI connect status per plug event */
            if (event == 0)
                 mHDMIConnected = 0;
            else
                 mHDMIConnected = 1;
    }

    private final void postNotifyHotplug(int event) {
        /* plug out event */
        if (event == 0) {
            update("HOTPLUG", ROUTE_TO_SPEAKER);
        } else {
            update("HOTPLUG", ROUTE_TO_HDMI);
        }
    }

    private final Handler mHandler = new Handler() {
        @Override
        public void handleMessage(Message msg) {
            //logv("handle message = " + (String)msg.obj);
            switch(msg.what) {
            case HDMI_STATE_CHANGE:
                sendIntents(msg.arg1, msg.arg2, (String)msg.obj);
                mWakeLock.release();
                break;
            case MSG_INPUT_TIMEOUT:
                logv("input is idle");
                mDs.updateInputState(false);
                break;
            case MSG_START_MONITORING_INPUT:
                startMonitoringInput();
                break;
            case MSG_STOP_MONITORING_INPUT:
                stopMonitoringInput();
                break;
            case HDMI_HOTPLUG:
                synchronized(this) {
                /* filter before msg which does not match with latest event */
                /*
                    if (mHDMIPlugEvent != msg.arg1)
                        return;

                    preNotifyHotplug(msg.arg1);

                    boolean ret = mDs.notifyHotPlug();
                    if (!ret) {
                        logv("fail to deal with hdmi hotlpug");
                        return;
                    }

                    postNotifyHotplug(msg.arg1);
                */
                }
                break;
            }
        }
    };

    private class DisplayObserverBroadcastReceiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (action.equals(TelephonyManager.ACTION_PHONE_STATE_CHANGED)) {
                if (TelephonyManager.EXTRA_STATE == null ||
                        TelephonyManager.EXTRA_STATE_RINGING == null)
                    return;
                String extras = intent.getStringExtra(TelephonyManager.EXTRA_STATE);
                if (extras == null)
                    return;
                if (extras.equals(TelephonyManager.EXTRA_STATE_RINGING)) {
                    mHasIncomingCall = true;
                    mInCallScreenFinished = false;
                    logv("Incoming call is initiated");
                    mDs.updatePhoneCallState(true);
                } else if (extras.equals(TelephonyManager.EXTRA_STATE_IDLE)) {
                    mHasIncomingCall = false;
                    mInCallScreenFinished = true;
                    logv("Call is terminated and Incallscreen disappeared");
                    mDs.updatePhoneCallState(false);
                }
            } else if (action.equals(HDMI_GET_INFO)) {
                // Handle HDMI_GET_INFO ACTION
                logv("HDMI is plugged " + (mHDMIConnected == 1 ? "in" : "out"));
                if (mHDMIConnected != 0) {
                    // Get Number of Timing Info
                    int Count = mDs.getHdmiInfoCount();
                    Intent outIntent = new Intent(HDMI_SERVER_GET_INFO);
                    outIntent.addFlags(Intent.FLAG_RECEIVER_REGISTERED_ONLY);
                    Bundle mBundle = new Bundle();

                    int[] arrWidth = new int[Count];
                    int[] arrHeight = new int[Count];
                    int[] arrRefresh = new int[Count];
                    int[] arrInterlace = new int[Count];
                    int[] arrRatio = new int[Count];
                    mDs.getHdmiTiming(arrWidth, arrHeight, arrRefresh, arrInterlace, arrRatio);
                    mBundle.putSerializable("width", arrWidth);
                    mBundle.putSerializable("height", arrHeight);
                    mBundle.putSerializable("refresh", arrRefresh);
                    mBundle.putSerializable("interlace", arrInterlace);
                    mBundle.putSerializable("ratio", arrRatio);
                    mBundle.putInt("count", Count);
                    mBundle.putInt("EdidChange", mEdidChange);
                    mBundle.putBoolean("mHasIncomingCall",mHasIncomingCall);
                    mEdidChange = 0;
                    outIntent.putExtras(mBundle);
                    mContext.sendBroadcast(outIntent);
                }
            } else if (action.equals(HDMI_SET_INFO)) {
                // Set Specified Timing Info: width, height ,refresh, interlace
                Bundle extras = intent.getExtras();
                if (extras == null)
                    return;
                int Width = extras.getInt("width", 0);
                int Height = extras.getInt("height", 0);
                int Refresh = extras.getInt("refresh", 0);
                int Ratio = extras.getInt("ratio", 0);
                int Interlace = extras.getInt("interlace", 0);
                int vic = extras.getInt("vic", 0);
                logv("set info <width:" + Width + "height:" + Height + "refresh:"
                        + Refresh + "interlace:" + Interlace + "ratio;" + Ratio + "VIC" + vic);
                /*
                 * Use native_setHdmiInfo for processing vic information too,
                 * instead of creating a new interface or modifying the existing
                 * one. When vic is present, set width to vic value and height
                 * '0' to differentiate.
                 */
                if (vic != 0) {
                    logv("set info vic = " + vic);
                    Width = vic;
                    Height = 0;
                }
                if (!mDs.setHdmiTiming(Width, Height, Refresh, Interlace, Ratio))
                    logv("Set HDMI Timing Info error");
            }
            else if (action.equals(HDMI_SET_SCALE)) {
                Bundle extras = intent.getExtras();
                if (extras == null)
                     return;
                int ScaleType = extras.getInt("Type", 0);

                logv("set scale info Type:" +  ScaleType);
                if (!mDs.setHdmiScaleType(ScaleType))
                    logv("Set HDMI Scale error");
            }
            else if (action.equals(HDMI_SET_STEP_SCALE)) {
                Bundle extras = intent.getExtras();
                if (extras == null)
                     return;
                int Step = extras.getInt("Step", 0);
                int Orientation = extras.getInt("Orientation", 0);
                logv("orientation" + Orientation);
                if (Orientation == 0)
                    mHoriRatio = Step;
                else
                    mVertRatio = Step;
                logv("set scale info step:" +  Step);
                if(!mDs.setHdmiOverscan(mHoriRatio, mVertRatio))
                    logv("Set HDMI Step Scale error");
            }
            else if (action.equals(HDMI_Get_DisplayBoot)) {
                Intent outIntent = new Intent(HDMI_Set_DisplayBoot);
                Bundle mBundle = new Bundle();
                if (mDisplayBoot == 1) {
                    mBundle.putInt("DisplayBoot",mDisplayBoot);
                    logv("mDisplayBoot: " + mDisplayBoot);
                    mDisplayBoot = 0;
                } else {
                    mBundle.putInt("DisplayBoot",mDisplayBoot);
                    logv("mDisplayBoot: " + mDisplayBoot);
                }
                outIntent.putExtras(mBundle);
                mContext.sendBroadcast(outIntent);
            }
            else if(action.equals(DisplayManager.ACTION_WIFI_DISPLAY_STATUS_CHANGED)) {
                boolean connected = false;
                WifiDisplayStatus status = (WifiDisplayStatus)intent.getParcelableExtra(DisplayManager.EXTRA_WIFI_DISPLAY_STATUS);
                if (status == null)
                    return;
                if (status.getActiveDisplayState() ==
                        WifiDisplayStatus.DISPLAY_STATE_CONNECTED) {
                    connected = true;
                }
                if (mWidiConnected == connected)
                    return;
                logv("Update vpp status " + connected);
                mDs.setVppState(mDs.DISPLAY_VIRTUAL, connected);
                mWidiConnected = connected;
            }
        }
    }
}
