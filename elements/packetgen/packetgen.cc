#include <click/config.h>
#include "packetgen.hh"
#include "packets.hh"
CLICK_DECLS

PacketGen::PacketGen() :
    _ctr(0), _timer(this)
{
}

PacketGen::~PacketGen()
{
}

int PacketGen::initialize(ErrorHandler *)
{
    _timer.initialize(this);
    _timer.schedule_now();
    
    return 0;
}

int PacketGen::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

void PacketGen::run_timer(Timer *)
{
    click_chatter("PacketGen: i=%d run_timer called\n", _ctr);
    _ctr++;
    
    send_hello();
    
    _timer.reschedule_after_sec(2);
}

void PacketGen::send_hello()
{
    WritablePacket *p = Packet::make(sizeof(hello_packet));
    if (!p)
    {
        click_chatter("Cannot make hello packet!\n");
        return;
    }
    
    memset(p->data(), 0, p->length());
    hello_packet *hello = (struct hello_packet *)(p->data());
    
    hello->set_version(1);
    hello->set_length(sizeof(hello_packet));
    hello->set_id(4);
    
    checked_output_push(0, p);
}

void PacketGen::push(int, Packet *p)
{
    p->kill();
}

Packet *PacketGen::pull(int)
{
    return 0;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(PacketGen)
ELEMENT_REQUIRES(userlevel)
