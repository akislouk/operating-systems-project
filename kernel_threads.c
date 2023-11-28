#include "tinyos.h"
#include "kernel_cc.h"
#include "kernel_proc.h"
#include "kernel_sched.h"
#include "kernel_streams.h"
#include "kernel_threads.h"

void initialize_PTCB(PTCB *ptcb)
{
    ptcb->tcb = NULL;

    ptcb->task = NULL;
    ptcb->argl = 0;
    ptcb->args = NULL;

    ptcb->exitval = 0;
    ptcb->exited = 0;
    ptcb->detached = 0;
    ptcb->exit_cv = COND_INIT;

    ptcb->refcount = 0;

    rlnode_init(&ptcb->ptcb_list_node, ptcb);
}

/*
    This function is provided as an argument to spawn,
    to execute a process's thread.
*/
void start_thread()
{
    int exitval;

    Task call = cur_thread()->ptcb->task;
    int argl = cur_thread()->ptcb->argl;
    void *args = cur_thread()->ptcb->args;

    exitval = call(argl, args);
    ThreadExit(exitval);
}

/**
    @brief Create a new thread in the current process.
*/
Tid_t sys_CreateThread(Task task, int argl, void *args)
{
    /* If no function is given, return NOTHREAD */
    if (task == NULL)
        return NOTHREAD;

    /* The current process */
    PCB *pcb = CURPROC;

    /* Create a new thread and get its TCB */
    TCB *tcb = spawn_thread(pcb, start_thread);

    /* Initialize the PTCB */
    PTCB *ptcb = xmalloc(sizeof(PTCB));
    initialize_PTCB(ptcb);
    ptcb->task = task;
    ptcb->argl = argl;
    ptcb->args = args;

    /* Connect the PTCB to the TCB and vice versa */
    ptcb->tcb = tcb;
    tcb->ptcb = ptcb;

    /* Push the PTCB to PCB's list of PTCBs and increase the thread count */
    rlist_push_back(&pcb->ptcb_list, &ptcb->ptcb_list_node);
    pcb->thread_count++;

    /* Wake up the thread */
    wakeup(tcb);

    /* Return the Tid of the new thread */
    return (Tid_t)ptcb;
}

/**
    @brief Return the Tid of the current thread.
*/
Tid_t sys_ThreadSelf()
{
    return (Tid_t)cur_thread()->ptcb;
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int *exitval)
{
    /* Check if the given tid is valid */
    if (tid == NOTHREAD)
        return -1;

    /* The PTCB of the given thread */
    PTCB *ptcb = (PTCB *)tid;

    /*
        Can't join if the given thread is not in the current process,
        if it is the current thread or if it is detached.
    */
    if (rlist_find(&CURPROC->ptcb_list, ptcb, NULL) == NULL || tid == sys_ThreadSelf() || ptcb->detached == 1)
        return -1;

    /* Increment the refcount since it looks like the given thread is joinable */
    ptcb->refcount++;

    /* Wait until the given thread exits or is detached */
    while (ptcb->exited == 0 && ptcb->detached == 0)
        kernel_wait(&ptcb->exit_cv, SCHED_USER);

    /* Decrement the refcount */
    ptcb->refcount--;

    /* Can't join if the given thread was detached */
    if (ptcb->detached == 1)
        return -1;

    /* Return the exit status of the given thread to the exitval pointer */
    if (exitval != NULL)
        *exitval = ptcb->exitval;

    /*
        If the refcount is 0 after the thread exits, it is no longer needed,
        so remove the PTCB from the current process's list of PTCBs
    */
    if (ptcb->refcount == 0)
    {
        rlist_remove(&ptcb->ptcb_list_node);
        free(ptcb); /* Free the memory allocated by xmalloc for the PTCB */
    }

    return 0;
}

/**
    @brief Detach the given thread.
*/
int sys_ThreadDetach(Tid_t tid)
{
    /* Check if the given tid is valid */
    if (tid == NOTHREAD)
        return -1;

    /* The PTCB of the given thread */
    PTCB *ptcb = (PTCB *)tid;

    /*
        Can't detach if the given thread is not in the current process or
        if it has already exited.
    */
    if (rlist_find(&CURPROC->ptcb_list, ptcb, NULL) == NULL || ptcb->exited == 1 )
        return -1;

    /* Mark the thread as detached and broadcast the exit_cv */
    ptcb->detached = 1;
    kernel_broadcast(&ptcb->exit_cv);

    return 0;
}

/**
    @brief Terminate the current thread.
*/
void sys_ThreadExit(int exitval)
{
    /* Cache for efficiency */
    PCB *curproc = CURPROC;
    PTCB *ptcb = (PTCB *)sys_ThreadSelf();

    /* Decrement the thread count */
    curproc->thread_count--;

    /* If this is the last thread, exit the process and clean up */
    if (curproc->thread_count == 0)
    {
        /*
            Here, we must check that we are not the init task.
            If we are, we must wait until all child processes exit.
        */
        if (get_pid(curproc) == 1)
        {
            while (sys_WaitChild(NOPROC, NULL) != NOPROC);
        }
        else
        {
            /* Re-parent any children of the exiting process to the initial task */
            PCB *initpcb = get_pcb(1);
            while (!is_rlist_empty(&curproc->children_list))
            {
                rlnode *child = rlist_pop_front(&curproc->children_list);
                child->pcb->parent = initpcb;
                rlist_push_front(&initpcb->children_list, child);
            }

            /* Add exited children to the initial task's exited list and signal the initial task */
            if (!is_rlist_empty(&curproc->exited_list))
            {
                rlist_append(&initpcb->exited_list, &curproc->exited_list);
                kernel_broadcast(&initpcb->child_exit);
            }

            /* Put me into my parent's exited list */
            rlist_push_front(&curproc->parent->exited_list, &curproc->exited_node);
            kernel_broadcast(&curproc->parent->child_exit);
        }

        assert(is_rlist_empty(&curproc->children_list));
        assert(is_rlist_empty(&curproc->exited_list));

        /*
            Do all the other cleanup we want here, close files etc.
        */

        /* Release the args data */
        if (curproc->args)
        {
            free(curproc->args);
            curproc->args = NULL;
        }

        /* Clean up FIDT */
        for (int i = 0; i < MAX_FILEID; i++)
        {
            if (curproc->FIDT[i] != NULL)
            {
                FCB_decref(curproc->FIDT[i]);
                curproc->FIDT[i] = NULL;
            }
        }

        /* Disconnect my main_thread */
        curproc->main_thread = NULL;

        /* Now, mark the process as exited. */
        curproc->pstate = ZOMBIE;
    }

    /* Mark the thread as exited and broadcast the exit_cv */
    ptcb->exited = 1;
    ptcb->exitval = exitval;
    kernel_broadcast(&ptcb->exit_cv);

    /* Bye-bye cruel world */
    kernel_sleep(EXITED, SCHED_USER);
}
