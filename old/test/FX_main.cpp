#include <Vulcan/Common/FX/FXGrid.h>
#include <gtest/gtest.h>

using namespace fx;
#define DoubleEq(a, b) (((a-b) < 0.0001) && ((a-b) > -0.0001))

class MockDailyRefRate : public FXReferenceRateBase
{
public:
	// return a zero or negative rate in case of a failure
	virtual double getRefPrice(__UNUSED__ eDailyRate type, eCurrencyPair cp) const
	{
		static const double rates [] = {
			 93.0,
			 1.1,
			 0.92,
			 96.0,
			 110.0,
			 1.5,
			 1.4,
			 1.2,
			 0.83,
			 136.0,
			 1.6,
			 1.35,
			 1.5,
			 163,
			 2.0,
			 1.6,
			 0.8,
			 83.0,
			 1.0,
			 0.9,
			 100};
		if (((int)cp >= TotalCP) || ((int)cp < 0))
			return -1.0;
		return rates[(int)cp];
	}
	void addRate(__UNUSED__ eCurrencyPair cp, __UNUSED__ FX_SIDE side, __UNUSED__ double rate) {};
};


class MockTwoSidedRefRate : public MockDailyRefRate
{
public:
	virtual double getDailyRateRefPrice(eDailyRate type, eCurrencyPair cp) const
	{
		// simple mock two sided price, ask is two times higher than bid
		double rate = MockDailyRefRate::getRefPrice(type, cp);
		if (rate <= 0) return rate;
		switch (type)
		{
		case DAILY:
			return rate;
		case LAST_BID:
			return rate;
		case LAST_ASK:
			return rate*2;
		}
		return -1.0;
	}
};

TEST(FX, InstrumentTest)
{
	// check all the CPs defined are in the instrument map
	for (int i = 0; i<(int)TotalCP; ++i)
	{
		const char* symbol = g_cpInfo[i].m_symbol;
		EXPECT_EQ(i, (int) CPMappings::instance().getCP(symbol));
	}
}

TEST(FX, FXGridDailyRate)
{
	MockDailyRefRate rate;
	FXGrid grid(rate);
	// check initial positions
	EXPECT_EQ(0.0, grid.getNetPositionLong());
	EXPECT_EQ(0.0, grid.getNetPositionShort());

	const char* symbol = "EUR/USD";
	double dailyRate = rate.getDaily(symbol);

	// after buying 1M EUR/USD, at "rate", we should be
	// have equal position of long=short=1M*rate;
	grid.addTransaction(FX_BUY, symbol, 1000000, dailyRate);
	EXPECT_TRUE(DoubleEq(grid.getPositionLocal(EUR), 1000000));
	EXPECT_TRUE(DoubleEq(grid.getPositionLocal(USD), -1000000*dailyRate));
	EXPECT_TRUE(DoubleEq(grid.getPositionUSD(EUR), 1000000*dailyRate));
	EXPECT_TRUE(DoubleEq(grid.getPositionUSD(USD), -1000000*dailyRate));
	EXPECT_TRUE(DoubleEq(grid.getNetPositionShort(), -1000000*dailyRate));
	EXPECT_TRUE(DoubleEq(grid.getNetPositionLong(), 1000000*dailyRate));

	// enter an offset trade, check position cleared
	grid.addTransaction(FX_SELL, symbol, 1000000, dailyRate);
	EXPECT_TRUE(DoubleEq(grid.getNetPositionShort(), 0));
	EXPECT_TRUE(DoubleEq(grid.getNetPositionLong(), 0));

	// triangle trade, check net position should be cleared
	grid.resetPosition();
	grid.addTransaction(FX_BUY, "GBP/CHF", 1000000, rate.getDaily("GBP/CHF"));
	grid.addTransaction(FX_SELL, "EUR/CHF", 1000000, rate.getDaily("EUR/CHF"));
	grid.addTransaction(FX_BUY, "EUR/GBP", 1000000, rate.getDaily("EUR/GBP"));
	EXPECT_TRUE(DoubleEq(grid.getNetPositionShort(), -333333.33333333331));
	EXPECT_TRUE(DoubleEq(grid.getNetPositionLong(), 272000));
}

TEST(FX, FXGrid2SideRate)
{
	// calculate position using two-sided price
	MockTwoSidedRefRate rate2;
	FXGrid grid2(rate2);

	// we buy USD/JPY at best ask
	double broughtrate = rate2.getLastAsk("USD/JPY");
	// JPY's short position
	double jpyPosition = -1000000*broughtrate;
	// convert to USD position - how much should we sell USD/JPY to cover JPY
	double jpyPositionUSD = jpyPosition / rate2.getLastBid("USD/JPY");

	grid2.addTransaction(FX_BUY, "USD/JPY", 1000000, broughtrate);
	EXPECT_TRUE(DoubleEq(grid2.getPositionLocal(USD), 1000000));
	EXPECT_TRUE(DoubleEq(grid2.getPositionLocal(JPY), jpyPosition));
	EXPECT_TRUE(DoubleEq(grid2.getPositionUSD(USD), 1000000));
	EXPECT_TRUE(DoubleEq(grid2.getPositionUSD(JPY), jpyPositionUSD));
	EXPECT_TRUE(DoubleEq(grid2.getNetPositionShort(), jpyPositionUSD));
	EXPECT_TRUE(DoubleEq(grid2.getNetPositionLong(), 1000000));

	// we sell USD/JPY at best bid
	double soldrate = rate2.getLastBid("USD/JPY");
	// JPY's position
	jpyPosition += 1000000 * soldrate;
	// JPY position should still be short, need to sell USD/JPY to cover
	jpyPositionUSD = jpyPosition / rate2.getLastBid("USD/JPY");

	grid2.addTransaction(FX_SELL, "USD/JPY", 1000000, soldrate);
	EXPECT_TRUE(DoubleEq(grid2.getPositionLocal(USD), 0));
	EXPECT_TRUE(DoubleEq(grid2.getPositionLocal(JPY), jpyPosition));
	EXPECT_TRUE(DoubleEq(grid2.getPositionUSD(USD), 0));
	EXPECT_TRUE(DoubleEq(grid2.getPositionUSD(JPY), jpyPositionUSD));
	EXPECT_TRUE(DoubleEq(grid2.getNetPositionShort(), jpyPositionUSD));
	EXPECT_TRUE(DoubleEq(grid2.getNetPositionLong(), 0));
}

TEST(FX, FXGridLocalRate)
{
	// calculate position using two-sided price
	FXReferenceLocal rate;
	FXGrid grid(rate);

	// adding all the rates
	rate.addRate(USDJPY, FX_BUY, 100.0);
	rate.addRate(USDJPY, FX_SELL, 200.0);
	// we push all rates by adding zero-size transaction
	// we buy USD/JPY at best ask
	double broughtrate = rate.getLastAsk("USD/JPY");
	// JPY's short position
	double jpyPosition = -1000000*broughtrate;
	// convert to USD position - how much should we sell USD/JPY to cover JPY
	double jpyPositionUSD = jpyPosition / rate.getLastBid("USD/JPY");

	grid.addTransaction(FX_BUY, "USD/JPY", 1000000, broughtrate);
	EXPECT_TRUE(DoubleEq(grid.getPositionLocal(USD), 1000000));
	EXPECT_TRUE(DoubleEq(grid.getPositionLocal(JPY), jpyPosition));
	EXPECT_TRUE(DoubleEq(grid.getPositionUSD(USD), 1000000));
	EXPECT_TRUE(DoubleEq(grid.getPositionUSD(JPY), jpyPositionUSD));
	EXPECT_TRUE(DoubleEq(grid.getNetPositionShort(), jpyPositionUSD));
	EXPECT_TRUE(DoubleEq(grid.getNetPositionLong(), 1000000));

	// we sell USD/JPY at best bid
	double soldrate = rate.getLastBid("USD/JPY");
	// JPY's position
	jpyPosition += 1000000 * soldrate;
	// JPY position should still be short, need to sell USD/JPY to cover
	jpyPositionUSD = jpyPosition / rate.getLastBid("USD/JPY");

	grid.addTransaction(FX_SELL, "USD/JPY", 1000000, soldrate);
	EXPECT_TRUE(DoubleEq(grid.getPositionLocal(USD), 0));
	EXPECT_TRUE(DoubleEq(grid.getPositionLocal(JPY), jpyPosition));
	EXPECT_TRUE(DoubleEq(grid.getPositionUSD(USD), 0));
	EXPECT_TRUE(DoubleEq(grid.getPositionUSD(JPY), jpyPositionUSD));
	EXPECT_TRUE(DoubleEq(grid.getNetPositionShort(), jpyPositionUSD));
	EXPECT_TRUE(DoubleEq(grid.getNetPositionLong(), 0));
}
