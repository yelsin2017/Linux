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
#include <linux/interrupt.h>

#include "platform_mb.h"

#define KEY_ENTER	(24 + 0)
#define KEY_ESC		(25 + 0)
#define KEY_UP 		(26 + 0)
#define KEY_DOWN	(27 + 0)
#define KEY_LEFT		(2 + 32)
#define KEY_RIGHT	(1 + 32)

#define KEY_ENTER_VAL	0x0d
#define KEY_ESC_VAL		0x1b
#define KEY_UP_VAL 		0x26
#define KEY_DOWN_VAL	0x28
#define KEY_LEFT_VAL	0x25
#define KEY_RIGHT_VAL	0x27

static struct gpio key_gpios[] = {
	{KEY_ENTER, GPIOF_IN, "ENTER"},
	{KEY_ESC, GPIOF_IN, "ESC"},
	{KEY_UP, GPIOF_IN, "UP"},
	{KEY_DOWN, GPIOF_IN, "DOWN"},
	{KEY_LEFT, GPIOF_IN, "LEFT"},
	{KEY_RIGHT, GPIOF_IN, "RIGHT"},
};

struct key_irq_desc{
	int nID;
	int nPin;
	struct gpio_interrupt_mode nMode;
	unsigned char nKeyVal;
	const char* pName;
};

static struct key_irq_desc key_irqs[] = {
	{
	.nID = 0,
	.nPin = KEY_ENTER,
	.nMode = {
			.trigger_method = GPIO_INT_TRIGGER_EDGE,
			.trigger_edge_nr = GPIO_INT_BOTH_EDGE,
			},
	.nKeyVal = KEY_ENTER_VAL,
	.pName = "ENTER",
	},
	{
	.nID = 1,
	.nPin = KEY_ESC,
	.nMode = {
			.trigger_method = GPIO_INT_TRIGGER_EDGE,
			.trigger_edge_nr = GPIO_INT_BOTH_EDGE,
			},
	.nKeyVal = KEY_ESC_VAL,
	.pName = "ESC",
	},
	{
	.nID = 2,
	.nPin = KEY_UP,
	.nMode = {
			.trigger_method = GPIO_INT_TRIGGER_EDGE,
			.trigger_edge_nr = GPIO_INT_BOTH_EDGE,
			},
	.nKeyVal = KEY_UP_VAL,
	.pName = "UP",
	},
	{
	.nID = 3,
	.nPin = KEY_DOWN,
	.nMode = {
			.trigger_method = GPIO_INT_TRIGGER_EDGE,
			.trigger_edge_nr = GPIO_INT_BOTH_EDGE,
			},
	.nKeyVal = KEY_DOWN_VAL,
	.pName = "DOWN",
	},
	{
	.nID = 4,
	.nPin = KEY_LEFT,
	.nMode = {
			.trigger_method = GPIO_INT_TRIGGER_EDGE,
			.trigger_edge_nr = GPIO_INT_BOTH_EDGE,
			},
	.nKeyVal = KEY_LEFT_VAL,
	.pName = "LEFT",
	},
	{
	.nID = 5,
	.nPin = KEY_RIGHT,
	.nMode = {
			.trigger_method = GPIO_INT_TRIGGER_EDGE,
			.trigger_edge_nr = GPIO_INT_BOTH_EDGE,
			},
	.nKeyVal = KEY_RIGHT_VAL,
	.pName = "RIGHT",
	},
};

struct dswmb_key_dev_t {
	char                name[16];   ///< device name
	struct semaphore    lock;       ///< device locker
	struct miscdevice   miscdev;    ///< device node for ioctl access

	int gTrig;
	wait_queue_head_t dswmb_key_q;
};

static struct dswmb_key_dev_t dswmb_key_dev;
static unsigned char gKey[6] = {0, 0, 0, 0, 0, 0};

static irqreturn_t key_interrupt(int irq, void* dev_id)
{
	struct key_irq_desc *key_irq = (struct key_irq_desc *)dev_id;
	int down;
	gpio_interrupt_clear(key_irq->nPin);
	down = !gpio_get_value(key_irq->nPin);
	if (down != (gKey[key_irq->nID] & 0x7f)){
		gKey[key_irq->nID] = (down ? 0x00 : 0x80) | key_irq->nKeyVal;
		dswmb_key_dev.gTrig = key_irq->nID;
		wake_up_interruptible(&dswmb_key_dev.dswmb_key_q);
	}
	else
		dswmb_key_dev.gTrig = -1;
	return IRQ_HANDLED;
}

static int dswmb_miscdev_open(struct inode *inode, struct file *file)
{
	struct dswmb_key_dev_t *pdev = NULL;
	int i, err;
	pdev = &dswmb_key_dev;
	pdev->gTrig = -1;
	init_waitqueue_head(&pdev->dswmb_key_q);
	file->private_data = (void *)pdev;
	err = 0;
	for (i = 0; i < ARRAY_SIZE(key_irqs); i++){
		gpio_interrupt_enable(key_irqs[i].nPin);
		gpio_interrupt_setup(key_irqs[i].nPin, &key_irqs[i].nMode);
		err = request_irq(gpio_to_irq(key_irqs[i].nPin), key_interrupt, IRQF_SHARED, key_irqs[i].pName, (void*)&key_irqs[i]);
		if (err)
			break;
	}
	if (err){
		i--;
		for (; i >= 0; i++){
			gpio_interrupt_disable(key_irqs[i].nPin);
			free_irq(gpio_to_irq(key_irqs[i].nPin), (void*)&key_irqs[i]);
		}
		printk("dswmb register key irq failed!\n");
	}
	return 0;
}

static int dswmb_miscdev_close(struct inode *inode, struct file *file)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(key_irqs); i++){
		gpio_interrupt_disable(key_irqs[i].nPin);
		free_irq(gpio_to_irq(key_irqs[i].nPin), (void*)&key_irqs[i]);
	}
	return 0;
}

static ssize_t dswmb_miscdev_read(struct file *filp, char *buf, size_t len, loff_t *off)
{
	struct dswmb_key_dev_t *pdev = (struct dswmb_key_dev_t *)filp->private_data;
	unsigned long err;
	if (pdev->gTrig < 0)
		return 0;
	err = copy_to_user(buf, (char*)&gKey[pdev->gTrig], 1);
	pdev->gTrig = -1;
	return 1;
}

static unsigned int dswmb_misc_dev_poll(struct file *filp, poll_table *wait)
{
	struct dswmb_key_dev_t *pdev = (struct dswmb_key_dev_t *)filp->private_data;
	unsigned int mask = 0;
	poll_wait(filp, &pdev->dswmb_key_q, wait);
	if (pdev->gTrig > 0)
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

static struct file_operations dswmb_miscdev_fops = {
    .owner		= THIS_MODULE,
    .open		= dswmb_miscdev_open,
    .release	= dswmb_miscdev_close,
    .read		= dswmb_miscdev_read,
    .poll		= dswmb_misc_dev_poll,
};

static int dswdb_miscdev_init(void)
{
	int ret;
	/* clear */
	memset(&dswmb_key_dev.miscdev, 0, sizeof(struct miscdevice));
	/* create device node */
	dswmb_key_dev.miscdev.name  = dswmb_key_dev.name;
	dswmb_key_dev.miscdev.minor = MISC_DYNAMIC_MINOR;
	dswmb_key_dev.miscdev.fops  = (struct file_operations *)&dswmb_miscdev_fops;
	ret = misc_register(&dswmb_key_dev.miscdev);
	if(ret) {
		printk("create %s misc device node failed!\n", dswmb_key_dev.name);
		dswmb_key_dev.miscdev.name = 0;
		goto exit;
	}
exit:
	return ret;
}

void dswmb_key_init(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	sema_init(&dswmb_key_dev.lock, 1);
#else
	init_MUTEX(&dswmb_key_dev.lock);
#endif
	sprintf(dswmb_key_dev.name, "dswmb_key");
	if (gpio_request_array(key_gpios, ARRAY_SIZE(key_gpios)) < 0)
		printk("%s:%d, gpio_request_array failed!\n", __FILE__, __LINE__);
	dswdb_miscdev_init();
}

void dswmb_key_uninit(void)
{
	misc_deregister(&dswmb_key_dev.miscdev);
	gpio_free_array(key_gpios, ARRAY_SIZE(key_gpios));
}

