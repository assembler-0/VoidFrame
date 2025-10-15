#ifndef VECTOR_H
#define VECTOR_H

#include "stdbool.h"
#include "KernelHeap.h"
#include "MemOps.h"
#include "Panic.h"

/* Generic vector implementation in C with pseudo dot-syntax */

/* Define vector operations struct - this holds our "methods" */
#define VECTOR_OPS(T) \
    struct vector_ops_##T { \
        void (*push_back)(void*, T); \
        void (*pop_back)(void*); \
        T (*at)(void*, size_t); \
        T* (*get)(void*, size_t); \
        size_t (*size)(void*); \
        size_t (*capacity)(void*); \
        void (*clear)(void*); \
        void (*resize)(void*, size_t); \
        void (*reserve)(void*, size_t); \
        int (*empty)(void*); \
        T (*front)(void*); \
        T (*back)(void*); \
        void (*destroy)(void*); \
    }

/* Define the vector structure */
#define VECTOR(T) \
    struct vector_##T { \
        T* data; \
        size_t size; \
        size_t capacity; \
        struct vector_ops_##T ops; \
    }

/* Forward declare all functions for a type */
#define VECTOR_DECLARE(T) \
    VECTOR_OPS(T); \
    VECTOR(T); \
    void vector_##T##_push_back(void* v, T val); \
    void vector_##T##_pop_back(void* v); \
    T vector_##T##_at(void* v, size_t idx); \
    T* vector_##T##_get(void* v, size_t idx); \
    size_t vector_##T##_size(void* v); \
    size_t vector_##T##_capacity(void* v); \
    void vector_##T##_clear(void* v); \
    void vector_##T##_resize(void* v, size_t new_size); \
    void vector_##T##_reserve(void* v, size_t new_cap); \
    int vector_##T##_empty(void* v); \
    T vector_##T##_front(void* v); \
    T vector_##T##_back(void* v); \
    void vector_##T##_destroy(void* v); \
    struct vector_##T vector_##T##_init(void);

/* Define all vector functions for a type */
#define VECTOR_DEFINE(T) \
    void vector_##T##_push_back(void* v, T val) { \
        struct vector_##T* vec = (struct vector_##T*)v; \
        if (vec->size >= vec->capacity) { \
            size_t new_cap = vec->capacity == 0 ? 1 : vec->capacity * 2; \
            vector_##T##_reserve(v, new_cap); \
        } \
        vec->data[vec->size++] = val; \
    } \
    \
    void vector_##T##_pop_back(void* v) { \
        struct vector_##T* vec = (struct vector_##T*)v; \
        if (vec->size > 0) vec->size--; \
    } \
    \
    T vector_##T##_at(void* v, size_t idx) { \
        struct vector_##T* vec = (struct vector_##T*)v; \
        ASSERT(idx < vec->size && "Index out of bounds"); \
        return vec->data[idx]; \
    } \
    \
    T* vector_##T##_get(void* v, size_t idx) { \
        struct vector_##T* vec = (struct vector_##T*)v; \
        ASSERT(idx < vec->size && "Index out of bounds"); \
        return &vec->data[idx]; \
    } \
    \
    size_t vector_##T##_size(void* v) { \
        struct vector_##T* vec = (struct vector_##T*)v; \
        return vec->size; \
    } \
    \
    size_t vector_##T##_capacity(void* v) { \
        struct vector_##T* vec = (struct vector_##T*)v; \
        return vec->capacity; \
    } \
    \
    void vector_##T##_clear(void* v) { \
        struct vector_##T* vec = (struct vector_##T*)v; \
        vec->size = 0; \
    } \
    \
    void vector_##T##_resize(void* v, size_t new_size) { \
        struct vector_##T* vec = (struct vector_##T*)v; \
        if (new_size > vec->capacity) { \
            vector_##T##_reserve(v, new_size); \
        } \
        vec->size = new_size; \
    } \
    \
    void vector_##T##_reserve(void* v, size_t new_cap) { \
        struct vector_##T* vec = (struct vector_##T*)v; \
        if (new_cap > vec->capacity) { \
            T* new_data = (T*)KernelMemoryAlloc(new_cap * sizeof(T)); \
            if (vec->data) { \
                FastMemcpy(new_data, vec->data, vec->size * sizeof(T)); \
                KernelFree(vec->data); \
            } \
            vec->data = new_data; \
            vec->capacity = new_cap; \
        } \
    } \
    \
    int vector_##T##_empty(void* v) { \
        struct vector_##T* vec = (struct vector_##T*)v; \
        return vec->size == 0; \
    } \
    \
    T vector_##T##_front(void* v) { \
        struct vector_##T* vec = (struct vector_##T*)v; \
        ASSERT(vec->size > 0 && "Vector is empty"); \
        return vec->data[0]; \
    } \
    \
    T vector_##T##_back(void* v) { \
        struct vector_##T* vec = (struct vector_##T*)v; \
        ASSERT(vec->size > 0 && "Vector is empty"); \
        return vec->data[vec->size - 1]; \
    } \
    \
    void vector_##T##_destroy(void* v) { \
        struct vector_##T* vec = (struct vector_##T*)v; \
        if (vec->data) { \
            KernelFree(vec->data); \
            vec->data = NULL; \
        } \
        vec->size = 0; \
        vec->capacity = 0; \
    } \
    \
    struct vector_##T vector_##T##_init(void) { \
        struct vector_##T vec = { \
            .data = NULL, \
            .size = 0, \
            .capacity = 0, \
            .ops = { \
                .push_back = vector_##T##_push_back, \
                .pop_back = vector_##T##_pop_back, \
                .at = vector_##T##_at, \
                .get = vector_##T##_get, \
                .size = vector_##T##_size, \
                .capacity = vector_##T##_capacity, \
                .clear = vector_##T##_clear, \
                .resize = vector_##T##_resize, \
                .reserve = vector_##T##_reserve, \
                .empty = vector_##T##_empty, \
                .front = vector_##T##_front, \
                .back = vector_##T##_back, \
                .destroy = vector_##T##_destroy \
            } \
        }; \
        return vec; \
    }

/* Convenience macros for initialization and type naming */
#define vector(T) struct vector_##T
#define vector_init(T) vector_##T##_init()

/* Macro for array-like access */
#define vec_at(vec, idx) ((vec).data[idx])

/* Alternative approach using compound literals for even cleaner syntax */
#define VECTOR_NEW(T) ({ \
    struct vector_##T* v = KernelMemoryAlloc(sizeof(struct vector_##T)); \
    *v = vector_##T##_init(); \
    v; \
})

#define VECTOR_FREE(v) do { \
    (v)->ops.destroy(v); \
    KernelFree(v); \
} while(0)


#endif /* VECTOR_H */