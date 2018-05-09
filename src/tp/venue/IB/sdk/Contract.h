/* Copyright (C) 2018 Interactive Brokers LLC. All rights reserved. This code is subject to the terms
 * and conditions of the IB API Non-Commercial License or the IB API Commercial License, as applicable. */

#pragma once
#ifndef contract_def
#define contract_def

#include "TagValue.h"

/*
	SAME_POS    = open/close leg value is same as combo
	OPEN_POS    = open
	CLOSE_POS   = close
	UNKNOWN_POS = unknown
*/
enum LegOpenClose { SAME_POS, OPEN_POS, CLOSE_POS, UNKNOWN_POS };

struct ComboLeg
{
	ComboLeg()
		: conId(0)
		, ratio(0)
		, openClose(0)
		, shortSaleSlot(0)
		, exemptCode(-1)
	{
	}

	long		conId;
	long		ratio;
	std::string	action; //BUY/SELL/SSHORT

	std::string	exchange;
	long		openClose; // LegOpenClose enum values

	// for stock legs when doing short sale
	long		shortSaleSlot; // 1 = clearing broker, 2 = third party
	std::string	designatedLocation;
	int			exemptCode;

	bool operator==( const ComboLeg& other) const
	{
		return (conId == other.conId &&
			ratio == other.ratio &&
			openClose == other.openClose &&
			shortSaleSlot == other.shortSaleSlot &&
			exemptCode == other.exemptCode &&
			action == other.action &&
			exchange == other.exchange &&
			designatedLocation == other.designatedLocation);
	}
};

struct DeltaNeutralContract
{
	DeltaNeutralContract()
		: conId(0)
		, delta(0)
		, price(0)
	{}

	long	conId;
	double	delta;
	double	price;
};

typedef std::shared_ptr<ComboLeg> ComboLegSPtr;

struct Contract
{
	Contract()
		: conId(0)
		, strike(0)
		, includeExpired(false)
		, comboLegs(NULL)
		, deltaNeutralContract(NULL)
	{
	}

	long		conId;
	std::string	symbol;
	std::string	secType;
	std::string	lastTradeDateOrContractMonth;
	double		strike;
	std::string	right;
	std::string	multiplier;
	std::string	exchange;
	std::string	primaryExchange; // pick an actual (ie non-aggregate) exchange that the contract trades on.  DO NOT SET TO SMART.
	std::string	currency;
	std::string	localSymbol;
	std::string	tradingClass;
	bool		includeExpired;
	std::string	secIdType;		// CUSIP;SEDOL;ISIN;RIC
	std::string	secId;

	// COMBOS
	std::string comboLegsDescrip; // received in open order 14 and up for all combos

	// combo legs
	typedef std::vector<ComboLegSPtr> ComboLegList;
	typedef std::shared_ptr<ComboLegList> ComboLegListSPtr;

	ComboLegListSPtr comboLegs;

	// delta neutral contract
	DeltaNeutralContract* deltaNeutralContract;

public:

	// Helpers
	static void CloneComboLegs(ComboLegListSPtr& dst, const ComboLegListSPtr& src);
};

struct ContractDetails
{
	ContractDetails()
		: minTick(0)
		, priceMagnifier(0)
		, underConId(0)
		, evMultiplier(0)
		, callable(false)
		, putable(false)
		, coupon(0)
		, convertible(false)
		, nextOptionPartial(false)
	{
	}

	Contract	contract;
	std::string	marketName;
	double		minTick;
	std::string	orderTypes;
	std::string	validExchanges;
	long		priceMagnifier;
	int			underConId;
	std::string	longName;
	std::string	contractMonth;
	std::string	industry;
	std::string	category;
	std::string	subcategory;
	std::string	timeZoneId;
	std::string	tradingHours;
	std::string	liquidHours;
	std::string	evRule;
	double		evMultiplier;
	int			mdSizeMultiplier;
	int			aggGroup;
	std::string	underSymbol;
	std::string	underSecType;
	std::string marketRuleIds;
	std::string realExpirationDate;
	std::string lastTradeTime;

	TagValueListSPtr secIdList;

	// BOND values
	std::string	cusip;
	std::string	ratings;
	std::string	descAppend;
	std::string	bondType;
	std::string	couponType;
	bool		callable;
	bool		putable;
	double		coupon;
	bool		convertible;
	std::string	maturity;
	std::string	issueDate;
	std::string	nextOptionDate;
	std::string	nextOptionType;
	bool		nextOptionPartial;
	std::string	notes;
};

struct ContractDescription
{
	Contract contract;
	typedef std::vector<std::string> DerivativeSecTypesList;
	DerivativeSecTypesList derivativeSecTypes;
};

inline void
Contract::CloneComboLegs(ComboLegListSPtr& dst, const ComboLegListSPtr& src)
{
	if (!src.get())
		return;

	dst->reserve(src->size());

	ComboLegList::const_iterator iter = src->begin();
	const ComboLegList::const_iterator iterEnd = src->end();

	for (; iter != iterEnd; ++iter) {
		const ComboLeg* leg = iter->get();
		if (!leg)
			continue;
		dst->push_back(ComboLegSPtr(new ComboLeg(*leg)));
	}
}

static inline
std::string printContractMsg(const Contract& contract) {
	char buf[1024];
	size_t cnt=0;
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tConId: %ld\n", contract.conId);
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tSymbol: %s\n", contract.symbol.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tSecType: %s\n", contract.secType.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tLastTradeDateOrContractMonth: %s\n", contract.lastTradeDateOrContractMonth.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tStrike: %g\n", contract.strike);
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tRight: %s\n", contract.right.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tMultiplier: %s\n", contract.multiplier.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tExchange: %s\n", contract.exchange.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tPrimaryExchange: %s\n", contract.primaryExchange.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tCurrency: %s\n", contract.currency.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tLocalSymbol: %s\n", contract.localSymbol.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tTradingClass: %s\n", contract.tradingClass.c_str());
	return std::string(buf);
}

static inline
std::string printContractDetailsSecIdList(const TagValueListSPtr &secIdList) {
	char buf[4096];
	size_t cnt=0;
	const int secIdListCount = secIdList.get() ? secIdList->size() : 0;
	if (secIdListCount > 0) {
		cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tSecIdList: {");
		for (int i = 0; i < secIdListCount; ++i) {
			const TagValue* tagValue = ((*secIdList)[i]).get();
			cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"%s=%s;",tagValue->tag.c_str(), tagValue->value.c_str());
		}
		cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"}\n");
	}
	return std::string(buf);
}

static inline
std::string printContractDetailsMsg(const ContractDetails& contractDetails) {
	char buf[2048];
	size_t cnt=0;
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tMarketName: %s\n", contractDetails.marketName.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tMinTick: %g\n", contractDetails.minTick);
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tPriceMagnifier: %ld\n", contractDetails.priceMagnifier);
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tOrderTypes: %s\n", contractDetails.orderTypes.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tValidExchanges: %s\n", contractDetails.validExchanges.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tUnderConId: %d\n", contractDetails.underConId);
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tLongName: %s\n", contractDetails.longName.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tContractMonth: %s\n", contractDetails.contractMonth.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tIndystry: %s\n", contractDetails.industry.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tCategory: %s\n", contractDetails.category.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tSubCategory: %s\n", contractDetails.subcategory.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tTimeZoneId: %s\n", contractDetails.timeZoneId.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tTradingHours: %s\n", contractDetails.tradingHours.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tLiquidHours: %s\n", contractDetails.liquidHours.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tEvRule: %s\n", contractDetails.evRule.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tEvMultiplier: %g\n", contractDetails.evMultiplier);
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tMdSizeMultiplier: %d\n", contractDetails.mdSizeMultiplier);
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tAggGroup: %d\n", contractDetails.aggGroup);
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tUnderSymbol: %s\n", contractDetails.underSymbol.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tUnderSecType: %s\n", contractDetails.underSecType.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tMarketRuleIds: %s\n", contractDetails.marketRuleIds.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tRealExpirationDate: %s\n", contractDetails.realExpirationDate.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tLastTradeTime: %s\n", contractDetails.lastTradeTime.c_str());
	return std::string(buf) + " " + printContractDetailsSecIdList(contractDetails.secIdList);
}

static inline
std::string printBondContractDetailsMsg(const ContractDetails& contractDetails) {
	char buf[2048];
	size_t cnt=0;
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tSymbol: %s\n", contractDetails.contract.symbol.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tSecType: %s\n", contractDetails.contract.secType.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tCusip: %s\n", contractDetails.cusip.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tCoupon: %g\n", contractDetails.coupon);
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tMaturity: %s\n", contractDetails.maturity.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tIssueDate: %s\n", contractDetails.issueDate.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tRatings: %s\n", contractDetails.ratings.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tBondType: %s\n", contractDetails.bondType.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tCouponType: %s\n", contractDetails.couponType.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tConvertible: %s\n", contractDetails.convertible ? "yes" : "no");
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tCallable: %s\n", contractDetails.callable ? "yes" : "no");
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tPutable: %s\n", contractDetails.putable ? "yes" : "no");
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tDescAppend: %s\n", contractDetails.descAppend.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tExchange: %s\n", contractDetails.contract.exchange.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tCurrency: %s\n", contractDetails.contract.currency.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tMarketName: %s\n", contractDetails.marketName.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tTradingClass: %s\n", contractDetails.contract.tradingClass.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tConId: %ld\n", contractDetails.contract.conId);
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tMinTick: %g\n", contractDetails.minTick);
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tMdSizeMultiplier: %d\n", contractDetails.mdSizeMultiplier);
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tOrderTypes: %s\n", contractDetails.orderTypes.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tValidExchanges: %s\n", contractDetails.validExchanges.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tNextOptionDate: %s\n", contractDetails.nextOptionDate.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tNextOptionType: %s\n", contractDetails.nextOptionType.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tNextOptionPartial: %s\n", contractDetails.nextOptionPartial ? "yes" : "no");
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tNotes: %s\n", contractDetails.notes.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tLong Name: %s\n", contractDetails.longName.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tEvRule: %s\n", contractDetails.evRule.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tEvMultiplier: %g\n", contractDetails.evMultiplier);
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tAggGroup: %d\n", contractDetails.aggGroup);
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tMarketRuleIds: %s\n", contractDetails.marketRuleIds.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tTimeZoneId: %s\n", contractDetails.timeZoneId.c_str());
	cnt+=snprintf(buf+cnt,sizeof(buf)-cnt,"\tLastTradeTime: %s\n", contractDetails.lastTradeTime.c_str());
	return std::string(buf) + " " + printContractDetailsSecIdList(contractDetails.secIdList);
}


#endif
