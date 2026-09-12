// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026, Igor Belwon <igor.belwon@mentallysanemainliners.org>
 */

#ifndef BOOT_FDT_H_
#define BOOT_FDT_H_

extern void *abl_dtb_ptr;

void patch_dtb(void** dt);

#endif // BOOT_FDT_H_
