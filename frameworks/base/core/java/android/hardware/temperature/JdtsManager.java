package android.hardware.temperature;

import android.os.RemoteException;
import android.hardware.temperature.IJdtsService;

/** {@hide} */
public class JdtsManager
{
    public JdtsTemperatureData readSample() {
		try {
		    return mService.readSample();
		} catch (RemoteException e) {
		    return null;
		}
    }

    public boolean activate(boolean enabled) {
		try {
		    return mService.activate(enabled);
		} catch (RemoteException e) {
		    return false;
		}
    }

    public boolean setMode(boolean is_continuous) {
		try {
		    return mService.setMode(is_continuous);
		} catch (RemoteException e) {
		    return false;
		}
    }

    public JdtsManager(IJdtsService service) {
        mService = service;
    }

    IJdtsService mService;
}