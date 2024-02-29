
#include <assert.h>
#include "kernel_cc.h"
#include "kernel_proc.h"
#include "kernel_streams.h"
#include "kernel_threads.h"


/*
 The process table and related system calls:
 - Exec
 - Exit
 - WaitPid
 - GetPid
 - GetPPid

 */

/* The process table */
PCB PT[MAX_PROC];
unsigned int process_count;

PCB* get_pcb(Pid_t pid)
{
  return PT[pid].pstate==FREE ? NULL : &PT[pid];
}

Pid_t get_pid(PCB* pcb)
{
  return pcb==NULL ? NOPROC : pcb-PT;
}

/* Initialize a PCB */
static inline void initialize_PCB(PCB* pcb)
{
  pcb->pstate = FREE;
  pcb->argl = 0;
  pcb->args = NULL;

  for(int i=0;i<MAX_FILEID;i++)
    pcb->FIDT[i] = NULL;

  rlnode_init(& pcb->children_list, NULL);
  rlnode_init(& pcb->exited_list, NULL);
  rlnode_init(& pcb->children_node, pcb);
  rlnode_init(& pcb->exited_node, pcb);
  pcb->child_exit = COND_INIT;

  rlnode_init(&pcb->ptcb_list, pcb);
  pcb->thread_count = 0;
}


static PCB* pcb_freelist;

void initialize_processes()
{
  /* initialize the PCBs */
  for(Pid_t p=0; p<MAX_PROC; p++) {
    initialize_PCB(&PT[p]);
  }

  /* use the parent field to build a free list */
  PCB* pcbiter;
  pcb_freelist = NULL;
  for(pcbiter = PT+MAX_PROC; pcbiter!=PT; ) {
    --pcbiter;
    pcbiter->parent = pcb_freelist;
    pcb_freelist = pcbiter;
  }

  process_count = 0;

  /* Execute a null "idle" process */
  if(Exec(NULL,0,NULL)!=0)
    FATAL("The scheduler process does not have pid==0");
}


/*
  Must be called with kernel_mutex held
*/
PCB* acquire_PCB()
{
  PCB* pcb = NULL;

  if(pcb_freelist != NULL) {
    pcb = pcb_freelist;
    pcb->pstate = ALIVE;
    pcb_freelist = pcb_freelist->parent;
    process_count++;
  }

  return pcb;
}

/*
  Must be called with kernel_mutex held
*/
void release_PCB(PCB* pcb)
{
  pcb->pstate = FREE;
  pcb->parent = pcb_freelist;
  pcb_freelist = pcb;
  process_count--;
}


/*
 *
 * Process creation
 *
 */

/*
	This function is provided as an argument to spawn,
	to execute the main thread of a process.
*/
void start_main_thread()
{
  int exitval;

  Task call =  CURPROC->main_task;
  int argl = CURPROC->argl;
  void* args = CURPROC->args;

  exitval = call(argl,args);
  Exit(exitval);
}


/*
	System call to create a new process.
 */
Pid_t sys_Exec(Task call, int argl, void* args)
{
  PCB *curproc, *newproc;
  /* The new process PCB */
  newproc = acquire_PCB();

  if(newproc == NULL) goto finish;  /* We have run out of PIDs! */

  if(get_pid(newproc)<=1) {
    /* Processes with pid<=1 (the scheduler and the init process)
       are parentless and are treated specially. */
    newproc->parent = NULL;
  }
  else
  {
    /* Inherit parent */
    curproc = CURPROC;

    /* Add new process to the parent's child list */
    newproc->parent = curproc;
    rlist_push_front(& curproc->children_list, & newproc->children_node);

    /* Inherit file streams from parent */
    for(int i=0; i<MAX_FILEID; i++) {
       newproc->FIDT[i] = curproc->FIDT[i];
       if(newproc->FIDT[i])
          FCB_incref(newproc->FIDT[i]);
    }
  }


  /* Set the main thread's function */
  newproc->main_task = call;

  /* Copy the arguments to new storage, owned by the new process */
  newproc->argl = argl;
  if(args!=NULL) {
    newproc->args = malloc(argl);
    memcpy(newproc->args, args, argl);
  }
  else
    newproc->args=NULL;

  /*
    Create and wake up the thread for the main function. This must be the last thing
    we do, because once we wakeup the new thread it may run! so we need to have finished
    the initialization of the PCB.
   */
  if(call != NULL) {
    newproc->main_thread = spawn_thread(newproc, start_main_thread);

    /* Initialize the PTCB */
    PTCB* ptcb = xmalloc(sizeof(PTCB));
    initialize_PTCB(ptcb);
    ptcb->task = call;
    ptcb->argl = argl;
    ptcb->args = args;

    /* Connect the PTCB to the TCB of the main thread and vice versa */
    ptcb->tcb = newproc->main_thread;
    newproc->main_thread->ptcb = ptcb;

    /* Push the PTCB to PCB's list of PTCBs and increase the thread count */
    rlist_push_back(&newproc->ptcb_list, &ptcb->ptcb_list_node);
    newproc->thread_count++;

    /* Wake up the thread */
    wakeup(newproc->main_thread);
  }


finish:
  return get_pid(newproc);
}


/* System call */
Pid_t sys_GetPid()
{
  return get_pid(CURPROC);
}


Pid_t sys_GetPPid()
{
  return get_pid(CURPROC->parent);
}


static void cleanup_zombie(PCB* pcb, int* status)
{
  if(status != NULL)
    *status = pcb->exitval;

  rlist_remove(& pcb->children_node);
  rlist_remove(& pcb->exited_node);

  release_PCB(pcb);
}


static Pid_t wait_for_specific_child(Pid_t cpid, int* status)
{

  /* Legality checks */
  if((cpid<0) || (cpid>=MAX_PROC)) {
    cpid = NOPROC;
    goto finish;
  }

  PCB* parent = CURPROC;
  PCB* child = get_pcb(cpid);
  if( child == NULL || child->parent != parent)
  {
    cpid = NOPROC;
    goto finish;
  }

  /* Ok, child is a legal child of mine. Wait for it to exit. */
  while(child->pstate == ALIVE)
    kernel_wait(& parent->child_exit, SCHED_USER);

  cleanup_zombie(child, status);

finish:
  return cpid;
}


static Pid_t wait_for_any_child(int* status)
{
  Pid_t cpid;

  PCB* parent = CURPROC;

  /* Make sure I have children! */
  int no_children, has_exited;
  while(1) {
    no_children = is_rlist_empty(& parent->children_list);
    if( no_children ) break;

    has_exited = ! is_rlist_empty(& parent->exited_list);
    if( has_exited ) break;

    kernel_wait(& parent->child_exit, SCHED_USER);
  }

  if(no_children)
    return NOPROC;

  PCB* child = parent->exited_list.next->pcb;
  assert(child->pstate == ZOMBIE);
  cpid = get_pid(child);
  cleanup_zombie(child, status);

  return cpid;
}


Pid_t sys_WaitChild(Pid_t cpid, int* status)
{
  /* Wait for specific child. */
  if(cpid != NOPROC) {
    return wait_for_specific_child(cpid, status);
  }
  /* Wait for any child */
  else {
    return wait_for_any_child(status);
  }

}


void sys_Exit(int exitval)
{
  /* Store the exit status */
  CURPROC->exitval = exitval;

  sys_ThreadExit(exitval);
}


/*
    Information stream read operation.
*/
int procinfo_read(void *_procinfo_cb, char *buf, unsigned int size)
{
    /* Get the information stream */
    procinfo_cb *info_cb = (procinfo_cb *)_procinfo_cb;

    /* Check if the stream is valid */
    if (info_cb == NULL)
        return -1; /* Return -1 to indicate error */

    /* Check if we reached the end of the process table */
    if (info_cb->pcb_cursor == MAX_PROC)
        return 0; /* Return 0 to indicate "end of data" */

    /* Find the first non-free process, starting from the cursor */
    while (info_cb->pcb_cursor < MAX_PROC - 1 && PT[info_cb->pcb_cursor].pstate == FREE)
        info_cb->pcb_cursor++;

    /* The PCB of the process we found */
    PCB pcb = PT[info_cb->pcb_cursor];

    /* Copy the information of the process we found */
    info_cb->info->pid = get_pid(&pcb);
    info_cb->info->ppid = get_pid(pcb.parent);
    info_cb->info->alive = pcb.pstate == ALIVE; /* 1 if alive, 0 if zombie */
    info_cb->info->thread_count = pcb.thread_count;
    info_cb->info->main_task = pcb.main_task;
    info_cb->info->argl = pcb.argl;

    /* Copy the arguments string. If the string is too long,
       copy only the first PROCINFO_MAX_ARGS_SIZE bytes */
    memcpy(info_cb->info->args, pcb.args, pcb.argl >= PROCINFO_MAX_ARGS_SIZE ? PROCINFO_MAX_ARGS_SIZE : pcb.argl);

    /* Copy the process' information to the buffer */
    memcpy(buf, info_cb->info, size);

    /* Increment the cursor */
    info_cb->pcb_cursor++;

    /* Return the number of bytes copied into buf */
    return size;
}

/*
    Information stream close operation.
*/
int procinfo_close(void *_procinfo_cb)
{
    /* Get the information stream */
    procinfo_cb *info_cb = (procinfo_cb *)_procinfo_cb;

    /* Check if the stream is valid */
    if (info_cb == NULL)
        return -1; /* Return -1 to indicate error */

    /* Free the stream */
    free(info_cb->info);
    free(info_cb);

    /* Return 0 to indicate success */
    return 0;
}

/*
    Information stream file operations.
*/
static file_ops procinfo_file_ops = {
    .Open = NULL,
    .Read = procinfo_read,
    .Write = NULL,
    .Close = procinfo_close};

/**
    Open a kernel information stream.
*/
Fid_t sys_OpenInfo()
{
    /* Create an FCB and corresponding Fid */
    Fid_t fid;
    FCB *fcb;

    /* Try to acquire the FCB and Fid and check if we succeeded */
    if (FCB_reserve(1, &fid, &fcb) == 0)
        return NOFILE; /* Return NOFILE to indicate error */

    /* Initialize the stream */
    procinfo_cb *info_cb = (procinfo_cb *)xmalloc(sizeof(procinfo_cb));
    info_cb->info = (procinfo *)xmalloc(sizeof(procinfo));
    info_cb->pcb_cursor = 0;
    fcb->streamobj = info_cb;
    fcb->streamfunc = &procinfo_file_ops;

    /* Return the Fid */
    return fid;
}
