#include "tinyos.h"
#include "kernel_cc.h"
#include "kernel_pipe.h"
#include "kernel_sched.h"

/*
    The reader file operations. It only implements read and close.
*/
static file_ops reader_file_ops = {
    .Open = NULL,
    .Read = pipe_read,
    .Write = NULL,
    .Close = pipe_reader_close};

/*
    The writer file operations. It only implements write and close.
*/
static file_ops writer_file_ops = {
    .Open = NULL,
    .Read = NULL,
    .Write = pipe_write,
    .Close = pipe_writer_close};

/**
    @brief Construct and return a pipe.
*/
int sys_Pipe(pipe_t *pipe)
{
    /* Create 2 FCBs and corresponding Fids */
    FCB *fcb[2];
    Fid_t fid[2];
    int retval = FCB_reserve(2, fid, fcb); /* Acquire the FCBs and Fids */

    /* Check if we got the FCBs and Fids successfully */
    if (retval == 0)
        return -1; /* Return -1 to indicate error */

    /* Initialize the pipe */
    pipe_cb *pipecb = (pipe_cb *)xmalloc(sizeof(pipe_cb));
    pipecb->has_space = COND_INIT;
    pipecb->has_data = COND_INIT;
    pipecb->w_position = pipecb->r_position = pipecb->w_bytes = 0;

    /* Connect the FCBs to the pipe and vice versa */
    pipecb->reader = fcb[0];
    pipecb->writer = fcb[1];
    fcb[0]->streamobj = pipecb;
    fcb[0]->streamfunc = &reader_file_ops;
    fcb[1]->streamobj = pipecb;
    fcb[1]->streamfunc = &writer_file_ops;

    /* Return the pipe */
    pipe->read = fid[0];
    pipe->write = fid[1];

    /* Return 0 to indicate success */
    return 0;
}

int pipe_write(void *pipecb_t, const char *buf, unsigned int n)
{
    /* Get the pipe */
    pipe_cb *pipe = (pipe_cb *)pipecb_t;

    /* Check if the reader and the writer are open */
    if (pipe->reader == NULL || pipe->writer == NULL)
        return -1; /* Return -1 to indicate error */

    /* Iterate over the buffer and write the bytes */
    int i;
    for (i = 0; i < n; i++)
    {
        /* Wait while the buffer is full and the reader is open */
        while (pipe->reader != NULL && pipe->w_bytes == PIPE_BUFFER_SIZE)
        {
            /* Signal to waiting readers that there is data available */
            kernel_broadcast(&pipe->has_data);

            /* Wait for space to be available */
            kernel_wait(&pipe->has_space, SCHED_PIPE);
        }

        /* Write the current byte to the pipe buffer */
        pipe->BUFFER[pipe->w_position] = buf[i];

        /* Increment the write position and the number of bytes written */
        pipe->w_position = (pipe->w_position + 1) % PIPE_BUFFER_SIZE;
        pipe->w_bytes++;
    }

    /* Signal to waiting readers that there is data available */
    kernel_broadcast(&pipe->has_data);

    /* Return the number of bytes copied from buf */
    return i;
}

int pipe_read(void *pipecb_t, char *buf, unsigned int n)
{
    /* Get the pipe */
    pipe_cb *pipe = (pipe_cb *)pipecb_t;

    /* Check if the reader is open */
    if (pipe->reader == NULL)
        return -1; /* Return -1 to indicate error */

    /* Check if we reached the "end of data" */
    if (pipe->writer == NULL && pipe->r_position == pipe->w_position)
        return 0; /* Return 0 to indicate "end of data" */

    /* Iterate over the pipe buffer and read the bytes */
    int i;
    for (i = 0; i < n; i++)
    {
        /* Wait while the buffer is empty and the writer is open */
        while (pipe->writer != NULL && pipe->w_bytes == 0)
        {
            /* Signal to waiting writers that there is space */
            kernel_broadcast(&pipe->has_space);

            /* Wait for data to be available */
            kernel_wait(&pipe->has_data, SCHED_PIPE);
        }

        /* Check if the writer has closed and there is no more data to be read */
        if (pipe->writer == NULL && pipe->w_bytes == 0)
            return i; /* Return the number of bytes copied into buf */

        /* Read the byte at read position and write it to the buffer */
        buf[i] = pipe->BUFFER[pipe->r_position];

        /* Increment the read position and decrement the number of bytes written */
        pipe->r_position = (pipe->r_position + 1) % PIPE_BUFFER_SIZE;
        pipe->w_bytes--;
    }

    /* Signal to waiting writers that there is space */
    kernel_broadcast(&pipe->has_space);

    /* Return the number of bytes copied into buf */
    return i;
}

int pipe_writer_close(void *_pipecb)
{
    /* Get the pipe */
    pipe_cb *pipe = (pipe_cb *)_pipecb;

    /* Check if it is already closed */
    if (pipe == NULL || pipe->writer == NULL)
        return -1; /* Return -1 to indicate error */

    /* Close the writer */
    pipe->writer = NULL;

    /* Return 0 to indicate success */
    return 0;
}

int pipe_reader_close(void *_pipecb)
{
    /* Get the pipe */
    pipe_cb *pipe = (pipe_cb *)_pipecb;

    /* Check if it is already closed */
    if (pipe == NULL || pipe->reader == NULL)
        return -1; /* Return -1 to indicate error */

    /* Close the reader */
    pipe->reader = NULL;

    /* Return 0 to indicate success */
    return 0;
}
