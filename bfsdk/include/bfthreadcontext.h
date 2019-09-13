/*
 * Copyright (C) 2019 Assured Information Security, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef BFTHREADCONTEXT
#define BFTHREADCONTEXT

#include "bftypes.h"

#pragma pack(push, 1)

#ifdef __cplusplus
extern "C" {
#endif

uint64_t _thread_context_get_sp(void) NOEXCEPT;

#ifdef __cplusplus
}
#endif

#define BFTLS_SIZE 0x1000
#define BFCANARY 0xBF42BF42BF42BF42

/**
 * @struct thread_context_t
 *
 * Thread Context
 *
 * On the top of every stack pointer sits one of these structures, which is
 * used to identify thread specific information. For more information on
 * how this works, please see the following post:
 *
 * https://github.com/Bareflank/hypervisor/issues/213
 *
 * WARNING:
 *
 * If you change this structure to add additional fields, ensure the canary
 * is the first field in the structure as this is the first field that
 * should be overwritten when an underflow occurs. Also, the structure must
 * remain 64byte aligned (i.e., 512 bit aligned) which ensure the resulting
 * stack has the proper alignment when optimizations are enabled, which will
 * use aligned SIMD instructions assuming the stack is aligned properly.
 *
 * @var thread_context_t::canary
 *      the id of the thread
 * @var thread_context_t::tlsptr
 *      the TLS pointer of the thread
 * @var thread_context_t::id
 *      the id of the thread
 * @var thread_context_t::reserved
 *      reserved
 */
struct thread_context_t {
    uint64_t canary;
    void *tlsptr;
    uint64_t id;
    uint64_t reserved[5];
};

#ifdef __cplusplus
static_assert(sizeof(struct thread_context_t) == 64);
#endif

/**
 * Stack Size
 *
 * The stack is always 2* the stack size that is provided by the compiler.
 * This is needed to ensure that we can create aligned memory that is
 * aligned to the size of the stack, which is needed to calculate the location
 * of the thread context block.
 *
 */
static inline uint64_t
stack_size(void) NOEXCEPT
{ return BFSTACK_SIZE * 2; }

/**
 * Top Of Stack
 *
 * This function returns the top of the stack given a stack pointer. Note
 * that if this function is called from the thread itself, 0 should be passed
 * to this function so that it can fetch the current stack pointer of the
 * thread. If this function is called from the loader, the stack pointer that
 * is provided should be the pointer you get when allocating the stack
 * itself.
 *
 * @param sp the stack pointer
 * @return returns the top of the stack given a stack pointer
 */
static inline uint64_t
thread_context_top_of_stack(uint64_t sp)
{
    if (sp == 0) {
        return (_thread_context_get_sp() + BFSTACK_SIZE) & ~(BFSTACK_SIZE - 1);
    }
    else {
        return (sp + stack_size()) & ~(BFSTACK_SIZE - 1);
    }
}

/**
 * Bottom Of Stack
 *
 * @param sp the stack pointer
 * @return returns the bottom of the stack given a stack pointer
 */
static inline uint64_t
thread_context_bottom_of_stack(uint64_t sp)
{ return thread_context_top_of_stack(sp) - BFSTACK_SIZE; }

/**
 * Thread Context Pointer
 *
 * @param sp the stack pointer
 * @return returns a pointer to the thread context structure given a stack ptr
 */
static inline struct thread_context_t *
thread_context_ptr(uint64_t sp)
{ return BFRCAST(struct thread_context_t *, thread_context_top_of_stack(sp) - sizeof(struct thread_context_t)); }

/**
 * Thread Context ID
 *
 * @return returns the current thread's ID
 */
static inline uint64_t
thread_context_id(void) NOEXCEPT
{ return thread_context_ptr(0)->id; }

/**
 * Thread Context TLS Pointer
 *
 * @return returns a pointer to the current thread's TLS block
 */
static inline uint64_t *
thread_context_tlsptr(void) NOEXCEPT
{ return BFRCAST(uint64_t *, thread_context_ptr(0)->tlsptr); }

/**
 * Setup Stack
 *
 * The following function sets up the stack to match the algorithm defined
 * in the following issue (with some mods to cleanup math errors):
 *
 * https://github.com/Bareflank/hypervisor/issues/213
 *
 * ------------ 0x9050 <-- 0x1050 + (BFSTACK_SIZE * 2)
 * |          |
 * |   ---    | 0x8000 <-- top stack
 * |   ---    | 0x7FF8 <-- id
 * |   ---    | 0x7FF0 <-- TLS pointer
 * |          |
 * |   ---    | 0x7FC0 <-- starting stack pointer (contains canary)
 * |          |
 * |          |
 * |          |
 * |   ---    | 0x4000 <-- bottom of stack (contains canary)
 * |          |
 * |          |
 * |          |
 * ------------ 0x1050 <-- returned by malloc(BFSTACK_SIZE * 2)
 *
 * @param stack a pointer to a newly allocated stack
 * @param id the ID for this thread
 * @param tlsptr the pointer to the TLS block for this thread
 * @return the starting stack pointer
 */
static inline uint64_t
setup_stack(void *stack, uint64_t id, void *tlsptr) NOEXCEPT
{
    uint64_t sp = BFRCAST(uint64_t, stack);

    /**
     * Fill in the thread context structure. A thread can use the functions
     * defined above to get this information as needed.
     */
    struct thread_context_t *tc = thread_context_ptr(sp);
    tc->id = id;
    tc->tlsptr = tlsptr;

    /**
     * The following sets up our stack canaries. We place a canary at the top
     * and the bottom of the stack to check for overflows and underflows of
     * the stack.
     */
    tc->canary = BFCANARY;
    BFRCAST(uint64_t *, thread_context_bottom_of_stack(sp))[0] = BFCANARY;

    /**
     * Finally we will return the location of the stack without the
     * thread context added which is the true top of stack
     */
    return BFRCAST(uint64_t, tc);
}

/**
 * Validate Canaries
 *
 * After a thread executes, this function can be used to see if the stack was
 * corrupt after it's execution. If this function detects and error, the
 * stack should be increased to prevent corruption, or the stack should be
 * used less in the C++ code itself.
 *
 * Note that this function will check for both a stack overflow and a stack
 * underflow.
 *
 * @param stack the thread's stack
 * @return BFSUCCESS on success, BFFAILURE on failure.
 */
static inline status_t
validate_canaries(void *stack) NOEXCEPT
{
    uint64_t sp = BFRCAST(uint64_t, stack);
    struct thread_context_t *tc = thread_context_ptr(sp);

    if (tc->canary != BFCANARY) {
        return BFFAILURE;
    }

    if (BFRCAST(uint64_t *, thread_context_bottom_of_stack(sp))[0] != BFCANARY) {
        return BFFAILURE;
    }

    return BFSUCCESS;
}

#pragma pack(pop)

#endif
