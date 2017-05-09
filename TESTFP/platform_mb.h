#ifndef _FE_PLATFORM_H_
#define _FE_PLATFORM_H_

#include "dswmb.h"
/*************************************************************************************
 *  Platform Version Definition
 *************************************************************************************/
#define PLAT_COMMON_VERSION    170221

void dswmb_comch_init(void);
void dswmb_comch_uninit(void);

void dswmb_key_init(void);
void dswmb_key_uninit(void);

#endif  /* _FE_PLATFORM_H_ */
