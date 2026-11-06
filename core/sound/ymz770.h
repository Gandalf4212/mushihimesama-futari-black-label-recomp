/* Yamaha YMZ770 (AMMS-A) sound chip, C API.
 * Port of MAME 0.289 src/devices/sound/ymz770.cpp (BSD-3-Clause, Olivier Galibert, R. Belmont,
 * MetalliC); see ymz770.cpp. GPL-2.0-or-later for the port. */
#ifndef YMZ770_H
#define YMZ770_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct Ymz770 Ymz770;

/* rom: the chip's 8 MB sample/sequence ROM (u23+u24, already word-swapped to byte order); kept by
 * reference. clock: chip clock in Hz (CV1000: 16.384 MHz). Output rate = clock / 1024. */
Ymz770 *ymz770_create(const uint8_t *rom, uint32_t rom_size, uint32_t clock);
void ymz770_destroy(Ymz770 *y);
void ymz770_reset(Ymz770 *y);
uint32_t ymz770_sample_rate(const Ymz770 *y);
/* CPU write to the chip (offset bit 0: 0 = register select, 1 = data). The caller renders all
 * samples up to the write's time first (MAME does m_stream->update() before every data write). */
void ymz770_write(Ymz770 *y, uint32_t offset, uint8_t data);
/* Render n stereo samples (interleaved L,R int16) into out. */
void ymz770_render(Ymz770 *y, int16_t *out, int n);

#ifdef __cplusplus
}
#endif
#endif
