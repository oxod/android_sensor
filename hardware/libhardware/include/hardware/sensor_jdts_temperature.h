#ifndef ANDROID_TECHART_MS_JDTS_INTERFACE_H
#define ANDROID_TECHART_MS_JDTS_INTERFACE_H

#include <stdint.h>
#include <sys/cdefs.h>
#include <sys/types.h>

#include <hardware/hardware.h>

__BEGIN_DECLS

#define TECHART_MS_JDTS_HARDWARE_MODULE_ID "techartmsjdts"

struct techartms_jdts_device_t {
    struct hw_device_t common;

    int (*read_sample)(unsigned short *psynchro, short *pobj_temp, short *pntc1_temp, short *pntc2_temp, short *pntc3_temp);
    int (*activate)(unsigned char enabled);
    int (*set_mode)(unsigned char is_continuous);
};

__END_DECLS

#endif // ANDROID_TECHART_MS_JDTS_INTERFACE_H