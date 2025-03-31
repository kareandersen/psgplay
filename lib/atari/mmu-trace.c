// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Fredrik Noring
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "internal/types.h"

#include "atari/bus.h"
#include "atari/machine.h"

static void mmu_trace(const char *op, u32 dev_address,
	const char *spacing, int size, u32 value,
	size_t (*sh)(const struct device *device,
		u32 dev_address, char *buf, size_t size),
	const struct device *dev)
{
	char description[256];

	/* FIXME: if (!dev->trace.format) with dev->trace.format(fmt, ...)
k	return;
*/

	if (strcmp(dev->name, "dma") == 0)
		goto trace;
	if (strcmp(dev->name, "mfp") == 0)
		goto trace;
	if (strcmp(dev->name, "psg") == 0)
        goto trace;

    int addr = dev->bus.address + dev_address;
    /* Fake ram device: vec_a (Auto Vectors) - temp hack for my boy Wietse
     * TODO: Clean this all up */
    if ( (int)addr >= 0x64  && (int)addr < 0x80) { /*MFP vector range */

        int dst_addr = dev->bus.address + dev_address;

        sprintf(description, "Auto Vector ");
        if (op[0] == 'r') {
            strncat(description, "read", 5);
        }
        else {
            strncat(description, "write", 6);
        }

		printf("%s %8" PRIu64 "  %6x: %s %s%.*x %s\n",
			"vec", machine_cycle(),
			dst_addr,
			op, spacing, size, value,
            description);
        return;
    }

    /* Fake ram device: vec (MFP Vectors)
     * TODO: Make it an actual device. */
    if ( (int)addr >= 0x100 && (int)addr < 0x140) { /*MFP vector range */

        int dst_addr = dev->bus.address + dev_address;

        sprintf(description, "MFP Vector ");
        if (op[0] == 'r') {
            strncat(description, "read", 5);
        }
        else {
            strncat(description, "write", 6);
        }

		printf("%s %8" PRIu64 "  %6x: %s %s%.*x %s\n",
			"vec", machine_cycle(),
			dst_addr,
			op, spacing, size, value,
            description);
        return;
    }

	if (dev->bus.address + dev_address < 2048)
		goto trace;

    return;

trace:

	if (sh)
		sh(dev, dev_address, description, sizeof(description));
	else
		description[0] = '\0';

	if (description[0] != '\0')
		printf("%s %8" PRIu64 "  %6x: %s %s%.*x %s\n",
			dev->name, machine_cycle(),
			dev->bus.address + dev_address,
			op, spacing, size, value,
			description);
	else
		printf("%s %8" PRIu64 "  %6x: %s %s%.*x\n",
			dev->name, machine_cycle(),
			dev->bus.address + dev_address,
			op, spacing, size, value);
}

void mmu_trace_rd_u8(u32 dev_address, u32 value, const struct device *dev)
{
	mmu_trace("rd  u8", dev_address, "  ", 2, value, dev->id_u8, dev);
}

void mmu_trace_rd_u16(u32 dev_address, u32 value, const struct device *dev)
{
	mmu_trace("rd u16", dev_address, "", 4, value, dev->id_u16, dev);
}

void mmu_trace_rd_u32(u32 dev_address, u32 value, const struct device *dev)
{
	mmu_trace("rd u32", dev_address, "", 4, value, dev->id_u32, dev);
}

void mmu_trace_wr_u8(u32 dev_address, u32 value, const struct device *dev)
{
	mmu_trace("wr  u8", dev_address, "  ", 2, value, dev->id_u8, dev);
}

void mmu_trace_wr_u16(u32 dev_address, u32 value, const struct device *dev)
{
	mmu_trace("wr u16", dev_address, "", 4, value, dev->id_u16, dev);
}

void mmu_trace_wr_u32(u32 dev_address, u32 value, const struct device *dev)
{
	mmu_trace("wr u32", dev_address, "", 4, value, dev->id_u32, dev);
}

