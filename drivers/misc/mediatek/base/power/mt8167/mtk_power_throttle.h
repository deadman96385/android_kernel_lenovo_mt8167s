/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#ifndef MT_POWERTHROTTLE_H
#define MT_POWERTHROTTLE_H

#define mt_cpu_dvfs_NO_NEED	0


struct mt_cpu_freq_info {
	unsigned int cpufreq_khz;
	unsigned int cpufreq_volt;  /* mv * 1000 */
#if mt_cpu_dvfs_NO_NEED
	const unsigned int cpufreq_volt_org;    /* mv * 1000 */
#endif
};

#if mt_cpu_dvfs_NO_NEED
#define OP(khz, volt) {            \
	.cpufreq_khz = khz,             \
	.cpufreq_volt = volt,           \
	.cpufreq_volt_org = volt,       \
}
#else
#define OP(khz, volt) {            \
	.cpufreq_khz = khz,             \
	.cpufreq_volt = volt,           \
}
#endif

extern struct mt_cpu_freq_info opp_tbl_default[];
extern int setup_power_table_tk(void);
extern void dump_power_table(void);

#endif

