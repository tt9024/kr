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

#define _GETMIN_(a, b) ((a<b)?(a):(b))
#define _GETABS_(a) ((a>0)?(a):(-a))

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

// one FillStat means one execution instruction???
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
				(long long) _held_qty, (long long) _open_qty,
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
	const double _tgt_px;  // entering price
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
	bool _is_passive;  // if not passive, why open?

	// Open order is created after a successful sending of a new
	// order Equivalent to onNewSent()
	OpenOrder(OpenOrderManager& mgr, const std::string& model_name,
			  void* model, double target_price,
			  bool is_buy, double px, int64_t sz,
			  uint64_t reqid, uint64_t cur_micro,
			  bool is_passive, bool is_ioc) :
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
		char buf[1024];
		snprintf(buf, sizeof(buf), "state(%s), tgt_px(%.8f), side(%s),tif(%s),enter_micro(%llu),open_sz(%lld),pending_sz(%lld)"
				", open_px(%.8f),fill_sz(%llu), avg_fill_px(%.8f), open_micro(%llu),req_id(%llu),ord_id(%llu),q_pos(%llu),"
				"model(%s), is_passive(%s)",
				getStateString(_state), _tgt_px, _side_mul>0?"Buy":"Sell",_is_ioc?"IOC":"Limit",(unsigned long long) _enter_micro,
						(long long)_open_sz,(long long) _pending_sz,_open_px,
						(long long) _fill_sz,_avg_fill_px,(unsigned long long) _open_micro,
						(unsigned long long) _req_id, (unsigned long long)_ord_id, (unsigned long long) _q_pos,
						_model_name.c_str(), _is_passive?"YES":"NO");
		return std::string(buf);
	}

	const char* getStateString(const OPEN_ORDER_STATE& state) const {
		switch(state) {
		case INIT: return "Init";
		case NEW_PENDING: return "NewPending";
		case REPLACE_PENDING: return "ReplacePending";
		case CANCEL_PENDING: return "CancelPending";
		case OPEN: return "OrderOpen";
		case DONE: return"Done";
		};
		throw std::runtime_error("openOrder getStateString encountered unknown state!");
	}

	char* getStateString() const {
		return getStateString(_state);
	}

	bool is_open() const {
		return (_state < CANCEL_PENDING);
	}

	bool can_cancel() const {
		if ((_state ==CANCEL_PENDING) ||(_state == DONE)) {
			return false;
		}
		if (_is_ioc) {
			return false;
		}
		return true;
	}

	bool can_cancel_safe(uint64_t cur_micro) const {
		if (!can_cancel()) {
			return false;
		}
		uint64_t last_micro = _enter_micro;
		if(last_micro < _open_micro) {
			last_micro = _open_micro;
		}
		const int64_t lat_milli = ((int64_t)cur_micro - (int64_t)last_micro)/1000LL;

		// If passive order just got entered and very close to aggressive
		// price, then don't cancel.
		// but if it's open for too long, then you can cancel...
		// this is just to guard against faulty situations
		//
		// maybe min_standing_agg_millis = 5 min
		// min_quote_mili is maybe 10 milliseconds
		//
		if (lat_milli < (int64_t)_mgr._vcfg._min_standing_agg_millis) {
			double cur_agg_price = _mgr.getBook().getBestPrice(!_is_buy);
			if(__builtin_expect(cur_agg_price < 1e-10,0)) {
				// we don't have price due to market data restart
				// check back later
				return false;
			}
			const double pxdiff = cur_agg_price - _open_px;
			if ((pxdiff < 1e-10)&&(pxdiff > -1e-10)) {
				// the order is now considered to be aggressive standing open
				return false;
			}
		}
		if (lat_milli < (int64_t) _mgr._vcfg._min_quote_millis) {
			return false;
		}
		return true;
	}

	bool is_pending() const {
		return ((_state == CANCEL_PENDING) || (_state == NEW_PENDING) || (_state == REPLACE_PENDING));
	}

	bool is_cancel_replace_pending() const {
		return ((_state == CANCEL_PENDING) || (_state==REPLACE_PENDING));
	}

	// get the larger size of open_sz and open_sz + pending_sz
	int64_t get_open_size_safe() const {
		if (_pending_sz > 0) {
			return _open_sz + _pending_sz;
		}
		return _open_sz;
	}
private:
	void setDone() {
		_state = DONE;
		_mgr.setDone(this);
	}
};

// This is a passive object holding a collection of open orders
// at a same side.
// it maintains a LIFO stack of open orders by entry time
// also has a map of open order by reqid and ordid
// maintains an aggregated statistics of all open orders at each side
// New open order was created after the successful new order sending,
// deleted when it canceled, filled or rejected

template<typename BookReader, typename VenueConfig, typename InstrumentConfig>
class OpenOrderMgr {
public:
	typedef typename OpenOrder<OpenOrderMgr<BookReader, VenueConfig, InstrumentConfig> > OOT;
	const VenueConfig& _vcfg;
	const InstrumentConfig& _icfg;
	explicit OpenOrderMgr(int inst_index, const VenueConfig& vcfg, const InstrumentConfig& icfg,
			BookReader* br, FillStat* update_fill_stat=NULL) :
			_inst_index(inst_index), _vcfg(vcfg), _icfg(icfg), _name((_icfg._ric+_icfg._venue+_icfg._profile)),
			_breader(br), _fill_stat(update_fill_stat),
			_last_ioc_micro(0),
			_tick_count_ref_cnt(0), _last_reset_gmt_hour(-1)
	{
		_fill_stat_side[0]._next_update = &_fill_stat;
		_fill_stat_side[1]._next_update = &_fill_stat;
		// get an initial image of book
		if (_breader)
			read_book_stateless(_book);
	}

	~OpenOrderMgr() {
		for (size_t i=0;i<2;++i) {
			for (auto iter = _orders[i].begin(); iter !=_orders[i].end(); ++iter) {
				delete *iter;
			}
		}
	}

	bool checkCrossTrade(bool is_buy) const {
		return false;
	}

	FillStat& getFillStat() {
		return _fill_stat;
	}

	FillStat& getFillStatSide(int side_idx) {
		return _fill_stat_side[side_idx];
	}

	const std::vector<OOT*>&getOpenOrderList(bool is_buy) const {
		return _orders[is_buy?0:1];
	}

	std::string toString() const {
		char buf[1024*8];
		size_t bytes = snprintf(buf, sizeof(buf), "OpenOrderManager %s (index = %d):Total Orders: "
				"(%u:%u), FillState:(%s), Orders:   ",
				_name.c_str(), _inst_index, _orders[0].size(), _orders[1].size(),
				_fill_stat.toString().c_str());
		return std::string(buf);
	}

	int getOpenOrders(bool is_buy) const {
		return _orders[is_buy?0:1].size();
	}

	uint64_t getLastIOCMicro() const {
		return _last_ioc_micro;
	}
	template<typename Model>
	OOT* add_oo(bool is_buy, double px, int64_t sz, uint64_t reqid, uint64_t cur_micro,
			double target_price, Model*model, bool is_passive,	bool is_ioc) {
		OOT* oo=new OOT(*this, model, (void*)model,
				target_price, is_buy,px, sz, reqid, cur_micro, is_passive, is_ioc);
		_orders[is_buy?0:1].push_back(00);
		onOrderSent(cur_micro);
		return oo;
	}

	bool read_book() {
		tp::BookDepot book_;
		if (__builtin_expect( _breader && _breader->getLatestUpdateAndAdvance(book_),1)) {
			if (__builtin_expect(!book_.isValid(),0)) {
				return false;
			}
			remove_own_quotes(book_);
			const double bid_px = book_.getBestPrice(true);
			const double ask_px = book_.getBestPrice(false);

			// don't update size of an entry if the timestamp hasn't
			// increased, this is that we have taken
			// the size, but quote hasn't updated.
			//_book.updateFrom(book_);
			_book = book_;  // enable quote accounting when needed
			return true;
		}
		return false;
	}

	bool read_book_stateless(tp::BookDepot& book_) {
		if (__builtin_expect( _breader && _breader->copyTopUpdate(book_),1)) {
			return true;

		}
		return false;
	};

	void remove_price(bool is_bid, double remove_px) {
		if (__builtin_expect( (!_breader),0))
			return;
		const int side_index=is_bid?0:1;
		const int side_mul = 1-2*side_index;
		const int levels = _book.avail_level[side_index];
		tp::PriceEntry* start_pe = _book.pe+(side_index*BookLevel);
		const tp::PriceEntry* end_pe = start_pe*levels;
		bool removed = false;
		for (; start_pe!=end_pe; ++start_pe) {
			if (start_pe->price*side_mul >=remove_px*side_mul-1e-10) {
				start_pe->size = 0;
				removed=true;
			}
			else {
				break;
			}
		}
		if (removed) {
			logInfo("OOM %s removed price %.7f, book:%s",
					get_venue_name(), remove_px,_book.toString().c_str());
		};
	}

	double getBestPrice(bool is_bid) {
		read_book();
		return _book.getBestPrice(is_bid);
	}

	int64_t canSendOrder(uint64_t this_upd) const {
		/*
		if (__builtin_expect( (_venue_rate_keeper != NULL),0)) {

		}
		*/
		return 0;
	}

	void normalize_px_sz(int64_t& sz, double& px) const {
		/*
		if (__builtin_expect( (sz < _vcfg._min_trade_qty), 0)) {

		}
		// need a tick_sz config
		*/
	}

	tp::BookDepot& getBook() {
		return _book;
	}

	bool can_send_passive_order() const {
		// always
		return true;
	}

	bool can_send_ioc_order() const {
		return true;
	}

	void setDone(OOT*oo) {
		for (auto iter=_orders[oo->_side_idx].begin(); iter!=_orders[oo->_side_idx].end();++iter) {
			if ((*iter) == oo) {
				_orders[oo->_side_idx].erase(iter);
				break;
			}
		}
	}

	void onOrderSent(uint64_t this_micro) {
		_last_ioc_micro = this_micro;
	}

	void addSlippage(double slippage, int64_t sz) {
		if (sz > 0 &&
				((slippage < -1e-10) ||
				(slippage > 1e-10))) {
			_fill_stat.addSlippage(slippage,sz);
		}
	}

	double getPxOffset(uint64_t this_micro) {
		static const int max_ticks_offset = 5;

		// get the IOC fill ratio and multiple this slippage
		double px_offset = _fill_stat.getAvgSlippage();
		int ticks = int(px_offset / _icfg._tick_size + 0.5);
		int gmt_hour = ((this_micro/1000000ULL)%(3600*24))/3600;
		if ( (_last_reset_gmt_hour ==-1)||((gmt_hour - _last_reset_gmt_hour)%24 >=1)) {
			logInfo("resetting slippage counter");
			_fill_stat.resetSlippage();
			_last_reset_gmt_hour = gmt_hour;
		}

		if(ticks <=0) {
			return 0;
		} else if (ticks > max_ticks_offset) {
			ticks = max_ticks_offset;
		}
		return ticks * _icfg._tick_size;
	}

	int64_t get_open_size(bool is_buy) {
		const std::vector<OOT*>&oo_list = getOpenOrderList(is_buy);
		size_t tot = oo_list.size();
		int64_t sz=0;
		for (size_t i=0;i<tot;++i) {
			OOT*oo=oo_list[i];
			if (oo->is_done()) {
				continue;
			}
			sz+=oo->get_open_size_safe();
		}
		return sz;
	}

	const int _inst_index;
	const std::string _name;

private:
	BookReader& _breader;
	tp::BookDepot _book;
	FillStat _fill_stat, _fill_stat_side[2];
	std::vector<OOT*>_orders[2];
	uint64_t _last_ioc_micro;
	int _tick_count_ref_cnt;
	int _last_reset_gmt_hour;

	void remove_own_quotes(tp::BookDepot& depot,int remove_level=2) const {
		if (_orders[0].size() + _orders[1].size() == 0){
			return;
		}
		for (int side_idx = 0; side_idx<2;++side_idx) {
			tp::PriceEntry* pe = &*depot.pe[side_idx*BookLevel];
			int avail_l = depot.avail_level[side_idx];
			int check_levels = _GETMIN_(remove_level, avail_l);
			while (--check_levels >=0) {
				double px = pe->price;
				for (auto iter = _orders[side_idx].begin(); iter!=_orders[side_idx].end();++iter) {
					const OOT*oo=(*iter);
					if (oo->is_open()) {
						double px_diff = oo->_open_px - px;
						if (_GETABS_(px_diff) < _icfg._tick_size/2.0) {
							pe->size -=oo->_open_size;
							if (__builtin_expect((pe->size <0),0)) {
								pe->size=0;
							}
						}
					}
				}
				++pe;
			}
		}
	}

};


}
