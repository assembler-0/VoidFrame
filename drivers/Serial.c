#include "Serial.h"
#include "Io.h"
#include "../fs/CharDevice.h"

// Serial port register offsets
#define SERIAL_DATA_REG     0  // Data register (DLAB=0)
#define SERIAL_IER_REG      1  // Interrupt Enable Register (DLAB=0)
#define SERIAL_DIVISOR_LOW  0  // Divisor low byte (DLAB=1)
#define SERIAL_DIVISOR_HIGH 1  // Divisor high byte (DLAB=1)
#define SERIAL_FIFO_REG     2  // FIFO Control Register
#define SERIAL_LCR_REG      3  // Line Control Register
#define SERIAL_MCR_REG      4  // Modem Control Register
#define SERIAL_LSR_REG      5  // Line Status Register
#define SERIAL_MSR_REG      6  // Modem Status Register

// Line Control Register bits
#define SERIAL_LCR_DLAB     0x80  // Divisor Latch Access Bit
#define SERIAL_LCR_8BITS    0x03  // 8 data bits
#define SERIAL_LCR_1STOP    0x00  // 1 stop bit
#define SERIAL_LCR_NOPARITY 0x00  // No parity

// Line Status Register bits
#define SERIAL_LSR_DATA_READY    0x01  // Data available
#define SERIAL_LSR_TRANSMIT_EMPTY 0x20 // Transmit buffer empty
#define SERIAL_LSR_IDLE          0x40  // Transmitter idle

// FIFO Control Register bits
#define SERIAL_FIFO_ENABLE      0x01
#define SERIAL_FIFO_CLEAR_RX    0x02
#define SERIAL_FIFO_CLEAR_TX    0x04
#define SERIAL_FIFO_TRIGGER_14  0xC0

// Modem Control Register bits
#define SERIAL_MCR_DTR          0x01
#define SERIAL_MCR_RTS          0x02
#define SERIAL_MCR_OUT2         0x08

static uint16_t serial_port = COM1;
static int serial_initialized = 0;

static int SerialDevRead(struct CharDevice* dev, void* buffer, uint32_t size) {
    if (!serial_initialized) return -1;
    char* buf = (char*)buffer;
    uint32_t i = 0;
    while (i < size) {
        int c = SerialReadChar();
        if (c == -1) {
            break;
        }
        buf[i++] = (char)c;
    }
    return i;
}

static int SerialDevWrite(struct CharDevice* dev, const void* buffer, uint32_t size) {
    if (!serial_initialized) return -1;
    const char* buf = (const char*)buffer;
    for (uint32_t i = 0; i < size; i++) {
        if (SerialWriteChar(buf[i]) < 0) {
            return i;
        }
    }
    return size;
}

static CharDevice_t g_serial_device = {
    .name = "Serial",
    .Read = SerialDevRead,
    .Write = SerialDevWrite,
};

int SerialInit(void) {
    int result = SerialInitPort(COM1);
    if (result == 0) {
        CharDeviceRegister(&g_serial_device);
    }
    return result;
}

int SerialInitPort(uint16_t port) {
    serial_port = port;

    // Test if serial port exists by writing to scratch register
    outb(port + 7, 0xAE);
    if (inb(port + 7) != 0xAE) {
        return -1; // Port doesn't exist
    }

    // Disable all interrupts
    outb(port + SERIAL_IER_REG, 0x00);

    // Enable DLAB to set baud rate
    outb(port + SERIAL_LCR_REG, SERIAL_LCR_DLAB);

    // Set divisor to 3 (38400 baud)
    outb(port + SERIAL_DIVISOR_LOW, 0x03);
    outb(port + SERIAL_DIVISOR_HIGH, 0x00);

    // Configure: 8 bits, no parity, 1 stop bit
    outb(port + SERIAL_LCR_REG, SERIAL_LCR_8BITS | SERIAL_LCR_NOPARITY | SERIAL_LCR_1STOP);

    // Enable and configure FIFO
    outb(port + SERIAL_FIFO_REG, SERIAL_FIFO_ENABLE | SERIAL_FIFO_CLEAR_RX |
         SERIAL_FIFO_CLEAR_TX | SERIAL_FIFO_TRIGGER_14);

    // Enable DTR, RTS, and OUT2
    outb(port + SERIAL_MCR_REG, SERIAL_MCR_DTR | SERIAL_MCR_RTS | SERIAL_MCR_OUT2);

    // Test serial chip (loopback test)
    outb(port + SERIAL_MCR_REG, SERIAL_MCR_DTR | SERIAL_MCR_RTS | SERIAL_MCR_OUT2 | 0x10);
    outb(port + SERIAL_DATA_REG, 0xAE);

    if (inb(port + SERIAL_DATA_REG) != 0xAE) {
        return -2; // Serial chip failed
    }

    // Restore normal operation
    outb(port + SERIAL_MCR_REG, SERIAL_MCR_DTR | SERIAL_MCR_RTS | SERIAL_MCR_OUT2);

    serial_initialized = 1;
    return 0; // Success
}

int SerialTransmitEmpty(void) {
    if (!serial_initialized) return 0;
    return inb(serial_port + SERIAL_LSR_REG) & SERIAL_LSR_TRANSMIT_EMPTY;
}

int SerialDataAvailable(void) {
    if (!serial_initialized) return 0;
    return inb(serial_port + SERIAL_LSR_REG) & SERIAL_LSR_DATA_READY;
}

int SerialWriteChar(const char a) {
    if (!serial_initialized) return -1;

    // Timeout counter to prevent infinite loops
    int timeout = 65536;

    // If newline, emit CR first, then LF
    if (a == '\n') {
        while (!SerialTransmitEmpty() && --timeout > 0);
        if (timeout <= 0) return -1;
        outb(serial_port + SERIAL_DATA_REG, '\r');
        timeout = 65536; // Reset timeout
    }

    while (!SerialTransmitEmpty() && --timeout > 0);
    if (timeout <= 0) return -1;

    outb(serial_port + SERIAL_DATA_REG, a);
    return 0;
}

int SerialReadChar(void) {
    if (!serial_initialized) return -1;

    if (!SerialDataAvailable()) {
        return -1; // No data available
    }

    return inb(serial_port + SERIAL_DATA_REG);
}

int SerialWrite(const char* str) {
    if (!str || !serial_initialized) return -1;

    for (int i = 0; str[i] != '\0'; i++) {
        if (SerialWriteChar(str[i]) < 0) {
            return -1;
        }
    }
    return 0;
}

void SerialWriteHex(uint64_t value) {
    if (!serial_initialized) return;

    const char hex[] = "0123456789ABCDEF";
    char buffer[17];
    buffer[16] = '\0';

    for (int i = 15; i >= 0; i--) {
        buffer[15-i] = hex[(value >> (i * 4)) & 0xF];
    }

    SerialWrite(buffer);
}

void SerialWriteDec(uint64_t value) {
    if (!serial_initialized) return;

    if (value == 0) {
        SerialWriteChar('0');
        return;
    }

    char buffer[21]; // Max digits for uint64_t
    int pos = 0;

    while (value > 0) {
        buffer[pos++] = '0' + (value % 10);
        value /= 10;
    }

    // Reverse the string
    for (int i = pos - 1; i >= 0; i--) {
        SerialWriteChar(buffer[i]);
    }
}

// Read a line from serial (with basic line editing)
int SerialReadLine(char* buffer, int max_length) {
    if (!buffer || !serial_initialized || max_length <= 0) return -1;

    int pos = 0;
    int c;

    while (pos < max_length - 1) {
        c = SerialReadChar();
        if (c < 0) continue; // No data, keep waiting

        if (c == '\r' || c == '\n') {
            SerialWriteChar('\n'); // Echo newline
            break;
        } else if (c == '\b' || c == 0x7F) { // Backspace or DEL
            if (pos > 0) {
                pos--;
                SerialWrite("\b \b"); // Erase character on terminal
            }
        } else if (c >= 32 && c < 127) { // Printable characters
            buffer[pos++] = c;
            SerialWriteChar(c); // Echo character
        }
    }

    buffer[pos] = '\0';
    return pos;
}