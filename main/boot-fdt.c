// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026, Igor Belwon <igor.belwon@mentallysanemainliners.org>
 */

#include <drivers/ramdisk-handler.h>
#include <lib/debug.h>
#include <lib/libfdt/libfdt.h>
#include <main/boot-fdt.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

void *abl_dtb_ptr __attribute__((section(".data"))) = NULL;

static char fdt_buf[CONFIG_FDT_BUF_SIZE];

static int get_cells(const void *fdt, int node, const char *prop_name, int def_val)
{
	int len = 0;
	const fdt32_t *p = (const fdt32_t *)fdt_getprop(fdt, node, prop_name, &len);
	if (p && len >= (int)sizeof(fdt32_t)) {
		int val = (int)fdt32_to_cpu(*p);
		if (val > 0 && val <= 4)
			return val;
	}
	return def_val;
}

static void patch_memory_from_abl(void *target_fdt)
{
	const void *abl_fdt = abl_dtb_ptr;
	int ret, node;
	int mem_count = 0;

	if (!abl_fdt || (uintptr_t)abl_fdt < 0x10000 || ((uintptr_t)abl_fdt & 7) != 0) {
		printk(KERN_WARNING, "ABL DTB pointer invalid or NULL (%p), skipping memory patch\n", abl_fdt);
		return;
	}

	ret = fdt_check_header(abl_fdt);
	if (ret != 0) {
		printk(KERN_WARNING, "ABL DTB header invalid (%s) at %p, skipping memory patch\n",
		       fdt_strerror(ret), abl_fdt);
		return;
	}

	printk(KERN_INFO, "ABL DTB valid at %p (size %u bytes)\n",
	       abl_fdt, fdt_totalsize(abl_fdt));

	/* Pass 1: Count memory nodes in ABL DTB */
	fdt_for_each_subnode(node, abl_fdt, 0) {
		const char *name = fdt_get_name(abl_fdt, node, NULL);
		const char *type = fdt_getprop(abl_fdt, node, "device_type", NULL);
		bool is_mem = false;

		if (type && strcmp(type, "memory") == 0)
			is_mem = true;
		else if (name && (strcmp(name, "memory") == 0 || strncmp(name, "memory@", 7) == 0))
			is_mem = true;

		if (is_mem) {
			int len = 0;
			const void *reg = fdt_getprop(abl_fdt, node, "reg", &len);
			if (reg && len > 0)
				mem_count++;
		}
	}

	if (mem_count == 0) {
		printk(KERN_WARNING, "No memory nodes with reg found in ABL DTB, keeping default memory\n");
		return;
	}

	printk(KERN_INFO, "Found %d memory node(s) in ABL DTB\n", mem_count);

	/* Pass 2: Delete existing memory nodes in target_fdt */
	bool deleted;
	do {
		deleted = false;
		fdt_for_each_subnode(node, target_fdt, 0) {
			const char *name = fdt_get_name(target_fdt, node, NULL);
			const char *type = fdt_getprop(target_fdt, node, "device_type", NULL);

			if ((type && strcmp(type, "memory") == 0) ||
			    (name && (strcmp(name, "memory") == 0 || strncmp(name, "memory@", 7) == 0))) {
				printk(KERN_INFO, "Removing bundled memory node '%s'\n", name ? name : "unknown");
				fdt_del_node(target_fdt, node);
				deleted = true;
				break;
			}
		}
	} while (deleted);

	int abl_addr_cells = get_cells(abl_fdt, 0, "#address-cells", 2);
	int abl_size_cells = get_cells(abl_fdt, 0, "#size-cells", 2);

	int tgt_addr_cells = get_cells(target_fdt, 0, "#address-cells", 2);
	int tgt_size_cells = get_cells(target_fdt, 0, "#size-cells", 2);

	/* Pass 3: Copy memory nodes from ABL DTB to target_fdt */
	fdt_for_each_subnode(node, abl_fdt, 0) {
		const char *name = fdt_get_name(abl_fdt, node, NULL);
		const char *type = fdt_getprop(abl_fdt, node, "device_type", NULL);
		bool is_mem = false;

		if (type && strcmp(type, "memory") == 0)
			is_mem = true;
		else if (name && (strcmp(name, "memory") == 0 || strncmp(name, "memory@", 7) == 0))
			is_mem = true;

		if (!is_mem)
			continue;

		int reg_len = 0;
		const void *reg = fdt_getprop(abl_fdt, node, "reg", &reg_len);
		if (!reg || reg_len <= 0)
			continue;

		int new_node = fdt_add_subnode(target_fdt, 0, name);
		if (new_node < 0) {
			if (new_node == -FDT_ERR_EXISTS)
				new_node = fdt_subnode_offset(target_fdt, 0, name);
			else {
				printk(KERN_ERR, "Failed to create memory node '%s': %s\n",
				       name, fdt_strerror(new_node));
				continue;
			}
		}

		fdt_setprop_string(target_fdt, new_node, "device_type", "memory");

		if (abl_addr_cells == tgt_addr_cells && abl_size_cells == tgt_size_cells) {
			ret = fdt_setprop(target_fdt, new_node, "reg", reg, reg_len);
		} else {
			/* Re-encode entries if cell sizes differ */
			uint32_t reencoded[64];
			int abl_entry_bytes = (abl_addr_cells + abl_size_cells) * 4;
			int tgt_entry_bytes = (tgt_addr_cells + tgt_size_cells) * 4;
			int num_entries = reg_len / abl_entry_bytes;
			int reencoded_len = num_entries * tgt_entry_bytes;

			if (reencoded_len <= (int)sizeof(reencoded)) {
				const uint32_t *src = (const uint32_t *)reg;
				uint32_t *dst = reencoded;
				for (int i = 0; i < num_entries; i++) {
					uint64_t base = 0, size = 0;
					if (abl_addr_cells == 1) {
						base = fdt32_to_cpu(*src++);
					} else {
						base = ((uint64_t)fdt32_to_cpu(src[0]) << 32) | fdt32_to_cpu(src[1]);
						src += 2;
					}
					if (abl_size_cells == 1) {
						size = fdt32_to_cpu(*src++);
					} else {
						size = ((uint64_t)fdt32_to_cpu(src[0]) << 32) | fdt32_to_cpu(src[1]);
						src += 2;
					}

					if (tgt_addr_cells == 1) {
						*dst++ = cpu_to_fdt32((uint32_t)base);
					} else {
						*dst++ = cpu_to_fdt32((uint32_t)(base >> 32));
						*dst++ = cpu_to_fdt32((uint32_t)base);
					}
					if (tgt_size_cells == 1) {
						*dst++ = cpu_to_fdt32((uint32_t)size);
					} else {
						*dst++ = cpu_to_fdt32((uint32_t)(size >> 32));
						*dst++ = cpu_to_fdt32((uint32_t)size);
					}
				}
				ret = fdt_setprop(target_fdt, new_node, "reg", reencoded, reencoded_len);
			} else {
				ret = fdt_setprop(target_fdt, new_node, "reg", reg, reg_len);
			}
		}

		if (ret < 0) {
			printk(KERN_ERR, "Failed to set reg on '%s': %s\n",
			       name, fdt_strerror(ret));
			continue;
		}

		printk(KERN_INFO, "Added memory node '%s' (%d bytes reg):\n", name, reg_len);

		/* Display memory ranges */
		int entry_size = (abl_addr_cells + abl_size_cells) * 4;
		if (entry_size > 0 && (reg_len % entry_size == 0)) {
			const uint32_t *p = (const uint32_t *)reg;
			int num_entries = reg_len / entry_size;
			for (int i = 0; i < num_entries; i++) {
				uint64_t base = 0, size = 0;
				if (abl_addr_cells == 1) {
					base = fdt32_to_cpu(*p++);
				} else {
					base = ((uint64_t)fdt32_to_cpu(p[0]) << 32) | fdt32_to_cpu(p[1]);
					p += 2;
				}
				if (abl_size_cells == 1) {
					size = fdt32_to_cpu(*p++);
				} else {
					size = ((uint64_t)fdt32_to_cpu(p[0]) << 32) | fdt32_to_cpu(p[1]);
					p += 2;
				}
				printk(KERN_INFO, "  RAM: 0x%lx - 0x%lx (%lu MB)\n",
				       (unsigned long)base,
				       (unsigned long)(base + size),
				       (unsigned long)(size >> 20));
			}
		}
	}
}

static void patch_chosen_from_abl(void *target_fdt)
{
	const void *abl_fdt = abl_dtb_ptr;
	int abl_chosen, target_chosen, len;

	if (!abl_fdt || fdt_check_header(abl_fdt) != 0)
		return;

	abl_chosen = fdt_path_offset(abl_fdt, "/chosen");
	if (abl_chosen < 0)
		return;

	target_chosen = fdt_path_offset(target_fdt, "/chosen");
	if (target_chosen < 0) {
		target_chosen = fdt_add_subnode(target_fdt, 0, "chosen");
		if (target_chosen < 0)
			return;
	}

	const void *seed = fdt_getprop(abl_fdt, abl_chosen, "kaslr-seed", &len);
	if (seed && (len == 4 || len == 8)) {
		fdt_setprop(target_fdt, target_chosen, "kaslr-seed", seed, len);
		printk(KERN_INFO, "Copied kaslr-seed from ABL DTB (%d bytes)\n", len);
	}

	const void *rng = fdt_getprop(abl_fdt, abl_chosen, "rng-seed", &len);
	if (rng && len > 0) {
		fdt_setprop(target_fdt, target_chosen, "rng-seed", rng, len);
		printk(KERN_INFO, "Copied rng-seed from ABL DTB (%d bytes)\n", len);
	}
}

void patch_dtb(void** dt)
{
	int ret;

	ret = ramdisk_handler_patch_dtb(*dt, &fdt_buf, sizeof(fdt_buf));
	if (ret != 0) {
		printk(KERN_ERR, "failed to patch dtb\n");
		return;
	}

	patch_memory_from_abl(&fdt_buf);
	patch_chosen_from_abl(&fdt_buf);

	*dt = &fdt_buf;
}
