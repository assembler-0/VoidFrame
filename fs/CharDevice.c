#include <CharDevice.h>
#include <kernel/etc/StringOps.h>

static CharDevice_t* g_char_devices[MAX_CHAR_DEVICES];
static int g_num_char_devices = 0;

void CharDeviceInit(void) {
    for (int i = 0; i < MAX_CHAR_DEVICES; i++) {
        g_char_devices[i] = 0;
    }
    g_num_char_devices = 0;
}

int CharDeviceRegister(CharDevice_t* device) {
    if (g_num_char_devices >= MAX_CHAR_DEVICES) {
        return -1;
    }
    g_char_devices[g_num_char_devices++] = device;
    return 0;
}

CharDevice_t* CharDeviceFind(const char* name) {
    for (int i = 0; i < g_num_char_devices; i++) {
        if (FastStrCmp(g_char_devices[i]->name, name) == 0) {
            return g_char_devices[i];
        }
    }
    return 0;
}

CharDevice_t* CharDeviceGet(int index) {
    if (index < 0 || index >= g_num_char_devices) {
        return 0;
    }
    return g_char_devices[index];
}

int CharDeviceGetCount(void) {
    return g_num_char_devices;
}
