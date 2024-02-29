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

/** @} */

#endif
