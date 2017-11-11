#pragma once

#include <click/element.hh>
#include <click/string.hh>
#include <click/task.hh>
#include <click/timer.hh>
#include <click/notifier.hh>
#include "../ip/iproutetable.hh"
#include <sys/un.h>
#include <click/handlercall.hh>
#include "bc_socket.hh"
CLICK_DECLS


class EmpSocket : public Element { public:

  EmpSocket() CLICK_COLD;
  ~EmpSocket() CLICK_COLD;


  const char *class_name() const	{ return "EmpSocket"; }
  const char *port_count() const	{ return "0-1/0-1"; }
  const char *processing() const	{ return "a/h"; }
  const char *flow_code() const		{ return "x/y"; }
  const char *flags() const		{ return "S3"; }

  virtual int configure(Vector<String> &conf, ErrorHandler *) CLICK_COLD;
  virtual int initialize(ErrorHandler *) CLICK_COLD;
  virtual void cleanup(CleanupStage) CLICK_COLD;

  void add_handlers() CLICK_COLD;
  bool run_task(Task *);
  void run_timer(Timer *);
  void selected(int fd, int mask);
  void push(int port, Packet*);

  bool allowed(IPAddress);
  void close_active(void);
  int write_packet(Packet*);

protected:
  Task _task;
  Timer _timer;

private:
  int _fd;	// socket descriptor
  int _active;	// connection descriptor

  // local address to bind()
  union { struct sockaddr_in in; struct sockaddr_un un; } _local;
  socklen_t _local_len;

  // remote address to connect() to or sendto() (for
  // non-connection-mode sockets)
  union { struct sockaddr_in in; struct sockaddr_un un; } _remote;
  socklen_t _remote_len;

  NotifierSignal _signal;	// packet is available to pull()
  WritablePacket *_rq;		// queue to receive pulled packets
  Packet *_wq;			// queue to store pulled packet for when sendto() blocks

  int _family;			// AF_INET or AF_UNIX
  int _socktype;		// SOCK_STREAM or SOCK_DGRAM
  int _protocol;		// for AF_INET, IPPROTO_TCP, IPPROTO_UDP, etc.
  IPAddress _local_ip;		// for AF_INET, address to bind()
  uint16_t _local_port;		// for AF_INET, port to bind()
  String _local_pathname;	// for AF_UNIX, file to bind()
  IPAddress _remote_ip;		// for AF_INET, address to connect() to or sendto()
  uint16_t _remote_port;	// for AF_INET, port to connect() to or sendto()
  String _remote_pathname;	// for AF_UNIX, file to sendto()

  bool _timestamp;		// set the timestamp on received packets
  int _sndbuf;			// maximum socket send buffer in bytes
  int _rcvbuf;			// maximum socket receive buffer in bytes
  int _snaplen;			// maximum received packet length
  unsigned _headroom;
  int _nodelay;			// disable Nagle algorithm
  bool _verbose;		// be verbose
  bool _client;			// client or server
  bool _proper;			// (PlanetLab only) use Proper to bind port
  IPRouteTable *_allow;		// lookup table of good hosts
  IPRouteTable *_deny;		// lookup table of bad hosts

  int initialize_socket_error(ErrorHandler *, const char *);
  
  int init_socket(ErrorHandler *);
  bool have_master;
  BroadcastSocket bc_socket;

  HandlerCall *_reconnect_call_h;
  
  static String read_handler(Element *, void *);

};

CLICK_ENDDECLS
