#ifndef __KERNEL_SOCKET_H
#define __KERNEL_SOCKET_H

/**
    @file kernel_socket.h
    @brief TinyOS kernel: Socket management.

    @defgroup sockets Sockets
    @ingroup kernel
    @brief Socket management.

    This file defines the socket_cb structure and basic helpers for
    socket implementation.

    @{
*/

#include "kernel_dev.h"
#include "kernel_pipe.h"
#include "util.h"

/**
    @brief Socket type

    A socket can be either a listener, an unbound socket or a peer socket.
*/
typedef enum socket_type_e
{
    SOCKET_LISTENER,
    SOCKET_UNBOUND,
    SOCKET_PEER
} socket_type;

typedef struct socket_control_block socket_cb;

/**
    @brief Listener socket.
*/
typedef struct listener_s
{
    rlnode queue;
    CondVar req_available;
} listener_socket;

/**
    @brief Unbound socket.
*/
typedef struct unbound_s
{
    rlnode unbound_socket;
} unbound_socket;

/**
    @brief Peer socket.
*/
typedef struct peer_s
{
    socket_cb *peer;
    pipe_cb *write_pipe;
    pipe_cb *read_pipe;
} peer_socket;

/**
    @brief Socket Control Block.

    This structure holds all information pertaining to a socket.
*/
typedef struct socket_control_block
{
    uint refcount;    /**< @brief Reference counter. */
    FCB *fcb;         /**< @brief The socket's FCB */
    socket_type type; /**< @brief The socket type */
    port_t port;      /**< @brief The socket's port */

    union
    {
        listener_socket listener_s;
        unbound_socket unbound_s;
        peer_socket peer_s;
    };
} socket_cb;

/**
    @brief Connection request.

    This structure holds all information pertaining to a connection request.
*/
typedef struct connection_request
{
    int admitted;
    socket_cb *peer;
    CondVar connected_cv;
    rlnode queue_node;
} request;

/**
    @brief Socket write operation.

    A wrapper around the pipe_write function that validates the socket
    before calling the pipe_write function.

    Write up to 'n' bytes from 'buf' to the stream 'sock_cb'.
    If it is not possible to write any data (e.g., a buffer is full),
    the thread will block.
    The write function should return the number of bytes copied from buf,
    or -1 on error.

    Possible errors are:
    - The socket is not a peer socket.
    - The socket doesn't have an open write pipe.
    - There was a I/O runtime problem.

    @param sock_cb The socket control block.
    @param buf The buffer to write from.
    @param n The number of bytes to write.
    @return The number of bytes written on success or -1 on error.
*/
int socket_write(void *sock_cb, const char *buf, unsigned int n);

/**
    @brief Socket read operation.

    A wrapper around the pipe_read function that validates the socket
    before calling the pipe_read function.

    Read up to 'n' bytes from stream 'sock_cb' into buffer 'buf'.
    If no data is available, the thread will block, to wait for data.
    The Read function should return the number of bytes copied into buf,
    or -1 on error. The call may return fewer bytes than 'n',
    but at least 1. A value of 0 indicates "end of data".

    Possible errors are:
    - The socket is not a peer socket.
    - The socket doesn't have an open read pipe.
    - There was a I/O runtime problem.

    @param sock_cb The Socket control block.
    @param buf The buffer to read into.
    @param n The number of bytes to read.
    @return The number of bytes read on success or -1 on error.
*/
int socket_read(void *sock_cb, char *buf, unsigned int n);

/**
    @brief Socket close operation.

    Close the stream object, deallocating any resources held by it.
    This function returns 0 is it was successful and -1 if not.
    Although the value in case of failure is passed to the calling process,
    the stream should still be destroyed.

    Possible errors are:
    - The socket is not valid.
    - There was a I/O runtime problem.

    @param sock_cb The socket control block.
    @return 0 on success or -1 on error.
*/
int socket_close(void *sock_cb);

/** @} */

#endif
