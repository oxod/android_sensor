package com.android.server.temperature;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.os.Process;
import android.util.Log;

import android.hardware.temperature.IJdtsService;
import android.hardware.temperature.JdtsTemperatureData;

public class JdtsService extends IJdtsService.Stub {
    private static final String TAG = "TECHARTMS_JDTS";

    private long mNativePointer;

    public JdtsService(Context context) {
    	super();

    	Log.i(TAG, "JdtsTemperature Service started");

        mNativePointer = init_native();
    }

    protected void finalize() throws Throwable {
        finalize_native(mNativePointer);
        super.finalize();
    }

    public JdtsTemperatureData readSample() {
        return read_sample_native(mNativePointer);
    }

    public boolean setMode(boolean is_continuous) {
        return set_mode_native(mNativePointer, is_continuous);
    }

    public boolean activate(boolean enabled) {
        return activate_native(mNativePointer, enabled);
    }

    private static native long init_native();
    private static native void finalize_native(long ptr);
    private static native JdtsTemperatureData read_sample_native(long ptr);
    private static native boolean activate_native(long ptr, boolean enabled);
    private static native boolean set_mode_native(long ptr, boolean is_continuous);
}