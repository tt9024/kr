#pragma once
#include "CurrencyPair.h"
#include <stdexcept>

// ------------------------------------------------------------------------
// Requirements: Keep track of positions in an array of 2 (0=Long, 1=Short)
// ------------------------------------------------------------------------

namespace utils {
	enum FX_SIDE
	{
		FX_BUY = 0,
		FX_SELL = 1
	};

	/**
	 * Base class for getting reference exchange rate
	 */
	class FXReferenceRateBase
	{
	public:
		enum eDailyRate
		{
			LAST_BID, /* rates based on last trade */
			LAST_ASK,
			DAILY     /* rates based on daily rate */
		};

		// return a zero or negative rate in case of a failure
		virtual double getRefPrice(eDailyRate type, eCurrencyPair cp) const = 0;
		virtual void addRate(eCurrencyPair cp, FX_SIDE side, double rate) = 0;
		virtual ~FXReferenceRateBase() {};

		// helper functions
		double getDaily(const char* symbol) const
		{
			return getRefPrice(DAILY, CPMappings::instance().getCP(symbol));
		}
		double getLastBid(const char* symbol) const
		{
			return getRefPrice(LAST_BID, CPMappings::instance().getCP(symbol));
		}
		double getLastAsk(const char* symbol) const
		{
			return getRefPrice(LAST_ASK, CPMappings::instance().getCP(symbol));
		}
	};

	/**
	 * exchange rates were pushed by addRate()
	 */
	class FXReferenceLocal : public FXReferenceRateBase
	{
	public:
		FXReferenceLocal () {
			for (int i=0; i<TotalCP; ++i)
			{
				m_rates[i][0] = m_rates[i][1] = -1;
			}
		}
		// return a zero or negative rate in case of a failure
		virtual double getRefPrice(eDailyRate type, eCurrencyPair cp) const {
			switch (type) {
			case LAST_BID:
				return m_rates[(int)cp][0];
			case LAST_ASK:
				return m_rates[(int)cp][1];
			case DAILY:
				return (m_rates[(int)cp][1] + m_rates[(int)cp][0])/2.0;
			default:
				return -1.0;
			}
		}
		virtual ~FXReferenceLocal() {};
		virtual void addRate(eCurrencyPair cp, FX_SIDE side, double rate) {
			if (side == FX_BUY)
				m_rates[cp][0] = rate;
			else
				m_rates[cp][1] = rate;
		}
	private:
		double m_rates[TotalCP][2];
	};

	static inline FX_SIDE getTheOtherSide(FX_SIDE side) { return  (FX_SIDE)(1-(int)side); };

	struct TransactionRecord
	{
		const char* m_cpSymbol;
		FX_SIDE m_side;
		int m_size;
		double m_exchangeRate;

		TransactionRecord(const char* cpSymbol = "", FX_SIDE side = FX_BUY, int size = 0, double exchangeRate = 0.0)
			: m_cpSymbol(cpSymbol), m_side(side), m_size(size), m_exchangeRate(exchangeRate)
		{};
		std::string toString()
		{
			char strBuf[128];
			snprintf(strBuf, 128, "Symbol = %s, side = %s, size = %d, rate = %.5lf\n",
				m_cpSymbol, (m_side) == FX_BUY? "BUY":"SELL",
				m_size, m_exchangeRate);
			return std::string(strBuf);
		}
	};


	class FXGrid
	{
	public:
		explicit FXGrid(const FXReferenceRateBase& refRate, const double* localPositions = NULL);

		// transaction updates
		bool addTransaction(FX_SIDE side, const char* currencyPairSymbol, int size, double exchangeRate);
		bool addTransaction(const TransactionRecord* transactions, int numRecords);
		void resetPosition();

		// get current positions
		double getPositionLocal(eCurrency currency) const { return gridLocal[(int)currency]; };
		double getPositionUSD(eCurrency currency) const { return gridUSD[(int)currency]; };
		double getNetPositionLong() const { return netPosition[0]; };
		double getNetPositionShort() const { return netPosition[1]; };

	private:
		void updateUSDPosition();
		double getUSDSize(eCurrency ccy, double size);

		// data members
		double gridLocal[TotalCCY];
		double gridUSD[TotalCCY];
		double netPosition[2]; // 0-Long, 1-Short
		const FXReferenceRateBase& m_refRate;
	};

	FXGrid::FXGrid(const FXReferenceRateBase& refRate, const double* localPositions)
	: m_refRate(refRate)
	{
		resetPosition();
		if (localPositions)
		{
			for (int i=0; i<TotalCCY; ++i)
			{
				gridLocal[i] = localPositions[i];
			}
			updateUSDPosition();
		}
	}

	inline
	void FXGrid::resetPosition()
	{
		for (int i=0; i<TotalCCY; ++i)
		{
			gridLocal[i] = 0;
			gridUSD[i] = 0;
		}
		netPosition[0] = netPosition[1] = 0;
	}

	inline
	bool FXGrid::addTransaction(FX_SIDE side, const char* currencyPairSymbol, int size, double exchangeRate)
	{
		eCurrencyPair cp = CPMappings::instance().getCP(currencyPairSymbol);
		if (cp == TotalCP)
			return false;
		eCurrency ccyBase = g_cpInfo[cp].m_base;
		eCurrency ccyQuote = g_cpInfo[cp].m_quote;
		int baseSize = (side == FX_BUY)? size : -size;
		double quoteSize = baseSize * exchangeRate;
		gridLocal[ccyBase] += baseSize;
		gridLocal[ccyQuote] -= quoteSize;
		updateUSDPosition();
		return true;
	}

	inline
	bool FXGrid::addTransaction(const TransactionRecord* transactions, int numRecords)
	{
		for (int i=0; i<numRecords; ++i)
		{
			if (!addTransaction(transactions[i].m_side,
								transactions[i].m_cpSymbol,
								transactions[i].m_size,
								transactions[i].m_exchangeRate))
				return false;
		};
		return true;
	}

	inline
	void FXGrid::updateUSDPosition()
	{
		netPosition[0] = 0;
		netPosition[1] = 0;
		for (int i=0; i<TotalCCY; ++i)
		{
			if (gridLocal[i] == 0)
			{
				gridUSD[i] = 0;
				continue;
			}
			double usdSize = getUSDSize((eCurrency) i, gridLocal[i]);
			if (usdSize)
			gridUSD[i] = usdSize;
			if (usdSize>0)
				netPosition[0] += usdSize;
			else
				netPosition[1] += usdSize;
		}
	}

	double FXGrid::getUSDSize(eCurrency ccy, double size)
	{
		if (ccy == USD)
			return size;

		double rate = 0;
		eCurrencyPair cp = g_ccyInfo[ccy].m_majorPair;
		bool isBase = g_ccyInfo[ccy].m_isBase;
		FX_SIDE side = (size * (isBase?1:-1) > 0)? FX_SELL:FX_BUY;

		// try to get the rate for that side
		FXReferenceRateBase::eDailyRate rateType =
			(side == FX_BUY)? FXReferenceRateBase::LAST_ASK:FXReferenceRateBase::LAST_BID;
		rate = m_refRate.getRefPrice(rateType, cp);
		if (rate <= 0)
		{
			// we assume daily rate is always available and
			// is the mid of ASK and BID
			rate = m_refRate.getRefPrice(FXReferenceRateBase::DAILY, cp);
		}
		if (rate <= 0)
		{
			// TODO: REVIEW - what if we don't have a rate?
			char strBuf[64];
			snprintf(strBuf, 64, "FXGrid Error: cannot get reference rate for %s", g_cpInfo[cp].m_symbol);
			std::cout << strBuf << std::endl;
			throw std::runtime_error(strBuf);
		}
		double newSize;
		eCurrency newCCY;
		if (isBase)
		{
			newSize = size*rate;
			newCCY = g_cpInfo[cp].m_quote;
		} else
		{
			newSize = (size/rate);
			newCCY = g_cpInfo[cp].m_base;
		}

		// check if the major pair leads to USD
		if (newCCY != USD)
			return getUSDSize(newCCY, newSize);
		return newSize;
	}

}
