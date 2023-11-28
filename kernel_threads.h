#ifndef __KERNEL_THREADS_H
#define __KERNEL_THREADS_H

/**
    @file kernel_threads.h
    @brief TinyOS kernel: Thread management.

    @defgroup threads Threads
    @ingroup kernel
    @brief Thread management.

    This file defines the PTCB structure and basic helpers for
    multithreading.

    @{
*/

#include "tinyos.h"
#include "kernel_sched.h"

/**
    @brief Process Thread Control Block.

    This structure holds all information pertaining to a thread.
*/
typedef struct process_thread_control_block
{
    TCB *tcb;              /**< @brief The thread's TCB */

    Task task;             /**< @brief The thread's function */
    int argl;              /**< @brief The thread's argument length */
    void *args;            /**< @brief The thread's argument string */

    int exitval;           /**< @brief The exit value of the thread */
    int exited;            /**< @brief Whether the thread has exited */
    int detached;          /**< @brief Whether the thread is detached */
    CondVar exit_cv;       /**< @brief The condition variable used to wait for the thread to exit */

    int refcount;          /**< @brief Reference counter. */

    rlnode ptcb_list_node; /**< @brief Intrusive list node */
} PTCB;

/**
    @brief Initialize a PTCB.

    @param ptcb The PTCB to initialize.
*/
void initialize_PTCB(PTCB *ptcb);

/** @} */

#endif
