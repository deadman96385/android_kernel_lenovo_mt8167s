/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/*****************************************************************************
 *
 * Filename:
 * ---------
 *   gc0312yuv_Sensor.h
 *
 * Project:
 * --------
 *   MAUI
 *
 * Description:
 * ------------
 *   Image sensor driver declare and macro define in the header file.
 *
 * Author:
 * -------
 *   Mormo
 *
 *=============================================================
 *             HISTORY
 * Below this line, this part is controlled by PVCS VM. DO NOT MODIFY!!
 *------------------------------------------------------------------------------
 *
 * 2011/10/25 Firsty Released By Mormo;
 *
 *
 *------------------------------------------------------------------------------
 * Upper this line, this part is controlled by PVCS VM. DO NOT MODIFY!!
 *=============================================================
 ******************************************************************************/

#ifndef __GC0312_SENSOR_H
#define __GC0312_SENSOR_H


extern int iReadRegI2C(u8 *a_pSendData, u16 a_sizeSendData, u8 *a_pRecvData, u16 a_sizeRecvData,
		       u16 i2cId);
extern int iWriteRegI2C(u8 *a_pSendData, u16 a_sizeSendData, u16 i2cId);

#define VGA_PERIOD_PIXEL_NUMS						694
#define VGA_PERIOD_LINE_NUMS						488

#define IMAGE_SENSOR_VGA_GRAB_PIXELS			0
#define IMAGE_SENSOR_VGA_GRAB_LINES			1

#define IMAGE_SENSOR_VGA_WIDTH					(640)
#define IMAGE_SENSOR_VGA_HEIGHT					(480)

#define IMAGE_SENSOR_PV_WIDTH					(IMAGE_SENSOR_VGA_WIDTH - 8)
#define IMAGE_SENSOR_PV_HEIGHT					(IMAGE_SENSOR_VGA_HEIGHT - 6)

#define IMAGE_SENSOR_FULL_WIDTH					(IMAGE_SENSOR_VGA_WIDTH - 8)
#define IMAGE_SENSOR_FULL_HEIGHT					(IMAGE_SENSOR_VGA_HEIGHT - 6)

#define GC0312_WRITE_ID							0x42
#define GC0312_READ_ID								0x43


enum {
	GC0312_RGB_Gamma_m1 = 0,
	GC0312_RGB_Gamma_m2,
	GC0312_RGB_Gamma_m3,
	GC0312_RGB_Gamma_m4,
	GC0312_RGB_Gamma_m5,
	GC0312_RGB_Gamma_m6,
	GC0312_RGB_Gamma_night
} GC0312_GAMMA_TAG;



UINT32 GC0312Open(void);
UINT32 GC0312Control(MSDK_SCENARIO_ID_ENUM ScenarioId,
	MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *pImageWindow,
	MSDK_SENSOR_CONFIG_STRUCT *pSensorConfigData);
UINT32 GC0312FeatureControl(MSDK_SENSOR_FEATURE_ENUM FeatureId, UINT8 *pFeaturePara, UINT32 *pFeatureParaLen);
UINT32 GC0312GetInfo(MSDK_SCENARIO_ID_ENUM ScenarioId, MSDK_SENSOR_INFO_STRUCT *pSensorInfo,
			MSDK_SENSOR_CONFIG_STRUCT *pSensorConfigData);
UINT32 GC0312GetResolution(MSDK_SENSOR_RESOLUTION_INFO_STRUCT *pSensorResolution);
UINT32 GC0312Close(void);

#endif /* __SENSOR_H */

