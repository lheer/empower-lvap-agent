#pragma once

#include <click/config.h>
#include <click/string.hh>
#include <click/etheraddress.hh>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdint.h>

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
        bool init(String bc_ip, unsigned short port, EtherAddress wtp);
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
        char bc_packet[18];
};


CLICK_ENDDECLS
