/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025, Ivaylo Ivanov <ivo.ivanov.ivanov1@gmail.com>
 */
#include "asmdefs.h"

#ifdef CONFIG_LINUX_KRNL_HEADER_IMG
#include "linux-kernel-image-header.h"
#endif

.extern _reloc_size
.extern _start

ENTRY_ALIAS(_head)
#ifdef CONFIG_LINUX_KRNL_HEADER_IMG
	linux_image_header _reloc_entry
#else
	adr	x1, _relocated_uni
	b	 _reloc_entry
#endif

ENTRY_ALIAS(_rel_stack_base)
	.quad _start

ENTRY_ALIAS(_rel_stack_size)
	.quad _reloc_size

ENTRY_ALIAS( _reloc_entry)
	adr	x4, _start
	ldr	x5, _rel_stack_base
	cmp	x4, x5
	beq	_entry
	ldr	x6,  _rel_stack_size
	adr     x8, _head
	sub     x9, x4, x5
	sub     x10, x4, x8
	sub     x11, x8, x9
	and     x13, x10, #0xf
	sub     x10, x10, x13

_copy_stub:
	cbz     x10, _copy_stub_tail
	ldp     x2, x3, [x8], #16
	stp     x2, x3, [x11], #16
	subs    x10, x10, #16
	b.ne    _copy_stub

_copy_stub_tail:
	cbz     x13, _copy_stub_done

_copy_stub_tail_loop:
	ldrb    w2, [x8], #1
	strb    w2, [x11], #1
	subs    x13, x13, #1
	b.ne    _copy_stub_tail_loop

_copy_stub_done:
	adr     x12, _reloc_resume
	sub     x12, x12, x9
	br      x12

_reloc_resume:
	BTI_C
	and     x13, x6, #0xf
	sub     x6, x6, x13
	add     x7, x4, x6
	cmp     x5, x4
	b.lo    _copy_forward

_copy_check_overlap:
	cmp     x5, x7
	b.hs    _copy_forward
	add     x4, x4, x6
	add     x5, x5, x6
	cbz     x13, _copy_backward

_copy_backward_tail:
	sub     x4, x4, #1
	sub     x5, x5, #1
	ldrb    w2, [x4]
	strb    w2, [x5]
	subs    x13, x13, #1
	b.ne    _copy_backward_tail

	b       _copy_backward

_copy_backward:
	cbz     x6, _entry_prep
	ldp     x2, x3, [x4, #-16]!
	stp     x2, x3, [x5, #-16]!
	subs    x6, x6, #16
	b.ne    _copy_backward
	b       _entry_prep

_copy_forward:
	cbz     x6, _copy_forward_tail
	ldp     x2, x3, [x4], #16
	stp     x2, x3, [x5], #16
	subs    x6, x6, #16
	b.ne    _copy_forward

_copy_forward_tail:
	cbz     x13, _entry_prep

_copy_forward_tail_loop:
	ldrb    w2, [x4], #1
	strb    w2, [x5], #1
	subs    x13, x13, #1
	b.ne    _copy_forward_tail_loop

_entry_prep:
	ldr     x5, _rel_stack_base

_entry:
	br	x5

_dead: /* we should never get here */
	b	_dead

ENTRY_ALIAS(_relocated_uni)
