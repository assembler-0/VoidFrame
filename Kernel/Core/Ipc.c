#include "Ipc.h"
#include "Process.h"
#include "Panic.h"
#include "../Memory/MemOps.h"

void IpcSendMessage(uint32_t target_pid, IpcMessage* msg) {
    ASSERT(msg != NULL);

    Process* target = GetProcessByPid(target_pid);
    if (!target) {
        // Handle error: target process not found
        return;
    }

    MessageQueue* queue = &target->ipc_queue;
    if (queue->count >= MAX_MESSAGES) {
        // Handle error: queue is full
        return;
    }

    FastMemcpy(&queue->messages[queue->tail], msg, sizeof(IpcMessage));
    queue->tail = (queue->tail + 1) % MAX_MESSAGES;
    queue->count++;

    if (target->state == PROC_BLOCKED) {
        target->state = PROC_READY;
    }
}

int IpcReceiveMessage(IpcMessage* msg_buffer) {
    ASSERT(msg_buffer != NULL);

    Process* current = GetCurrentProcess();
    MessageQueue* queue = &current->ipc_queue;

    while (queue->count == 0) {
        current->state = PROC_BLOCKED;
        Yield();
    }

    FastMemcpy(msg_buffer, &queue->messages[queue->head], sizeof(IpcMessage));
    queue->head = (queue->head + 1) % MAX_MESSAGES;
    queue->count--;

    return 0; // Success
}
