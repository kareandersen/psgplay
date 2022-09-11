// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Fredrik Noring
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "internal/assert.h"
#include "internal/compare.h"

#include "atari/cpu.h"
#include "atari/machine.h"
#include "atari/psg.h"

#include "psgplay/psgplay.h"
#include "psgplay/sndh.h"

struct fir8 {
	s16 xn[8];
	int k;
};

struct stereo_buffer {
	size_t index;
	size_t count;
	size_t capacity;
	struct psgplay_stereo *sample;
};

struct digital_buffer {
	size_t index;
	struct {
		size_t psg;
		size_t sound;
		size_t mixer;
	} count;
	size_t capacity;
	struct psgplay_digital *sample;
};

struct psgplay {
	struct stereo_buffer stereo_buffer;
	struct digital_buffer digital_buffer;

	struct psgplay_downsample {
		int stereo_frequency;
		u64 psg_cycle;
		u64 downsample_sample_cycle;

		struct {
			struct fir8 left;
			struct fir8 right;
		} lowpass;
	} downsample;

	const struct machine *machine;

	struct {
		void (*cb)(uint32_t pc, void *arg);
		void *arg;
	} instruction_callback;

	int errno_;
};

static int buffer_stereo_sample(struct stereo_buffer *sb,
	const struct psgplay_stereo *stereo, const size_t count)
{
	for (size_t i = 0; i < count; i++) {
		if (sb->capacity <= sb->count) {
			const size_t capacity = sb->capacity +
				max_t(size_t, sb->capacity, 1024);

			void *sample = realloc(sb->sample,
				capacity * sizeof(*sb->sample));
			if (!sample)
				return errno;

			sb->sample = sample;
			sb->capacity = capacity;
		}

		sb->sample[sb->count++] = stereo[i];
	}

	return 0;
}

static int digital_buffer_allocate(struct digital_buffer *db)
{
	const size_t capacity = db->capacity +
		max_t(size_t, db->capacity, 1024);

	void *sample = realloc(db->sample, capacity * sizeof(*db->sample));
	if (!sample)
		return errno;

	db->sample = sample;
	db->capacity = capacity;

	return 0;
}

static int buffer_digital_psg_sample(const struct psg_sample *sample,
	struct digital_buffer *db)
{
	int err = 0;

	if (db->capacity <= db->count.psg)
		err = digital_buffer_allocate(db);

	if (!err)
		db->sample[db->count.psg++].psg =
			(struct psgplay_digital_psg) {
				.lva = sample->lva,
				.lvb = sample->lvb,
				.lvc = sample->lvc,
			};

	return err;
}

static int buffer_digital_sound_sample(const struct sound_sample *sample,
	struct digital_buffer *db)
{
	int err = 0;

	if (db->capacity <= db->count.sound)
		err = digital_buffer_allocate(db);

	if (!err)
		db->sample[db->count.sound++].sound =
			(struct psgplay_digital_sound) {
				.left  = sample->left,
				.right = sample->right,
			};

	return err;
}

static int buffer_digital_mixer_sample(const struct mixer_sample *sample,
	struct digital_buffer *db)
{
	int err = 0;

	if (db->capacity <= db->count.mixer)
		err = digital_buffer_allocate(db);

	if (!err)
		db->sample[db->count.mixer++].mixer =
			(struct psgplay_digital_mixer) {
				.volume = {
					.main = sample->volume.main,
					.left = sample->volume.left,
					.right = sample->volume.right,
				},
				.tone = {
					.bass = sample->tone.bass,
					.treble = sample->tone.treble,
				},
				.mix = sample->mix,
			};

	return err;
}

static s16 sample_lowpass(s16 sample, struct fir8 *lowpass)
{
	lowpass->xn[lowpass->k++ % ARRAY_SIZE(lowpass->xn)] = sample;

	s32 x = 0;
	for (int i = 0; i < ARRAY_SIZE(lowpass->xn); i++)
		x += lowpass->xn[i];	/* Simplistic 8 tap FIR filter. */

	return x / ARRAY_SIZE(lowpass->xn);
}

static s16 psg_dac(const u8 level)
{
	/*
	 * Table is computed with 2^[ (lvl-15)/2 ] although the levels in
	 * figure 9 in AY-3-8910/8912 Programmable Sound Generator Data Manual,
	 * February 1979, p. 29 are slightly different as shown in the comment.
	 */

	static const u16 dac[16] = {
		0.006 * 0xffff,		/* 0.000 */
		0.008 * 0xffff,		/* ..... */
		0.011 * 0xffff,		/* ..... */
		0.016 * 0xffff,		/* ..... */
		0.022 * 0xffff,		/* ..... */
		0.031 * 0xffff,		/* ..... */
		0.044 * 0xffff,		/* ..... */
		0.062 * 0xffff,		/* ..... */
		0.088 * 0xffff,		/* ..... */
		0.125 * 0xffff,		/* 0.125 */
		0.177 * 0xffff,		/* 0.152 */
		0.250 * 0xffff,		/* 0.250 */
		0.354 * 0xffff,		/* 0.303 */
		0.500 * 0xffff,		/* 0.500 */
		0.707 * 0xffff,		/* 0.707 */
		1.000 * 0xffff,		/* 1.000 */
	};

	return (level < 16 ? dac[level] : 0xffff) - 0x8000;
}

static void digital_to_stereo(
	struct psgplay *pp,
	struct psgplay_stereo *stereo,
	const struct psgplay_digital *digital,
	size_t count)
{
	for (size_t i = 0; i < count; i++) {
		const s16 sa = psg_dac(digital[i].psg.lva);
		const s16 sb = psg_dac(digital[i].psg.lvb);
		const s16 sc = psg_dac(digital[i].psg.lvc);

		/* Simplistic linear channel mix. */
		const s16 s = digital->mixer.mix ? (sa + sb + sc) / 3 : 0;

		/* Simplistic half volume each to PSG and sound. */
		stereo[i] = (struct psgplay_stereo) {
			.left  = (digital[i].sound.left  + s) / 2,
			.right = (digital[i].sound.right + s) / 2
		};
	}
}

static size_t stereo_downsample(
	struct psgplay *pp,
	struct psgplay_stereo *resample,
	const struct psgplay_stereo *stereo,
	size_t count)
{
	struct psgplay_downsample *ds = &pp->downsample;
	size_t r = 0;

	for (size_t i = 0; i < count; i++) {
		const u64 n = (ds->stereo_frequency * ds->psg_cycle) / PSG_FREQUENCY;
		const struct psgplay_stereo s = {
			.left  = sample_lowpass(stereo[i].left,  &ds->lowpass.left),
			.right = sample_lowpass(stereo[i].right, &ds->lowpass.right)
		};

		for (; ds->downsample_sample_cycle < n; ds->downsample_sample_cycle++)
			resample[r++] = s;

		ds->psg_cycle += 8;
	}

	return r;
}

static void digital_to_stereo_downsample(struct psgplay *pp,
	const struct psgplay_digital *digital, const size_t count)
{
	struct psgplay_stereo stereo[4096];
	struct psgplay_stereo resample[ARRAY_SIZE(stereo)];
	size_t i = 0;

	while (i < count && !pp->errno_) {
		const size_t n = min(count - i, ARRAY_SIZE(stereo));

		digital_to_stereo(pp, stereo, &digital[i], n);

		const size_t r = stereo_downsample(pp, resample, stereo, n);

		pp->errno_ = buffer_stereo_sample(&pp->stereo_buffer, resample, r);

		i += n;
	}
}

static void psg_digital(const struct psg_sample *sample,
	size_t count, void *arg)
{
	struct psgplay *pp = arg;

	for (size_t i = 0; i < count; i++)
		if (!pp->errno_)
			pp->errno_ = buffer_digital_psg_sample(
				&sample[i], &pp->digital_buffer);
}

static void sound_digital(const struct sound_sample *sample,
	size_t count, void *arg)
{
	struct psgplay *pp = arg;

	for (size_t i = 0; i < count; i++)
		if (!pp->errno_)
			pp->errno_ = buffer_digital_sound_sample(
				&sample[i], &pp->digital_buffer);
}

static void mixer_digital(const struct mixer_sample *sample,
	size_t count, void *arg)
{
	struct psgplay *pp = arg;

	for (size_t i = 0; i < count; i++)
		if (!pp->errno_)
			pp->errno_ = buffer_digital_mixer_sample(
				&sample[i], &pp->digital_buffer);
}

static u32 parse_timer(const void *data, size_t size)
{
	struct sndh_timer timer;

	if (!sndh_tag_timer(&timer, data, size))
		return 0;

	return sndh_timer_to_u32(timer);
}

struct psgplay *psgplay_init(const void *data, size_t size,
	int track, int stereo_frequency)
{
	if (stereo_frequency &&
			(stereo_frequency < 1000 || 250000 < stereo_frequency))
		return NULL;

	struct psgplay *pp = calloc(1, sizeof(struct psgplay));
	if (!pp)
		return NULL;

	const u32 offset = MACHINE_PROGRAM;
	const struct machine_registers regs = {
		.d = { size, track, parse_timer(data, size) },
		.a = { offset },
	};

	const struct machine_ports ports = {
		.psg_sample = psg_digital,
		.sound_sample = sound_digital,
		.mixer_sample = mixer_digital,
		.arg = pp
	};

	pp->downsample.stereo_frequency = stereo_frequency;
	pp->machine = &atari_st;
	pp->machine->init(data, size, offset, &regs, &ports);

	return pp;
}

static size_t digital_buffer_min_count(struct digital_buffer *db)
{
	return min3(db->count.psg, db->count.sound, db->count.mixer);
}

static size_t digital_buffer_max_count(struct digital_buffer *db)
{
	return max3(db->count.psg, db->count.sound, db->count.mixer);
}

static void digital_buffer_shift(struct digital_buffer *db)
{
	const size_t n = digital_buffer_max_count(db) - db->index;

	if (n)
		memmove(&db->sample[0], &db->sample[db->index],
			n * sizeof(*db->sample));

	db->count.psg -= db->index;
	db->count.sound -= db->index;
	db->count.mixer -= db->index;

	db->index = 0;
}

static ssize_t psgplay_read_digital__(struct psgplay *pp,
	struct psgplay_digital *buffer, size_t count)
{
	struct digital_buffer *db = &pp->digital_buffer;
	size_t index = 0;

	cpu_instruction_callback(
		pp->instruction_callback.cb,
		pp->instruction_callback.arg);

	while (index < count) {
		if (db->index == digital_buffer_min_count(db)) {
			digital_buffer_shift(db);

			while (!digital_buffer_min_count(db))
				if (pp->errno_) {
					errno = pp->errno_;
					return -1;
				} else if (!pp->machine->run()) {
					errno = -EIO;
					return -1;
				}
		}

		const size_t n = min(count - index,
			digital_buffer_min_count(db) - db->index);

		if (buffer != NULL)
			memcpy(&buffer[index], &db->sample[db->index],
				n * sizeof(*buffer));

		index += n;
		db->index += n;
	}

	return index;
}

ssize_t psgplay_read_stereo(struct psgplay *pp,
	struct psgplay_stereo *buffer, size_t count)
{
	struct stereo_buffer *sb = &pp->stereo_buffer;
	size_t index = 0;

	if (!pp->downsample.stereo_frequency)
		return -EINVAL;

	while (index < count) {
		if (sb->index == sb->count) {
			sb->index = 0;
			sb->count = 0;

			if (pp->errno_) {
				errno = pp->errno_;
				return -1;
			}

			struct psgplay_digital d[4096];
			const ssize_t n = psgplay_read_digital__(
				pp, d, ARRAY_SIZE(d));

			if (n < 0)
				return n;

			digital_to_stereo_downsample(pp, d, n);
		}

		const size_t n = min(count - index, sb->count - sb->index);

		if (buffer != NULL)
			memcpy(&buffer[index], &sb->sample[sb->index],
				n * sizeof(*buffer));

		index += n;
		sb->index += n;
	}

	return index;
}

ssize_t psgplay_read_digital(struct psgplay *pp,
	struct psgplay_digital *buffer, size_t count)
{
	if (pp->downsample.stereo_frequency)
		return -EINVAL;

	return psgplay_read_digital__(pp, buffer, count);
}

void psgplay_free(struct psgplay *pp)
{
	if (!pp)
		return;

	free(pp->stereo_buffer.sample);
	free(pp->digital_buffer.sample);
	free(pp);
}

void psgplay_instruction_callback(struct psgplay *pp,
	void (*cb)(uint32_t pc, void *arg), void *arg)
{
	pp->instruction_callback.cb = cb;
	pp->instruction_callback.arg = arg;
}
