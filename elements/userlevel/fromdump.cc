// -*- mode: c++; c-basic-offset: 4 -*-
/*
 * fromdump.{cc,hh} -- element reads packets from tcpdump file
 * Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2001 International Computer Science Institute
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include "fromdump.hh"
#include <click/confparse.hh>
#include <click/router.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/handlercall.hh>
#include <click/packet_anno.hh>
#include "fakepcap.hh"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef ALLOW_MMAP
#include <sys/mman.h>
#endif

#define	SWAPLONG(y) \
	((((y)&0xff)<<24) | (((y)&0xff00)<<8) | (((y)&0xff0000)>>8) | (((y)>>24)&0xff))
#define	SWAPSHORT(y) \
	( (((y)&0xff)<<8) | ((u_short)((y)&0xff00)>>8) )

FromDump::FromDump()
    : Element(0, 1), _fd(-1), _buffer(0), _data_packet(0), _packet(0),
      _last_time_h(0), _task(this), _pipe(0)
{
    MOD_INC_USE_COUNT;
}

FromDump::~FromDump()
{
    MOD_DEC_USE_COUNT;
    delete _last_time_h;
    uninitialize();
}

void
FromDump::notify_noutputs(int n)
{
    set_noutputs(n <= 1 ? 1 : 2);
}

int
FromDump::configure(Vector<String> &conf, ErrorHandler *errh)
{
    bool timing = false, stop = false, active = true, force_ip = false;
#ifdef __linux__
    bool mmap = false;
#else
    bool mmap = true;
#endif
    struct timeval first_time, first_time_off, last_time, last_time_off, interval;
    timerclear(&first_time);
    timerclear(&first_time_off);
    timerclear(&last_time);
    timerclear(&last_time_off);
    timerclear(&interval);
    _sampling_prob = (1 << SAMPLING_SHIFT);
    
    if (cp_va_parse(conf, this, errh,
		    cpFilename, "dump file name", &_filename,
		    cpOptional,
		    cpBool, "use original packet timing?", &timing,
		    cpKeywords,
		    "TIMING", cpBool, "use original packet timing?", &timing,
		    "STOP", cpBool, "stop driver when done?", &stop,
		    "ACTIVE", cpBool, "start active?", &active,
		    "MMAP", cpBool, "access file with mmap()?", &mmap,
		    "SAMPLE", cpUnsignedReal2, "sampling probability", SAMPLING_SHIFT, &_sampling_prob,
		    "FORCE_IP", cpBool, "emit IP packets only?", &force_ip,
		    "START", cpTimeval, "starting time", &first_time,
		    "START_AFTER", cpTimeval, "starting time offset", &first_time_off,
		    "END", cpTimeval, "ending time", &last_time,
		    "END_AFTER", cpTimeval, "ending time offset", &last_time_off,
		    "INTERVAL", cpTimeval, "time interval", &interval,
		    "END_CALL", cpWriteHandlerCall, "write handler for ending time", &_last_time_h,
		    0) < 0)
	return -1;

    // check sampling rate
    if (_sampling_prob > (1 << SAMPLING_SHIFT)) {
	errh->warning("SAMPLE probability reduced to 1");
	_sampling_prob = (1 << SAMPLING_SHIFT);
    } else if (_sampling_prob == 0)
	errh->warning("SAMPLE probability is 0; emitting no packets");

    // check times
    _have_first_time = _have_last_time = true;
    _first_time_relative = _last_time_relative = _last_time_interval = false;
    
    if ((timerisset(&first_time) != 0) + (timerisset(&first_time_off) != 0) > 1)
	return errh->error("`START' and `START_AFTER' are mutually exclusive");
    else if (timerisset(&first_time))
	_first_time = first_time;
    else if (timerisset(&first_time_off))
	_first_time = first_time_off, _first_time_relative = true;
    else {
	timerclear(&_first_time);
	_have_first_time = false, _first_time_relative = true;
    }
    
    if ((timerisset(&last_time) != 0) + (timerisset(&last_time_off) != 0) + (timerisset(&interval) != 0) > 1)
	return errh->error("`END', `END_AFTER', and `INTERVAL' are mutually exclusive");
    else if (timerisset(&last_time))
	_last_time = last_time;
    else if (timerisset(&last_time_off))
	_last_time = last_time_off, _last_time_relative = true;
    else if (timerisset(&interval))
	_last_time = interval, _last_time_interval = true;
    else
	_have_last_time = false;

    if (_have_last_time && !_last_time_h)
	_last_time_h = new HandlerCall(id() + ".active false");
    
    // set other variables
    _have_any_times = false;
    _timing = timing;
    _stop = stop;
    _force_ip = force_ip;
#ifdef ALLOW_MMAP
    _mmap = mmap;
#else
    if (mmap)
	errh->warning("`MMAP' is not supported on this platform");
#endif
    _active = active;
    return 0;
}

static void
swap_file_header(const fake_pcap_file_header *hp, fake_pcap_file_header *outp)
{
    outp->magic = SWAPLONG(hp->magic);
    outp->version_major = SWAPSHORT(hp->version_major);
    outp->version_minor = SWAPSHORT(hp->version_minor);
    outp->thiszone = SWAPLONG(hp->thiszone);
    outp->sigfigs = SWAPLONG(hp->sigfigs);
    outp->snaplen = SWAPLONG(hp->snaplen);
    outp->linktype = SWAPLONG(hp->linktype);
}

static void
swap_packet_header(const fake_pcap_pkthdr *hp, fake_pcap_pkthdr *outp)
{
    outp->ts.tv_sec = SWAPLONG(hp->ts.tv_sec);
    outp->ts.tv_usec = SWAPLONG(hp->ts.tv_usec);
    outp->caplen = SWAPLONG(hp->caplen);
    outp->len = SWAPLONG(hp->len);
}

int
FromDump::error_helper(ErrorHandler *errh, const char *x)
{
    if (errh)
	errh->error("%s: %s", _filename.cc(), x);
    else
	click_chatter("%s: %s", declaration().cc(), x);
    return -1;
}

#ifdef ALLOW_MMAP
static void
munmap_destructor(unsigned char *data, size_t amount)
{
    if (munmap((caddr_t)data, amount) < 0)
	click_chatter("FromDump: munmap: %s", strerror(errno));
}

int
FromDump::read_buffer_mmap(ErrorHandler *errh)
{
    if (_mmap_unit == 0) {  
	size_t page_size = getpagesize();
	_mmap_unit = (WANT_MMAP_UNIT / page_size) * page_size;
	_mmap_off = 0;
	// don't report most errors on the first time through
	errh = ErrorHandler::silent_handler();
    }

    // get length of file
    struct stat statbuf;
    if (fstat(_fd, &statbuf) < 0)
	return error_helper(errh, String("stat: ") + strerror(errno));

    // check for end of file
    // But return -1 if we have not mmaped before: it might be a pipe, not
    // true EOF.
    if (_mmap_off >= statbuf.st_size)
	return (_mmap_off == 0 ? -1 : 0);

    // actually mmap
    _len = _mmap_unit;
    if ((off_t)(_mmap_off + _len) > statbuf.st_size)
	_len = statbuf.st_size - _mmap_off;
    
    void *mmap_data = mmap(0, _len, PROT_READ, MAP_SHARED, _fd, _mmap_off);

    if (mmap_data == MAP_FAILED)
	return error_helper(errh, String("mmap: ") + strerror(errno));

    _data_packet = Packet::make((unsigned char *)mmap_data, _len, munmap_destructor);
    _buffer = _data_packet->data();
    _file_offset = _mmap_off;
    _mmap_off += _len;

#ifdef HAVE_MADVISE
    // don't care about errors
    (void) madvise((caddr_t)mmap_data, _len, MADV_SEQUENTIAL);
#endif
    
    return 1;
}
#endif

int
FromDump::read_buffer(ErrorHandler *errh)
{
    if (_data_packet)
	_data_packet->kill();
    _data_packet = 0;

    _file_offset += _len;
    _pos -= _len;		// adjust _pos by _len: it might validly point
				// beyond _len
    _len = 0;

#ifdef ALLOW_MMAP
    if (_mmap) {
	int result = read_buffer_mmap(errh);
	if (result >= 0)
	    return result;
	// else, try a regular read
	_mmap = false;
	(void) lseek(_fd, _mmap_off, SEEK_SET);
	_len = 0;
    }
#endif
    
    _data_packet = Packet::make(0, 0, BUFFER_SIZE, 0);
    if (!_data_packet)
	return errh->error("out of memory!");
    _buffer = _data_packet->data();
    unsigned char *data = _data_packet->data();
    assert(_data_packet->headroom() == 0);
    
    while (_len < BUFFER_SIZE) {
	ssize_t got = read(_fd, data + _len, BUFFER_SIZE - _len);
	if (got > 0)
	    _len += got;
	else if (got == 0)	// premature end of file
	    return _len;
	else if (got < 0 && errno != EINTR && errno != EAGAIN)
	    return error_helper(errh, strerror(errno));
    }
    
    return _len;
}

int
FromDump::read_into(void *vdata, uint32_t dlen, ErrorHandler *errh)
{
    unsigned char *data = reinterpret_cast<unsigned char *>(vdata);
    uint32_t dpos = 0;

    while (dpos < dlen) {
	if (_pos < _len) {
	    uint32_t howmuch = dlen - dpos;
	    if (howmuch > _len - _pos)
		howmuch = _len - _pos;
	    memcpy(data + dpos, _buffer + _pos, howmuch);
	    dpos += howmuch;
	    _pos += howmuch;
	}
	if (dpos < dlen && read_buffer(errh) <= 0)
	    return dpos;
    }

    return dlen;
}

int
FromDump::initialize_failed(ErrorHandler *errh, const char *format, ...)
{
    va_list val;
    va_start(val, format);
    if (format && errh)
	errh->verror(ErrorHandler::ERR_ERROR, String(), format, val);
    va_end(val);
    uninitialize();
    return -EINVAL;
}

int
FromDump::initialize(ErrorHandler *errh)
{
    if (_filename == "-") {
	_fd = STDIN_FILENO;
	_filename = "<stdin>";
    } else
	_fd = open(_filename.cc(), O_RDONLY);
    if (_fd < 0)
	return errh->error("%s: %s", _filename.cc(), strerror(errno));

  retry_file:
#ifdef ALLOW_MMAP
    _mmap_unit = 0;
#endif
    _file_offset = 0;
    int result = read_buffer(errh);
    if (result < 0)
	return initialize_failed(errh, 0);
    else if (result == 0)
	return initialize_failed(errh, "%s: empty file", _filename.cc());
    else if (_len < sizeof(fake_pcap_file_header))
	return initialize_failed(errh, "%s: not a tcpdump file (too short)", _filename.cc());

    // check for a gziped or bzip2d dump
    if (_fd == STDIN_FILENO || _pipe)
	/* cannot handle gzip or bzip2 */;
    else if (_len >= 3
	     && ((_buffer[0] == 037 && _buffer[1] == 0213)
		 || (_buffer[0] == 'B' && _buffer[1] == 'Z' && _buffer[2] == 'h'))) {
	close(_fd);
	_fd = -1;
	String command = (_buffer[0] == '\037' ? "zcat " : "bzcat ") + _filename;
	_pipe = popen(command.cc(), "r");
	if (!_pipe)
	    return initialize_failed(errh, "%s while executing `%s'", strerror(errno), command.cc());
	_fd = fileno(_pipe);
	goto retry_file;
    }
    
    // check magic number
    fake_pcap_file_header swapped_fh;
    const fake_pcap_file_header *fh = (const fake_pcap_file_header *)_buffer;

    if (fh->magic == FAKE_PCAP_MAGIC || fh->magic == FAKE_MODIFIED_PCAP_MAGIC)
	_swapped = false;
    else {
	swap_file_header(fh, &swapped_fh);
	_swapped = true;
	fh = &swapped_fh;
    }
    if (fh->magic != FAKE_PCAP_MAGIC && fh->magic != FAKE_MODIFIED_PCAP_MAGIC)
	return initialize_failed(errh, "%s: not a tcpdump file (bad magic number)", _filename.cc());
    // compensate for extra crap appended to packet headers
    _extra_pkthdr_crap = (fh->magic == FAKE_PCAP_MAGIC ? 0 : sizeof(fake_modified_pcap_pkthdr) - sizeof(fake_pcap_pkthdr));

    if (fh->version_major != FAKE_PCAP_VERSION_MAJOR)
	return initialize_failed(errh, "%s: unknown major version %d", _filename.cc(), fh->version_major);
    _minor_version = fh->version_minor;
    _linktype = fh->linktype;

    // if forcing IP packets, check datalink type to ensure we understand it
    if (_force_ip) {
	if (!fake_pcap_dlt_force_ipable(_linktype))
	    return initialize_failed(errh, "%s: unknown linktype %d; can't force IP packets", _filename.cc(), _linktype);
	if (_timing)
	    return initialize_failed(errh, "FORCE_IP and TIMING options are incompatible");
    } else if (_linktype == FAKE_DLT_RAW)
	// force FORCE_IP.
	_force_ip = true;	// XXX _timing?

    // check handler call
    if (_last_time_h && _last_time_h->initialize_write(this, errh) < 0)
	return initialize_failed(errh, 0);
    
    // done
    _pos = sizeof(fake_pcap_file_header);
    if (output_is_push(0))
	ScheduleInfo::initialize_task(this, &_task, _active, errh);
    return 0;
}

void
FromDump::uninitialize()
{
    if (_pipe)
	pclose(_pipe);
    else if (_fd >= 0 && _fd != STDIN_FILENO)
	close(_fd);
    if (_packet)
	_packet->kill();
    if (_data_packet)
	_data_packet->kill();
    _fd = -1;
    _packet = _data_packet = 0;
    _pipe = 0;
}

void
FromDump::set_active(bool active)
{
    if (_active != active) {
	_active = active;
	if (active && output_is_push(0) && !_task.scheduled())
	    _task.reschedule();
    }
}

void
FromDump::prepare_times(const fake_bpf_timeval &btv)
{
    if (_first_time_relative)
	timeradd(&btv, &_first_time, &_first_time);
    if (_last_time_relative)
	timeradd(&btv, &_last_time, &_last_time);
    else if (_last_time_interval)
	timeradd(&_first_time, &_last_time, &_last_time);
    if (_timing) {
	struct timeval now;
	click_gettimeofday(&now);
	timersub(&now, &btv, &_time_offset);
    }
    _have_any_times = true;
}

bool
FromDump::read_packet(ErrorHandler *errh)
{
    fake_pcap_pkthdr swapped_ph;
    const fake_pcap_pkthdr *ph;
    int len, caplen;
    Packet *p;
    int tries = 0;
    assert(!_packet);

  retry:
    // quit if we have tried too many times
    tries++;
    if ((tries % 16) == 0 && output_is_push(0))
	return true;
    
    // we may need to read bits of the file
    if (_pos + sizeof(*ph) <= _len) {
	ph = reinterpret_cast<const fake_pcap_pkthdr *>(_buffer + _pos);
	_pos += sizeof(*ph);
    } else {
	ph = &swapped_ph;
	if (read_into(&swapped_ph, sizeof(*ph), errh) < (int)sizeof(*ph))
	    return false;
    }

    if (_swapped) {
	swap_packet_header(ph, &swapped_ph);
	ph = &swapped_ph;
    }

    // may need to swap 'caplen' and 'len' fields at or before version 2.3
    if (_minor_version > 3 || (_minor_version == 3 && ph->caplen <= ph->len)) {
	len = ph->len;
	caplen = ph->caplen;
    } else {
	len = ph->caplen;
	caplen = ph->len;
    }

    // check for errors
    if (caplen > len || caplen > 65535) {
	error_helper(errh, "bad packet header; giving up");
	return false;
    }

    // compensate for modified pcap versions
    _pos += _extra_pkthdr_crap;

    // check times
  check_times:
    if (!_have_any_times)
	prepare_times(ph->ts);
    if (_have_first_time) {
	if (timercmp(&ph->ts, &_first_time, <)) {
	    _pos += caplen;
	    goto retry;
	} else
	    _have_first_time = false;
    }
    if (_have_last_time && !timercmp(&ph->ts, &_last_time, <)) {
	_have_last_time = false;
	(void) _last_time_h->call_write(this, errh);
	// The handler might have scheduled us, in which case we might crash
	// at fast_reschedule()! Don't want that -- make sure we are
	// unscheduled.
	_task.fast_unschedule();
	if (!_active) {
	    _pos += caplen;
	    return false;
	}
	// retry _last_time in case someone changed it
	goto check_times;
    }
    
    // checking sampling probability
    if (_sampling_prob < (1 << SAMPLING_SHIFT)
	&& (uint32_t)(random() & ((1<<SAMPLING_SHIFT)-1)) >= _sampling_prob) {
	_pos += caplen;
	goto retry;
    }
    
    // create packet
    if (_pos + caplen <= _len) {
	p = _data_packet->clone();
	if (!p) {
	    error_helper(errh, "out of memory!");
	    return false;
	}
	p->change_headroom_and_length(_pos, caplen);
	p->set_timestamp_anno(ph->ts.tv_sec, ph->ts.tv_usec);
	SET_EXTRA_LENGTH_ANNO(p, len - caplen);
	_pos += caplen;
	
    } else {
	WritablePacket *wp = Packet::make(0, 0, caplen, 0);
	if (!wp) {
	    error_helper(errh, "out of memory!");
	    return false;
	}
	// set annotations now: may unmap earlier memory!
	wp->set_timestamp_anno(ph->ts.tv_sec, ph->ts.tv_usec);
	SET_EXTRA_LENGTH_ANNO(wp, len - caplen);
	
	if (read_into(wp->data(), caplen, errh) < caplen) {
	    // XXX error message too annoying
	    // error_helper(errh, "short packet");
	    wp->kill();
	    return false;
	}
	
	p = wp;
    }

    if (_force_ip && !fake_pcap_force_ip(p, _linktype)) {
	checked_output_push(1, p);
	goto retry;
    }

    _packet = p;
    return true;
}

void
FromDump::run_scheduled()
{
    if (!_active)
	return;

    bool more = true;
    if (!_packet)
	more = read_packet(0);
    if (_packet && _timing) {
	struct timeval now;
	click_gettimeofday(&now);
	timersub(&now, &_time_offset, &now);
	if (timercmp(&_packet->timestamp_anno(), &now, >)) {
	    _task.fast_reschedule();
	    return;
	}
    }

    if (more)
	_task.fast_reschedule();
    else if (_stop)
	router()->please_stop_driver();

    if (_packet) {
	output(0).push(_packet);
	_packet = 0;
    }
}

Packet *
FromDump::pull(int)
{
    if (!_active)
	return 0;

    bool more = true;
    if (!_packet)
	more = read_packet(0);
    if (_packet && _timing) {
	struct timeval now;
	click_gettimeofday(&now);
	timersub(&now, &_time_offset, &now);
	if (timercmp(&_packet->timestamp_anno(), &now, >))
	    return 0;
    }

    if (!more && _stop)
	router()->please_stop_driver();
    
    Packet *p = _packet;
    _packet = 0;
    return p;
}

enum {
    H_SAMPLING_PROB, H_ACTIVE, H_ENCAP, H_STOP,
    H_FILESIZE, H_FILEPOS, H_EXTEND_INTERVAL
};

String
FromDump::read_handler(Element *e, void *thunk)
{
    FromDump *fd = static_cast<FromDump *>(e);
    switch ((int)thunk) {
      case H_SAMPLING_PROB:
	return cp_unparse_real2(fd->_sampling_prob, SAMPLING_SHIFT) + "\n";
      case H_ACTIVE:
	return cp_unparse_bool(fd->_active) + "\n";
      case H_ENCAP:
	return String(fake_pcap_unparse_dlt(fd->_linktype)) + "\n";
      case H_FILESIZE: {
	  struct stat s;
	  if (fd->_fd >= 0 && fstat(fd->_fd, &s) >= 0 && S_ISREG(s.st_mode))
	      return String(s.st_size) + "\n";
	  else
	      return "-\n";
      }
      case H_FILEPOS:
	return String(fd->_file_offset + fd->_pos) + "\n";
      default:
	return "<error>\n";
    }
}

int
FromDump::write_handler(const String &s_in, Element *e, void *thunk, ErrorHandler *errh)
{
    FromDump *fd = static_cast<FromDump *>(e);
    String s = cp_uncomment(s_in);
    switch ((int)thunk) {
      case H_ACTIVE: {
	  bool active;
	  if (cp_bool(s, &active)) {
	      fd->set_active(active);
	      return 0;
	  } else
	      return errh->error("`active' should be Boolean");
      }
      case H_STOP:
	fd->set_active(false);
	fd->router()->please_stop_driver();
	return 0;
      case H_EXTEND_INTERVAL: {
	  struct timeval tv;
	  if (cp_timeval(s, &tv)) {
	      timeradd(&fd->_last_time, &tv, &fd->_last_time);
	      if (fd->_last_time_h)
		  fd->_have_last_time = true, fd->set_active(true);
	      return 0;
	  } else
	      return errh->error("`extend_interval' takes a time interval");
      }
      default:
	return -EINVAL;
    }
}

void
FromDump::add_handlers()
{
    add_read_handler("sampling_prob", read_handler, (void *)H_SAMPLING_PROB);
    add_read_handler("active", read_handler, (void *)H_ACTIVE);
    add_write_handler("active", write_handler, (void *)H_ACTIVE);
    add_read_handler("encap", read_handler, (void *)H_ENCAP);
    add_write_handler("stop", write_handler, (void *)H_STOP);
    add_read_handler("filesize", read_handler, (void *)H_FILESIZE);
    add_read_handler("filepos", read_handler, (void *)H_FILEPOS);
    add_write_handler("extend_interval", write_handler, (void *)H_EXTEND_INTERVAL);
    if (output_is_push(0))
	add_task_handlers(&_task);
}

ELEMENT_REQUIRES(userlevel FakePcap)
EXPORT_ELEMENT(FromDump)
