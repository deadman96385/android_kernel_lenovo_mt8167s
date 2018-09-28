#ifndef __GSL_TS_DRIVER_H__
#define __GSL_TS_DRIVER_H__
/*********************************/
#define TPD_HAVE_BUTTON		//按键宏
#define GSL_ALG_ID			//有没有id算法
#define GSL_COMPATIBLE_CHIP	//芯片兼容宏
#define GSL_THREAD_EINT		//线程中断
#define GSL_DEBUG			//调试
#define TPD_PROC_DEBUG		//adb调试
//#define GSL_TIMER			//定时器宏

#if 0	// defined(DROI_PRO_PU6A_RF)|| defined(DROI_PRO_PU5_XDH)
#define TPD_PROXIMITY
#else
//#define TPD_PROXIMITY		//距离传感器宏
#endif

#define GSL_GESTURE

//#define GSL_GPIO_IDT_TP	//GPIO兼容	
//#define GSL_DRV_WIRE_IDT_TP	//驱动线兼容

#define GSL9XX_VDDIO_1800    0

#define TPD_POWER_VTP28_USE_VCAMA   //[add for mx2116 2015-11-03]


#define GSL_PAGE_REG    0xf0
#define GSL_CLOCK_REG   0xe4
#define GSL_START_REG   0xe0
#define POWE_FAIL_REG   0xbc
#define TOUCH_INFO_REG  0x80
#define TPD_DEBUG_TIME	0x20130424
struct gsl_touch_info
{
	int x[10];
	int y[10];
	int id[10];
	int finger_num;	
};

struct gsl_ts_data {
	struct i2c_client *client;
	struct workqueue_struct *wq;
	struct work_struct work;
	unsigned int irq;
	//struct early_suspend pm;
};

/*button*/
#ifdef TPD_HAVE_BUTTON
#define TPD_KEY_COUNT           3
#define TPD_KEYS                {KEY_MENU,KEY_HOMEPAGE,KEY_BACK}
#if defined (DROI_PRO_PU6A_RF_R9002) || defined(DROI_PRO_PU6A_DNT_P09)
#define TPD_KEYS_DIM            {{120,1540,20,20},{360,1540,20,20},{600,1540,20,20}}
#else
#define TPD_KEYS_DIM            {{80,1030,20,20},{240,1030,20,20},{460,1030,20,20}}
#endif
#endif


#ifdef GSL_ALG_ID 
extern unsigned int gsl_mask_tiaoping(void);
extern unsigned int gsl_version_id(void);
extern void gsl_alg_id_main(struct gsl_touch_info *cinfo);
extern void gsl_DataInit(int *ret);
#endif

#if defined (DROI_PRO_PU6A_RF_R9002)
    #include "gsl_ts_fw_pu6a_rf_r9002.h"
#elif defined(DROI_PRO_PU6A_DNT_P09)
    #include "gsl_ts_fw_pu6a_dnt_p09.h"
#elif defined(DROI_PRO_PU5_LG)
    #include "gsl_ts_fw_pu5_lg.h"
#elif defined(DROI_PRO_PU5_RF)
    #include "gsl_ts_fw_pu5_rf.h"
#elif defined(DROI_PRO_PQ7_LG)
    #include "gsl_ts_fw_pq7_lg.h"
#elif defined(DROI_PRO_PU5_XDH)
	#include "gsl_ts_fw_pu5_xdh.h"
#else
/* Fixme mem Alig */
/*
struct fw_data
{
    u32 offset : 8;
    u32 : 0;
    u32 val;
};
*/
#ifdef GSL_DRV_WIRE_IDT_TP
#include "gsl_idt_tp.h"
#endif

#include "gsl_ts_fw.h"
#endif

static unsigned char gsl_cfg_index = 0;

struct fw_config_type
{
	const struct fw_data *fw;
	unsigned int fw_size;
	unsigned int *data_id;
	unsigned int data_size;
};
static struct fw_config_type gsl_cfg_table[9] = {
/*0*/{GSLX680_FW,(sizeof(GSLX680_FW)/sizeof(struct fw_data)),gsl_config_data_id,(sizeof(gsl_config_data_id)/4)},
/*1*///{GSLX68X_FW_ZHENGHAI,(sizeof(GSLX68X_FW_ZHENGHAI)/sizeof(struct fw_data)),gsl_config_data_id_zhenghai,(sizeof(gsl_config_data_id_zhenghai)/4)},
/*2*///{GSLX68X_FW_DUNZHENG,(sizeof(GSLX68X_FW_DUNZHENG)/sizeof(struct fw_data)),gsl_config_data_id_denzheng,(sizeof(gsl_config_data_id_denzheng)/4)},
};

#endif
