#ifndef CLICK_TRANSMISSIONPOLICY_HH
#define CLICK_TRANSMISSIONPOLICY_HH
#include <click/config.h>
#include <click/element.hh>
#include <click/ipaddress.hh>
#include <click/etheraddress.hh>
#include <click/bighashmap.hh>
#include <click/straccum.hh>
#include <click/glue.hh>
CLICK_DECLS

/*
=c

TransmissionPolicy()

=s Wifi, Wireless Station, Wireless AccessPoint

Tracks bit-rate capabilities of other stations.

=d

Tracks a list of bitrates other stations are capable of.

=a BeaconScanner
 */

enum tx_mcast_type {
	TX_MCAST_LEGACY = 0x0,
	TX_MCAST_DMS = 0x1,
	TX_MCAST_UR = 0x2,
};

class TxPolicyInfo {
public:

	Vector<int> _mcs;
	bool _no_ack;
	tx_mcast_type _tx_mcast;
	int _ur_mcast_count;
	int _rts_cts;

	TxPolicyInfo() {
		_mcs = Vector<int>();
		_no_ack = false;
		_tx_mcast = TX_MCAST_DMS;
		_rts_cts = 2436;
		_ur_mcast_count = 3;
	}

	TxPolicyInfo(Vector<int> mcs, bool no_ack, tx_mcast_type tx_mcast,
			int ur_mcast_count, int rts_cts) {

		_mcs = mcs;
		_no_ack = no_ack;
		_tx_mcast = tx_mcast;
		_rts_cts = rts_cts;
		_ur_mcast_count = ur_mcast_count;
	}

	String unparse() {
		StringAccum sa;
		sa << "mcs [";
		for (int i = 0; i < _mcs.size(); i++) {
			sa << " " << _mcs[i] << " ";
		}
		sa << "]";
		sa << " no_ack " << _no_ack << " mcast ";
		if (_tx_mcast == TX_MCAST_LEGACY) {
			sa << "legacy";
		} else if (_tx_mcast == TX_MCAST_DMS) {
			sa << "dms";
		} else if (_tx_mcast == TX_MCAST_UR) {
			sa << "ur";
		} else {
			sa << "unknown";
		}
		sa << " ur_mcast_count " << _ur_mcast_count;
		sa << " rts_cts " << _rts_cts;
		return sa.take_string();
	}

};

class TransmissionPolicy : public Element { public:

  TransmissionPolicy() CLICK_COLD;
  ~TransmissionPolicy() CLICK_COLD;

  const char *class_name() const		{ return "TransmissionPolicy"; }
  const char *port_count() const		{ return PORTS_0_0; }

  int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
  TxPolicyInfo * tx_policy() { return &_tx_policy; }

private:

  TxPolicyInfo _tx_policy;

};

CLICK_ENDDECLS
#endif