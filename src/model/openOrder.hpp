/*
 * openOrder.hpp
 *
 *  Created on: May 22, 2018
 *      Author: zfu
 */

#pragma once

#include <string>
#include <vector>
#include <stdint.h>
#include <map>
#include <stdexcept>
#include <bookL2.hpp>
#include <plcc/PLCC.hpp>

// favorite open order is here!
namespace trader {
enum OPEN_ORDER_STATE {
	INIT = 0,
	NEW_PENDING,
	OPEN,
	REPLACE_PENDING,
	CANCEL_PENDING,

	// the above states for orders on market
	DONE,
	NUMBER_STATES
};

struct FillStat {
	// these three qty (sign significant) count
	// position for a collection of open orders (OOM)
	int64_t _held_qty;  // qty not on the market
	int64_t _open_qty;  // qty open on the market
	int64_t _pending_qty; // qty pending in transient, could be open shortly

	// Fill Quantlity
	uint64_t _fills;
	uint64_t _slippage_sz;
	double _slippage;

	uint64_t _avg_latency_micro[2]; // 0 pass, 1 agg
	uint64_t _offered_sz[2];
	uint64_t _filled_sz[2];

	FillStat* _next_update;
	std::string toString() const {
		char buf[1024*2];
		size_t bytes = snprintf(buf,sizeof(buf),
				"FillStat: held_qty(%lld),open_qty(%lld),"
				"pending_qty(%lld),next_update (%llu), %s",
				(long long) _help_qty, (long long) _open_qty,
				(long long) _pending_qty, (unsigned long long) _next_update,
				statString().c_str());
		return std::string(buf);
	}

	std::string statString() const {
		char buf[512];
		size_t bytes = snprintf(buf, sizeof(buf),
				"stat( total_fills:%lld offered_sz:%lld:%lld "
				"fill_ratio:^.2f:%.2f latency:%lld slippage %.7lf)",
				(long long) _fills,
				(long long) _offered_sz[0], (long long) _offered_sz[1],
				getPassiveFillRatio(), getAggressiveFillRatio(),
				(long long) _avg_latency_micro[0], (long long) _avg_latency_micro[1],
				getAvgSlippage());
		return std::string(buf);
	}

	int64_t getOpenQty() const {
		return _open_qty + _pending_qty;
	}

	double getAvgSlippage() const {
		return _slippage;
	};

	double getPassiveFillRatio() const {
		if (_offered_sz[0] == 0){
			return 1;
		}
		return (double) _filled_sz[0]/((double)_offered_sz[0]);
	}
	double getAggressiveFillRatio() const {
		if (_offered_sz[1] == 0) {
			return 1;
		}
		return (double) _filled_sz[1]/((double)_offered_sz[1]);
	}
	explicit FillStat(FillStat*next_fs=0){
		memset(this, 0, sizeof(FillStat));
		_next_update = next_fs;
	};

	void add_offered_sz(uint64_t sz, bool is_passive) {
		_offered_sz[is_passive?0:1] += sz;
		if(_next_update)
			_next_update->add_offered_sz(sz, is_passive);
	}

	void add_open_qty(int64_t qty) {
		_open_qty += qty;
		if (_next_update)
			_next_update->add_open_qty(qty);
	};

	void add_pending_qty(int64_t qty) {
		_pending_qty += qty;
		if(_next_update)
			_next_update->add_pending_qty(qty);
	}

	void add_filled_sz(int64_t sz, double px, int side_mul,
			double tgt_px, bool is_passive,
			uint64_t fill_lat_micro=0) {
		_open_qty -= (sz*side_mul);
		_fills += sz;
		double slp = (px - tgt_px) * side_mul;
		int idx = is_passive?0:1;
		if (fill_lat_micro)
			_avg_latency_micro[idx] = ((_avg_latency_micro[idx] * 7)>> 3)+(fill_lat_micro>>3);
		_filled_sz[is_passive?0:1]+=sz;
		if (_next_update)
			_next_update->add_filled_sz(sz,px,side_mul,tgt_px,is_passive,fill_lat_micro);
	}

	// add the qty back to held qty, no partial cancel here
	void add_canceled(int64_t qty, bool is_passive, uint64_t cancel_lat_micro=0) {
		_held_qty += qty;
		_open_qty = 0;
		_pending_qty = 0;
		if (cancel_lat_micro && (!is_passive))
			_avg_latency_micro[1] = ((_avg_latency_micro[1]*7)>>3) + (cancel_lat_micro>>3);
		if (_next_update)
			_next_update->add_canceled(qty, is_passive, cancel_lat_micro);
	}

	void addSlippage(double slippage, int64_t sz) {
		if (slippage < 0 && _slippage < 0) {
			// WHY???
			slippage /= 2;
			sz/=2;
		}
		_slippage = (_slippage*_slippage_sz) + slippage*sz;
		_slippage_sz += sz;
		_slippage = _slippage/_slippage_sz;
	}

	void resetSlippage() {
		if (_slippage < 0) {
			_slippage_sz = 0;
			_slippage = 0;
		} else {
			_slippage_sz /=2;
			_slippage /=2;
		}
	}

	FillStat& operator+=(const FillStat& fs) {
		_open_qty += fs._open_qty;
		_pending_qty += fs._pending_qty;
		_held_qty += fs._held_qty;
		_fills += fs._fills;
		addSlippage(fs._slippage, fs._slippage_sz);
		return *this;
	}
};

// Passive data structure to store per-order information
template<class OpenOrderManager>
struct OpenOrder {
	const std::string _model_name;
	const bool _is_buy;
	const double _tgt_px;
	const int _side_mul;
	const int _side_idx;
	const bool _is_ioc;
	uint64_t _enter_micro;

	OPEN_ORDER_STATE _state;
	int64_t _open_sz;
	int64_t _pending_sz;
	int64_t _fill_sz;

	uint64_t _open_micro;
	double _open_px;
	double _avg_fill_px;
	uint64_t _req_id;
	uint64_t _ord_id;
	uint64_t _q_pos;

	OpenOrderManager& _mgr;
	void *const _model;
	FillStat& _mgr_fill_stat;
	bool _is_passive;

	// Open order is created after a successful sending of a new
	// order Equivalent to onNewSent()
	OpenOrder(OpenOrderManager& mgr, const std::string& model_name,
			  void* model, double target_price,
			  bool is_buy, double px, int64_t sz, uint64_t reqid, uint64_t cur_micro, bool is_passive, bool is_ioc) :
				_model_name(model_name), _is_buy(is_buy),
				_tgt_px(target_price), _side_mul(is_buy?1:-1),
				_side_idx(is_buy?0:1),
				_is_ioc(is_ioc),
				_enter_micro(cur_micro),
				_state(NEW_PENDING),
				_open_sz(0),
				_pending_sz(sz),
				_fill_sz(0), _open_micro(0),
				_open_px(px), _avg_fill_px(0),
				_req_id(reqid), _ord_id(0), _q_pos(-1),
				_mgr(mgr),
				_model(model),
				_mgr_fill_stat(_mgr.getFillStatSide(_side_idx)),
				_is_passive(is_passive)
	{
		_mgr_fill_stat.add_pending_qty(sz*_side_mul);
	}
	~OpenOrder() {}

	void onReplaceQtySent(uint64_t reqid, int64_t new_sz, uint64_t cur_micro=0) {
		//_req_id = reqid; //update at new
		if (new_sz == 0) {
			_state = CANCEL_PENDING;
		} else {
			_state = REPLACE_PENDING;
		}
		// pay attention to replace on pending qty
		int64_t qty_delta = new_sz - (_open_sz + _pending_sz);
		if (qty_delta != 0) {
			_pending_sz += qty_delta;
			_mgr_fill_stat.add_pending_qty(qty_delta);
		}
		_req_id=reqid;
		if (cur_micro>0)
			_mgr.onOrderSent(cur_micro);
	}

	void onReplacePxSendt(uint64_t cur_micro = 0) {
		onReplaceQtySent(_req_id, 0, cur_micro);
	}

	// req_id is not really important, since it is used for callback at
	// trader and OOM level. ord_id is important, as it will be used
	// for can/rep. and nobody else has it.
	void onNew(uint64_t req_id, uint64_t ord_id, int64_t sz, double px, uint64_t cur_micro) {
		_req_id = req_id;
		_ord_id = ord_id;
		_open_micro = cur_micro;
		_open_px = px;
		switch (_state) {
		case NEW_PENDING : {
			// this also handles the case sending a new and replace,
			// now receiving new
			_open_sz = sz;
			_pending_sz -= sz;

			int64_t qty = sz* _side_mul;
			_mgr_fill_stat.add_pending_qty(-qty);
			_mgr_fill_stat.add_open_qty(qty);
			_mgr_fill_stat.add_offered_sz(sz,!_is_ioc);
			break;
		}
		case REPLACE_PENDING : {
			// handling consecutive replaces
			int64_t sz_delta = sz - _open_sz;
			_open_sz += sz_delta;
			_pending_sz -= sz_delta;

			int64_t qty_delta = sz_delta * _side_mul;
			_mgr_fill_stat.add_open_qty(qty_delta);
			_mgr_fill_stat.add_pending_qty(-qty_delta);

			// aggressive order using replace should also be counted
			// continuous aggressive orders are counted separately
			if (!_is_passive)
				_mgr_fill_stat.add_offered_sz(sz,false);
			break;
		}
		case CANCEL_PENDING : {
			// we received a very delayed NEW, log a warning and ignore
			// error!!!
			logInfo("open Order Warning: onNew ignored on CancelPending, ord_id(%llu), OpenOrder:%s",
					(unsigned long long) ord_id, toString().c_str());
			return;
		}
		default :
			// error!!!
			logError("openOrder Error: onNew called on state %s, ord_id(%llu), OpenOrder:%s",
					getStateString(_state), (unsigned long long) ord_id, toString().c_str());
			return;
		}
		_state = OPEN;
	}

	void onReject(uint64_t cur_micro) {
		// consider a can/rep chain, the assumption
		// is that if one in the middle is rejected,
		// the subsequent ones will be rejected as well
		if ((_state == OPEN) &&(_pending_sz == 0)) {
			onCancel(cur_micro);
			return;
		}
		int64_t sz = _pending_sz;
		_pending_sz = 0;
		//OPEN with size 0 is a valid state
		_mgr_fill_stat.add_pending_qty(-sz*_side_mul);
		if(_open_sz==0) {
			setDone();
			return;
		}
		_state = OPEN;
	}

	void onCancel(uint64_t cur_micro) {
		if (_is_ioc) {
			_mgr.remove_price(!_is_buy, _open_px);
		}
		if (_state == NEW_PENDING || _state == CANCEL_PENDING || _state == OPEN) {
			int64_t osz = _open_sz;
			_pending_sz = 0;
			_open_sz=0;
			// if there is any pending size when receiveing a cancel
			// the pencing size will not be able to make it
			// Use the open size to adjust the position
			uint64_t can_lat = (_state == OPEN)?cur_micro - _enter_micro:0;
			_mgr_fill_stat.add_canceled(osz*_side_mul, !_is_ioc, can_lat);
			setDone();
		}
		// the cancel in the middle of can/rep
		// shound't be processed here
	}

	// size is always positive
	void onFill(int64_t sz, double px, uint64_t cur_micro) {
		_open_sz -= sz;
		_avg_fill_px = (_avg_fill_px * _fill_sz + sz*px)/(_fill_sz + sz);
		_fill_sz += sz;
		_mgr_fill_stat.add_filled_sz(sz,px,_side_mul,_tgt_px,!_is_ioc,cur_micro-_enter_micro);
		if (_open_sz == 0) {
			// yay!
			setDone();
		}
	}

	std::string toString() const {

	}

	const char* getStateString(const OPEN_ORDER_STATE& state) const {

	}
};

class OpenOrder {


};

}
