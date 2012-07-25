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
 */

package com.android.server;
//import static android.provider.Settings.System.DISPLAY_BOOT;

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
import com.android.server.DisplaySetting;
import android.provider.Settings;
import android.content.ContentResolver;
import android.database.ContentObserver;

import java.io.FileReader;
import java.io.FileNotFoundException;

/**
 * <p>DisplayObserver.
 */
class DisplayObserver extends UEventObserver {
    private static final String TAG = "MultiDisplay-DisplayObserver";
    private static final boolean LOG = true;

    private final String HDMI_UEVENT_MATCH = "DEVPATH=/devices/pci0000:00/0000:00:02.0/drm/card0";

    // Assuming unplugged (i.e. 0) for initial state, assign initial state in init() below.
    private final int ROUTE_TO_SPEAKER = 0;
    private final int ROUTE_TO_HDMI    = 1;
    private final int HDMI_HOTPLUG     = 2;
    private final int HDMI_POWER_OFF   = 3;
    private int mAudioRoute =  ROUTE_TO_SPEAKER;
    private int mPreAudioRoute = -1;
    private String mHDMIName;
    private int mHdmiEnable = 1;
    private int mEdidChange = 0;
    private int mDisplayBoot = 1;
    private int mHoriRatio =5;
    private int mVertRatio =5;

    // indicate HDMI connect state
    private int mHDMIConnected = 0;
    private int mHDMIPlugEvent = 0;

    private Context mContext;
    private WakeLock mWakeLock;  // held while there is a pending route change
    private boolean hasIncomingCall = false;
    private boolean IncomingCallFinished = true;
    private DisplaySetting mDs;

    //Message need to handle
    private final int HDMI_STATE_CHANGE = 0;

    private final String PHONE_INCALLSCREEN_FINISH = "com.android.phone_INCALLSCREEN_FINISH";
    private static final String HDMI_GET_INFO = "android.hdmi.GET_HDMI_INFO";
    private static final String HDMI_SET_INFO = "android.hdmi.SET_HDMI_INFO";
    private static final String HDMI_SERVER_GET_INFO = "HdmiObserver.GET_HDMI_INFO";
    private static final String HDMI_SET_STATUS= "android.hdmi.SET.HDMI_STATUS";
    private static final String HDMI_SET_SCALE= "android.hdmi.SET.HDMI_SCALE";
    private static final String HDMI_SET_STEP_SCALE= "android.hdmi.SET.HDMI_STEP_SCALE";

    // Broadcast receiver for device connections intent broadcasts
    private final BroadcastReceiver mReceiver = new DisplayObserverBroadcastReceiver();

    public DisplayObserver(Context context) {
        mContext = context;
        mDs = new DisplaySetting();
        IntentFilter intentFilter = new IntentFilter(TelephonyManager.ACTION_PHONE_STATE_CHANGED);
        intentFilter.addAction(PHONE_INCALLSCREEN_FINISH);
        intentFilter.addAction(HDMI_GET_INFO);
        intentFilter.addAction(HDMI_SET_INFO);
        intentFilter.addAction(HDMI_SET_STATUS);
        intentFilter.addAction(HDMI_SET_SCALE);
        intentFilter.addAction(HDMI_SET_STEP_SCALE);
        /*android.provider.Settings.System.putInt(mContext.getContentResolver(),
        Settings.System.DISPLAY_BOOT, mDisplayBoot);*/

        mContext.registerReceiver(mReceiver, intentFilter);
        PowerManager pm = (PowerManager)context.getSystemService(Context.POWER_SERVICE);
        mWakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "DisplayObserver");
        mWakeLock.setReferenceCounted(false);
        mDs.setModeChangeListener(mListener);
        startObserving(HDMI_UEVENT_MATCH);
        if ((mDs.getMode() & mDs.HDMI_MODE_BIT) ==
                                    mDs.HDMI_MODE_BIT) {
            mHDMIConnected = 1;
            update("HOTPLUG", ROUTE_TO_HDMI);
        } else {
            mHDMIConnected = 0;
            update("HOTPLUG", ROUTE_TO_SPEAKER);
        }
    }

    protected void finalize() throws Throwable {
        mContext.unregisterReceiver(mReceiver);
        super.finalize();
    }

    DisplaySetting.onModeChangeListener mListener =
                        new DisplaySetting.onModeChangeListener() {
        public boolean onModeChange(int mode) {
            if (LOG) Slog.i(TAG, "mode is changed to " + Integer.toHexString(mode));
            return true;
        };
    };

    @Override
    public synchronized void onUEvent(UEventObserver.UEvent event) {
        if (LOG) Slog.v(TAG, "HDMI UEVENT: " + event.toString());
        if (event.toString().contains("HOTPLUG")) {
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

    private synchronized void update(String newName, int newState) {
        // Retain only relevant bits
        int delay = 0;
        Slog.v(TAG, "Set Audio output from " +
                (mAudioRoute == 0 ? "SPEAKER":"HDMI") +
                " to " + (newState == 0 ? "SPEAKER":"HDMI"));
        if (newState == mAudioRoute) {
            Slog.v(TAG, "Same Audio output, Don't set");
            return;
        }
        mHDMIName = newName;
        mPreAudioRoute = mAudioRoute;
        mAudioRoute = newState;

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
        if (LOG) Slog.v(TAG, "State:" + State + " prev:" + prev + " Name:" + Name);
        //  Pack up the values and broadcast them to everyone
        Intent intent = new Intent(Intent.ACTION_HDMI_AUDIO_PLUG);
        intent.addFlags(Intent.FLAG_RECEIVER_REGISTERED_ONLY);
        intent.putExtra("state", State);
        intent.putExtra("name", Name);

        // Should we require a permission?
        ActivityManagerNative.broadcastStickyIntent(intent, null);
    }

    private final void preNotifyHotplug(int event) {
            /* no matter plug in or out, remove previous power off msg */
            mHandler.removeMessages(HDMI_POWER_OFF);
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

            /*
             * delay 1s to power off in case
             * there are continous plug in/out
             */
            Message mmsg = mHandler.obtainMessage(HDMI_POWER_OFF);
            mHandler.sendMessageDelayed(mmsg, 1000);
        } else
            update("HOTPLUG", ROUTE_TO_HDMI);
    }

    private final Handler mHandler = new Handler() {
        @Override
        public void handleMessage(Message msg) {
            if (LOG) Slog.v(TAG, "handle message = " + (String)msg.obj);
            switch(msg.what) {
            case HDMI_STATE_CHANGE:
                sendIntents(msg.arg1, msg.arg2, (String)msg.obj);
                mWakeLock.release();
                break;
            case HDMI_HOTPLUG:
                synchronized(this) {
                    /* filter before msg which does not match with latest event */
                    if (mHDMIPlugEvent != msg.arg1)
                        return;

                    preNotifyHotplug(msg.arg1);

                    boolean ret = mDs.notifyHotPlug();
                    if (!ret) {
                        if (LOG) Slog.e(TAG, "fail to deal with hdmi hotlpug");
                        return;
                    }

                    postNotifyHotplug(msg.arg1);
                }
                break;
            case HDMI_POWER_OFF:
                mDs.setHdmiPowerOff();
                break;
            }
        }
    };

    private class DisplayObserverBroadcastReceiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (action.equals(TelephonyManager.ACTION_PHONE_STATE_CHANGED) ||
                                action.equals(PHONE_INCALLSCREEN_FINISH)) {
                if (action.equals(PHONE_INCALLSCREEN_FINISH)) {
                    IncomingCallFinished = true;
                    if (LOG) Slog.v(TAG, "incoming call screen finished: " + IncomingCallFinished);
                } else {
                    if (TelephonyManager.EXTRA_STATE == null ||
                                TelephonyManager.EXTRA_STATE_RINGING == null)
                        return;
                    String extras = intent.getStringExtra(TelephonyManager.EXTRA_STATE);
                    if (extras == null)
                        return;
                    if (extras.equals(TelephonyManager.EXTRA_STATE_RINGING)) {
                        IncomingCallFinished = false;
                        hasIncomingCall = true;
                    } else if (extras.equals(TelephonyManager.EXTRA_STATE_IDLE))
                        hasIncomingCall = false;
                     else
                        return;
                    if (LOG) Slog.v(TAG, "incoming call " +
                            (hasIncomingCall == true ? "initiated" : "terminated"));
                }
                if (hasIncomingCall) {
                    mDs.setModePolicy(mDs.MIPI_OFF_NOT_ALLOWED);
                    mDs.setModePolicy(mDs.HDMI_ON_NOT_ALLOWED);
                } else if ((!hasIncomingCall) && IncomingCallFinished) {
                    mDs.setModePolicy(mDs.MIPI_OFF_NOT_ALLOWED);
                    mDs.setModePolicy(mDs.HDMI_ON_ALLOWED);
                }
            }else if (action.equals(HDMI_GET_INFO)) {
                // Handle HDMI_GET_INFO ACTION
                if (LOG) Slog.v(TAG, "HDMI is plugged "+ (mHDMIConnected == 1 ? "in" : "out"));
                if (mHDMIConnected != 0) {
                    // Get Number of Timing Info
                    int Count = mDs.getHdmiInfoCount();
                    mEdidChange = mDs.getHdmiDeviceChange();
                    if (LOG) Slog.v(TAG, "HDMI timing number:" + Count);
                    Intent outIntent = new Intent(HDMI_SERVER_GET_INFO);
                    outIntent.addFlags(Intent.FLAG_RECEIVER_REGISTERED_ONLY);
                    Bundle mBundle = new Bundle();
                    if (-1 != Count) {
                        int[] arrWidth = new int[Count];
                        int[] arrHeight = new int[Count];
                        int[] arrRefresh = new int[Count];
                        int[] arrInterlace = new int[Count];
                        mDs.getHdmiTiming(arrWidth, arrHeight, arrRefresh, arrInterlace);
                        mBundle.putSerializable("width", arrWidth);
                        mBundle.putSerializable("height", arrHeight);
                        mBundle.putSerializable("refresh", arrRefresh);
                        mBundle.putSerializable("interlace", arrInterlace);
                        mBundle.putInt("count", Count);
                        mBundle.putInt("EdidChange",mEdidChange);
                        mEdidChange = 0;
                        outIntent.putExtras(mBundle);
                        mContext.sendBroadcast(outIntent);
                    } else {
                        if (LOG) Slog.v(TAG, "fail to get HDMI info");
                    }
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
                if (LOG) Slog.v(TAG, "set info <width:" + Width + "height:" + Height + "refresh:"
                    + Refresh + "interlace:" + Interlace + "ratio;" + Ratio + "VIC" + vic);
                /*
                 * Use native_setHdmiInfo for processing vic information too,
                 * instead of creating a new interface or modifying the existing
                 * one. When vic is present, set width to vic value and height
                 * '0' to differentiate.
                 */
                if (vic != 0) {
                    Slog.v(TAG, "set info vic = " + vic);
                    Width = vic;
                    Height = 0;
                }
                if (!mDs.setHdmiTiming(Width, Height, Refresh, Interlace, Ratio))
                    if (LOG) Slog.v(TAG, "Set HDMI Timing Info error");
            } else if (action.equals(HDMI_SET_STATUS)) {
                Bundle extras = intent.getExtras();
                if (extras == null)
                     return;
                mHdmiEnable = extras.getInt("Status", 0);
                if (LOG) Slog.v(TAG, "HDMI_SET_STATUS,EnableHdmi: " +  mHdmiEnable);
                if (mHdmiEnable == 0) {
                    mDs.setModePolicy(mDs.MIPI_OFF_NOT_ALLOWED);
                    mDs.setModePolicy(mDs.HDMI_ON_NOT_ALLOWED);
                }
                else {
                    mDs.setModePolicy(mDs.HDMI_ON_ALLOWED);
                    mDs.setModePolicy(mDs.MIPI_OFF_NOT_ALLOWED);
                }
            }
            else if (action.equals(HDMI_SET_SCALE)) {
                Bundle extras = intent.getExtras();
                if (extras == null)
                     return;
                int ScaleType = extras.getInt("Type", 0);

                if (LOG) Slog.v(TAG, "set scale info Type:" +  ScaleType);
                if (!mDs.setHdmiScaleType(ScaleType))
                    if (LOG) Slog.v(TAG, "Set HDMI Scale error");
            }
            else if (action.equals(HDMI_SET_STEP_SCALE)) {
                Bundle extras = intent.getExtras();
                if (extras == null)
                     return;
                int Step = extras.getInt("Step", 0);
                int Orientation = extras.getInt("Orientation", 0);
                if (LOG) Slog.i(TAG,"orientation" + Orientation) ;
                if (Orientation == 0)
                    mHoriRatio = Step;
                else
                    mVertRatio = Step;
                if (LOG) Slog.v(TAG, "set scale info step:" +  Step);
                if(!mDs.setHdmiScaleStep(mHoriRatio,mVertRatio))
                    if (LOG) Slog.v(TAG, "Set HDMI Step Scale error");
            }
        }
    }
}

