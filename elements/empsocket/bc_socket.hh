#pragma once

#include <click/config.h>
#include <click/string.hh>
#include <sys/socket.h>
#include <arpa/inet.h>

CLICK_DECLS

struct bc_answer
{
    char *buffer;
    struct in_addr ip;
    unsigned short port;
};

class BroadcastSocket
{
    public:
        static unsigned int const BUFSIZE;
    
        BroadcastSocket();
        bool init(String bc_ip="255.255.255.255", unsigned short port=8000, unsigned int recv_timeout_us=1000);
        bool send_bc();
        struct bc_answer recv_answer();
        bool is_inited();
        String get_err();
        bool close_sockets();
    
    
    private:
        int bc_socket, recv_socket;
        struct sockaddr_in bc_addr,  recv_addr;
        String err;
        bool inited;
};


CLICK_ENDDECLS
