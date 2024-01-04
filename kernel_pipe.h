#ifndef __KERNEL_PIPE_H
#define __KERNEL_PIPE_H

/**
    @file kernel_pipe.h
    @brief TinyOS kernel: Pipe management.

    @defgroup pipes Pipes
    @ingroup kernel
    @brief Pipe management.

    This file defines the pipe_cb structure and basic helpers for
    pipe implementation.

    @{
*/

#include "kernel_streams.h"

#define PIPE_BUFFER_SIZE 512 /**< @brief The size of the pipe buffer */

/**
    @brief Pipe control block.

    This structure holds all information pertaining to a pipe.
*/
typedef struct pipe_control_block
{
    FCB *reader, *writer;          /**< @brief Reader and writer file control blocks */
    CondVar has_space;             /**< @brief For blocking writer if no space is available */
    CondVar has_data;              /**< @brief For blocking reader until data are available */
    int w_position, r_position;    /**< @brief Write/Read position in buffer */
    int w_bytes;                   /**< @brief Amount of bytes written */
    char BUFFER[PIPE_BUFFER_SIZE]; /**< @brief Bounded (cyclic) byte buffer */
} pipe_cb;

// int sys_Pipe(pipe_t *pipe);

/**
    @brief Pipe write operation.

    Write up to 'n' bytes from 'buf' to the stream 'pipecb_t'.
    If it is not possible to write any data (e.g., a buffer is full),
    the thread will block.
    The write function should return the number of bytes copied from buf,
    or -1 on error.

    Possible errors are:
    - There was a I/O runtime problem.

    @param pipecb_t The pipe control block.
    @param buf The buffer to write from.
    @param n The number of bytes to write.
    @return The number of bytes written on success or -1 on error.
*/
int pipe_write(void *pipecb_t, const char *buf, unsigned int n);

/**
    @brief Pipe read operation.

    Read up to 'n' bytes from stream 'pipecb_t' into buffer 'buf'.
    If no data is available, the thread will block, to wait for data.
    The Read function should return the number of bytes copied into buf,
    or -1 on error. The call may return fewer bytes than 'n',
    but at least 1. A value of 0 indicates "end of data".

    Possible errors are:
    - There was a I/O runtime problem.

    @param pipecb_t The pipe control block.
    @param buf The buffer to read into.
    @param n The number of bytes to read.
    @return The number of bytes read on success or -1 on error.
*/
int pipe_read(void *pipecb_t, char *buf, unsigned int n);

/**
    @brief Pipe writer close operation.

    Close the stream object, deallocating any resources held by it.
    This function returns 0 is it was successful and -1 if not.
    Although the value in case of failure is passed to the calling process,
    the stream should still be destroyed.

    Possible errors are:
    - There was a I/O runtime problem.

    @param _pipecb The pipe control block.
    @return 0 on success or -1 on error.
*/
int pipe_writer_close(void *_pipecb);

/**
    @brief Pipe reader close operation.

    Close the stream object, deallocating any resources held by it.
    This function returns 0 is it was successful and -1 if not.
    Although the value in case of failure is passed to the calling process,
    the stream should still be destroyed.

    Possible errors are:
    - There was a I/O runtime problem.

    @param _pipecb The pipe control block.
    @return 0 on success or -1 on error.
*/
int pipe_reader_close(void *_pipecb);

/** @} */

#endif
