package android.hardware.temperature;

import android.os.Parcel;
import android.os.Parcelable;

/** {@hide} */
public final class JdtsTemperatureData implements Parcelable {
	public int synchro;
    public int objectTemperature;
    public int ntc1Temperature;
    public int ntc2Temperature;
    public int ntc3Temperature;

    public static final Parcelable.Creator<JdtsTemperatureData> CREATOR = new Parcelable.Creator<JdtsTemperatureData>() {
        public JdtsTemperatureData createFromParcel(Parcel in) {
            return new JdtsTemperatureData(in);
        }

        public JdtsTemperatureData[] newArray(int size) {
            return new JdtsTemperatureData[size];
        }
    };

    public JdtsTemperatureData() {
    }

    private JdtsTemperatureData(Parcel in) {
        readFromParcel(in);
    }

    @Override
    public void writeToParcel(Parcel out, int flags) {
        out.writeInt(synchro);
        out.writeInt(objectTemperature);
        out.writeInt(ntc1Temperature);
        out.writeInt(ntc2Temperature);
        out.writeInt(ntc3Temperature);
    }

    public void readFromParcel(Parcel in) {
        synchro = in.readInt();
        objectTemperature = in.readInt();
        ntc1Temperature = in.readInt();
        ntc2Temperature = in.readInt();
        ntc3Temperature = in.readInt();
    }

    @Override
    public int describeContents() {
        return 0;
    }

}