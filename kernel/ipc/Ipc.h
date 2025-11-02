#ifndef IPC_H
#define IPC_H

#include <SpinlockRust.h>
#include <StringOps.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_MESSAGES 32 // Increased for better throughput
#define IPC_MAX_PAYLOAD 512 // Larger payload support

// Enhanced message types
typedef enum {
    IPC_TYPE_DATA = 0,
    IPC_TYPE_NOTIFICATION = 1,
    IPC_TYPE_REQUEST = 2,      // Request-response pattern
    IPC_TYPE_RESPONSE = 3,     // Response to request
    IPC_TYPE_SIGNAL = 4,       // Process signals
    IPC_TYPE_BROADCAST = 5,    // Broadcast to multiple processes
    IPC_TYPE_URGENT = 6        // High priority messages
} IpcMessageType;

// Message priorities
typedef enum {
    IPC_PRIORITY_LOW = 0,
    IPC_PRIORITY_NORMAL = 1,
    IPC_PRIORITY_HIGH = 2,
    IPC_PRIORITY_URGENT = 3
} IpcPriority;

typedef struct {
    uint32_t sender_pid;
    uint32_t sequence_id;      // For request-response matching
    IpcMessageType type;
    IpcPriority priority;
    uint64_t timestamp;        // When message was sent
    uint64_t size;
    union {
        char data[IPC_MAX_PAYLOAD];
        uint64_t value;
        struct {
            uint32_t request_id;
            uint32_t flags;
            char request_data[IPC_MAX_PAYLOAD - 8];
        } request;
        struct {
            uint32_t request_id;
            int32_t status;
            char response_data[IPC_MAX_PAYLOAD - 8];
        } response;
    } payload;
} IpcMessage;


typedef struct {
    IpcMessage messages[MAX_MESSAGES];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    RustSpinLock* lock;         // Spinlock for thread safety
    uint32_t dropped_count;    // Track dropped messages
    uint32_t priority_bitmap;  // Track priority levels present
} MessageQueue;

// IPC error codes
typedef enum {
    IPC_SUCCESS = 0,
    IPC_ERROR_QUEUE_FULL = -1,
    IPC_ERROR_NO_PROCESS = -2,
    IPC_ERROR_INVALID_MSG = -3,
    IPC_ERROR_TIMEOUT = -4,
    IPC_ERROR_INTERRUPTED = -5
} IpcResult;

// Core IPC functions
IpcResult IpcSendMessage(uint32_t target_pid, const IpcMessage* msg);
IpcResult IpcReceiveMessage(IpcMessage* msg_buffer);
// IpcResult IpcReceiveMessageTimeout(IpcMessage* msg_buffer, uint64_t timeout_ticks);
IpcResult IpcReceiveMessageType(IpcMessage* msg_buffer, IpcMessageType type);

// Advanced IPC functions
IpcResult IpcSendRequest(uint32_t target_pid, const void* request_data, uint64_t size, uint32_t* request_id);
IpcResult IpcSendResponse(uint32_t target_pid, uint32_t request_id, const void* response_data, uint64_t size, int32_t status);
// IpcResult IpcBroadcast(const IpcMessage* msg, uint32_t* target_pids, uint32_t count);

// Utility functions
uint32_t IpcGetQueueCount(void);
bool IpcHasMessages(void);
bool IpcHasMessageType(IpcMessageType type);
void IpcFlushQueue(void);

static inline bool ToData(IpcMessage * data, const char * msg) {
    const size_t msg_len = StringLength(msg);
    for (int i = 0; i < msg_len; i++) {
        data->payload.data[i] = msg[i];
    }
    return true;
}

#endif
