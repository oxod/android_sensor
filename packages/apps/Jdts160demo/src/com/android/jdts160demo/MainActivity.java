package com.android.jdts160demo;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.widget.CompoundButton;
import android.widget.ImageView;
import android.widget.Switch;
import android.widget.TextView;

import android.hardware.temperature.*;

public class MainActivity extends Activity {

    private final String TAG = "jdts160demo";

    private final int POLLING_PERIOD_MS = 200;

    private JdtsManager mServiceManager = null;
    private JdtsTemperatureData mSensorData = null;

    private GaugeView mGaugeObj;
    private GaugeView mGaugeNtc1;
    private GaugeView mGaugeNtc2;
    private GaugeView mGaugeNtc3;
    private TextView mTextSynchro;
    private ImageView mIrqImage;

    private TextView mTextObj;
    private TextView mTextNtc1;
    private TextView mTextNtc2;
    private TextView mTextNtc3;

    // all temperatures are .2 points precision values in degrees Celsius
    final private Object mDataSync = new Object();
    private boolean mMeasModeUpdateRequired;
    // set up when user switches between measurement modes and queues I2C expander command
    // to switch the mode
    private boolean mIsContinuousMode;
    // continuous mode (power is always on, no control)
    // burst mode (every cycle power on, read and power off required)

    private boolean mPowerState;
    private boolean mPowerUpdateRequired;

    private Thread mCommThread = null;
    private boolean mIsRunning = true; // the communication thread goes on unless onDestroy method is called
        
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // enforce the sensor to switch into continuous mode on startup
        mPowerUpdateRequired = true;
        mPowerState = true;
        mMeasModeUpdateRequired = true;
        mIsContinuousMode = true;

        mIrqImage = (ImageView) findViewById(R.id.image_led_irq);

        mGaugeObj = (GaugeView) findViewById(R.id.gauge_view_obj);
        mGaugeNtc1 = (GaugeView) findViewById(R.id.gauge_view_ntc1);
        mGaugeNtc2 = (GaugeView) findViewById(R.id.gauge_view_ntc2);
        mGaugeNtc3 = (GaugeView) findViewById(R.id.gauge_view_ntc3);
        mTextSynchro = (TextView) findViewById(R.id.text_synchro);

        mTextObj = (TextView) findViewById(R.id.text_obj);
        mTextNtc1 = (TextView) findViewById(R.id.text_ntc1);
        mTextNtc2 = (TextView) findViewById(R.id.text_ntc2);
        mTextNtc3 = (TextView) findViewById(R.id.text_ntc3);

        Switch switch_mode = (Switch) findViewById(R.id.switch1);
        switch_mode.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                synchronized (mDataSync) {
                    mIsContinuousMode = isChecked;
                    mMeasModeUpdateRequired = true;
                }
            }
        });

        Switch switch_power = (Switch) findViewById(R.id.switch_power);
        switch_power.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                synchronized (mDataSync) {
                    mPowerState = isChecked;
                    mPowerUpdateRequired = true;
                }
            }
        });
        switch_power.setChecked(true); // power is on by default

        mServiceManager = (JdtsManager) getSystemService(JDTS_TEMPERATURE_SERVICE);

        mCommThread = new Thread() {
            @Override
            public void run() {
                while(mIsRunning) {

                    synchronized (mDataSync) {
                        if (mPowerUpdateRequired) {
                            if (mServiceManager.activate(mPowerState)) {
                                mPowerUpdateRequired = false;
                            } else {
                                Log.w(TAG, "Cannot update power state");
                            }
                        }

                        if (mMeasModeUpdateRequired) {
                            if (mServiceManager.setMode(mIsContinuousMode)) {
                                mMeasModeUpdateRequired = false;
                            } else {
                                Log.w(TAG, "Cannot update measurement mode");
                            }
                        }
                    }

                    mSensorData = mServiceManager.readSample();
                    if (mSensorData != null) {
                        updateUI();
                    } else {
                        updateNonIRQUI();
                    }

                    try {
                        Thread.sleep(POLLING_PERIOD_MS);
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                }
            }
        };

        mCommThread.setUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            @Override
            public void uncaughtException(Thread t, Throwable e) {
                e.printStackTrace();
            }
        });
        mCommThread.start();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        try {
            mCommThread.join(POLLING_PERIOD_MS * 2);
        } catch (InterruptedException e) {
            e.printStackTrace();
        }
    }

    private void updateUI() {
        runOnUiThread(new Runnable() {
            public void run() {
                float obj_temp = mSensorData.objectTemperature / 100.F;
                float ntc1_temp = mSensorData.ntc1Temperature / 100.F;
                float ntc2_temp = mSensorData.ntc2Temperature / 100.F;
                float ntc3_temp = mSensorData.ntc3Temperature / 100.F;

                String s_obj = String.format("%.2f °C", obj_temp);
                String s_ntc1 = String.format("%.2f °C", ntc1_temp);
                String s_ntc2 = String.format("%.2f °C", ntc2_temp);
                String s_ntc3 = String.format("%.2f °C", ntc3_temp);
                String s_synchro = String.format("Synchro = %d", mSensorData.synchro);

                mGaugeObj.setTargetValue(obj_temp);
                mTextObj.setText(s_obj);

                mGaugeNtc1.setTargetValue(ntc1_temp);
                mTextNtc1.setText(s_ntc1);

                mGaugeNtc2.setTargetValue(ntc2_temp);
                mTextNtc2.setText(s_ntc2);

                mGaugeNtc3.setTargetValue(ntc3_temp);
                mTextNtc3.setText(s_ntc3);

                mTextSynchro.setText(s_synchro);

                mIrqImage.setImageDrawable(getResources().getDrawable(R.drawable.led_green_hi));

                Log.d(TAG, 
                    s_synchro
                    + "Obj = " + s_obj 
                    + " NTC1 = " + s_ntc1 
                    + " NTC2 = " + s_ntc2 
                    + " NTC3 = " + s_ntc3);
            }
        });
    }

    private void updateNonIRQUI() {
        runOnUiThread(new Runnable() {
            public void run() {
                mIrqImage.setImageDrawable(getResources().getDrawable(R.drawable.led_green_md));
            }
        });
    }
}
