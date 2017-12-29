/*
 * Copyright (C) 2017 MediaTek Inc.
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

#ifndef	__ERRNO_H__
#define	__ERRNO_H__

/* Nand Driver Extended Error Codes */
#define NAND_OK		0

#ifndef EIO
#define	EIO		5 /* I/O error */
#define	ENOMEM		12 /* Out of memory */
#define	EFAULT		14 /* Bad address */
#define	EBUSY		16 /* Device or resource busy */
#define	ENODEV		19 /* No such device */
#define	EINVAL		22 /* Invalid argument */
#define	ENOSPC		28 /* No space left on device */
#define	EOPNOTSUPP	95 /* Operation not supported on transport endpoint */
#define ETIMEDOUT	110 /* Connection timed out */
#endif

#define ENANDFLIPS	1024 /* Too many bitflips, corrected */
#define ENANDREAD	1025 /* Read fail, can't correct */
#define ENANDWRITE	1026 /* Write fail */
#define ENANDERASE	1027 /* Erase fail */
#define ENANDBAD	1028 /* Bad block */

#define IS_NAND_ERR(err)	((err) >= -ENANDBAD && (err) <= -ENANDFLIPS)

#endif /* __ERRNO_H__ */
