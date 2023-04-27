#pragma once
#include <string>
#include "ExecutionReport.h"
#include "PositionData.h"

namespace trader {
/*
 * trader's interface for WorkOrder
 */
struct WorkOrderParameters {
public:
    std::string _algo;
    std::string _symbol;
    std::string _woid;
    int64_t _qty;
    uint64_t _expire;
    double _stop_px;

    WorkOrderParameters();
    WorkOrderParameters(const char* wo_id, const char* wo_instruction);
}

struct WorkOrderStat {
public:
    double _enter_px;
    double _vap;
    uint64_t _passive_fills;
    uint64_t _aggressive_fills;

    WorkOrderStat();
}

struct WorkOrderState {
public:
    const WorkOrderParameters _param;
    WorkOrderStat _stat;

    int64_t _held_qty;
    double _enter_px;
    std::vector<pm::OpenOrder> _oo;

    WorkOrderState();
    WorkOrderState(const char* wo_str);
}

/*
 * trader's interface for Work Order
 */
class TraderForWorkOrder {
public:
    virtual int subL1(const std::string& tradable) = 0;
    virtual void unsubMD(int sub_id) = 0;
    virtual int addTimer(uint64_t trigger_micro, int type) = 0;
    virtual int sendStat() = 0;
    virtual int sendOrder() = 0;
}



class WorkOrder {
public:
    WorkOrder(TraderForWorkOrder& trader, const char* wo_id, const char* wo_instruction);
    virtual ~WorkOrder();

    // called by trader
    virtual void onTimer(uint64_t cur_micro, int timer_id);
    virtual void onOneSec(uint64_t cur_micro);
    virtual void onMDUpdate(int subid);
    virtual void onER(const pm::ExecutionReport& er);
    virtual void onControl(const std::string& control_str);
    virtual void toString() const;

    // const accessor
    int64_t heldQty() const;

protected:
    TraderForWorkOrder& _trader;
    WorkOrderState _state;
}

/*
 * trader's interface for Floor Manager
 */
class TraderForFloorManager {
public:
    virtual std::string newWrokOrder(const char* wo_id, const char* wo_instruction) = 0;
    virtual int64_t query() = 0;
    virtual void update(pm::ExecutionReport& er) = 0;
}

/* 
 * Create traders according to configuration
 */
class TraderBase: public TraderForWorkOrder, public TraderForFloorManager {
public:
    TraderBase(const std::string& name);
    virtual ~TraderBase();

    // for Work Order
    int subL1(const std::string& tradable) override;
    void unsubMD(int sub_id) override;
    int addTimer(uint64_t trigger_micro, int type) override;
    int sendStat() 
    int sendOrder() = 0;

    // for Floor Manager
    std::string newWrokOrder(const char* wo_id, const char* wo_instruction) = 0;
    int64_t query() = 0;
    void update(pm::ExecutionReport& er) = 0;
}
}
