#include <click/config.h>
#include <click/error.hh>
#include <click/args.hh>
#include <click/glue.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/packet_anno.hh>
#include <click/packet.hh>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <click/timer.hh>
#include <click/handlercall.hh>
#include "empsocket.hh"

#ifdef HAVE_PROPER
#include <proper/prop.h>
#endif

CLICK_DECLS

EmpSocket::EmpSocket()
  : _task(this), _timer(this),
    _fd(-1), _active(-1), _rq(0), _wq(0),
    _local_port(0), _local_pathname(""),
    _timestamp(true), _sndbuf(-1), _rcvbuf(-1),
    _snaplen(2048), _headroom(Packet::default_headroom), _nodelay(1),
    _verbose(false), _client(false), _proper(false), _allow(0), _deny(0), 
    _reconnect_call_h(0), have_master(false)
{
}

EmpSocket::~EmpSocket()
{
}

void EmpSocket::run_timer(Timer *) {

  ErrorHandler *errh = new ErrorHandler();

  /* Re_init socket in case of disconnect (active = -1) */
  if (_active == -1) {
    have_master = false;
    initialize(errh);
    init_socket(errh);
  }

  if (bc_socket.is_inited() && !have_master)
  {
      bc_socket.send_bc();
      struct bc_answer ans = bc_socket.recv_answer();
      click_chatter("Received broadcast answer %s from %s\n", ans.buffer, inet_ntoa(ans.ip));
    
    /* check if we got the right answer or just our own */
    if (strstr(ans.buffer, "hello") != NULL)
    {
      click_chatter("Controller found at %s\n", inet_ntoa(ans.ip));
      _remote_ip = IPAddress(inet_ntoa(ans.ip));
      
      have_master = true;
      init_socket(errh);
    }
    else
    {
      click_chatter("Invalid broadcast answer message, discarded\n");
    }
  }
  
  _timer.reschedule_after_sec(2);
  return;

}

int
EmpSocket::configure(Vector<String> &conf, ErrorHandler *errh)
{
  String socktype;
  _client = (noutputs() == 0);
  Args args = Args(this, errh).bind(conf);
  if (args.read_mp("TYPE", socktype).execute() < 0)
    return -1;
  socktype = socktype.upper();

  if (args.read_m("WTP", wtp).execute() < 0)
    return -1;

  String reconnect_call;
  // remove keyword arguments
  Element *allow = 0, *deny = 0;
  if (args.read("VERBOSE", _verbose)
      .read("SNAPLEN", _snaplen)
      .read("HEADROOM", _headroom)
      .read("TIMESTAMP", _timestamp)
      .read("RCVBUF", _rcvbuf)
      .read("SNDBUF", _sndbuf)
      .read("NODELAY", _nodelay)
      .read("CLIENT", _client)
      .read("PROPER", _proper)
      .read("RECONNECT_CALL", AnyArg(), reconnect_call)
      .read("ALLOW", allow)
      .read("DENY", deny)
      .consume() < 0)
    return -1;

  if (reconnect_call)
    _reconnect_call_h = new HandlerCall(reconnect_call);

  if (allow && !(_allow = (IPRouteTable *)allow->cast("IPRouteTable")))
    return errh->error("%s is not an IPRouteTable", allow->name().c_str());

  if (deny && !(_deny = (IPRouteTable *)deny->cast("IPRouteTable")))
    return errh->error("%s is not an IPRouteTable", deny->name().c_str());

  if (socktype == "TCP" || socktype == "UDP") {
    _family = AF_INET;
    _socktype = socktype == "TCP" ? SOCK_STREAM : SOCK_DGRAM;
    _protocol = socktype == "TCP" ? IPPROTO_TCP : IPPROTO_UDP;
    if (args.read_p("ADDR", _remote_ip)
	.read_p("PORT", IPPortArg(_protocol), _remote_port)
	.read_p("LOCAL_ADDR", _local_ip)
	.read_p("LOCAL_PORT", IPPortArg(_protocol), _local_port)
	.complete() < 0)
      return -1;
  }

  else if (socktype == "UNIX" || socktype == "UNIX_DGRAM") {
    _family = AF_UNIX;
    _socktype = socktype == "UNIX" ? SOCK_STREAM : SOCK_DGRAM;
    _protocol = 0;
    if (args.read_mp("FILENAME", FilenameArg(), _remote_pathname)
	.read_p("LOCAL_FILENAME", FilenameArg(), _local_pathname)
	.complete() < 0)
      return -1;
    int max_path = (int)sizeof(((struct sockaddr_un *)0)->sun_path);
    // if not in the abstract namespace (begins with zero byte),
    // reserve room for trailing NUL
    if ((_remote_pathname[0] && _remote_pathname.length() >= max_path) ||
	(_remote_pathname[0] == 0 && _remote_pathname.length() > max_path))
      return errh->error("remote filename '%s' too long", _remote_pathname.printable().c_str());
    if ((_local_pathname[0] && _local_pathname.length() >= max_path) ||
	(_local_pathname[0] == 0 && _local_pathname.length() > max_path))
      return errh->error("local filename '%s' too long", _local_pathname.printable().c_str());
  }

  else
    return errh->error("unknown socket type `%s'", socktype.c_str());

  if (!_remote_ip)
  {
      click_chatter("EmpSocket: No remote ip specified, starting broadcasting\n");
  }
  else
  {
    click_chatter("EmpSocket: Remote IP specified, disable broadcasting\n");
    have_master = true;
    init_socket(errh);
  }
  return 0;
}


int
EmpSocket::initialize_socket_error(ErrorHandler *errh, const char *syscall)
{
  int e = errno;		// preserve errno

  if (_fd >= 0) {
    remove_select(_fd, SELECT_READ | SELECT_WRITE);
    close(_fd);
    _fd = -1;
  }

  click_chatter("%s: %s: %s", declaration().c_str(), syscall, strerror(e));

  return 0;

}

int
EmpSocket::initialize(ErrorHandler *errh)
{
  // init bc socket
  if (!bc_socket.is_inited())
  {
    if (!bc_socket.init("192.168.0.255", 4434, wtp))
    {
      click_chatter(bc_socket.get_err().c_str());
    }
  }
  
  // initialize timer
  _timer.initialize(this);
  _timer.reschedule_after_sec(2);

  // initialize callback
  if (_reconnect_call_h && (_reconnect_call_h->initialize_write(this, errh) < 0))
    return initialize_socket_error(errh, "callback");

  return 0;
}


int EmpSocket::init_socket(ErrorHandler *errh)
{
  if (!have_master || !_remote_ip)
  {
    click_chatter("EmpSocket:: init_socket called, but no controller found yet\n");
    return 0;
  }
  _remote_port = 4433; // hard code empower port
  click_chatter("EmpSocket: init socket with ip %s and port %d\n", _remote_ip.unparse().c_str(), _remote_port);
    
  // open socket, set options
  _fd = socket(_family, _socktype, _protocol);
  if (_fd < 0)
    return initialize_socket_error(errh, "socket");

  if (_family == AF_INET) {
    _remote.in.sin_family = _family;
    _remote.in.sin_port = htons(_remote_port);
    _remote.in.sin_addr = _remote_ip.in_addr();
    _remote_len = sizeof(_remote.in);
    _local.in.sin_family = _family;
    _local.in.sin_port = htons(_local_port);
    _local.in.sin_addr = _local_ip.in_addr();
    _local_len = sizeof(_local.in);
  }
  else {
    _remote.un.sun_family = _family;
    _remote_len = offsetof(struct sockaddr_un, sun_path) + _remote_pathname.length();
    if (_remote_pathname[0]) {
      strcpy(_remote.un.sun_path, _remote_pathname.c_str());
      _remote_len++;
    } else
      memcpy(_remote.un.sun_path, _remote_pathname.c_str(), _remote_pathname.length());
    _local.un.sun_family = _family;
    _local_len = offsetof(struct sockaddr_un, sun_path) + _local_pathname.length();
    if (_local_pathname[0]) {
      strcpy(_local.un.sun_path, _local_pathname.c_str());
      _local_len++;
    } else
      memcpy(_local.un.sun_path, _local_pathname.c_str(), _local_pathname.length());
  }

#ifdef TCP_NODELAY
  // disable Nagle algorithm
  if (_protocol == IPPROTO_TCP && _nodelay)
    if (setsockopt(_fd, IP_PROTO_TCP, TCP_NODELAY, &_nodelay, sizeof(_nodelay)) < 0)
      return initialize_socket_error(errh, "setsockopt(TCP_NODELAY)");
#endif

  // set socket send buffer size
  if (_sndbuf >= 0)
    if (setsockopt(_fd, SOL_SOCKET, SO_SNDBUF, &_sndbuf, sizeof(_sndbuf)) < 0)
      return initialize_socket_error(errh, "setsockopt(SO_SNDBUF)");

  // set socket receive buffer size
  if (_rcvbuf >= 0)
    if (setsockopt(_fd, SOL_SOCKET, SO_RCVBUF, &_rcvbuf, sizeof(_rcvbuf)) < 0)
      return initialize_socket_error(errh, "setsockopt(SO_RCVBUF)");

  // if a server, then the first arguments should be interpreted as
  // the address/port/file to bind() to, not to connect() to
  if (!_client) {
    memcpy(&_local, &_remote, _remote_len);
    _local_len = _remote_len;
  }

  // if a server, or if the optional local arguments have been
  // specified, bind() to the specified address/port/file
  if (!_client || _local_port != 0 || _local_pathname != "") {
#ifdef HAVE_PROPER
    int ret = -1;
    if (_proper) {
      ret = prop_bind_socket(_fd, (struct sockaddr *)&_local, _local_len);
      if (ret < 0)
	errh->warning("prop_bind_socket: %s", strerror(errno));
    }
    if (ret < 0)
#endif
    if (bind(_fd, (struct sockaddr *)&_local, _local_len) < 0)
      return initialize_socket_error(errh, "bind");
  }

  if (_client) {
    // connect
    if (_socktype == SOCK_STREAM) {
      if (connect(_fd, (struct sockaddr *)&_remote, _remote_len) < 0)
	return initialize_socket_error(errh, "connect");
      if (_verbose)
	click_chatter("%s: opened connection %d to %s:%d", declaration().c_str(), _fd, IPAddress(_remote.in.sin_addr).unparse().c_str(), ntohs(_remote.in.sin_port));
    }
    _active = _fd;
  } else {
    // start listening
    if (_socktype == SOCK_STREAM) {
      if (listen(_fd, 2) < 0)
	return initialize_socket_error(errh, "listen");
      if (_verbose) {
	if (_family == AF_INET)
	  click_chatter("%s: listening for connections on %s:%d (%d)", declaration().c_str(), IPAddress(_local.in.sin_addr).unparse().c_str(), ntohs(_local.in.sin_port), _fd);
	else
	  click_chatter("%s: listening for connections on %s (%d)", declaration().c_str(), _local.un.sun_path, _fd);
      }
    } else {
      _active = _fd;
    }
  }

  // nonblocking I/O and close-on-exec for the socket
  fcntl(_fd, F_SETFL, O_NONBLOCK);
  fcntl(_fd, F_SETFD, FD_CLOEXEC);

  if (noutputs())
    add_select(_fd, SELECT_READ);

  if (ninputs() && input_is_pull(0)) {
    ScheduleInfo::join_scheduler(this, &_task, errh);
    _signal = Notifier::upstream_empty_signal(this, 0, &_task);
    add_select(_fd, SELECT_WRITE);
  }

  if (_reconnect_call_h)
    (void) _reconnect_call_h->call_write();

  click_chatter("EmpSocket: Socket inited with %s\n", _remote_ip.s().c_str());
  
  return 0;
}

void
EmpSocket::cleanup(CleanupStage)
{
  if (_active >= 0 && _active != _fd) {
    close(_active);
    _active = -1;
  }
  if (_rq)
    _rq->kill();
  if (_wq)
    _wq->kill();
  if (_fd >= 0) {
    // shut down the listening socket in case we forked
#ifdef SHUT_RDWR
    shutdown(_fd, SHUT_RDWR);
#else
    shutdown(_fd, 2);
#endif
    close(_fd);
    if (_family == AF_UNIX)
      unlink(_local_pathname.c_str());
    _fd = -1;
  }
}

bool
EmpSocket::allowed(IPAddress addr)
{
  IPAddress gw;

  if (_allow && _allow->lookup_route(addr, gw) >= 0)
    return true;
  else if (_deny && _deny->lookup_route(addr, gw) >= 0)
    return false;
  else
    return true;
}

void
EmpSocket::close_active(void)
{
  if (_active >= 0) {
    remove_select(_active, SELECT_READ | SELECT_WRITE);
    close(_active);
    if (_verbose)
      click_chatter("%s: closed connection %d", declaration().c_str(), _active);
    _active = -1;
  }
}

void
EmpSocket::selected(int fd, int)
{
  int len;
  union { struct sockaddr_in in; struct sockaddr_un un; } from;
  socklen_t from_len = sizeof(from);
  bool allow;

  if (noutputs()) {
    // accept new connections
    if (_socktype == SOCK_STREAM && !_client && _active < 0 && fd == _fd) {
      _active = accept(_fd, (struct sockaddr *)&from, &from_len);

      if (_active < 0) {
	if (errno != EAGAIN)
	  click_chatter("%s: accept: %s", declaration().c_str(), strerror(errno));
	return;
      }

      if (_family == AF_INET) {
	allow = allowed(IPAddress(from.in.sin_addr));

	if (_verbose)
	  click_chatter("%s: %s connection %d from %s:%d", declaration().c_str(),
			allow ? "opened" : "denied",
			_active, IPAddress(from.in.sin_addr).unparse().c_str(), ntohs(from.in.sin_port));

	if (!allow) {
	  close(_active);
	  _active = -1;
	  return;
	}
      } else {
	if (_verbose)
	  click_chatter("%s: opened connection %d from %s", declaration().c_str(), _active, from.un.sun_path);
      }

      fcntl(_active, F_SETFL, O_NONBLOCK);
      fcntl(_active, F_SETFD, FD_CLOEXEC);

      add_select(_active, SELECT_READ);
    }

    // read data from socket
    if (!_rq)
      _rq = Packet::make(_headroom, 0, _snaplen, 0);
    if (_rq) {
      if (_socktype == SOCK_STREAM)
	len = read(_active, _rq->data(), _rq->length());
      else if (_client)
	len = recv(_active, _rq->data(), _rq->length(), MSG_TRUNC);
      else {
	// datagram server, find out who we are talking to
	len = recvfrom(_active, _rq->data(), _rq->length(), MSG_TRUNC, (struct sockaddr *)&from, &from_len);

	if (_family == AF_INET && !allowed(IPAddress(from.in.sin_addr))) {
	  if (_verbose)
	    click_chatter("%s: dropped datagram from %s:%d", declaration().c_str(),
			  IPAddress(from.in.sin_addr).unparse().c_str(), ntohs(from.in.sin_port));
	  len = -1;
	  errno = EAGAIN;
	} else if (len > 0) {
	  memcpy(&_remote, &from, from_len);
	  _remote_len = from_len;
	}
      }

      // this segment OK
      if (len > 0) {
	if (len > _snaplen) {
	  // truncate packet to max length (should never happen)
	  assert(_rq->length() == (uint32_t)_snaplen);
	  SET_EXTRA_LENGTH_ANNO(_rq, len - _snaplen);
	} else {
	  // trim packet to actual length
	  _rq->take(_snaplen - len);
	}

	// set timestamp
	if (_timestamp)
	  _rq->timestamp_anno().assign_now();

	// push packet
	output(0).push(_rq);
	_rq = 0;
      }

      // connection terminated or fatal error
      else if (len == 0 || errno != EAGAIN) {
	if (errno != EAGAIN && _verbose)
	  click_chatter("%s: %s", declaration().c_str(), strerror(errno));
	close_active();
	return;
      }
    }
  }

  if (ninputs() && input_is_pull(0))
    run_task(0);
}

int
EmpSocket::write_packet(Packet *p)
{
  int len;

  assert(_active >= 0);

  while (p->length()) {
    if (!IPAddress(_remote_ip) && _client && _family == AF_INET && _socktype != SOCK_STREAM) {
      // If the IP address specified when the element was created is 0.0.0.0,
      // send the packet to its IP destination annotation address
      _remote.in.sin_addr = p->dst_ip_anno();
    }

    // write segment
    if (_socktype == SOCK_STREAM)
      len = write(_active, p->data(), p->length());
    else
      len = sendto(_active, p->data(), p->length(), 0,
		   (struct sockaddr *)&_remote, _remote_len);

    // error
    if (len < 0) {
      // out of memory or would block
      if (errno == ENOBUFS || errno == EAGAIN)
	return -1;

      // interrupted by signal, try again immediately
      else if (errno == EINTR)
	continue;

      // connection probably terminated or other fatal error
      else {
	if (_verbose)
	  click_chatter("%s: %s", declaration().c_str(), strerror(errno));
	close_active();
	break;
      }
    } else
      // this segment OK
      p->pull(len);
  }

  p->kill();
  return 0;
}

void
EmpSocket::push(int, Packet *p)
{
  fd_set fds;
  int err;

  if (!have_master)
  {
    click_chatter("Push called on socket but not yet bound, ignoring\n");
    p->kill();
    return;
  }
  
  if (_active >= 0) {
    // block
    do {
      FD_ZERO(&fds);
      FD_SET(_active, &fds);
      err = select(_active + 1, NULL, &fds, NULL, NULL);
    } while (err < 0 && errno == EINTR);

    if (err >= 0) {
      // write
      do {
	err = write_packet(p);
      } while (err < 0 && (errno == ENOBUFS || errno == EAGAIN));
    }

    if (err < 0) {
      if (_verbose)
	click_chatter("%s: %s, dropping packet", declaration().c_str(), strerror(err));
      p->kill();
    }
  } else
    p->kill();
}

bool
EmpSocket::run_task(Task *)
{
  assert(ninputs() && input_is_pull(0));
  bool any = false;

  if (_active >= 0) {
    Packet *p = 0;
    int err = 0;

    // write as much as we can
    do {
      p = _wq ? _wq : input(0).pull();
      _wq = 0;
      if (p) {
	any = true;
	err = write_packet(p);
      }
    } while (p && err >= 0);

    if (err < 0) {
      // queue packet for writing when socket becomes available
      _wq = p;
      p = 0;
      add_select(_active, SELECT_WRITE);
    } else if (_signal)
      // more pending
      // (can't use fast_reschedule() cause selected() calls this)
      _task.reschedule();
    else
      // wrote all we could and no more pending
      remove_select(_active, SELECT_WRITE);
  }

  // true if we wrote at least one packet
  return any;
}

String EmpSocket::read_handler(Element *e, void *thunk) {
  click_chatter("EmpSocket: read handler called");
  
  EmpSocket *es = (EmpSocket *)e;
  
  switch ((uintptr_t)thunk) {
    case 0:
      click_chatter("EmpSocket: got restart signal, closing and starting again");
      es->close_active();
      return String("done");
    default:
      return String();
  }
}

void
EmpSocket::add_handlers()
{
  add_task_handlers(&_task);
  add_read_handler("restart", read_handler, (void *) 0);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel IPRouteTable BroadcastSocket)
EXPORT_ELEMENT(EmpSocket)
