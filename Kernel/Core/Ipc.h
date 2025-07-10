#ifndef IPC_H
#define IPC_H

#include "stdint.h"

#define MAX_MESSAGES 16 // Max messages per process queue

// Extensible message types
typedef enum {
    IPC_TYPE_DATA,
    IPC_TYPE_NOTIFICATION,
    // Future types can be added here
} IpcMessageType;

typedef struct {
    uint32_t sender_pid;
    IpcMessageType type;
    uint64_t size;
    union {
        char data[256]; // For general data
        uint64_t value; // For notifications or simple values
    } payload;
} IpcMessage;

typedef struct {
    IpcMessage messages[MAX_MESSAGES];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} MessageQueue;

void IpcSendMessage(uint32_t target_pid, IpcMessage* msg);
int IpcReceiveMessage(IpcMessage* msg_buffer);

#endif
