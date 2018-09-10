/*
 * tpcme.hpp
 *
 *  Created on: Sep 8, 2018
 *      Author: zfu

CME data structure

1. Transact Time in nanoseconds --> 1503959239515809059 (Epoch time)
2. Symbol --> CLF9
3. Side -->  B (Bid), A (Ask), E (Implied Bid), F (Implied Ask),
             Y (Aggressor BUYs), Z (Aggressor SELLs),
             O (NO Aggressor, execution coming from Spread markets,
                need to match the execution price with the book price
                to decide whether it's BUY or SELL)
4. Level --> 0 for 43(MBOrder) and 42(Execution) messages, 1 to 10 for 32 (MBPrice) messages
5. Price
6. Size
7. Orders --> 0 for 43 (MBO) messages
   * In Book Entry  - aggregate number of orders at the given price level.
   * In Trade Entry - identifies the total number of non-implied orders per
                      instrument that participated in this match event.
                      NOTE: it's the sweeping order (1) + orders sweeped.
                      Usually at least 2 for trade.
8. Action --> 0 (New), 1 (Change), 2 (Delete), 3 (Delete Thru), 4 (Delete From), 5 (Trade Execution)
9. Message Type
   *  32 Market Data Incremental Refresh Book (Market By Price)
   *  42 Market Data Trade Summary (Execution)
   *  43 Market Data Incremental Refresh Order Book (Market By Order)
10. Order ID (Unique) --> 644521434625 (0 means no Order ID existed in message)
11. Order Priority --> 5582229231 (0 means no Order Priority existed in message)

Some old files are only MBP (type 32), for example ESU8 @ 2017-06-01 :
1496287731361780949,ESM8,A,1,2417.75,15,1,0

New files have all columns, for example ESU8 at 2017-12-01:
1512082601035356357,ESU8,B,1,2636.5,16,1,0,32,644526534015,5589816048

The utility should use the cem historical files and output to a sequence of
book depot
*/

#pragma once
#include "../../featcol.hpp"
#include "bookL2.hpp"

/*
 * This reads a CME file, extract BookDepot events, and call collector.
 * It is currently a L2 (price level) update interface. i.e. you don't
 * know the number of order counts and queue position.
 * To be compatible with IB, of course
 *
 */
class CMEFileReader {
public:
	CMEFileReader(const char* hist_file, Collector& col);
	void read(); // read the file and call collector on events
};
