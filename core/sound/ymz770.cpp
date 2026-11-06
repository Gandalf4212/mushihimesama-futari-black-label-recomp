// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, R. Belmont, MetalliC
/* Yamaha YMZ770 (AMMS-A), standalone port of MAME 0.289 src/devices/sound/ymz770.cpp.
 * The chip logic (channels, sequencers, mixing, register map) follows MAME line by line; the MAME
 * device/stream framework is replaced by a plain struct and ymz770_render(), and the YMZ774 variant
 * is left out (CV1000 uses the YMZ770). Kept under MAME's BSD-3-Clause licence; the project as a
 * whole is GPL-2.0-or-later. */
#include "ymz770.h"
#include "mpeg_audio.h"
#include <algorithm>
#include <cstring>

namespace {

struct voice_params {
	uint8_t pan, pan_delay, pan1, pan1_delay, volume_target, volume_delay, volume2;
};

struct channel_t {
	voice_params current, latch;
	void apply_params() { current = latch; }
	uint16_t phrase;
	int32_t volume;
	uint8_t loop;
	bool pending, is_playing, is_paused, last_block;
	mpeg_audio *decoder;
	int16_t output_data[0x1000];
	int output_remaining, output_ptr, atbl, pptr;
};

struct sequence_t {
	uint32_t delay;
	uint16_t sequence, timer, stopchan;
	uint8_t loop;
	uint32_t offset;
	uint8_t bank;
	bool is_playing, is_paused;
};

} // namespace

struct Ymz770 {
	const uint8_t *rom;
	uint32_t rom_size;
	uint32_t sclock;
	uint8_t cur_reg, mute, doen, vlma, vlma1, bsl, cpl;
	channel_t channels[16];
	sequence_t sequences[8];

	uint8_t rom_byte(uint32_t offset) const { return rom[offset % rom_size]; }
	uint32_t phrase_offs(int phrase) const { return rom[(4 * phrase) + 1] << 16 | rom[(4 * phrase) + 2] << 8 | rom[(4 * phrase) + 3]; }
	uint32_t seq_offs(int sqn) const { return rom[(4 * sqn) + 1 + 0x400] << 16 | rom[(4 * sqn) + 2 + 0x400] << 8 | rom[(4 * sqn) + 3 + 0x400]; }

	void reset()
	{
		for (auto &channel : channels)
		{
			channel.latch.pan = 64;
			channel.latch.pan_delay = 0;
			channel.latch.pan1 = 64;
			channel.latch.pan1_delay = 0;
			channel.latch.volume_target = 0;
			channel.latch.volume_delay = 0;
			channel.latch.volume2 = 0;
			channel.apply_params();
			channel.phrase = 0;
			channel.volume = 0;
			channel.loop = 0;
			channel.pending = false;
			channel.is_playing = false;
			channel.is_paused = false;
			channel.last_block = false;
			channel.output_remaining = 0;
			channel.decoder->clear();
		}
		for (auto &sequence : sequences)
		{
			sequence.delay = 0;
			sequence.sequence = 0;
			sequence.timer = 0;
			sequence.stopchan = 0;
			sequence.loop = 0;
			sequence.offset = 0;
			sequence.bank = 0;
			sequence.is_playing = false;
			sequence.is_paused = false;
		}
	}

	void sequencer()
	{
		for (auto &sequence : sequences)
		{
			if (sequence.is_playing)
			{
				if (sequence.delay > 0)
					sequence.delay--;
				else
				{
					int reg = rom_byte(sequence.offset++);
					uint8_t data = rom_byte(sequence.offset++);
					switch (reg)
					{
					case 0x0f:
						for (int ch = 0; ch < 8; ch++) // might be wrong, ie not needed in case of loop
							if (sequence.stopchan & (1 << ch))
								channels[ch].is_playing = false;
						if (sequence.loop)
							sequence.offset = seq_offs(sequence.sequence); // loop sequence
						else
							sequence.is_playing = false;
						break;
					case 0x0e:
						sequence.delay = sequence.timer * 32 + 32 - 1;
						break;
					default:
						internal_reg_write(reg, data);
						break;
					}
				}
			}
		}
	}

	void internal_reg_write(uint8_t reg, uint8_t data)
	{
		if (reg < 0x40)
		{
			switch (reg)
			{
			case 0x00: mute = data & 1; doen = data >> 1 & 1; break;
			case 0x01: vlma = data; break;
			case 0x02: bsl = data & 7; cpl = data >> 4 & 7; break;
			default: break;   // MAME: unimplemented
			}
		}
		else if (reg < 0x60)
		{
			int ch = reg >> 2 & 0x07;
			switch (reg & 0x03)
			{
			case 0: channels[ch].phrase = data; break;
			case 1: channels[ch].volume = 128 << 17; channels[ch].latch.volume2 = data; break;
			case 2: channels[ch].latch.pan = data << 3; break;
			case 3:
				if ((data & 6) == 2 || ((data & 6) == 6 && !channels[ch].is_playing)) // both KON bits is 1 = "Keep Playing", do not restart channel in this case
				{
					channels[ch].pending = true;
					channels[ch].is_playing = true;
					channels[ch].last_block = false;
				}
				else if ((data & 6) == 0)
					channels[ch].is_playing = false;
				channels[ch].loop = (data & 1) ? 255 : 0;
				break;
			}
		}
		else if (reg >= 0x80)
		{
			int ch = reg >> 4 & 0x07;
			switch (reg & 0x0f)
			{
			case 0: // SQSN
				sequences[ch].sequence = data;
				break;
			case 1: // SQON SQLP
				if ((data & 6) == 2 || ((data & 6) == 6 && !sequences[ch].is_playing)) // both KON bits 1 is "Keep Playing"
				{
					sequences[ch].offset = seq_offs(sequences[ch].sequence);
					sequences[ch].delay = 0;
					sequences[ch].is_playing = true;
				}
				else if ((data & 6) == 0 && sequences[ch].is_playing)
				{
					sequences[ch].is_playing = false;
					for (int i = 0; i < 8; i++)
						if (sequences[ch].stopchan & (1 << i))
							channels[i].is_playing = false;
				}
				sequences[ch].loop = data & 1;
				break;
			case 2: sequences[ch].timer = (sequences[ch].timer & 0x00ff) | (data << 8); break; // TMRH
			case 3: sequences[ch].timer = (sequences[ch].timer & 0xff00) | data; break;        // TMRL
			case 6: sequences[ch].stopchan = data; break;                                       // SQOF
			default: break;
			}
		}
	}

	void render(int16_t *out, int samples)
	{
		for (int i = 0; i < samples; i++)
		{
			sequencer();

			int32_t mixl = 0;
			int32_t mixr = 0;

			for (auto &channel : channels)
			{
				if (channel.output_remaining == 0 && channel.is_playing && !channel.is_paused)
				{
retry:
					if (channel.last_block)
					{
						if (channel.loop)
						{
							if (channel.loop != 255)
								--channel.loop;
							channel.pending = true;
						}
						else
						{
							channel.is_playing = false;
							channel.output_remaining = 0;
							channel.decoder->clear();
						}
					}

					if (channel.is_playing)
					{
						if (channel.pending)
						{
							int phrase = channel.phrase;
							channel.atbl = rom[4 * phrase] >> 4 & 7;
							channel.pptr = 8 * phrase_offs(phrase);
							if (!channel.last_block && channel.latch.volume2 != channel.current.volume2)
								channel.decoder->clear();
							channel.pending = false;
						}

						int sample_rate = sclock, channel_count;
						if (!channel.decoder->decode_buffer(channel.pptr, rom_size * 8, channel.output_data, channel.output_remaining, sample_rate, channel_count, channel.atbl) || channel.output_remaining == 0)
						{
							channel.is_playing = !channel.last_block; // detect infinite retry loop
							channel.last_block = true;
							channel.output_remaining = 0;
							goto retry;
						}
						// MAME changes the stream rate when a phrase reports another rate; CV1000 data
						// is all 16 kHz, so the output rate stays fixed here.
						channel.last_block = channel.output_remaining < 1152;
						channel.output_ptr = 0;
						channel.apply_params();
					}
				}

				if (channel.output_remaining > 0)
				{
					int32_t smpl = channel.output_data[channel.output_ptr++];
					smpl = (smpl * (channel.volume >> 17)) >> 7;
					smpl = (smpl * channel.current.volume2) >> 7;
					mixr += (smpl * channel.current.pan) >> 7;
					mixl += (smpl * (128 - channel.current.pan)) >> 7;
					channel.output_remaining--;

					if (channel.output_remaining == 0 && !channel.is_playing)
						channel.decoder->clear();
				}
			}

			if (mute)
				mixr = mixl = 0;
			else
			{
				mixr *= vlma;
				mixl *= vlma;
				mixr >>= 7 - bsl;
				mixl >>= 7 - bsl;
				constexpr int32_t ClipMax3 = 32768 * 75 / 100;
				constexpr int32_t ClipMax2 = 32768 * 875 / 1000;
				switch (cpl)
				{
				case 3: mixl = std::clamp(mixl, -ClipMax3, ClipMax3); mixr = std::clamp(mixr, -ClipMax3, ClipMax3); break;
				case 2: mixl = std::clamp(mixl, -ClipMax2, ClipMax2); mixr = std::clamp(mixr, -ClipMax2, ClipMax2); break;
				case 1: mixl = std::clamp(mixl, -32768, 32767); mixr = std::clamp(mixr, -32768, 32767); break;
				}
			}
			// MAME: stream.put_int(ch, i, mix, 32768) -> mix / 32768.0 as a float sample; clamp for int16
			out[2 * i + 0] = (int16_t)std::clamp(mixl, -32768, 32767);
			out[2 * i + 1] = (int16_t)std::clamp(mixr, -32768, 32767);
		}
	}
};

extern "C" {

Ymz770 *ymz770_create(const uint8_t *rom, uint32_t rom_size, uint32_t clock)
{
	Ymz770 *y = new Ymz770();
	y->rom = rom; y->rom_size = rom_size;
	y->sclock = clock / 1024;
	y->cur_reg = y->mute = y->doen = y->vlma = y->vlma1 = y->bsl = y->cpl = 0;
	for (auto &channel : y->channels)
	{
		channel.is_playing = false;
		channel.decoder = new mpeg_audio(rom, mpeg_audio::AMM, false, 0);
	}
	for (auto &sequence : y->sequences)
		sequence.is_playing = false;
	y->reset();
	return y;
}

void ymz770_destroy(Ymz770 *y)
{
	if (!y) return;
	for (auto &channel : y->channels) delete channel.decoder;
	delete y;
}

void ymz770_reset(Ymz770 *y) { y->reset(); }
uint32_t ymz770_sample_rate(const Ymz770 *y) { return y->sclock; }

void ymz770_write(Ymz770 *y, uint32_t offset, uint8_t data)
{
	if (offset & 1)
		y->internal_reg_write(y->cur_reg, data);
	else
		y->cur_reg = data;
}

void ymz770_render(Ymz770 *y, int16_t *out, int n) { y->render(out, n); }

}
