#pragma once
#include <click/config.h>
#include <click/element.hh>
#include <click/timer.hh>
CLICK_DECLS


class PacketGen : public Element
{
    public:
    PacketGen() CLICK_COLD;
    ~PacketGen() CLICK_COLD;

    const char *class_name() const	{ return "PacketGen"; }
    const char *port_count() const	{ return "0/1"; }
    const char *processing() const	{ return PUSH; }
    const char *flow_code() const	{ return "x/x"; }

    int initialize(ErrorHandler *);
    int configure(Vector<String> &, ErrorHandler *);
    void run_timer(Timer *);
    void send_hello();
    
    void push(int, Packet *);
    Packet *pull(int);

    private:
        unsigned int _ctr;
        Timer _timer;
    
};

CLICK_ENDDECLS
