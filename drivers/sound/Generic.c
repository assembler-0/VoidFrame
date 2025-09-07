#include "Generic.h"
#include "Console.h"
#include "Cpu.h"
#include "Io.h"

static int pc_speaker_initialized = 0;

void PCSpkr_Init(void) {
    PrintKernel("PCSpkr: Initializing PC Speaker driver...\n");
    pc_speaker_initialized = 1;
    PrintKernelSuccess("PCSpkr: Driver initialized\n");
}

void PCSpkr_PlayTone(uint16_t frequency) {
    if (!pc_speaker_initialized || frequency < 20) {
        return;
    }
    
    // Calculate PIT divisor for frequency
    uint32_t divisor = 1193180 / frequency;
    if (divisor > 65535) divisor = 65535;
    
    // Configure PIT channel 2 for square wave
    outb(PIT_COMMAND, PIT_CMD_CHANNEL_2);
    outb(PIT_CHANNEL_2, divisor & 0xFF);        // Low byte
    outb(PIT_CHANNEL_2, (divisor >> 8) & 0xFF); // High byte
    
    // Enable speaker
    uint8_t speaker_reg = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, speaker_reg | 0x03);
}

void PCSpkr_Stop(void) {
    if (!pc_speaker_initialized) {
        return;
    }
    
    // Disable speaker
    uint8_t speaker_reg = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, speaker_reg & 0xFC);
}

void PCSpkr_Beep(uint16_t frequency, uint32_t duration_ms) {
    PCSpkr_PlayTone(frequency);
    delay(duration_ms * 1000); // Convert ms to microseconds
    PCSpkr_Stop();
}

void PCSpkr_PlayMelody(const uint16_t* notes, const uint32_t* durations, int count) {
    if (!pc_speaker_initialized) {
        return;
    }
    
    for (int i = 0; i < count; i++) {
        if (notes[i] == 0) {
            // Rest (silence)
            PCSpkr_Stop();
            delay(durations[i] * 1000);
        } else {
            PCSpkr_Beep(notes[i], durations[i]);
        }
        delay(50000); // Small gap between notes
    }
}