#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "time_util.h"
#include "PosixTestClient.h"
#include <stdexcept>
#include <map>
#include "IBConst.h"

#include "Contract.h"
#include "Order.h"
#include "OrderState.h"
#include "Execution.h"
#include "ScannerSubscription.h"
#include "CommissionReport.h"

#include "EPosixClientSocket.h"
#include "EPosixClientSocketPlatform.h"

// Currently, entire thing has one infintely loop in a single thread
// poll order update
// poll control
// poll timer
// poll Model
// poll MD
// Therefore, the client is a massive cluster of wires flying around!
// It has the socket to TP, socket to Model, socket to control,
// nonblocking mode for read+write
// To make life less miserable, some interface is enforced
// ModelClient - handles Model socket
//     - bool MdlClient.poll(ModelSignal& signal)
//       - where signal can include symbol, venue, side, px, vol, parameters, etc
//     - bool MdlClient.start/stop/pause/resume
//     - MdlClient(Trader&), which allows it to have access to global resources
//       - upon start, it needs to getConfig, connect socket, initialize the model
//          engine, getBook, register timer
//     - Optionally Model would like to be pushed MD update/Trade update
//     - MdlClient.onBBO(BBOPrice) and onTrade(OrderInfo)

// TPClient - handles Venue specific MD and Order Entry
//     - EventType TPClient.poll(BBO, OrderInfo)
//       - EventType can be BBO, Order
//       - calls Trader's onBBO and onOrderUpdate
//       - Trader will do everything, including position logging
//     - bool sendOrder()
//     - bool subscribe()
//     - start/stop()

// PLCC - Position/Logger/Config/Control is a set of utilities
//     - PLCC.log()
//     - PLCC.getConfig()
//     - PLCC.poll()
//       nonblockingly checking control command
//     - PLCC.sendMsg()
//     - PLCC.onOrderUpdate()/onBBO()
//       updates the position logs


class ModelClient : public PosixTestClient {
public:
	struct OrderInfo {
		OrderId order_id;
		Contract* p_contract;
		Order* p_order;

		int ord_qty;
		int cum_qty;
		int last_qty;
		int leaves_qty;

		double last_px;
		double avg_px;

		bool is_done;
		uint64_t t_in;
		uint64_t t_done;

		OrderInfo(Contract* con, Order*ord) :
		order_id(ord->orderId), p_contract(con), p_order(ord),
		ord_qty(ord->totalQuantity), cum_qty(0), last_qty(0),
		leaves_qty(ord_qty), last_px(0), avg_px(0), is_done(false),
		t_done(0)
		{
			t_in = TimeUtil::cur_time_gmt_micro();
		}
	};
	typedef std::map<OrderId, OrderInfo*> OrderMap;

	// venue side
	void connectToVenu();
	void subscribeMD();


	// order management
	void makeOrderMock() {
		static bool ifmake = false;
		if (ifmake) return;
		printf("Mock order: got price at %.5lf and %.5lf\n", m_price[0][0].price, m_price[0][1].price);
		makeOrder("EUR/USD", 3000000, m_price[0][0].price+0.0002, true);
		ifmake = true;
	}

	void makeOrder(const char* symbol, int qty, double price, bool isBuy) {
		Contract* contract = new Contract();
		Order* order = new Order();
		makeContract(*contract, symbol);

		order->action = isBuy? "BUY":"SELL";
		order->totalQuantity = qty;
		order->orderType = "LMT";
		//order->orderType = "LMT,IOC";
		order->lmtPrice = price;
		order->orderId = ++m_orderId;

		printf( "Placing Order %ld: %s %ld %s at %f\n", m_orderId, order->action.c_str(), order->totalQuantity, contract->symbol.c_str(), order->lmtPrice);
		m_pClient->placeOrder( m_orderId, *contract, *order);
		m_ordMap[m_orderId] = new OrderInfo(contract, order);
	}
	void cancelOrder(OrderId oid) {
		m_pClient->cancelOrder( oid);
	}

	/*
	 * In execDetails: reqId, contract.ToString().c_str(), execution.ToString().c_str() =
	 *     -1,
	 *     Contract{conId:12087792,symbol:EUR,secType:CASH,expiry:,exchange:IDEALPRO,
	 *              primaryExchange:,currency:USD,localSymbol:EUR.USD,
	 *              tradingClass:EUR.USD,secIdType:,secId:,},
	 *     Execution{execId:0001f4e8.5372b124.01.01,time:20140514  01:20:56,acctNumber:DU183665,
	 *               exchange:IDEALPRO,side:BOT,shares:710000,price:1.37208,permId:1283261980,
	 *               clientId:1023,orderId:12,liquidation:0,cumQty:3000000,avgPrice:1.37208,
	 *               orderRef:,evRule:,evMultiplier:0,}
	 * In openOrder: (long) orderId, contract.ToString().c_str(), order.ToString().c_str(), ostate.ToString().c_str() =
	 *     12,
	 *     Contract{conId:12087792,symbol:EUR,secType:CASH,expiry:,exchange:IDEALPRO,primaryExchange:,
	 *              currency:USD,localSymbol:EUR.USD,tradingClass:EUR.USD,secIdType:,secId:,},
	 *     ORDER{orderId:12,clientId:1023,action:BUY,totalQuantity:3000000,orderType:LMT,
	 *           lmtPrice:1.37225,auxPrice:0,tif:DAY,},
	 *     OrderState{status:Filled,initMargin:1.7976931348623157E308,
	 *           maintMargin:1.7976931348623157E308,commission:82.3248,minCommission:1.79769e+308,
	 *           maxCommission:1.79769e+308,commissionCurrency:USD,warningText:,}
	 *
	 */
	void openOrder( OrderId orderId, const Contract& contract, const Order& order, const OrderState& ostate) {
		OrderInfo* oi = m_ordMap[orderId];
		if (!oi) {
			printf("Warning! received unidentified open order\n");
		}
		FunctionPrint("%ld, %s, %s, %s", (long) orderId, contract.ToString().c_str(), order.ToString().c_str(), ostate.ToString().c_str());
	}

	void execDetails( int reqId, const Contract& contract, const Execution& execution) {
		FunctionPrint("%d, %s, %s", reqId, contract.ToString().c_str(), execution.ToString().c_str());
	}

	// model side
	void connectToModel();
	void updateModel();
	void processModel();

	// market data
	void tickPrice(TickerId id, TickType field, double price, int canAutoExecute) {
		if ((field < 1) || (field > 2)) return;
		PriceEntry *pe = &(m_price[id-1][field-1]);
		pe->price = price;
		//printf("%llu %d %d %d %d %.5lf %d\n",
		//		TimeUtil::cur_time_gmt_micro(), (int)(id - 1), 0, 1, (field==1)? 1:0, price, pe->size);
		//printf("updated price: %d %d %.5lf \n", (int) id, (int)field, price);
		//PosixTestClient::tickPrice( id, field, price, canAutoExecute);
	}

	void tickSize(TickerId id, TickType field, int size) {
		if ((field != 0) && (field != 3)) return;
		PriceEntry *pe = &(m_price[id-1][field/3]);
		pe->size = size;
		//printf("%llu %d %d %d %d %.5lf %d\n",
		//		TimeUtil::cur_time_gmt_micro(), (int)(id - 1), 0, 1, (field==0)? 1:0, pe->price, size);
		//PosixTestClient::tickSize( id, field, size);
		//printf("updated size: %d %d %d \n", (int) id, (int)field, size);
	}
private:
	struct PriceEntry {
		double price;
		int size;
		PriceEntry() : price(0), size(0) {};
		PriceEntry(double p, int s) : price(p), size(s) {};
		void reset() {
			price = 0; size = 0;
		}
		void set(double p, int s) {
			price = p; size = s;
		}
	};

public:
	PriceEntry m_price[MaxNumSymbols][2];
	OrderMap m_ordMap;
};

ModelClient * client;

void sig_handler(int signo)
{
  if (signo == SIGINT)
    printf("received SIGINT, exiting...\n");

  delete client;
  client = NULL;
  exit (1);
}

// Current function - real time data
int main(int argc, const char** argv)
{
	if (argc < 3) {
		printf("Usage: %s client_id symbol [IBG|TWS] \n", argv[0]);
		return -1;
	}

    if (signal(SIGINT, sig_handler) == SIG_ERR)
    {
	    printf("\ncan't catch SIGINT\n");
	    return -1;
    }

	int clientId = atoi(argv[1]);
	const char* symbol = argv[2];

	int port = IBG_PORT;
	if (argc > 3) {
		if (strcmp("TWS", argv[3]) == 0) {
			port = TWS_PORT;
		}
	}

	while (1) {
		client = new ModelClient();
		printf("client %d trying to get data for %s, connecting... \n", clientId, argv[2]);
		client->connect( NULL, port, clientId);
		printf("client %d trying to get data for %s, connected!\n", clientId, argv[2]);
		client->reqMDFx(symbol);
		printf("\n");
		while( client->isConnected()) {
			if (client->m_price[0][0].size != 0 &&  client->m_price[0][1].size != 0) {
				client->makeOrderMock();
			}
			if (!client->sock_recv(10000)) {
				printf("client read error!");
				break;
			}
		}
		printf("client %d exit.\n", clientId);

		sleep(10);
		delete client;
		client = NULL;
	}
	return 0;
}

