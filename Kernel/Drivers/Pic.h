#ifndef PIC_H
#define PIC_H

#define PIT_FREQUENCY_HZ 5000 // Default PIT frequency in Hz (1000Hz = 1ms intervals)

int PicInstall();
void PitInstall();

#endif