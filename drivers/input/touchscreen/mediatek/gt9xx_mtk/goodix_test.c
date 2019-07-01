#include "tpd.h"
#include "include/tpd_gt9xx_common.h"
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>	/*proc */
#ifdef GTP_SELF_TEST

#define  GTP_ANDROID_TOUCH   	"android_touch"
#define  GTP_ITO_SELF_TEST     "self_test"
static ssize_t gtp_ito_test_write(struct file *file, const char __user *buffer,
        size_t count, loff_t *pos)
{
	return -EPERM;
}

/*
 * return value:
 * 0x0: open test failed, short test failed
 * 0x1: open test success, short test failed
 * 0x2: open test failed, short test success
 * 0x3: open test success, short test success
 */
/*
static void focal_press_powerkey(void)
{
    GTP_INFO(" %s POWER KEY event %x press\n",  KEY_POWER);
    input_report_key(tpd->dev, KEY_POWER, 1);
    input_sync(tpd->dev);
    GTP_INFO(" %s POWER KEY event %x release\n",  KEY_POWER);
    input_report_key(tpd->dev, KEY_POWER, 0);
    input_sync(tpd->dev);
}
*/

static int gtp_ito_test_show(struct seq_file *file, void* data);

static int gtp_ito_test_show(struct seq_file *file, void* data)
{
	int count=0;
	printk("%s----\n",__func__);
    return count;
}

static int gtp_ito_test_open_on (struct inode* inode, struct file* file)
{
	return single_open(file, gtp_ito_test_show, NULL);
}

static const struct file_operations ito_test_ops_on = {
    .owner = THIS_MODULE,
    .open = gtp_ito_test_open_on,
    .read = seq_read,
    .write = gtp_ito_test_write,
};

static struct proc_dir_entry *gtp_android_touch_proc=0;
static struct proc_dir_entry *gtp_ito_test_proc_on=0;
int ITO_TEST_COUNT=0;

int gtp_create_ito_test_proc(struct i2c_client *client)
{
	ITO_TEST_COUNT=0;
	gtp_android_touch_proc = proc_mkdir(GTP_ANDROID_TOUCH, NULL);
	gtp_ito_test_proc_on = proc_create(GTP_ITO_SELF_TEST, 0666,
					      gtp_android_touch_proc, &ito_test_ops_on);
		if (!gtp_ito_test_proc_on){
			dev_err(&client->dev, "create_proc_entry %s failed\n",
				GTP_ITO_SELF_TEST);}
		  else {
			dev_info(&client->dev, "create proc entry %s success\n",
				 GTP_ITO_SELF_TEST);
	            }
	return 0;
}

#endif

