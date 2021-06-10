#!/usr/bin/python3

import mts_repo
import tickdata_parser
import mts_util
import symbol_map

import datetime
import os
import sys

MTS_Bar_Path = './repo/mts'
Tickdata_Path = './repo/tickdata/FUT'
Extract_Tmp_Path = './repo/tickdata'

if __name__ == "__main__":
    if len(sys.argv) < 4 :
        print("Usage: %s mts_symbol month max_N [log_file]"%(sys.argv[0]))
        print("This processes one month of the given MTS symbol from tickdata and produce MTS Bar up to max_N contracts")
        sys.exit(1)


    org_stdout = None
    org_stderr = None
    logfile = None
    if len(sys.argv) == 5 :
        print ("Logging to file " + sys.argv[4])
        logfile = open(sys.argv[4], "w")
        if not logfile :
            raise RuntimeError("Cannot create log file " + sys.argv[4])
        org_stdout = sys.stdout
        org_stderr = sys.stderr
        sys.stdout = logfile
        sys.stderr = logfile


    try :
        mts_symbol = sys.argv[1]
        month_ym = sys.argv[2]
        if len(month_ym) != 6 :
            print("month has to be yyyymm")
            sys.exit(1)
        max_N = int(sys.argv[3])
        assert (max_N>0), "N has to be positive"

        map_obj = symbol_map.SymbolMap(max_N=max_N)
        td_obj = tickdata_parser.TickdataMap(map_obj)
        td_symbol = td_obj.get_td_by_mts(mts_symbol)

        repo_obj = mts_repo.MTS_REPO(MTS_Bar_Path, map_obj)

        print("Extracting tickdata %s %s(%s) to %s"%(month_ym, td_symbol, mts_symbol, Extract_Tmp_Path))
        td_obj.get_td_monthly_file(td_symbol, month_ym, Tickdata_Path, Extract_Tmp_Path)

        data_obj = mts_repo.MTS_DATA(-6,0,17,0)
        sday = month_ym+'01'
        eday = month_ym+'31'
        data_obj.fromTickDataMultiDay(sday, eday, mts_symbol, Extract_Tmp_Path, repo_obj, td_obj)

        print ("Done!\n")
    except :
        print ("Problem running the parser!")
        traceback.print_exc()

    if logfile :
        sys.stdout = org_stdout
        sys.stderr = org_stderr
        logfile.close()


