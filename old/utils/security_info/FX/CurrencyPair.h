#pragma once
#include <string.h>
#include <stdexcept>

namespace fx {
	enum eCurrency
	{
		AUD,
		CAD,
		CHF,
		EUR,
		GBP,
		JPY,
		NZD,
		USD,
		TotalCCY
	};

	enum eCurrencyPair
	{
		AUDJPY,
		AUDNZD,
		AUDUSD,
		CADJPY,
		CHFJPY,
		EURAUD,
		EURCAD,
		EURCHF,
		EURGBP,
		EURJPY,
		EURNZD,
		EURUSD,
		GBPCHF,
		GBPJPY,
		GBPNZD,
		GBPUSD,
		NZDUSD,
		NZDJPY,
		USDCAD,
		USDCHF,
		USDJPY,
		TotalCP
	};

	struct CPInfo {
		eCurrencyPair m_cp;
		const char* m_symbol;
		eCurrency m_base, m_quote;
	};

	struct CCYInfo {
		eCurrency m_currency;
		const char* m_symbol;
		// the pair linking (indirectly maybe) to USD
		eCurrencyPair m_majorPair;
		bool m_isBase;
	};

	static const CPInfo g_cpInfo[] = {
		{ AUDJPY, "AUD/JPY", AUD, JPY },
		{ AUDNZD, "AUD/NZD", AUD, NZD },
		{ AUDUSD, "AUD/USD", AUD, USD },
		{ CADJPY, "CAD/JPY", CAD, JPY },
		{ CHFJPY, "CHF/JPY", CHF, JPY },
		{ EURAUD, "EUR/AUD", EUR, AUD },
		{ EURCAD, "EUR/CAD", EUR, CAD },
		{ EURCHF, "EUR/CHF", EUR, CHF },
		{ EURGBP, "EUR/GBP", EUR, GBP },
		{ EURJPY, "EUR/JPY", EUR, JPY },
		{ EURNZD, "EUR/NZD", EUR, NZD },
		{ EURUSD, "EUR/USD", EUR, USD },
		{ GBPCHF, "GBP/CHF", GBP, CHF },
		{ GBPJPY, "GBP/JPY", GBP, JPY },
		{ GBPNZD, "GBP/NZD", GBP, NZD },
		{ GBPUSD, "GBP/USD", GBP, USD },
		{ NZDUSD, "NZD/USD", NZD, USD },
		{ NZDJPY, "NZD/JPY", NZD, JPY },
		{ USDCAD, "USD/CAD", USD, CAD },
		{ USDCHF, "USD/CHF", USD, CHF },
		{ USDJPY, "USD/JPY", USD, JPY },
	};

	static const CCYInfo g_ccyInfo[] = {
		{AUD,"AUD",AUDUSD,true},
		{CAD,"CAD",USDCAD,false},
		{CHF,"CHF",USDCHF,false},
		{EUR,"EUR",EURUSD,true},
		{GBP,"GBP",GBPUSD,true},
		{JPY,"JPY",USDJPY,false},
		{NZD,"NZD",NZDUSD,true},
		{USD,"USD",EURUSD,false},
	};

	class CPMappings {
	public:
		static CPMappings& instance() {
			static CPMappings mappings;
			return mappings;
		};
		eCurrency getCCY(const char* symbol) const
		{
			int i=0;
			for (; i<TotalCCY; ++i)
			{
				if (strcmp(symbol, g_ccyInfo[i].m_symbol) == 0)
					break;
			}
			return (eCurrency) i;
		}
		eCurrencyPair getCP(const char* symbol) const
		{
			eCurrencyPair cp = m_cpMap[getSymbolHash(symbol)];
			if (__builtin_expect((cp != TotalCP), 1))
			{
				if (__builtin_expect((memcmp(symbol, g_cpInfo[(int)cp].m_symbol, 7) == 0), 1)) {
					return cp;
				}
			}
			return TotalCP;
		}

		const char* getCCYSymbol(const eCurrency ccy) const
		{
			return g_ccyInfo[(int) ccy].m_symbol;
		}
		const char* getCPSymbol(const eCurrencyPair cp) const
		{
			return g_cpInfo[(int)cp].m_symbol;
		}
	private:
		const static int HashSize = 512;
		eCurrencyPair m_cpMap[HashSize];
		int getSymbolHash(const char* symbol) const
		{
			// symbol is assumed in "EUR/USD" format
			int hash = symbol[0] + 2*symbol[2] + 8*symbol[4] + 9*symbol[6] - 1300;
			if (__builtin_expect((hash >= HashSize),0)) {
				return HashSize-1;
			}
			return hash;
		}
		CPMappings() {
			for (int i=0; i<HashSize; ++i)
			{
				m_cpMap[i] = TotalCP;
			}
			for (int i=0; i<TotalCP; ++i)
			{
				int hash = getSymbolHash(g_cpInfo[i].m_symbol);
				if (m_cpMap[hash] != TotalCP)
					throw std::runtime_error("Currency Pair Symbol hash collision! ");

				m_cpMap[hash] = (eCurrencyPair)i;
			}
		}
		CPMappings(const CPMappings& mapping);
		CPMappings& operator = (const CPMappings& mapping);
	};
}
