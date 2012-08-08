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
import android.provider.Settings;
import android.content.ContentResolver;
import android.database.ContentObserver;

import java.io.FileReader;
import java.io.FileNotFoundException;

/**
 * <p>DisplayObserver, dummy implement.
 */
class DisplayObserver extends UEventObserver {
    public DisplayObserver(Context context) {
    }

    protected void finalize() throws Throwable {
        super.finalize();
    }

    @Override
    public synchronized void onUEvent(UEventObserver.UEvent event) {
    }
}
