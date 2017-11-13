#pragma once
CLICK_DECLS

struct packet_header
{
    private:
        uint8_t _version;
        uint32_t _length;
    
    public:
        uint8_t version() {return _version;}
        uint32_t length() {return _length;}
        void set_version(uint8_t v) {_version = v;}
        void set_length(uint32_t l) {_length = l;}
} CLICK_SIZE_PACKED_ATTRIBUTE;

struct hello_packet : public packet_header
{
    private:
        uint8_t _id;
    
    public:
        uint8_t id() {return _id;}
        void set_id(uint8_t id) {_id = id;}
} CLICK_SIZE_PACKED_ATTRIBUTE;


CLICK_ENDDECLS
