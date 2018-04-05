#include <errno.h>
#include <cutils/log.h>
#include <cutils/sockets.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <hardware/sensor_jdts_temperature.h>

#define     LOG_TAG  "TECHARTMS_JDTS"
#define     DEVICE_NAME "/dev/jdts_temperature"

#define     TECHART_MS_JDTS_MODE_CONTINOUS  0
#define     TECHART_MS_JDTS_MODE_BURST      1

int fd = 0;

int read_sample(unsigned short *psynchro, short *pobj_temp, short *pntc1_temp, short *pntc2_temp, short *pntc3_temp)
{
    int ret = 0;
    unsigned char buffer[10];
    
    ALOGD("HAL -- read_sample() called");

    ret = read(fd, (char*)buffer, sizeof(buffer));    
    if (ret < 0) {
        ALOGE("HAL -- cannot read raw temperature data");
        return -1;
    }

    if (psynchro)   *psynchro   = (unsigned short)(buffer[3] << 8 | buffer[2]);
    if (pobj_temp)  *pobj_temp  = (short)(buffer[1] << 8 | buffer[0]);
    if (pntc1_temp) *pntc1_temp = (short)(buffer[5] << 8 | buffer[4]);
    if (pntc2_temp) *pntc2_temp = (short)(buffer[7] << 8 | buffer[6]);
    if (pntc3_temp) *pntc3_temp = (short)(buffer[9] << 8 | buffer[8]);

    ALOGD("HAL - sample read OK");
    return 0;
}

int activate(unsigned char enabled)
{
    int ret = 0;
    unsigned char control_buffer[2]; 

/* from driver
[0] - first byte to write is a command type: power control or meas mode control    
#define CMD_TYPE_POWER        0x00
[1] - second byte to write is a command of power mode
#define CMD_POWER_SLEEP       0x00
#define CMD_POWER_WAKEUP      0x01
*/

    control_buffer[0] = 0x00; // CMD_TYPE_POWER
    control_buffer[1] = enabled ? 0x01 /*CMD_POWER_WAKEUP*/ : 0x00 /*CMD_POWER_SLEEP*/;
    
    ALOGD("HAL - activate(%d) called", enabled);

    ret = write(fd, (char*)control_buffer, sizeof(control_buffer));
    if (ret < 0) {
        ALOGE("HAL - cannot write activation state");
        return -1;
    }

    ALOGD("HAL - activation state written OK");
    return 0;
}

int set_mode(unsigned char is_continuous)
{
    int ret;
    unsigned char control_buffer[2]; 

/* from driver
[0] - first byte to write is a command type: power control or meas mode control    
#define CMD_TYPE_MEAS_MODE    0x01
[1] - second byte to write is a command of measurement mode
#define CMD_MEAS_MODE_CONT    0x00
#define CMD_MEAS_MODE_BURST   0x01
*/

    control_buffer[0] = 0x01; // CMD_TYPE_MEAS_MODE
    control_buffer[1] = is_continuous ? 0x00 /*CMD_MEAS_MODE_CONT*/ : 0x01 /*CMD_MEAS_MODE_BURST*/;
    
    ALOGD("HAL -- set_mode(%d) called", is_continuous);

    ret = write(fd, (char*)control_buffer, sizeof(control_buffer));
    if (ret < 0) {
        ALOGE("HAL - cannot write mode state");
        return -1;
    }

    ALOGD("HAL - mode state written OK");
    return 0;
}

static int open_techartms_jdts(const struct hw_module_t* module, char const* name, struct hw_device_t** device)
{
    int ret = 0;

    struct techartms_jdts_device_t *dev = malloc(sizeof(struct techartms_jdts_device_t));
    if (dev == NULL) {
        ALOGE("HAL - cannot allocate memory for the device");
        return -ENOMEM;
    }
    else {
        memset(dev, 0, sizeof(*dev));
    }

    ALOGD("HAL - openHAL() called");

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t*)module;
    dev->read_sample = read_sample;
    dev->activate = activate;
    dev->set_mode = set_mode;

    *device = (struct hw_device_t*) dev;

    fd = open(DEVICE_NAME, O_RDWR);
    if (fd <= 0) {
        ALOGE("HAL - cannot open device driver");
        return -1;
    }

    ALOGD("HAL - has been initialized");
    return 0;
}

static struct hw_module_methods_t techartms_jdts_module_methods = {
    .open = open_techartms_jdts
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = TECHART_MS_JDTS_HARDWARE_MODULE_ID,
    .name = "TechartMS JDTS HAL Module",
    .author = "TechartMS LLC",
    .methods = &techartms_jdts_module_methods,
};