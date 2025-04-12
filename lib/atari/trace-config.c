#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "internal/types.h"
#include "atari/trace-config.h"

struct trace_region trace_regions[MAX_TRACE_REGIONS];
size_t trace_region_count = 0;

static char *trim(char *s) {
	while (*s && isspace((unsigned char)*s)) s++;
	if (*s == 0) return s;
	char *end = s + strlen(s) - 1;
	while (end > s && isspace((unsigned char)*end)) *end-- = 0;
	return s;
}

static void str_to_lower(char *s) {
	for (; *s; ++s)
		*s = tolower((unsigned char)*s);
}

static u32 parse_hex_or_dec(const char *s) {
	if (s[0] == '$') {
		return (u32)strtoul(s + 1, NULL, 16);
	} else if (strncmp(s, "0x", 2) == 0 || strncmp(s, "0X", 2) == 0) {
		return (u32)strtoul(s + 2, NULL, 16);
	} else {
		return (u32)strtoul(s, NULL, 10);
	}
}

void load_trace_regions_from_ini(const char *filename) {
	FILE *f = fopen(filename, "r");
	if (!f)
		return;

	char line[256];
	char current_tag[64] = {0};
	char *description = NULL;
	u32 start = 0, end = 0;
	int active = 0;

	while (fgets(line, sizeof(line), f)) {
		char *s = trim(line);
		if (*s == ';' || *s == '#' || *s == '\0')
			continue;

		if (*s == '[') {
			char *end_bracket = strchr(s, ']');
			if (!end_bracket)
				continue;
			*end_bracket = 0;
			strncpy(current_tag, s + 1, sizeof(current_tag) - 1);
			current_tag[sizeof(current_tag) - 1] = 0;
			str_to_lower(current_tag);

			description = NULL;
			start = end = 0;
			active = 1;
		} else if (active) {
			char *eq = strchr(s, '=');
			if (!eq)
				continue;
			*eq = 0;
			char *key = trim(s);
			char *val = trim(eq + 1);
			str_to_lower(key);  // make key lowercase before comparing

			if (strcmp(key, "description") == 0) {
				description = strdup(val);
			} else if (strcmp(key, "start") == 0) {
				start = parse_hex_or_dec(val);
			} else if (strcmp(key, "end") == 0) {
				end = parse_hex_or_dec(val);
			}
		}

		if (*current_tag && description && start && end && active) {
			if (trace_region_count < MAX_TRACE_REGIONS) {
				trace_regions[trace_region_count++] = (struct trace_region){
					.tag = strdup(current_tag),
					.description = description,
					.start = start,
					.end = end
				};
			}
			*current_tag = 0;
			description = NULL;
			active = 0;
		}
	}

	fclose(f);
}

