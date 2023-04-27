#!/usr/bin/python3

import mts_repo
import tickdata_parser
import mts_util
import symbol_map

import datetime
import os
import sys
import traceback
import numpy as np
import copy

MTS_Bar_Path = './repo/tickdata_prod'
Tickdata_Path = './repo/tickdata/FUT'
Extract_Tmp_Path = './repo/tickdata'

Exclude_Symbol = [] # too big to handle in uat

####################################################
# tickdata doesn't have spread contract data.
# Set include_spread=False when tickdata starts to 
# provide this data.
######################################################
def run_month(mts_symbol, month_ym, max_N, overwrite_repo=False, extra_N=[]) :
    map_obj = symbol_map.SymbolMap(max_N=max_N)
    td_obj = tickdata_parser.TickdataMap(map_obj)
    td_symbol = td_obj.get_td_by_mts(mts_symbol)

    repo_obj = mts_repo.MTS_REPO(MTS_Bar_Path, map_obj)

    print("Extracting tickdata %s %s(%s) to %s"%(month_ym, td_symbol, mts_symbol, Extract_Tmp_Path))
    td_obj.get_td_monthly_file(td_symbol, month_ym, Tickdata_Path, Extract_Tmp_Path)

    data_obj = mts_repo.MTS_DATA(-6,0,17,0)
    sday = month_ym+'01'
    eday = month_ym+'31'
    data_obj.fromTickDataMultiDay(sday, eday, mts_symbol, Extract_Tmp_Path, repo_obj, td_obj, extended_fields = extended_fields, overwrite_repo=overwrite_repo, include_spread=False, extra_N=extra_N)

if __name__ == "__main__":
    if len(sys.argv) < 6 :
        print("Usage: %s mts_symbols year_month max_N [extended_fields: 0|1] [overwrite: 0|1] [extra_N]"%(sys.argv[0]))
        print("This processes one month of the given MTS symbols from tickdata and produce MTS Bar up to max_N contracts.")
        print("Optional fourth param being 0 (default) or 1 to add extended fields to the bar")
        print("NOTE: mts_symbols can be \"all\", which will try to do all listed in symbol map")
        print("      mts_symbols can also be a comma delimited list, such as WTI,SPX")
        print("      year_month can be either yyyymm, or yyyy, which will do all months")
        print("      extra_N, if given, is a comma delimited list of N to also be included, i.e. 6,12, cannot exceed 12")
        sys.exit(1)

    max_N = int(sys.argv[3])
    assert (max_N>0), "N has to be positive"

    extended_fields = False
    if len(sys.argv) >= 5 :
        extended_fields = (int(sys.argv[4]) == 1)
    overwrite_repo = False
    if len(sys.argv) >= 6 :
        assert (int(sys.argv[5]) in [0,1]), 'overwrite_repo has to be 0 or 1'
        overwrite_repo = (int(sys.argv[5]) == 1)
    extra_N = []
    if len(sys.argv) >= 7 :
        extra_N = list( np.array(sys.argv[6].split(',')).astype(int) )

    month_ym = sys.argv[2]
    if len(month_ym)==4:
        # take as a year
        month_ym = list((np.arange(12)+1+int(month_ym)*100).astype(str))
    else :
        month_ym=[month_ym]

    mts_symbol = copy.deepcopy(sys.argv[1])
    smap = symbol_map.SymbolMap(max_N=max_N)
    Extract_Tmp_Path += ('_'+datetime.datetime.now().strftime('%s'))
    os.system('mkdir -p '+ Extract_Tmp_Path+' > /dev/null 2>&1')
    print ('extract to ' + Extract_Tmp_Path + ' extended_fields: ', extended_fields, 'overwrite_repo:', overwrite_repo)

    # getting mts_symbols for all month_ym, in case "all"
    mts_symbol0 = []
    if mts_symbol == 'all' :
        for yyyymm in month_ym:
            tday = yyyymm +'11'
            tdi = mts_util.TradingDayIterator(tday)
            tday=tdi.begin()
            print('getting symbol list ', tday)
            mts_symbol0 += smap.list_symbol(today_yyyymmdd = tday, add_prev_day=True)
            tday = tdi.next(10)
            mts_symbol0 += smap.list_symbol(today_yyyymmdd = tday, add_prev_day=True)

        mts_symbol0 = list(set(mts_symbol0))
    else :
        if ',' in mts_symbol :
            mts_symbol0 = mts_symbol.split(',')
        else :
            mts_symbol0 = [mts_symbol]
    print('getting ', month_ym, len(mts_symbol0), ' symbols: ', str(mts_symbol0))

    # main loop
    for yyyymm in month_ym:
        for sym in mts_symbol0:
            print (sym)
            if sym in Exclude_Symbol:
                print (sym, ' excluded!')
                continue
            try :
                print ('running for ', sym, ' in ', yyyymm, 'max_n', max_N)
                run_month(sym, yyyymm, max_N,overwrite_repo=overwrite_repo,extra_N=extra_N)
            except :
                pass
        
    os.system('rm -fR ' + Extract_Tmp_Path + ' > /dev/null 2>&1')
    print ("Done!\n")


