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

import com.intel.multidisplay.DisplaySetting;
/**
 * <p>DisplayObserver.
 */
public class DisplayObserver {
    private static final String TAG = "MultiDisplay";
    private static final boolean LOG = true;

    // Please follow this style to define name and content of Intent
    // if need to indicate display device, "DPY" is need
    // MDS_DPY_XXX = "com.intel.mds.DPY_XXX";
    private static final String MDS_HDMI_REQ_TIMING_LIST     = "com.intel.mds.hdmi.req.timing.list";
    private static final String MDS_HDMI_SET_TIMING          = "com.intel.mds.hdmi.set.timing";
    private static final String MDS_HDMI_TIMING_LIST_READY   = "com.intel.mds.hdmi.timing.list.ready";
    private static final String MDS_HDMI_SET_SCALING         = "com.intel.mds.hdmi.set.scaling";
    private static final String MDS_HDMI_SET_OVERSCAN        = "com.intel.mds.hdmi.set.overscan";
    // only for HDMI setting app
    private static final String MDS_HDMI_GET_SETTING_FIRST_START  = "com.intel.mds.hdmi.get.setting.first.start";
    private static final String MDS_HDMI_SET_SETTING_FIRST_START  = "com.intel.mds.hdmi.set.setting.first.start";

    // for audio switch
    private final int ROUTE_TO_UNKNOWN = -1;
    private final int ROUTE_TO_SPEAKER = 0;
    private final int ROUTE_TO_HDMI    = 1;
    private int mAudioRoute =  ROUTE_TO_SPEAKER;
    private int mPreAudioRoute = ROUTE_TO_UNKNOWN;

    // indicate HDMI sink device is change
    private int mEdidChange  = 0;
    // HDMI setting need to clear old states if it is first starting
    private int mSettingFirstStart = 1;
    // overscan compensation
    private int mHoriRatio   = 5;
    private int mVertRatio   = 5;

    // indicate HDMI connect state
    private boolean mHDMIConnected = false;

    // monitor the state of incoming or outgoing call
    private boolean mHasIncomingCall = false;
    private boolean mInCallScreenFinished = true;

    // the display capabilty of platform
    private int mDisplayCapability = 0;

    //Message need to handle
    private final int HDMI_STATE_CHANGE = 0;
    private Context mContext;
    private WakeLock mWakeLock;  // held while there is a pending route change
    // Broadcast receiver for device connections intent broadcasts
    private final BroadcastReceiver mReceiver = new DisplayObserverBroadcastReceiver();

    private DisplaySetting mDs;

    public DisplayObserver(Context context) {
        mContext = context;
        mDs = new DisplaySetting();
        IntentFilter intentFilter = new IntentFilter(TelephonyManager.ACTION_PHONE_STATE_CHANGED);
        intentFilter.addAction(MDS_HDMI_REQ_TIMING_LIST);
        intentFilter.addAction(MDS_HDMI_SET_TIMING);
        intentFilter.addAction(MDS_HDMI_SET_SCALING);
        intentFilter.addAction(MDS_HDMI_SET_OVERSCAN);
        intentFilter.addAction(MDS_HDMI_GET_SETTING_FIRST_START);

        mContext.registerReceiver(mReceiver, intentFilter);
        PowerManager pm = (PowerManager)context.getSystemService(Context.POWER_SERVICE);
        mWakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "DisplayObserver");
        mWakeLock.setReferenceCounted(false);
        mDs.setMdsMessageListener(mListener);
        mDisplayCapability = mDs.getDisplayCapability();
        logv("Get platform display capability:" + mDisplayCapability);
        if (checkDisplayCapability(mDs.HW_SUPPORT_HDMI) &&
            ((mDs.getMode() & mDs.HDMI_CONNECTED_BIT) == mDs.HDMI_CONNECTED_BIT)) {
            mHDMIConnected = true;
            update("HOTPLUG", ROUTE_TO_HDMI);
        } else {
            mHDMIConnected = false;
            update("HOTPLUG", ROUTE_TO_SPEAKER);
        }
    }

    protected void finalize() throws Throwable {
        mContext.unregisterReceiver(mReceiver);
        super.finalize();
    }

    private void logv(String s) {
        if (LOG) {
            Slog.v(TAG, s);
        }
    }

    private boolean checkDisplayCapability(int value) {
        if ((mDisplayCapability & value) == value)
            return true;
        return false;
    }

    DisplaySetting.onMdsMessageListener mListener =
                        new DisplaySetting.onMdsMessageListener() {
        public boolean onMdsMessage(int msg, int value) {
            if (msg == mDs.MDS_MSG_HOT_PLUG) {
                /// audio switch
                int connected = value;
                preNotifyHotplug(connected);
                postNotifyHotplug(connected);
            }
            return true;
        };
    };

    private synchronized void update(String newName, int newState) {
        // Retain only relevant bits
        int delay = 0;
        logv("Set Audio output from " +
                (mAudioRoute == ROUTE_TO_SPEAKER ? "SPEAKER":"HDMI") +
                " to " + (newState == ROUTE_TO_SPEAKER ? "SPEAKER":"HDMI"));
        if (newState == mAudioRoute) {
            logv("Same Audio output, Don't set");
            return;
        }

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
                                    newName),
                                    delay);
    }

    private synchronized final void sendIntents(int cur, int prev, String name) {
        if (cur == ROUTE_TO_HDMI || (prev == ROUTE_TO_HDMI))
            sendIntent(1, cur, prev, name);
    }

    private final void sendIntent(int hdmi, int State, int prev, String Name) {
        logv("State:" + State + " prev:" + prev + " Name:" + Name);
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
            mHDMIConnected = false;
        else
            mHDMIConnected = true;
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
            logv("handle message = " + (String)msg.obj);
            switch(msg.what) {
            case HDMI_STATE_CHANGE:
                sendIntents(msg.arg1, msg.arg2, (String)msg.obj);
                mWakeLock.release();
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
                    mDs.setPhoneState(mDs.PHONE_STATE_ON);
                } else if (extras.equals(TelephonyManager.EXTRA_STATE_IDLE)) {
                    mHasIncomingCall = false;
                    mInCallScreenFinished = true;
                    logv("Call is terminated and Incallscreen disappeared");
                    mDs.setPhoneState(mDs.PHONE_STATE_OFF);
                }
            } else if (action.equals(MDS_HDMI_REQ_TIMING_LIST)) {
                logv("HDMI is plugged " + (mHDMIConnected? "in" : "out"));
                if (mHDMIConnected) {
                    // Get Number of Timing Info
                    int Count = mDs.getHdmiInfoCount();
                    mEdidChange = mDs.getHdmiDeviceChange();
                    logv("HDMI timing number:" + Count);
                    if (Count <= 0) {
                        logv("fail to get HDMI info");
                        return;
                    }

                    Intent outIntent = new Intent(MDS_HDMI_TIMING_LIST_READY);
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
                    mBundle.putInt("EdidChange",mEdidChange);
                    mBundle.putBoolean("mHasIncomingCall",mHasIncomingCall);
                    mEdidChange = 0;
                    outIntent.putExtras(mBundle);
                    mContext.sendBroadcast(outIntent);
                }
            } else if (action.equals(MDS_HDMI_SET_TIMING)) {
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
            else if (action.equals(MDS_HDMI_SET_SCALING)) {
                Bundle extras = intent.getExtras();
                if (extras == null)
                     return;
                int ScaleType = extras.getInt("Type", 0);

                logv("set scale info Type:" +  ScaleType);
                if (!mDs.setHdmiScaleType(ScaleType))
                    logv("Set HDMI Scale error");
            }
            else if (action.equals(MDS_HDMI_SET_OVERSCAN)) {
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
            else if (action.equals(MDS_HDMI_GET_SETTING_FIRST_START)) {
                Intent outIntent = new Intent(MDS_HDMI_SET_SETTING_FIRST_START);
                Bundle mBundle = new Bundle();
                logv("HDMI setting is 1st starting: %d " + mSettingFirstStart);
                if (mSettingFirstStart == 1) {
                    mBundle.putInt("SettingFirstStart", mSettingFirstStart);
                    mSettingFirstStart = 0;
                } else {
                    mBundle.putInt("SettingFirstStart",mSettingFirstStart);
                }
                outIntent.putExtras(mBundle);
                mContext.sendBroadcast(outIntent);
            }
        }
    }
}

