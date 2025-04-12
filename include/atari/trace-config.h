#ifndef PSGPLAY_SYSTEM_UNIX_TRACE_CONFIG_H
#define PSGPLAY_SYSTEM_UNIX_TRACE_CONFIG_H

#include "internal/types.h"

struct trace_region {
	const char *tag;
	const char *description;
	u32 start;
	u32 end;
};

#define MAX_TRACE_REGIONS 32

extern struct trace_region trace_regions[MAX_TRACE_REGIONS];
extern size_t trace_region_count;

void load_trace_regions_from_ini(const char *filename);

#endif /* PSGPLAY_SYSTEM_UNIX_TRACE_CONFIG_H */

