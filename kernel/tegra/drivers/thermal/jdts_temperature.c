/**
 * @file   jdts_temperature.c
 * @author Pavel Akimov
 * @date   25 January 2017
 * @version 0.1
 * @brief   Driver for the temperature sensor by ... . This module maps to /dev/jdts_temperature and
 * comes with a helper C program that can be run in Linux user space to communicate with
 * this the LKM.
 * @see http://www.techart-ms.com/ for contacts.
 */

#include <linux/init.h>           // Macros used to mark up functions e.g. __init __exit
#include <linux/module.h>         // Core header for loading LKMs into the kernel
#include <linux/device.h>         // Header to support the kernel Driver Model
#include <linux/kernel.h>         // Contains types, macros, functions for the kernel
#include <linux/fs.h>             // Header for the Linux file system support
#include <linux/i2c.h>            // main sensor communication protocol
#include <linux/gpio.h>           // sensor`s wake/sleep and new data interruption are processed via two control lines
#include <linux/interrupt.h>      // Required to support GPIO IRQ handler
#include <linux/slab.h>           // kmalloc declaration
#include <asm/uaccess.h>          // Required for the copy to user function
#include <asm/io.h>               // Required to access memset()
#include <linux/workqueue.h>      // Required to make IRQ event into deferred handler task
#include <linux/mutex.h>          // Required to sync data buffer usage between IRQ-work and outer read requests
#include <linux/delay.h>

#define  DEVICE_NAME "jdts_temperature"   ///< The device will appear at /dev/jdts_temperature using this value
#define  CLASS_NAME  "jdts"               ///< The device class -- this is a character device driver

// Raspberry Pi 3 pinout
#define GPIO_PWR_DOWN         221   /// #define TEGRA_GPIO_PBB5   221

#define I2C_SLAVE_ADDRESS     0x55
#define I2C_DATA_SIZE         10
/*
From address 0x08 10 bytes data has:

[0,1] - (int16_t) object temperature in 0.01C
[2,3] - (uint16_t) measurements counter
[4,5] - (int16_t) ntc1 temperature in 0.01C
[6,7] - (int16_t) ntc2 temperature in 0.01C
[8,9] - (int16_t) ntc3 temperature in 0.01C
*/

#define CMD_TYPE_POWER        0x00
#define CMD_TYPE_MEAS_MODE    0x01

#define CMD_POWER_SLEEP       0x00
#define CMD_POWER_WAKEUP      0x01

#define CMD_MEAS_MODE_CONT    0x00
#define CMD_MEAS_MODE_BURST   0x01

MODULE_LICENSE("GPL");            ///< The license type -- this affects available functionality
MODULE_AUTHOR("Pavel Akimov");    ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("Temperature Linux driver for the JDTS sensor");  ///< The description -- see modinfo
MODULE_VERSION("0.1");            ///< A version number to inform users
 
static int    majorNumber;                   ///< Stores the device number -- determined automatically
static struct class*  jdtsClass  = NULL;     ///< The device-driver class struct pointer
static struct device* jdtsDevice = NULL;     ///< The device-driver device struct pointer

static const u8 i2c_read_temperatures_address[] = { 0x00, 0x08 };       // a command to read I2C data from the sensor
static const u8 i2c_meas_mode_continuous[] = { 0x00, 0x20, 0x01, 0x01 };// a command to write I2C meas mode to the sensor
static const u8 i2c_meas_mode_burst[] = { 0x00, 0x20, 0x01, 0x01 }; // FIXIT: a command to write I2C meas mode to the sensor
static u8 sensor_data_buffer[I2C_DATA_SIZE] = { 0 }; ///< Data buffer for temperatures
static u8 sensor_mode;                       ///< Continous - awake, burst - single meas after wake up

// I2C client to access and write sensor parameters
struct i2c_client *tms_jdts_i2c_client = NULL;

// The prototype functions for the character driver -- must come before the struct definition
static int dev_open(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);

// The prototype functions for the I2C characters
static int tms_jdts_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int tms_jdts_i2c_remove(struct i2c_client *i2c_client);
static int tms_jdts_i2c_detect(struct i2c_client *client, struct i2c_board_info *info);

static int execute_command(u8 type, u8 cmd);
static int set_sensor_power(u8 enabled);
static int read_raw_temperatures(void);
static irq_handler_t jdts_data_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs);
static void read_data_work_handler(struct work_struct *w);

static unsigned long read_data_work_delay;
static struct workqueue_struct *wq = NULL;
static DECLARE_DELAYED_WORK(read_data_work, read_data_work_handler);

static struct mutex read_data_mutex; /* shared between the threads */

/** @brief Devices are represented as file structure in the kernel. The file_operations structure from
 *  /linux/fs.h lists the callback functions that you wish to associated with your file operations
 *  using a C99 syntax structure. char devices usually implement open, read, write and release calls
 */
static struct file_operations fops =
{
   .open = dev_open,
   .read = dev_read,
   .write = dev_write
};

static const unsigned short normal_i2c[] = {
   I2C_SLAVE_ADDRESS,
   I2C_CLIENT_END
};

/*!
 * jdts i2c_driver - i2c device identification
 * This structure tells the I2C subsystem how to identify and support given I2C device
 */
static const struct i2c_device_id tms_jdts_i2c_id[] = {
   { CLASS_NAME, 0 },
   { },
};
MODULE_DEVICE_TABLE(i2c, tms_jdts_i2c_id);

static struct i2c_driver tms_jdts_i2c_driver = {
   .driver = {
      .owner = THIS_MODULE,
      .name = CLASS_NAME, // techartms,jdts
   },
   
   .id_table = tms_jdts_i2c_id,
   .probe = tms_jdts_i2c_probe,
   .remove = tms_jdts_i2c_remove,
   
   .detect = tms_jdts_i2c_detect,
   .address_list = normal_i2c
};

/*!
 * TMS_JDTS I2C probe function.
 * Function set in i2c_driver struct.
 * Called by insmod.
 *
 *  @param     *adapter  I2C adapter descriptor.
 *  @return    Error code indicating success or failure.
 */
static int tms_jdts_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
   printk(KERN_INFO "TechartMicroSystems JDTS: Probing the JDTS LKM\n");

   tms_jdts_i2c_client = client;
   return 0;
}

static int tms_jdts_i2c_remove(struct i2c_client *i2c_client)
{
   if (tms_jdts_i2c_client != NULL) {
      i2c_unregister_device(tms_jdts_i2c_client);
      tms_jdts_i2c_client = NULL;
   }

   printk(KERN_INFO "TechartMicroSystems JDTS: Remove the JDTS LKM\n");   
   return 0;
}

static int tms_jdts_i2c_detect(struct i2c_client *client, struct i2c_board_info *info)
{
   printk(KERN_INFO "TechartMicroSystems JDTS: Autodetection JDTS LKM\n");

   tms_jdts_i2c_client = client;
   return 0;
}
 
/** @brief The LKM initialization function
 *  The static keyword restricts the visibility of the function to within this C file. The __init
 *  macro means that for a built-in driver (not a LKM) the function is only used at initialization
 *  time and that it can be discarded and its memory freed up after that point.
 *  @return returns 0 if successful
 */
static int __init jdts_temperature_init(void){
   int err;

   printk(KERN_INFO "TechartMicroSystems JDTS: Initializing the JDTS LKM\n");
 
   // *******************************************************
   // General module initialization
   // *******************************************************

   // Try to dynamically allocate a major number for the device -- more difficult but worth it
   majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
   if (majorNumber<0){
      pr_err(KERN_ALERT "TechartMicroSystems JDTS failed to register a major number\n");
      return majorNumber;
   }
   printk(KERN_INFO "TechartMicroSystems JDTS: registered correctly with major number %d\n", majorNumber);
 
   // Register the device class
   jdtsClass = class_create(THIS_MODULE, CLASS_NAME);
   if (IS_ERR(jdtsClass)){                // Check for error and clean up if there is
      pr_err(KERN_ALERT "Failed to register device class\n");
      err = PTR_ERR(jdtsClass);          // Correct way to return an error on a pointer
      goto err_char_dev;
   }
   printk(KERN_INFO "TechartMicroSystems JDTS: device class registered correctly\n");
 
   // Register the device driver
   jdtsDevice = device_create(jdtsClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
   if (IS_ERR(jdtsDevice)){               // Clean up if there is an error
      pr_err(KERN_ALERT "Failed to create the device\n");
      err = PTR_ERR(jdtsDevice);
      goto err_class;
   }
   printk(KERN_INFO "TechartMicroSystems JDTS: device class created correctly\n"); // Made it! device was initialized

   // *******************************************************
   // Initialize I2C driver
   // *******************************************************
   err = i2c_add_driver(&tms_jdts_i2c_driver);
   if (err < 0) {
      pr_err("TechartMicroSystems JDTS: Error: %s: driver registration failed, error=%d\n", __func__, err);
      goto err_dev;
   }

   // *******************************************************
   // Configure sensor state
   // *******************************************************  
   sensor_mode = CMD_MEAS_MODE_CONT;
   err = execute_command(CMD_TYPE_MEAS_MODE, CMD_MEAS_MODE_CONT);
   if (err < 0) {
      pr_err("TechartMicroSystems JDTS: Error: %s: sensor meas mode failed, error=%d\n", __func__, err);
      goto err_drv;
   }

   err = request_irq(
      tms_jdts_i2c_client->irq,
      (irq_handler_t)jdts_data_irq_handler,
      IRQF_TRIGGER_FALLING,
      "jdts_gpio_handler",
      &tms_jdts_i2c_client->irq); // no shared interrupt lines
   if (err < 0) {
      pr_err("TechartMicroSystems JDTS: Error: %s: cannot register irq handler: Error=%d\n", __func__, err);
      goto err_drv;
   }  

   enable_irq(tms_jdts_i2c_client->irq);

   // *******************************************************
   // Initialize work queue to read temperatures by nIRQ
   // *******************************************************
   wq = create_singlethread_workqueue("jdts_work");
   read_data_work_delay = msecs_to_jiffies(1);

   mutex_init(&read_data_mutex);

   mutex_lock(&read_data_mutex);
   read_raw_temperatures();
   mutex_unlock(&read_data_mutex);

   printk(KERN_INFO "TechartMicroSystems JDTS: initialization completed\n");
   return 0;

err_drv:
   i2c_del_driver(&tms_jdts_i2c_driver);
err_dev:
   device_destroy(jdtsClass, MKDEV(majorNumber, 0));     // remove the device
   class_unregister(jdtsClass);                          // unregister the device class
err_class:
   class_destroy(jdtsClass);                             // remove the device class
err_char_dev:
   unregister_chrdev(majorNumber, DEVICE_NAME);             // unregister the major number

   return err;
}
 
/** @brief The LKM cleanup function
 *  Similar to the initialization function, it is static. The __exit macro notifies that if this
 *  code is used for a built-in driver (not a LKM) that this function is not required.
 */
static void __exit jdts_temperature_exit(void) {

   if (delayed_work_pending(&read_data_work) != 0)
      cancel_delayed_work_sync(&read_data_work);
   destroy_workqueue(wq);

   disable_irq(tms_jdts_i2c_client->irq);
   free_irq(tms_jdts_i2c_client->irq, NULL);
   i2c_del_driver(&tms_jdts_i2c_driver);

   if (tms_jdts_i2c_client != NULL) {
      i2c_unregister_device(tms_jdts_i2c_client);
      tms_jdts_i2c_client = NULL;
   }

   device_destroy(jdtsClass, MKDEV(majorNumber, 0));     // remove the device
   class_unregister(jdtsClass);                          // unregister the device class
   class_destroy(jdtsClass);                             // remove the device class
   unregister_chrdev(majorNumber, DEVICE_NAME);             // unregister the major number
   printk(KERN_INFO "TechartMicroSystems JDTS: Goodbye from the LKM!\n");
}

/** @brief This function is called whenever device is being opened from user space.
 *  @param inode A pointer to a general device read-only data
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int dev_open(struct inode *node, struct file *filep) {
   printk(KERN_INFO "TechartMicroSystems JDTS: Open the LKM!\n");
   return 0;
}

/** @brief This function is called whenever device is being read from user space i.e. data is
 *  being sent from the device to the user.
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 *  @param buffer The pointer to the buffer to which this function writes the data
 *  @param len The length of the b
 *  @param offset The offset if required
 */
static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset){
   int ret;

   printk(KERN_INFO "TechartMicroSystems JDTS: dev_read() called\n");

   if (!buffer || len != I2C_DATA_SIZE) {
      pr_err(KERN_INFO "TechartMicroSystems JDTS: Output buffer is NULL or data len equals zero\n");
      return -EINVAL;
   }

   if (sensor_mode == CMD_MEAS_MODE_BURST) {
      // wake up sensor
      ret = set_sensor_power(1);
      if (ret < 0) {
         return ret;
      }

      // TODO: somehow wait for IRQ GPIO set ready by the sensor
      mdelay(250);

      // the new sample is in 'sensor_data_buffer'
      // current mode is 'sensor_mode'
      mutex_lock(&read_data_mutex);
      ret = read_raw_temperatures();
      mutex_unlock(&read_data_mutex);
      if (ret < 0) {
         set_sensor_power(0);
         return ret;
      }

      // sleep off sensor
      set_sensor_power(0);
   }

   mutex_lock(&read_data_mutex);
   ret = copy_to_user(buffer, sensor_data_buffer, I2C_DATA_SIZE);
   mutex_unlock(&read_data_mutex);

   if (ret != 0) {
      pr_err(KERN_INFO "TechartMicroSystems JDTS: Cannot copy sensor data from kernel object to user space\n");
      return -ENOMEM;
   }

   printk(KERN_INFO "TechartMicroSystems JDTS: dev_read() finished OK\n");
   return 0;
}
 
/** @brief Write command takes two bytes array pointer (see above):
 *  [0] - command type
 *  [1] - command argument
 *  which basically can switch device`s power mode or change the measurement mode
 *  @param filep A pointer to a file object
 *  @param buffer The buffer to that contains the string to write to the device
 *  @param len The length of the array of data that is being passed in the const char buffer
 *  @param offset The offset if required
 */
static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset){
   int ret;
   u8 raw_buffer[2];

   printk(KERN_INFO "TechartMicroSystems JDTS: dev_write() called\n");

   if (len != sizeof(raw_buffer)) {
      pr_err(KERN_INFO "TechartMicroSystems JDTS: invalid data argument to write\n");
      return -EINVAL;
   }

   ret = copy_from_user(raw_buffer, buffer, sizeof(raw_buffer));
   if (ret != 0) {
      pr_err(KERN_INFO "TechartMicroSystems JDTS: Cannot copy sensor data from user object to kernel\n");
      return -ENOMEM;
   }

   ret = execute_command(raw_buffer[0], raw_buffer[1]);
   if (ret == 0) {
      printk(KERN_INFO "TechartMicroSystems JDTS: dev_write() call OK\n");
   }

   return 0;
}

static int execute_command(u8 type, u8 cmd) {
   int ret;
   struct i2c_msg write_message;
   write_message.addr = I2C_SLAVE_ADDRESS;
   write_message.flags = 0; // plain write

   if (type == CMD_TYPE_POWER) {
      if (cmd == CMD_POWER_WAKEUP) {
         ret = set_sensor_power(1);
         if (ret < 0) {
            pr_err(KERN_INFO "TechartMicroSystems JDTS: Cannot wake up the sensor\n");
            return ret;
         }
      } else if (cmd == CMD_POWER_SLEEP) {
         ret = set_sensor_power(0);
         if (ret < 0) {
            pr_err(KERN_INFO "TechartMicroSystems JDTS: Cannot sleep off the sensor\n");
            return ret;
         }
      } else {
         pr_err(KERN_INFO "TechartMicroSystems JDTS: invalid power mode to write\n");
         return -EINVAL;
      }
   } else if (type == CMD_TYPE_MEAS_MODE) {
      if (cmd == CMD_MEAS_MODE_CONT) {
         write_message.buf = (char*)i2c_meas_mode_continuous;
         write_message.len = sizeof(i2c_meas_mode_continuous);
         ret = i2c_transfer(tms_jdts_i2c_client->adapter, &write_message, 1);
         if (ret == 0)
            sensor_mode = CMD_MEAS_MODE_CONT;

      } else if (cmd == CMD_MEAS_MODE_BURST) {
         write_message.buf = (char*)i2c_meas_mode_burst;
         write_message.len = sizeof(i2c_meas_mode_burst);
         ret = i2c_transfer(tms_jdts_i2c_client->adapter, &write_message, 1);
         if (ret == 0)
            sensor_mode = CMD_MEAS_MODE_BURST;

      } else {
         pr_err(KERN_INFO "TechartMicroSystems JDTS: invalid measurement mode to write\n");
         return -EINVAL;
      }

      if (ret <= 0) {
         pr_err(KERN_INFO "TechartMicroSystems JDTS: Cannot write measurement mode command\n");
         return ret;
      }
   } else {
      pr_err(KERN_INFO "TechartMicroSystems JDTS: invalid command type to apply\n");
      return -EINVAL;
   }

   return 0;
}

static int set_sensor_power(u8 enabled) {
   gpio_set_value(GPIO_PWR_DOWN, enabled != 0);
   return 0;
}

static int read_raw_temperatures(void) {
   int ret;
   struct i2c_msg write_message;
   struct i2c_msg read_message;

   write_message.addr = I2C_SLAVE_ADDRESS;
   write_message.flags = 0; // plain write
   write_message.buf = (char*)i2c_read_temperatures_address;
   write_message.len = sizeof(i2c_read_temperatures_address);

   memset(sensor_data_buffer, 0, sizeof(sensor_data_buffer));
   read_message.addr = I2C_SLAVE_ADDRESS;
   read_message.flags = I2C_M_RD; // plain read
   read_message.buf = (char*)sensor_data_buffer;
   read_message.len = sizeof(sensor_data_buffer);

   // read out temperature data
   ret = i2c_transfer(tms_jdts_i2c_client->adapter, &write_message, 1);
   if (ret < 0) {
      pr_err(KERN_INFO "TechartMicroSystems JDTS: Cannot write temperatures data address. Error=%d\n", ret);
      return ret;
   }

   ret = i2c_transfer(tms_jdts_i2c_client->adapter, &read_message, 1);
   if (ret < 0) {
      pr_err(KERN_INFO "TechartMicroSystems JDTS: Cannot read temperatures from sensor. Error=%d\n", ret);
      return ret;
   }

   return 0;
}

static irq_handler_t jdts_data_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs) {
   printk(KERN_INFO "TechartMicroSystems JDTS: jdts_data_irq_handler\n");

   if (delayed_work_pending(&read_data_work) == 0)
      queue_delayed_work(wq, &read_data_work, read_data_work_delay);

   return (irq_handler_t)IRQ_HANDLED;
}

static void read_data_work_handler(struct work_struct *w) {
   int ret;

   printk(KERN_INFO "TechartMicroSystems JDTS: read_data_work_handler\n");

   mutex_lock(&read_data_mutex);
   ret = read_raw_temperatures();
   mutex_unlock(&read_data_mutex);

   printk(KERN_INFO "TechartMicroSystems JDTS: read_data_work_handler. Ret = %d\n", ret);
}
 
/** @brief A module must use the module_init() module_exit() macros from linux/init.h, which
 *  identify the initialization function at insertion time and the cleanup function (as
 *  listed above)
 */
module_init(jdts_temperature_init);
module_exit(jdts_temperature_exit);