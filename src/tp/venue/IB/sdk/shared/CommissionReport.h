/* Copyright (C) 2013 Interactive Brokers LLC. All rights reserved. This code is subject to the terms
 * and conditions of the IB API Non-Commercial License or the IB API Commercial License, as applicable. */

#ifndef commissionreport_def
#define commissionreport_def

#include "IBString.h"

struct CommissionReport
{
	CommissionReport()
	{
		commission = 0;
		realizedPNL = 0;
		yield = 0;
		yieldRedemptionDate = 0;
	}

	// commission report fields
	IBString	execId;
	double		commission;
	IBString	currency;
	double		realizedPNL;
	double		yield;
	int			yieldRedemptionDate; // YYYYMMDD format
	std::string ToString() const {
		std::ostringstream oss;
		oss << "CommissionReport{" << PrintVar(execId) << PrintVar(commission)
			<< PrintVar(currency) << PrintVar(realizedPNL) << PrintVar(yield)
			<< PrintVar(yieldRedemptionDate) << "}";
		return oss.str();
	}
};

#endif // commissionreport_def
