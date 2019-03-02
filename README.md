So it's getting complicated and the whole layout is the following. 

System is launched by python/launch.py
1) update config/main.cfg, roll, stuff
2) start/sustain the processes:
   * tpib - connecting to tws, subscribe md and write to queue
                SubL1:  L1 subscription, BBO, quote and trade flow
                SubL2:  L2 subscription, 10 level book, no trade from TWS
                        but tpib write the trade to L2 queue from L1 subscription
                        (tpib.hpp _l1_2_l2_book_queue)
                        Because of IB's limitation on maximum L2 subscriptions, it is current
                        6: CL, LCO, ES, 6E, GC, ZN, maximum to 10
                SubL1n: L1 subscription on Future's next contract
                        BBO and trade
                tpib writes updates to /dev/shm queues, use booktap to 
                get tick-by-tick update on each queue

   * tickrec -  L1 bar writer writing to bar/NYM_CL_B1S.csv
                It reads from the shm queue, and write to bar file
                at fixed interval (1 second bar) currently 
                utc, bsz, bp, ap, asz, vb, vs, last_utc, bid_upd_cnt, ask_upd_cnt, #buys, #sell, time_wt_ism
                It is intended to add features to the bar repo generated from hist file
                To monitor, 
                    tail -f bar/NYM_CL_B1S.csv
                refer to bookL2.hpp::barWriter

   * tickrecL2- L2 delta writer.  Writes tick-by-tick of depth of book updates as new/del/update and trades
                delta format to a binary file. It currently not only writes L2 queues, but also L1 BBO/trades
                in a tick-by-tick fashion.  The intention is that this should provide useful information. 
                The set up is that tickrecL2 will record all L2, L1 and L1n subscriptions. 
                To monitor, 
                    bin/l2filetap sym [L2|L1|L1n] [full|tail] 
                    tail means read from the latest snapshot and iterate forward, including live updates
                    until ctrl-C
                    full is to read everything from the bar/NYM_CL_[L1|L2][_bc].csv and stay live
                refer to bookL2.hpp::L2DeltaWriter

3) The tpib has a check_alive, which will check CL's L2 updates, will restart if no update for 1 minutes

4) The entire rule for future roll is in l1.py, used accross by kdb/ib history as well as tpib live

In case reinstall the TWS:
1. Go to Computer
2. C drive
3. Rename the Folder Jts to Jtsold or a name you prefer
4. Delete the previous installed folder of .i4j_jres and .oracle_jre_usage by going into to C:\Users\(Username).
5. Go to our website to download a new installer: https://www.interactivebrokers.com/en/index.php?f=14099#tws-software
6. Install Trader WorkStation

Issues Log
1/28/2019
- the problem with security definition of ETF/VXX.  For some reason, at the night, it starts to throw such security definition
  cannot find. After removed almost all of the subscription, the tpib.exe kept throwing, and TWS DATA flash rad. Turned out to be a problem
  with IB backend, the live chat asks to ping, restart computer, reinstall the TWS, and finally admit problem. 
  NOT HELPFUL AT ALL!  Be aware of such crappy customer service!

