#include <click/config.h>
#include <click/glue.hh>
#include <unistd.h>
#include <sys/types.h> 
#include <cstring>
#include "bc_socket.hh"

CLICK_DECLS

unsigned int const BroadcastSocket::BUFSIZE = 128;

BroadcastSocket::BroadcastSocket()
{
    inited = false;
}

bool BroadcastSocket::init(String bc_ip, unsigned short port, unsigned int recv_timeout_us)
{
    /* Create broadcast socket */
    if ((bc_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        err = "Creation of broadcast socket failed";
        return false;
    }
    
    /* Create socket to receive broadcast answers */
    if ((recv_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        err = "Creation of receive socket failed";
        return false;
    }
    
    /* Set permissions to broadcast socket */
    int bc_perm = 1;
    if (setsockopt(bc_socket, SOL_SOCKET,  SO_BROADCAST, (void *)&bc_perm, sizeof(bc_perm)) < 0)
    {
        err = "setsockopt for broadcast socket failed";
        return false;
    }
    
    /* Set timeout for recveive socket */
    struct timeval read_timeout;
    read_timeout.tv_sec = 0;
    read_timeout.tv_usec = recv_timeout_us;
    
    if (setsockopt(recv_socket, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout)) < 0)
    {
        err = "setsockopt for receive socket failed";
        return false;
    }
    
    bc_addr.sin_family = AF_INET;
    bc_addr.sin_addr.s_addr = inet_addr(bc_ip.c_str());
    bc_addr.sin_port = htons(port);
    
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_addr.s_addr = INADDR_ANY;
    recv_addr.sin_port = htons(port);
    
    /* Bind receive socket */
    if (bind(recv_socket, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) == -1)
    {
        err = "Binding of receive socket failed";
        return false;
    }
    
    inited = true;
    return true;
}

bool BroadcastSocket::send_bc()
{
    ssize_t ret = sendto(bc_socket, "ping", 4,  0, (struct sockaddr *)&bc_addr, sizeof(bc_addr));
    if (ret == -1)
    {
        err = "Sending broadcast failed";
        return false;
    }
    click_chatter("Broadcast sent\n");
    return true;
}

/* Non-blocking - see init method */
struct bc_answer BroadcastSocket::recv_answer()
{
    struct bc_answer ans;
    ans.buffer = new char[BUFSIZE];
    ans.ip.s_addr = 0;
    ans.port = 0;
    
    char buffer[BUFSIZE];
    unsigned int slen = sizeof(recv_addr);
    ssize_t ret = recvfrom(recv_socket, buffer, BUFSIZE, 0, (struct sockaddr *)&recv_addr, &slen);
    
    if (ret == -1)
    {
        err = "Receiving failed";
        return ans;
    }

    strncpy(ans.buffer, buffer, BUFSIZE);
    ans.ip = recv_addr.sin_addr;
    ans.port = recv_addr.sin_port;
    
    return ans;
}

String BroadcastSocket::get_err()
{
    return err;
}

bool BroadcastSocket::is_inited()
{
    return inited;
}

bool BroadcastSocket::close_sockets()
{
    if (close(bc_socket) == -1)
    {
        err = "Closing broadcast socket failed";
        return false;
    }
    if (close(recv_socket) == -1)
    {
        err = "Closing receive socket failed";
        return false;
    }
    return true;
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel)
ELEMENT_PROVIDES(BroadcastSocket)
