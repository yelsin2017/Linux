#ifndef _DSWMB_H_
#define _DSWMB_H_

#include <linux/ioctl.h>

#define LED_ON 0
#define LED_OFF 1

typedef enum _LEDTYPE{
	EM_RUN_LED,
	EM_ALARM_LED,
} LEDTYPE;

struct state_led{
	LEDTYPE nLed;
	unsigned char nAct;
};

union dswmb_ioc_data {
	unsigned char ncomch;
	struct state_led nstateled;
};

#define DSWMB_IOC_MAGIC       'n'

#define DSWMB_SET_COMCH  _IOWR(DSWMB_IOC_MAGIC, 1, union dswmb_ioc_data)
#define DSWMB_SET_LED  _IOWR(DSWMB_IOC_MAGIC, 2, union dswmb_ioc_data)


#endif  /* _FE_DEC_NVP1914C_H_ */
