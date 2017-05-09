#include <linux/version.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/proc_fs.h>
#include <linux/kthread.h>
#include <linux/miscdevice.h>
#include <linux/i2c.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <mach/ftpmu010.h>
#include <asm/gpio.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/poll.h>
#include <asm-generic/gpio.h>

#include "platform_mb.h"

#define CH_SEL_0 (28 + 32)
#define CH_SEL_1 (29 + 32)

#define ALARM_LED (0 + 32)
#define RUN_LED (4 + 0)

static struct gpio chsel_gpios[] = {
	{CH_SEL_0, GPIOF_OUT_INIT_LOW, "CH_SEL_0"},
	{CH_SEL_1, GPIOF_OUT_INIT_LOW, "CH_SEL_1"},
	{ALARM_LED, GPIOF_OUT_INIT_LOW, "ALARM_LED"},
	{RUN_LED, GPIOF_OUT_INIT_LOW, "RUN_LED"},
};

struct dswmb_dev_t {
	char                name[16];   ///< device name
	struct semaphore    lock;       ///< device locker
	struct miscdevice   miscdev;    ///< device node for ioctl access
};

static struct dswmb_dev_t dswmb_ch_dev;

void set_comch(unsigned char nch)
{
	gpio_set_value(CH_SEL_0, nch & 0x01);
	gpio_set_value(CH_SEL_1, (nch & 0x02) ? 1 : 0);
}

void set_ledstate(struct state_led *pStateLed)
{
	if (pStateLed->nLed == EM_RUN_LED)
		gpio_set_value(RUN_LED, pStateLed->nAct);
	else if (pStateLed->nLed == EM_ALARM_LED)
		gpio_set_value(ALARM_LED, pStateLed->nAct);
}

static int dswmb_miscdev_open(struct inode *inode, struct file *file)
{
	struct dswmb_dev_t *pdev = NULL;
	pdev = &dswmb_ch_dev;
	file->private_data = (void *)pdev;
	return 0;
}

static int dswmb_miscdev_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long dswmb_miscdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	union dswmb_ioc_data tmp;
	struct dswmb_dev_t *pdev = (struct dswmb_dev_t *)file->private_data;
	down(&pdev->lock);
	if(_IOC_TYPE(cmd) != DSWMB_IOC_MAGIC) {
		ret = -ENOIOCTLCMD;
		goto exit;
	}
	switch(cmd){
	case DSWMB_SET_COMCH:
		if(copy_from_user(&tmp, (void __user *)arg, sizeof(tmp))) {
			ret = -EFAULT;
			goto exit;
		}
		set_comch(tmp.ncomch);
		break;
	case DSWMB_SET_LED:
		if(copy_from_user(&tmp, (void __user *)arg, sizeof(tmp))) {
			ret = -EFAULT;
			goto exit;
		}
		set_ledstate(&tmp.nstateled);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}
exit:
	up(&pdev->lock);
	return ret;
}

static struct file_operations dswmb_miscdev_fops = {
    .owner          = THIS_MODULE,
    .open            = dswmb_miscdev_open,
    .release         = dswmb_miscdev_release,
    .unlocked_ioctl = dswmb_miscdev_ioctl,
};

static int dswmb_miscdev_init(void)
{
	int ret;
	/* clear */
	memset(&dswmb_ch_dev.miscdev, 0, sizeof(struct miscdevice));
	/* create device node */
	dswmb_ch_dev.miscdev.name  = dswmb_ch_dev.name;
	dswmb_ch_dev.miscdev.minor = MISC_DYNAMIC_MINOR;
	dswmb_ch_dev.miscdev.fops  = (struct file_operations *)&dswmb_miscdev_fops;
	ret = misc_register(&dswmb_ch_dev.miscdev);
	if(ret) {
		printk("create %s misc device node failed!\n", dswmb_ch_dev.name);
		dswmb_ch_dev.miscdev.name = 0;
		goto exit;
	}
exit:
	return ret;
}

void dswmb_comch_init(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	sema_init(&dswmb_ch_dev.lock, 1);
#else
	init_MUTEX(&dswmb_ch_dev.lock);
#endif
	sprintf(dswmb_ch_dev.name, "dswmb_comch");
	if (gpio_request_array(chsel_gpios, ARRAY_SIZE(chsel_gpios)) < 0)
		printk("%s:%d, gpio_request_array failed!\n", __FILE__, __LINE__);
	dswmb_miscdev_init();
}

void dswmb_comch_uninit(void)
{
	misc_deregister(&dswmb_ch_dev.miscdev);
	gpio_free_array(chsel_gpios, ARRAY_SIZE(chsel_gpios));
}

