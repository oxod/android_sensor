#define LOG_TAG "TECHARTMS_JDTS"

#include "jni.h"
#include "JNIHelp.h"
#include "android_runtime/AndroidRuntime.h"

#include <utils/misc.h>
#include <utils/Log.h>
#include <hardware/hardware.h>
#include <hardware/sensor_jdts_temperature.h>

#include <stdio.h>

namespace android
{
    static jlong init_native(JNIEnv *env, jobject clazz)
    {
        int err;
        hw_module_t* module;
        techartms_jdts_device_t* dev = NULL;
        
        // find the HAL
        // internal function checks several paths where HW modules can locate
        // and must have structurized naming, this is why our module`s name ends with
        // ".default" - it would be better to give it a more HW specific postfix, but who cares...
        err = hw_get_module(TECHART_MS_JDTS_HARDWARE_MODULE_ID, (hw_module_t const**)&module);
        if (err == 0) {
            err = module->methods->open(module, "", ((hw_device_t**) &dev));
            if (err != 0) {
                ALOGE("init_native: cannot open device module: %d", err);
                return -1;
            }
        } else {
            ALOGE("init_native: cannot get device module: %d", err);
            return 0;
        }

        ALOGD("init_native: start ok");
        // this pointer is saved in Java part of the service to access other HAL functions
        return (jlong)dev;
    }

    static void finalize_native(JNIEnv *env, jobject clazz, jlong ptr)
    {
        techartms_jdts_device_t* dev = (techartms_jdts_device_t*)ptr;

        if (dev == NULL) {
            ALOGE("finalize_native: invalid device pointer");
            return;
        }

        free(dev);
        ALOGD("finalize_native: finalized ok");
    }

    // read the data from HAL here
    // the return type is newly created JdtsTemperatureData
    static jobject read_sample_native(JNIEnv *env, jobject clazz, jlong ptr)
    {
        techartms_jdts_device_t* dev = (techartms_jdts_device_t*)ptr;
        int ret = 0;

        unsigned short synchro = 0;
        short obj_temp = 0;
        short ntc1_temp = 0;
        short ntc2_temp = 0;
        short ntc3_temp = 0;

        if (dev == NULL) {
            ALOGE("read_sample_native: invalid device pointer");
            return (jobject)NULL;
        }

        ret = dev->read_sample(&synchro, &obj_temp, &ntc1_temp, &ntc2_temp, &ntc3_temp);
        if (ret < 0) {
            ALOGE("read_sample_native: Cannot read JdtsTemperatureData");
            return (jobject)NULL;
        }

        jclass c = env->FindClass("android/hardware/temperature/JdtsTemperatureData");
        if (c == 0) {
            ALOGE("read_sample_native: Find Class JdtsTemperatureData Failed");
            return (jobject)NULL;
        }

        jmethodID cnstrctr = env->GetMethodID(c, "<init>", "()V");
        if (cnstrctr == 0) {
            ALOGE("read_sample_native: Find constructor JdtsTemperatureData Failed");
            return (jobject)NULL;
        }

        jfieldID synchroField = env->GetFieldID(c, "synchro", "I");
        jfieldID objTempField = env->GetFieldID(c, "objectTemperature", "I");
        jfieldID ntc1TempField = env->GetFieldID(c, "ntc1Temperature", "I");
        jfieldID ntc2TempField = env->GetFieldID(c, "ntc2Temperature", "I");
        jfieldID ntc3TempField = env->GetFieldID(c, "ntc3Temperature", "I");
        if (synchroField == 0 || objTempField == 0 ||
            ntc1TempField == 0 || ntc2TempField == 0 || ntc3TempField == 0) {
            ALOGE("read_sample_native: cannot get fields of resulting object");
            return (jobject)NULL;
        }

        jobject jdtsData = env->NewObject(c, cnstrctr);

        env->SetIntField(jdtsData, synchroField, (jint)synchro);
        env->SetIntField(jdtsData, objTempField, (jint)obj_temp);
        env->SetIntField(jdtsData, ntc1TempField, (jint)ntc1_temp);
        env->SetIntField(jdtsData, ntc2TempField, (jint)ntc2_temp);
        env->SetIntField(jdtsData, ntc3TempField, (jint)ntc3_temp);

        ALOGD("read_sample_native: read ok");
        return jdtsData;
    }

    static jboolean activate_native(JNIEnv *env, jobject clazz, jlong ptr, jboolean enabled)
    {
        techartms_jdts_device_t* dev = (techartms_jdts_device_t*)ptr;
        if (dev == NULL) {
            ALOGE("activate_native: invalid device pointer");
            return JNI_FALSE;
        }

        unsigned char ok = dev->activate((unsigned char)enabled);
        if (ok != 0) {
            ALOGE("activate_native: Activate JdtsSensor Failed");
            return JNI_FALSE;
        }

        ALOGD("activate_native: Activate JdtsSensor OK");
        return JNI_TRUE;
    }

    static jboolean set_mode_native(JNIEnv *env, jobject clazz, jlong ptr, jboolean is_continuous)
    {
        techartms_jdts_device_t* dev = (techartms_jdts_device_t*)ptr;
        if (dev == NULL) {
            ALOGE("set_mode_native: invalid device pointer");
            return JNI_FALSE;
        }
        
        unsigned char ok = dev->set_mode((unsigned char)is_continuous);
        if (ok != 0) {
            ALOGE("set_mode_native: Set power mode JdtsSensor Failed");
            return JNI_FALSE;
        }

        ALOGD("set_mode_native: Set power mode JdtsSensor OK");
        return JNI_TRUE;
    }

    static JNINativeMethod method_table[] = {
        { "init_native", "()J", (void*)init_native },
        { "finalize_native", "(J)V", (void*)finalize_native },
        { "read_sample_native", "(J)Landroid/hardware/temperature/JdtsTemperatureData;", (void*)read_sample_native },
        { "activate_native", "(JZ)Z", (void*)activate_native },
        { "set_mode_native", "(JZ)Z", (void*)set_mode_native},
    };

    // this function is called from onload.cpp, 
    // which in turn is called when system service starts
    int register_android_server_JdtsService(JNIEnv *env)
    {
        ALOGD("register_android_server_JdtsService");

        return jniRegisterNativeMethods(
            env, 
            "com/android/server/temperature/JdtsService",
            method_table,
            NELEM(method_table));
    };
};