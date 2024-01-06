#include "tinyos.h"
#include "kernel_cc.h"
#include "kernel_pipe.h"
#include "kernel_sched.h"
#include "kernel_socket.h"
#include "kernel_streams.h"
#include "util.h"

/* Initialize the port map */
socket_cb *PORT_MAP[MAX_PORT + 1] = {NULL};

/**
    Decrement the reference counter of a socket and free the memory allocated
    by xmalloc if no longer referenced.
*/
void SCB_decref(socket_cb *socket)
{
    socket->refcount--;
    if (socket->refcount < 0)
        free(socket);
    return;
}

/*
    The socket file operations.
*/
file_ops socket_file_ops = {
    .Open = NULL,
    .Read = socket_read,
    .Write = socket_write,
    .Close = socket_close};

/**
    @brief Return a new socket bound on a port.
*/
Fid_t sys_Socket(port_t port)
{
    /* Check if the given port is within the valid range */
    if (port < NOPORT || port > MAX_PORT)
        return NOFILE; /* Return NOFILE to indicate error */

    /* Create the FCB and corresponding Fid */
    FCB *fcb;
    Fid_t fid;

    /* Try to acquire the FCB and Fid and check if we succeeded */
    if (FCB_reserve(1, &fid, &fcb) == 0)
        return NOFILE; /* Return NOFILE to indicate error */

    /* Initialize the socket */
    socket_cb *socket = xmalloc(sizeof(socket_cb));
    socket->refcount = 0;
    socket->type = SOCKET_UNBOUND;
    socket->port = port;

    /* Connect the FCB to the socket and vice versa */
    socket->fcb = fcb;
    fcb->streamobj = socket;
    fcb->streamfunc = &socket_file_ops;

    /* Return the Fid */
    return fid;
}

/**
    @brief Initialize a socket as a listening socket.
*/
int sys_Listen(Fid_t sock)
{
    /* Get the FCB of the socket */
    FCB *fcb = get_fcb(sock);

    /* Check if the given Fid was legal */
    if (fcb == NULL)
        return -1; /* Return -1 to indicate error */

    /* Get the socket */
    socket_cb *socket = fcb->streamobj;

    /* Check if the socket is valid and uninitialized (unbound) */
    if (socket == NULL || socket->type != SOCKET_UNBOUND)
        return -1; /* Return -1 to indicate error */

    /* Check if the socket is bound to a valid port */
    if (socket->port == NOPORT)
        return -1; /* Return -1 to indicate error */

    /* Check if the port is occupied by another listener */
    if (PORT_MAP[(int)(socket->port)] != NULL && PORT_MAP[(int)(socket->port)]->type == SOCKET_LISTENER)
        return -1; /* Return -1 to indicate error */

    /* Initialize the socket as a listener and add it to the port map */
    socket->type = SOCKET_LISTENER;
    PORT_MAP[(int)(socket->port)] = socket;

    /* Initialize the listener queue and the request available condition variable */
    rlnode_init(&socket->listener_s.queue, NULL);
    socket->listener_s.req_available = COND_INIT;

    /* Return 0 to indicate success */
    return 0;
}

/**
    @brief Wait for a connection.
*/
Fid_t sys_Accept(Fid_t lsock)
{
    /* Get the FCB of the listening socket */
    FCB *fcb = get_fcb(lsock);

    /* Check if the given Fid was legal */
    if (fcb == NULL)
        return NOFILE; /* Return NOFILE to indicate error */

    /* Get the socket */
    socket_cb *socket = fcb->streamobj;

    /* Check if the socket is valid and a listener */
    if (socket == NULL || socket->type != SOCKET_LISTENER)
        return NOFILE; /* Return NOFILE to indicate error */

    /* Increment the reference counter and wait for a request */
    socket->refcount++;
    while (is_rlist_empty(&socket->listener_s.queue) && PORT_MAP[socket->port] == socket)
        kernel_wait(&socket->listener_s.req_available, SCHED_IO);

    /* Check if the socket was closed while waiting */
    if (PORT_MAP[socket->port] == NULL || socket->type == SOCKET_UNBOUND)
    {
        SCB_decref(socket); /* Decrement the reference counter */
        return NOFILE;      /* Return NOFILE to indicate error */
    }

    /* Get the first connection request from the queue */
    request *req = rlist_pop_front(&socket->listener_s.queue)->req;

    /* Create a new socket for the server */
    Fid_t server_fid = sys_Socket(socket->port);
    FCB *server_fcb = get_fcb(server_fid);

    /* Check if the server socket was created successfully */
    if (server_fcb == NULL)
    {
        kernel_signal(&req->connected_cv); /* Signal the Connect side */
        SCB_decref(socket);                /* Decrement the reference counter */
        return NOFILE;                     /* Return NOFILE to indicate error */
    }

    req->admitted = 1; /* The connection was accepted */

    socket_cb *client = req->peer; /* The client socket from the request */
    socket_cb *server = server_fcb->streamobj;
    server->type = client->type = SOCKET_PEER;
    server->peer_s.peer = client;
    client->peer_s.peer = server;

    /* Create pipes for the sockets */
    FCB *pipe_fcb = (FCB *)xmalloc(sizeof(FCB));
    pipe_cb *pipes = (pipe_cb *)xmalloc(sizeof(pipe_cb) * 2);
    for (int i = 0; i < 2; i++)
    { /* Initialize the pipes */
        pipes[i].has_space = COND_INIT;
        pipes[i].has_data = COND_INIT;
        pipes[i].w_position = pipes[i].r_position = pipes[i].w_bytes = 0;
        pipes[i].reader = pipe_fcb;
        pipes[i].writer = pipe_fcb;
    }

    /* Connect the pipes to the sockets */
    server->peer_s.read_pipe = pipes;
    server->peer_s.write_pipe = pipes + 1;
    client->peer_s.read_pipe = pipes + 1;
    client->peer_s.write_pipe = pipes;

    kernel_signal(&req->connected_cv); /* Signal the Connect side */
    SCB_decref(socket);                /* Decrement the reference counter */

    /* Return the Fid of the server socket */
    return server_fid;
}

/**
    @brief Create a connection to a listener at a specific port.
*/
int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
    /* Check if the given port is within the valid range */
    if (port < NOPORT || port > MAX_PORT)
        return -1; /* Return -1 to indicate error */

    /* Get the FCB of the socket */
    FCB *fcb = get_fcb(sock);

    /* Check if the given Fid was legal */
    if (fcb == NULL)
        return -1; /* Return -1 to indicate error */

    /* Get the socket */
    socket_cb *socket = fcb->streamobj;

    /* Check if the socket is valid and uninitialized (unbound) */
    if (socket == NULL || socket->type != SOCKET_UNBOUND)
        return -1; /* Return -1 to indicate error */

    /* Get the listener socket at the given port */
    socket_cb *listener = PORT_MAP[port];

    /* Check if the listener socket is valid and indeed a listener */
    if (listener == NULL || listener->type != SOCKET_LISTENER)
        return -1; /* Return -1 to indicate error */

    /* Increment the reference counter */
    socket->refcount++;

    /* Initialize the connection request */
    request *req = (request *)xmalloc(sizeof(request));
    req->admitted = 0;
    req->peer = socket;
    req->connected_cv = COND_INIT;
    rlnode_init(&req->queue_node, req);

    /* Add the connection request to the listener queue and signal the listener */
    rlist_push_back(&listener->listener_s.queue, &req->queue_node);
    kernel_signal(&listener->listener_s.req_available);

    /* Wait for the connection to be accepted for the specified timeout */
    kernel_timedwait(&req->connected_cv, SCHED_IO, timeout);
    SCB_decref(socket); /* Decrement the reference counter */

    /* Store the result of the connection request */
    int res = req->admitted;

    /* Remove the connection request from the listener queue and free the memory */
    rlist_remove(&req->queue_node);
    free(req);

    /* Return 0 to indicate success if the connection was accepted, -1 otherwise */
    return res == 1 ? 0 : -1;
}

/**
    @brief Shut down one direction of socket communication.
*/
int sys_ShutDown(Fid_t sock, shutdown_mode mode)
{
    /* Get the FCB of the socket */
    FCB *fcb = get_fcb(sock);

    /* Check if the given Fid was legal */
    if (fcb == NULL)
        return -1; /* Return -1 to indicate error */

    /* Get the socket */
    socket_cb *socket = fcb->streamobj;

    /* Check if the socket is valid */
    if (socket == NULL)
        return -1; /* Return -1 to indicate error */

    /* Shut down a direction of communication, based on the shutdown mode */
    switch (mode)
    {
    case SHUTDOWN_READ: /* Shut down the read pipe */
        pipe_reader_close(socket->peer_s.read_pipe);
        socket->peer_s.read_pipe = NULL;
        break;
    case SHUTDOWN_BOTH: /* Shut down both ends of the pipe */
        pipe_reader_close(socket->peer_s.read_pipe);
        socket->peer_s.read_pipe = NULL;
    case SHUTDOWN_WRITE: /* Shut down the write pipe */
        pipe_writer_close(socket->peer_s.write_pipe);
        socket->peer_s.write_pipe = NULL;
        break;
    }

    /* Return 0 to indicate success */
    return 0;
}

int socket_write(void *sock_cb, const char *buf, unsigned int n)
{
    /* Get the socket */
    socket_cb *socket = (socket_cb *)sock_cb;

    /* Check if the socket is valid and a peer */
    if (socket == NULL || socket->type != SOCKET_PEER)
        return -1; /* Return -1 to indicate error */

    /* Check if the socket has an open write pipe */
    if (socket->peer_s.write_pipe == NULL)
        return -1; /* Return -1 to indicate error */

    /* Get the write pipe and write to it */
    pipe_cb *pipe = socket->peer_s.write_pipe;
    return pipe_write(pipe, buf, n); /* Returns the number of bytes written or -1 on error */
}

int socket_read(void *sock_cb, char *buf, unsigned int n)
{
    /* Get the socket */
    socket_cb *socket = (socket_cb *)sock_cb;

    /* Check if the socket is valid and a peer */
    if (socket == NULL || socket->type != SOCKET_PEER)
        return -1; /* Return -1 to indicate error */

    /* Check if the socket has an open read pipe */
    if (socket->peer_s.read_pipe == NULL)
        return -1; /* Return -1 to indicate error */

    /* Get the read pipe and read from it */
    pipe_cb *pipe = socket->peer_s.read_pipe;
    return pipe_read(pipe, buf, n); /* Returns the number of bytes read or -1 on error */
}

int socket_close(void *sock_cb)
{
    /* Get the socket */
    socket_cb *socket = (socket_cb *)sock_cb;

    /* Check if the socket is valid */
    if (socket == NULL)
        return -1; /* Return -1 to indicate error */

    /* Check if the socket is a peer or a listener */
    if (socket->type == SOCKET_PEER)
    { /* If it is a peer, close both its pipes */
        pipe_reader_close(socket->peer_s.read_pipe);
        pipe_writer_close(socket->peer_s.write_pipe);
        socket->peer_s.read_pipe = NULL;
        socket->peer_s.write_pipe = NULL;
    }
    else if (socket->type == SOCKET_LISTENER)
    { /* If it is a listener, empty its queue, unbind it and signal the waiters */
        while (!is_rlist_empty(&socket->listener_s.queue))
            free(rlist_pop_back(&socket->listener_s.queue));
        PORT_MAP[socket->port] = NULL;
        kernel_broadcast(&socket->listener_s.req_available);
    }

    /* Decrement the reference counter */
    SCB_decref(socket);

    /* Return 0 to indicate success */
    return 0;
}
