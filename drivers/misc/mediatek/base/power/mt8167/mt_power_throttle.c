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

#include <linux/kernel.h> /* ARRAY_SIZE */
#include <linux/slab.h> /* kzalloc */
#include "mt_static_power.h"	/* static power */
#include <linux/cpufreq.h>
#include "mt_power_throttle.h"

#define NR_MAX_OPP_TBL  8
#define NR_MAX_CPU      4

struct mt_cpu_power_info {
	unsigned int cpufreq_khz;
	unsigned int cpufreq_ncpu;
	unsigned int cpufreq_power;
};

struct mt_cpu_dvfs {
#if mt_cpu_dvfs_NO_NEED
	const char *name;
	unsigned int cpu_id;	                /* for cpufreq */
	unsigned int cpu_level;
	struct mt_cpu_dvfs_ops *ops;
#endif

	/* opp (freq) table */
	struct mt_cpu_freq_info *opp_tbl;       /* OPP table */
	int nr_opp_tbl;                         /* size for OPP table */
#if mt_cpu_dvfs_NO_NEED
	int idx_opp_tbl;                        /* current OPP idx */
	int idx_normal_max_opp;                 /* idx for normal max OPP */
	int idx_opp_tbl_for_late_resume;	/* keep the setting for late resume */

	struct cpufreq_frequency_table *freq_tbl_for_cpufreq; /* freq table for cpufreq */

#endif
	/* power table */
	struct mt_cpu_power_info *power_tbl;
	unsigned int nr_power_tbl;

#if mt_cpu_dvfs_NO_NEED
	/* enable/disable DVFS function */
	int dvfs_disable_count;
	bool dvfs_disable_by_ptpod;
	bool dvfs_disable_by_suspend;
	bool dvfs_disable_by_early_suspend;
	bool dvfs_disable_by_procfs;

	/* limit for thermal */
	unsigned int limited_max_ncpu;
	unsigned int limited_max_freq;
	unsigned int idx_opp_tbl_for_thermal_thro;
	unsigned int thermal_protect_limited_power;

	/* limit for HEVC (via. sysfs) */
	unsigned int limited_freq_by_hevc;

	/* limit max freq from user */
	unsigned int limited_max_freq_by_user;

	/* for ramp down */
	int ramp_down_count;
	int ramp_down_count_const;

#ifdef CONFIG_CPU_DVFS_DOWNGRADE_FREQ
	/* param for micro throttling */
	bool downgrade_freq_for_ptpod;
#endif

	int over_max_cpu;
	int ptpod_temperature_limit_1;
	int ptpod_temperature_limit_2;
	int ptpod_temperature_time_1;
	int ptpod_temperature_time_2;

	int pre_online_cpu;
	unsigned int pre_freq;
	unsigned int downgrade_freq;

	unsigned int downgrade_freq_counter;
	unsigned int downgrade_freq_counter_return;

	unsigned int downgrade_freq_counter_limit;
	unsigned int downgrade_freq_counter_return_limit;

	/* turbo mode */
	unsigned int turbo_mode;

	/* power throttling */
#ifdef CONFIG_CPU_DVFS_POWER_THROTTLING
	int idx_opp_tbl_for_pwr_thro;           /* keep the setting for power throttling */
	int idx_pwr_thro_max_opp;               /* idx for power throttle max OPP */
	unsigned int pwr_thro_mode;
#endif
#endif
};

struct mt_cpu_dvfs cpu_dvfs;

struct mt_cpu_freq_info opp_tbl_default[] = {
	OP(0, 0),
	OP(0, 0),
	OP(0, 0),
	OP(0, 0),
	OP(0, 0),
	OP(0, 0),
	OP(0, 0),
	OP(0, 0),
#if 0
	OP(1500000, 1300000),
	OP(1350000, 1250000),
	OP(1200000, 1200000),
	OP(1050000, 1150000),
	OP(871000,  1110000),
	OP(741000,  1080000),
	OP(624000,  1060000),
	OP(600000,  1050000),
#endif
};

static void _power_calculation(struct mt_cpu_dvfs *p, int oppidx, int ncpu)
{
/* TBD, need MN confirm */
#define CA53_8CORE_REF_POWER	2286	/* mW  */
#define CA53_6CORE_REF_POWER	1736	/* mW  */
#define CA53_4CORE_REF_POWER	1159	/* mW  */
#define CA53_REF_FREQ	1690000 /* KHz */
#define CA53_REF_VOLT	1000000	/* mV * 1000 */

	int ref_freq, ref_volt;
	int	p_dynamic = 0, p_leakage = 0;
	int possible_cpu = NR_MAX_CPU;

	ref_freq  = CA53_REF_FREQ;
	ref_volt  = CA53_REF_VOLT;

	switch (possible_cpu) {
	case 4:
		p_dynamic = CA53_4CORE_REF_POWER;
		break;
	case 6:
		p_dynamic = CA53_6CORE_REF_POWER;
		break;
	case 8:
	default:
		p_dynamic = CA53_8CORE_REF_POWER;
		break;
	}

	p_leakage = mt_spower_get_leakage(MT_SPOWER_CA7, p->opp_tbl[oppidx].cpufreq_volt / 1000, 65);

	p_dynamic = p_dynamic *
	(p->opp_tbl[oppidx].cpufreq_khz / 1000) / (ref_freq / 1000) *
	p->opp_tbl[oppidx].cpufreq_volt / ref_volt *
	p->opp_tbl[oppidx].cpufreq_volt / ref_volt +
	p_leakage;

	p->power_tbl[NR_MAX_OPP_TBL * (possible_cpu - 1 - ncpu) + oppidx].cpufreq_ncpu
		= ncpu + 1;
	p->power_tbl[NR_MAX_OPP_TBL * (possible_cpu - 1 - ncpu) + oppidx].cpufreq_khz
		= p->opp_tbl[oppidx].cpufreq_khz;
	p->power_tbl[NR_MAX_OPP_TBL * (possible_cpu - 1 - ncpu) + oppidx].cpufreq_power
		= p_dynamic * (ncpu + 1) / possible_cpu;
}

void init_mt_cpu_dvfs(struct mt_cpu_dvfs *p)
{
	p->nr_opp_tbl = ARRAY_SIZE(opp_tbl_default);
	p->opp_tbl = opp_tbl_default;
}

void dump_power_table(void)
{
	int	i;

	/* dump power table */
	for (i = 0; i < cpu_dvfs.nr_opp_tbl * NR_MAX_CPU; i++) {
		pr_info("[%d] = { .cpufreq_khz = %d,\t.cpufreq_ncpu = %d,\t.cpufreq_power = %d }\n",
		i,
		cpu_dvfs.power_tbl[i].cpufreq_khz,
		cpu_dvfs.power_tbl[i].cpufreq_ncpu,
		cpu_dvfs.power_tbl[i].cpufreq_power
		);
	}
}

int setup_power_table(struct mt_cpu_dvfs *p)
{
	unsigned int pwr_eff_tbl[NR_MAX_OPP_TBL][NR_MAX_CPU];
	int possible_cpu = NR_MAX_CPU;
	int i, j;
	int ret = 0;

	WARN_ON(p == NULL);

	if (p->power_tbl)
		goto out;

	/* allocate power table */
	memset((void *)pwr_eff_tbl, 0, sizeof(pwr_eff_tbl));
	p->power_tbl = kzalloc(p->nr_opp_tbl * possible_cpu * sizeof(struct mt_cpu_power_info), GFP_KERNEL);

	if (p->power_tbl == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	p->nr_power_tbl = p->nr_opp_tbl * possible_cpu;

	/* calc power and fill in power table */
	for (i = 0; i < p->nr_opp_tbl; i++) {
		for (j = 0; j < possible_cpu; j++) {
			if (pwr_eff_tbl[i][j] == 0)
				_power_calculation(p, i, j);
		}
	}

	/* sort power table */
	for (i = p->nr_opp_tbl * possible_cpu; i > 0; i--) {
		for (j = 1; j < i; j++) {
			if (p->power_tbl[j - 1].cpufreq_power < p->power_tbl[j].cpufreq_power) {
				struct mt_cpu_power_info tmp;

				tmp.cpufreq_khz					= p->power_tbl[j - 1].cpufreq_khz;
				tmp.cpufreq_ncpu				= p->power_tbl[j - 1].cpufreq_ncpu;
				tmp.cpufreq_power				= p->power_tbl[j - 1].cpufreq_power;

				p->power_tbl[j - 1].cpufreq_khz   = p->power_tbl[j].cpufreq_khz;
				p->power_tbl[j - 1].cpufreq_ncpu  = p->power_tbl[j].cpufreq_ncpu;
				p->power_tbl[j - 1].cpufreq_power = p->power_tbl[j].cpufreq_power;

				p->power_tbl[j].cpufreq_khz		= tmp.cpufreq_khz;
				p->power_tbl[j].cpufreq_ncpu	= tmp.cpufreq_ncpu;
				p->power_tbl[j].cpufreq_power	= tmp.cpufreq_power;
			}
		}
	}

	/* dump power table */
	dump_power_table();

out:
	return ret;
}

int setup_power_table_tk(void)
{
	init_mt_cpu_dvfs(&cpu_dvfs);

	if (cpu_dvfs.opp_tbl[0].cpufreq_khz == 0)
		return 0;

	return setup_power_table(&cpu_dvfs);
}

unsigned int limited_max_ncpu;
unsigned int limited_max_freq;
void mt_thermal_protect(unsigned int limited_power)
{
	struct mt_cpu_dvfs *p;
	int possible_cpu;
	int ncpu;
	int found = 0;
	int i;

	p = &cpu_dvfs;

	WARN_ON(p == NULL);

	possible_cpu = NR_MAX_CPU;

	/* no limited */
	if (limited_power == 0) {
		limited_max_ncpu = possible_cpu;
		limited_max_freq = (p->opp_tbl[0].cpufreq_khz); /* cpu_dvfs_get_max_freq(p); */
	} else {
		for (ncpu = possible_cpu; ncpu > 0; ncpu--) {
			for (i = 0; i < p->nr_opp_tbl * possible_cpu; i++) {
				/* p->power_tbl[i].cpufreq_ncpu == ncpu && */
				if (p->power_tbl[i].cpufreq_power <= limited_power) {
					limited_max_ncpu = p->power_tbl[i].cpufreq_ncpu;
					limited_max_freq = p->power_tbl[i].cpufreq_khz;
					found = 1;
					ncpu = 0; /* for break outer loop */
					break;
				}
			}
		}

		/* not found and use lowest power limit */
		if (!found) {
			limited_max_ncpu = p->power_tbl[p->nr_power_tbl - 1].cpufreq_ncpu;
			limited_max_freq = p->power_tbl[p->nr_power_tbl - 1].cpufreq_khz;
		}
	}

#if mt_cpu_dvfs_NO_NEED
	hps_set_cpu_num_limit(LIMIT_THERMAL, limited_max_ncpu, 0);
	/* correct opp idx will be calcualted in _thermal_limited_verify() */
	_mt_cpufreq_set(MT_CPU_DVFS_LITTLE, -1);

#endif
}

