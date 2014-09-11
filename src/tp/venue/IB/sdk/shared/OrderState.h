/* Copyright (C) 2013 Interactive Brokers LLC. All rights reserved. This code is subject to the terms
 * and conditions of the IB API Non-Commercial License or the IB API Commercial License, as applicable. */

#ifndef ORDER_STATE_H__INCLUDED
#define ORDER_STATE_H__INCLUDED

#include "Order.h"

struct OrderState {

	explicit OrderState()
		:
		commission(UNSET_DOUBLE),
		minCommission(UNSET_DOUBLE),
		maxCommission(UNSET_DOUBLE)
	{}

	IBString status;

	IBString initMargin;
	IBString maintMargin;
	IBString equityWithLoan;

	double  commission;
	double  minCommission;
	double  maxCommission;
	IBString commissionCurrency;

	IBString warningText;
	std::string ToString() const {
		std::ostringstream oss;
		oss << "OrderState{" << PrintVar(status) << PrintVar(initMargin)
				<< PrintVar(maintMargin) << PrintVar(commission) << PrintVar(minCommission)
				<< PrintVar(maxCommission) << PrintVar(commissionCurrency)<< PrintVar(warningText)
				<< "}";
		return oss.str();
	}
};

#endif
