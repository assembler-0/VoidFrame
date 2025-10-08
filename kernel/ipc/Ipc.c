#include "Ipc.h"
#include "../../mm/MemOps.h"
#include "MLFQ.h"
#include "Spinlock.h"

static uint32_t next_sequence_id = 1;

static inline uint32_t get_next_sequence_id(void) {
    return __sync_fetch_and_add(&next_sequence_id, 1);
}

static inline void update_priority_bitmap(MessageQueue* queue, IpcPriority priority) {
    queue->priority_bitmap |= (1U << priority);
}

static inline IpcPriority get_highest_priority(MessageQueue* queue) {
    if (!queue->priority_bitmap) return IPC_PRIORITY_NORMAL;
    return (IpcPriority)(__builtin_clz(queue->priority_bitmap) ^ 31);
}

static uint32_t find_priority_message(MessageQueue* queue, IpcPriority min_priority) {
    uint32_t best_idx = queue->head;
    IpcPriority best_priority = IPC_PRIORITY_LOW;
    uint64_t oldest_time = UINT64_MAX;
    
    for (uint32_t i = 0; i < queue->count; i++) {
        uint32_t idx = (queue->head + i) % MAX_MESSAGES;
        IpcMessage* msg = &queue->messages[idx];
        
        if (msg->priority >= min_priority && 
            (msg->priority > best_priority || 
             (msg->priority == best_priority && msg->timestamp < oldest_time))) {
            best_idx = idx;
            best_priority = msg->priority;
            oldest_time = msg->timestamp;
        }
    }
    return best_idx;
}

IpcResult IpcSendMessage(uint32_t target_pid, const IpcMessage* msg) {
    if (!msg) return IPC_ERROR_INVALID_MSG;
    MLFQProcessControlBlock* target = MLFQGetCurrentProcessByPID(target_pid);
    if (!target) return IPC_ERROR_NO_PROCESS;
    MessageQueue* queue = &target->ipc_queue;
    SpinLock(&queue->lock);
    IpcMessage* dest = NULL;
    if (queue->count >= MAX_MESSAGES) {
        // Try to drop lowest priority message if this is higher priority
        if (msg->priority > IPC_PRIORITY_LOW) {
            uint32_t drop_idx = queue->head;
            IpcPriority lowest = IPC_PRIORITY_URGENT;
            for (uint32_t i = 0; i < queue->count; i++) {
                uint32_t idx = (queue->head + i) % MAX_MESSAGES;
                if (queue->messages[idx].priority < lowest) {
                    lowest = queue->messages[idx].priority;
                    drop_idx = idx;
                }
            }
            if (msg->priority > lowest) {
                // Overwrite the dropped entry in‐place
                dest = &queue->messages[drop_idx];
                queue->dropped_count++;
            } else {
                SpinUnlock(&queue->lock);
                return IPC_ERROR_QUEUE_FULL;
            }
        } else {
            SpinUnlock(&queue->lock);
            return IPC_ERROR_QUEUE_FULL;
        }
    } else {
        // Normal enqueue when there's room
        dest = &queue->messages[queue->tail];
        queue->tail = (queue->tail + 1) % MAX_MESSAGES;
        queue->count++;
    }
    FastMemcpy(dest, msg, sizeof(IpcMessage));
    dest->timestamp   = MLFQGetSystemTicks();
    dest->sequence_id = get_next_sequence_id();
    // Recompute the priority bitmap so it's accurate after any replacement
    queue->priority_bitmap = 0;
    for (uint32_t i = 0; i < queue->count; i++) {
        uint32_t idx = (queue->head + i) % MAX_MESSAGES;
        update_priority_bitmap(queue, queue->messages[idx].priority);
    }
    // Wake up the target under the same lock to avoid missed wakeups
    if (target->state == PROC_BLOCKED) {
        target->state = PROC_READY;
    }
    SpinUnlock(&queue->lock);
    return IPC_SUCCESS;
}

IpcResult IpcReceiveMessage(IpcMessage* msg_buffer) {
    if (!msg_buffer) return IPC_ERROR_INVALID_MSG;
    MLFQProcessControlBlock* current = MLFQGetCurrentProcess();
    MessageQueue* queue = &current->ipc_queue;
    while (true) {
        SpinLock(&queue->lock);

        if (queue->count > 0) {
            uint32_t msg_idx = find_priority_message(queue, IPC_PRIORITY_LOW);
            FastMemcpy(msg_buffer, &queue->messages[msg_idx], sizeof(IpcMessage));

            // Remove message by shifting if not at head
            if (msg_idx != queue->head) {
                for (uint32_t i = msg_idx; i != queue->head; ) {
                    uint32_t prev = (i - 1 + MAX_MESSAGES) % MAX_MESSAGES;
                    FastMemcpy(&queue->messages[i], &queue->messages[prev], sizeof(IpcMessage));
                    i = prev;
                }
            }

            queue->head = (queue->head + 1) % MAX_MESSAGES;
            queue->count--;

            // Update priority bitmap
            if (queue->count == 0) {
                queue->priority_bitmap = 0;
            } else {
                queue->priority_bitmap = 0;
                for (uint32_t i = 0; i < queue->count; i++) {
                    uint32_t idx = (queue->head + i) % MAX_MESSAGES;
                    update_priority_bitmap(queue, queue->messages[idx].priority);
                }
            }

            SpinUnlock(&queue->lock);
            return IPC_SUCCESS;
        }

        current->state = PROC_BLOCKED;
        SpinUnlock(&queue->lock);
        MLFQYield();
    }
}

IpcResult IpcReceiveMessageType(IpcMessage* msg_buffer, IpcMessageType type) {
    if (!msg_buffer) return IPC_ERROR_INVALID_MSG;
    MLFQProcessControlBlock* current = MLFQGetCurrentProcess();
    MessageQueue* queue = &current->ipc_queue;
    while (true) {
        SpinLock(&queue->lock);

        // Look for message of specific type
        for (uint32_t i = 0; i < queue->count; i++) {
            uint32_t idx = (queue->head + i) % MAX_MESSAGES;
            if (queue->messages[idx].type == type) {
                FastMemcpy(msg_buffer, &queue->messages[idx], sizeof(IpcMessage));

                // Shift messages to fill gap
                for (uint32_t j = i; j > 0; j--) {
                    uint32_t curr = (queue->head + j) % MAX_MESSAGES;
                    uint32_t prev = (queue->head + j - 1) % MAX_MESSAGES;
                    FastMemcpy(&queue->messages[curr], &queue->messages[prev], sizeof(IpcMessage));
                }

                queue->head = (queue->head + 1) % MAX_MESSAGES;
                queue->count--;

                // Recompute priority bitmap
                queue->priority_bitmap = 0;
                for (uint32_t k = 0; k < queue->count; k++) {
                    uint32_t idx2 = (queue->head + k) % MAX_MESSAGES;
                    update_priority_bitmap(queue, queue->messages[idx2].priority);
                }
                SpinUnlock(&queue->lock);
                return IPC_SUCCESS;
            }
        }

        // Mark blocked while still holding the lock to avoid a wakeup‐before‐block race
        current->state = PROC_BLOCKED;
        SpinUnlock(&queue->lock);
        MLFQYield();
    }
}

IpcResult IpcSendRequest(uint32_t target_pid, const void* request_data, uint64_t size, uint32_t* request_id) {
    if (!request_data || size > (IPC_MAX_PAYLOAD - 8) || !request_id) {
        return IPC_ERROR_INVALID_MSG;
    }
    
    IpcMessage msg = {
        .sender_pid = MLFQGetCurrentProcess()->pid,
        .type = IPC_TYPE_REQUEST,
        .priority = IPC_PRIORITY_NORMAL,
        .size = size + 8
    };
    
    *request_id = get_next_sequence_id();
    msg.payload.request.request_id = *request_id;
    msg.payload.request.flags = 0;
    FastMemcpy(msg.payload.request.request_data, request_data, size);
    
    return IpcSendMessage(target_pid, &msg);
}

IpcResult IpcSendResponse(uint32_t target_pid, uint32_t request_id, const void* response_data, uint64_t size, int32_t status) {
    if (size > (IPC_MAX_PAYLOAD - 8)) return IPC_ERROR_INVALID_MSG;
    
    IpcMessage msg = {
        .sender_pid = MLFQGetCurrentProcess()->pid,
        .type = IPC_TYPE_RESPONSE,
        .priority = IPC_PRIORITY_HIGH,
        .size = size + 8
    };
    
    msg.payload.response.request_id = request_id;
    msg.payload.response.status = status;
    if (response_data && size > 0) {
        FastMemcpy(msg.payload.response.response_data, response_data, size);
    }
    
    return IpcSendMessage(target_pid, &msg);
}

uint32_t IpcGetQueueCount(void) {
    MLFQProcessControlBlock* current = MLFQGetCurrentProcess();
    return current->ipc_queue.count;
}

bool IpcHasMessages(void) {
    return IpcGetQueueCount() > 0;
}

bool IpcHasMessageType(IpcMessageType type) {
    MLFQProcessControlBlock* current = MLFQGetCurrentProcess();
    MessageQueue* queue = &current->ipc_queue;
    
    SpinLock(&queue->lock);
    for (uint32_t i = 0; i < queue->count; i++) {
        uint32_t idx = (queue->head + i) % MAX_MESSAGES;
        if (queue->messages[idx].type == type) {
            SpinUnlock(&queue->lock);
            return true;
        }
    }
    SpinUnlock(&queue->lock);
    return false;
}

void IpcFlushQueue(void) {
    MLFQProcessControlBlock* current = MLFQGetCurrentProcess();
    MessageQueue* queue = &current->ipc_queue;
    
    SpinLock(&queue->lock);
    queue->head = queue->tail = queue->count = 0;
    queue->priority_bitmap = 0;
    SpinUnlock(&queue->lock);
}
