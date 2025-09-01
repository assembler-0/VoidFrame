#include "Ipc.h"
#include "../../mm/MemOps.h"
#include "Panic.h"
#include "Process.h"

void IpcSendMessage(uint32_t target_pid, const IpcMessage * msg) {
    ASSERT(msg != NULL);

    ProcessControlBlock* target = GetProcessByPid(target_pid);
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

    ProcessControlBlock* current = GetCurrentProcess();
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
