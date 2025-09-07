#ifndef VOIDFRAME_GENERIC_SOUND_H
#define VOIDFRAME_GENERIC_SOUND_H

#include "stdint.h"

// PC Speaker ports
#define PC_SPEAKER_PORT     0x61
#define PIT_CHANNEL_2       0x42
#define PIT_COMMAND         0x43

// PIT command for channel 2 (PC Speaker)
#define PIT_CMD_CHANNEL_2   0xB6

// Musical note frequencies (Hz)
#define NOTE_C4     262
#define NOTE_D4     294
#define NOTE_E4     330
#define NOTE_F4     349
#define NOTE_G4     392
#define NOTE_A4     440
#define NOTE_B4     494
#define NOTE_C5     523

// Function prototypes
void PCSpkr_Init(void);
void PCSpkr_Beep(uint16_t frequency, uint32_t duration_ms);
void PCSpkr_PlayTone(uint16_t frequency);
void PCSpkr_Stop(void);
void PCSpkr_PlayMelody(const uint16_t* notes, const uint32_t* durations, int count);

#endif // VOIDFRAME_GENERIC_SOUND_H