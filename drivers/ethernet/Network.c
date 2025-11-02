#include <Network.h>
#include <Console.h>
#include <StringOps.h>
#include <intel/E1000.h>
#include <interface/Arp.h>
#include <realtek/RTL8139.h>

static NetworkDevice g_network_devices[MAX_NETWORK_DEVICES];
static int g_device_count = 0;

void Net_Initialize(void) {
    PrintKernel("Initializing network devices...\n");
    g_device_count = 0;
    ArpInit();

    // Try to initialize E1000
    if (E1000_Init() == 0) {
        Net_RegisterDevice("E1000", (send_packet_t)E1000_SendPacket, (get_mac_t)E1000_GetDevice, E1000_HandleReceive);
    }

    // Try to initialize RTL8139
    Rtl8139_Init();
    const Rtl8139Device* rtl_dev = GetRtl8139Device();
    if (rtl_dev && rtl_dev->io_base) { // Check if init was successful
        Net_RegisterDevice("RTL8139", (send_packet_t)Rtl8139_SendPacket, (get_mac_t)GetRtl8139Device, Rtl8139_HandleReceive);
    }
}

void Net_RegisterDevice(const char* name, send_packet_t sender, get_mac_t mac_getter, poll_receive_t poller) {
    if (g_device_count < MAX_NETWORK_DEVICES) {
        NetworkDevice* dev = &g_network_devices[g_device_count++];
        FastStrCopy(dev->name, name, 256);
        dev->send_packet = sender;
        dev->get_mac_address = mac_getter;
        dev->poll_receive = poller;
        PrintKernel("Registered network device: ");
        PrintKernel(name);
        PrintKernel("\n");
    } else {
        PrintKernel("Cannot register more network devices.\n");
    }
}

void Net_UnregisterDevice() {

}

NetworkDevice* Net_GetDevice(int index) {
    if (index < g_device_count) {
        return &g_network_devices[index];
    }
    return NULL;
}

void Net_Poll(void) {
    for (int i = 0; i < g_device_count; i++) {
        if (g_network_devices[i].poll_receive) {
            g_network_devices[i].poll_receive();
        }
    }
}
