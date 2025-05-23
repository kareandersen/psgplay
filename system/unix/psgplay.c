// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Fredrik Noring
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/build-assert.h"
#include "internal/compare.h"
#include "internal/print.h"
#include "internal/types.h"

#include "psgplay/sndh.h"

#include "out/alsa.h"
#include "out/portaudio.h"
#include "out/wave.h"

#include "system/unix/command-mode.h"
#include "system/unix/diagnostic.h"
#include "system/unix/disassemble.h"
#include "system/unix/file.h"
#include "system/unix/info.h"
#include "system/unix/option.h"
#include "system/unix/sndh.h"
#include "system/unix/text-mode.h"

char progname[] = "psgplay";

typedef void (*replay_f)(const struct options *options, struct file file,
	const struct output *output);

static void NORETURN info_exit(struct file file)
{
	sndh_print(file);

	exit(EXIT_SUCCESS);
}

#ifdef HAVE_SSO
static void NORETURN disassemble_exit(struct options *options, struct file file)
{
	sndh_disassemble(options, file);

	exit(EXIT_SUCCESS);
}

static void NORETURN trace_exit(struct options *options, struct file file)
{
	sndh_trace(options, file);

	exit(EXIT_SUCCESS);
}
#else
static void NORETURN disassemble_exit(struct options *options, struct file file)
{
	pr_fatal_error("Disassembler disabled: The C compiler does not support "
		"__attribute__((__scalar_storage_order__(\"big-endian\")))\n");
}

static void NORETURN trace_exit(struct options *options, struct file file)
{
	pr_fatal_error("Disassembler disabled: The C compiler does not support "
		"__attribute__((__scalar_storage_order__(\"big-endian\")))\n");
}
#endif

static int default_subtune(struct file file)
{
	int track;

	if (!sndh_tag_default_subtune(&track, file.data, file.size))
		track = 1;

	return track;
}

static void select_subtune(int *track, struct file file)
{
	int track_count;

	if (*track < 1)
		*track = default_subtune(file);

	if (sndh_tag_subtune_count(&track_count, file.data, file.size))
		if (*track > track_count)
			pr_fatal_error("%s: track %d out of range 1 to %d\n",
				file.path, *track, track_count);
}

static const struct output *select_output(const struct options *options)
{
#ifdef HAVE_PORTAUDIO
	if (!options->output)
		return &portaudio_output;
#endif /* HAVE_PORTAUDIO */

	if (alsa_output_handle(options->output)) {
#ifdef HAVE_ALSA
		return &alsa_output;
#else
		pr_fatal_error("ALSA was disabled during compilation\n");
#endif /* HAVE_ALSA */
	}

	if (!options->output)
		pr_fatal_error("missing output file\n");

	return &wave_output;
}

static replay_f select_replay(const struct options *options)
{
	return text_mode_option() ? text_replay : command_replay;
}

int main(int argc, char *argv[])
{
	struct options *options = parse_options(argc, argv);

	struct file file = sndh_read_file(options->input);
	if (!file_valid(file))
		pr_fatal_errno(options->input);

	if (option_verbosity() >= 0)
		sndh_diagnostic(file);

	if (options->info)
		info_exit(file);

	if (options->disassemble)
		disassemble_exit(options, file);

	if (options->trace.m)
		trace_exit(options, file);

	select_subtune(&options->track, file);

	select_replay(options)(options, file, select_output(options));

	file_free(file);

	return EXIT_SUCCESS;
}
