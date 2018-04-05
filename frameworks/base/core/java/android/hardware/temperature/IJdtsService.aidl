package android.hardware.temperature;

import android.hardware.temperature.JdtsTemperatureData;

/** {@hide} */
interface IJdtsService {
JdtsTemperatureData readSample();
boolean setMode(boolean is_continuous);
boolean activate(boolean enabled);
}