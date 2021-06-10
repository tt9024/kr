#!/usr/bin/python3

import xml.etree.ElementTree as ET
import numpy as np
import datetime
import copy
import traceback

TradableKeys = ["symbol", "exch_symbol", "venue", "tick_size", "point_value", "px_multiplier", "type", "mts_contract", "contract_month", "mts_symbol", "N", "expiration_days", "tt_security_id", "tt_venue", "currency", "expiration_date","bbg_id","tickdata_id", "tickdata_px_multiplier", "tickdata_timezone", "lotspermin"]

def gen_asset_root(xml_path, assets_xml) :
    xml_cfg = xml_path + "/" + assets_xml
    root = ET.parse(xml_cfg).getroot()
    return root

def update_symbol_map(today_yyyy_mm_dd, xml_path, max_N = 2, assets_xml = "assets.xml", mts_symbols=None, root = None) :
    """
    Input: 
        today_yyyy_mm_dd: today's date as yyyy-mm-dd
        xml_path: the path to the mappings xml, i.e. config/symbol
        max_N: number of contracts to include for each future symbol
        assets_xml: the xml file, ie. assets.xml
        mts_symbols: optional list of mts_symbols (taken without _N, otherwise ignores _N) 
                     to generate mappings for. If None then generates all.
        root:        optional xml root if None then generates from xml_path

    return :
        it reads assets.xml and roll_schedules/* 
        and parse into a list of tradable and a dict of venues for current day:
        tradable = [ 
            {
              tradable = ESH1
              symbol = SPX
              exch_symbol = ES
              venue = CME
              currency = USD
              tick_size = 0.25
              point_value = 50
              px_multiplier = 0.01
              type = FUT

              mts_contract = SPX_202103
              contract_month = 202103
              mts_symbol = SPX_N1
              N = 1
              expiration_days = 4
              tt_security_id = 0123455
              tt_venue = CME
              subscribed = 0
              bbg_id = ESH1 INDEX
              tickdata_id = ESH21
              lotspermin = 40
            }
            ...
          ]

        venue = {
          CME = {
                hours = [ -6, 0, 17, 0 ]
          }
        }
    """

    if mts_symbols is not None:
        # remove "_N?" from mts_symbols if exists
        symbols0 = []
        for symbol in mts_symbols :
            if symbol[-3:-1] == '_N' :
                symbol = symbol[:-3]
            symbols0.append(symbol)
        mts_symbols = symbols0

    today = today_yyyy_mm_dd
    if '-' not in today_yyyy_mm_dd and len(today_yyyy_mm_dd) == 8 :
        today = today_yyyy_mm_dd[:4] + '-' + today_yyyy_mm_dd[4:6] + '-' + today_yyyy_mm_dd[6:]
    #today += " 00:00:00"

    if root is None :
        root = gen_asset_root(xml_path, assets_xml)

    tradable = []
    venues = {}

    for assets in root :
        sym = {}
        for asset in assets :
            if asset.tag == "symbol" :
                if mts_symbols is not None and len(mts_symbols)>0 and asset.text not in mts_symbols :
                    break
                sym["symbol"] = asset.text
            elif asset.tag == "exchange_symbol":
                sym["exch_symbol"] = asset.text
            elif asset.tag == "type" :
                sym["type"] = asset.text
            elif asset.tag == "currency" :
                sym["currency"] = asset.text
            elif asset.tag == "exchange" :
                sym["venue"] = asset.text
            elif asset.tag == "ticksize" :
                sym["tick_size"] = asset.text
            elif asset.tag == "currency" :
                sym["currency"] = asset.text
            elif asset.tag == "calendar" :
                cal_file = xml_path + "/calendars/" + asset.text

                sym["cal"] = {"file": cal_file}
                # getting the calendar for first max_N contract of today,
                # include N0, N1, etc
                c = np.genfromtxt(cal_file, delimiter = ",", dtype = "|S32")
                try :
                    ct = c[np.nonzero(c[:,0] == str.encode(today))[0][0]]
                    s = ct[2].decode()
                    e = ct[3].decode()
                    if s == "" or e == "" :
                        # not a trading day
                        raise today + " is not a trading day from calendar" 
                    sym["cal"]["s"] = s
                    sym["cal"]["e"] = e
                    sym["expiration_days"] = int(ct[6])
                    # read upto max_N contracts into "contracts"
                    # for N = 0, 1, .., max_N 
                    sym["contracts"] = []
                    for n in np.arange(max_N+1).astype(int) :
                        sym["contracts"].append(ct[7+n].decode())

                except :
                    #traceback.print_exc()
                    #raise TradingDayError( today + " is not a trading day from " + cal_file)
                    #print ( today + " is not a trading day from " + cal_file)
                    break

                # set the venue
                if "venue" in sym.keys() :
                    v = sym["venue"]
                    if v in venues.keys() :
                        if venues[v]["s"] != s or venues[v]["e"] != e :
                            #print ("venue hour update for " + v)
                            if venues[v]["s"] > s :
                                venues[v]["s"] = s
                            if venues[v]["e"] < e :
                                venues[v]["e"] = e
                    else :
                        venues[v] = {"s": s, "e": e}

            elif asset.tag == "providers" :
                # getting the tt's px mul
                # and tickdata's px_multiplier and timezone
                for  p in asset :
                    if p.tag == "tt" :
                        for p0 in p :
                            if p0.tag == "px_multiplier" :
                                sym["px_multiplier"] = p0.text
                            if p0.tag == "exchange":
                                sym["tt_venue"] = p0.text
                    elif p.tag == "tickdata" :
                        for p0 in p :
                            if p0.tag == "px_multiplier" :
                                sym["tickdata_px_multiplier"] = p0.text
                            elif p0.tag == "timezone" :
                                sym["tickdata_timezone"] = p0.text
            elif asset.tag == "execution" :
                # getting the twap lotspermin
                for  p in asset :
                    if p.tag == "twap" :
                        for p0 in p :
                            if p0.tag == "lotspermin":
                                sym["lotspermin"] = p0.text

            elif asset.tag == "contracts" :
                # getting the tradable name
                for c in asset :
                    con0 = {}
                    for f in c :
                        if f.tag == "pointvalue" :
                            con0["point_value"] = f.text
                        elif f.tag == "expiry" :
                            expiry = f.text
                            if expiry not in sym["contracts"] :
                                con0 = {}
                                break
                            n = np.nonzero(np.array(sym["contracts"])==expiry)[0][0]
                            con0["contract_month"] = expiry
                            con0["N"] = n
                        elif f.tag == "tt_exchange_id" :
                            con0["tradable"] = f.text
                        elif f.tag == "tt_security_id" :
                            con0["tt_security_id"] = f.text
                        elif f.tag == "expiration_date" :
                            con0["expiration_date"] = f.text
                        elif f.tag == "bbg_id" :
                            con0["bbg_id"] = f.text
                        elif f.tag == "tickdata_id" :
                            con0["tickdata_id"] = f.text
                        elif f.tag == "symbol" :
                            con0["mts_contract"] = f.text

                    if len(con0.keys()) >= 4 :
                        sym[n] = copy.deepcopy(con0)

        # finish parsing this asset into sym
        # write maxN tradable into trd 
        # 
        for n in np.arange(max_N+1).astype(int) :
            if n not in sym.keys() :
                continue
        
            sym0 = copy.deepcopy(sym)
            sym0.update(sym[n])
            sym0["mts_symbol"] = sym["symbol"] + "_N"+str(n)
            trd0 = {}
            for k in TradableKeys :
                # write to the files
                try :
                    trd0[k] = sym0[k]
                except :
                    if 'tickdata' not in k :
                        print (k + " is not defined " + str(sym0["symbol"]))
                    trd0[k] = 'None'

            trd0["tradable"] = sym0["tradable"]
            if trd0['expiration_date'] > today :
                # only add contracts expires later than today
                tradable.append(copy.deepcopy(trd0))

    # finish all tradable parsing, write to file
    return tradable, venues


def writeToConfigure(tradable, venues, cfg_path, map_file = "symbol_map.cfg") :
    """
    persist the two dictionaries to cfg_path/map_file
    """

    with open(cfg_path + "/" + map_file, "w") as f :
        f.write("tradable = {\n")
        for trd in tradable :
            f.write("    " + trd["tradable"] + " = {\n")
            for key in TradableKeys :
                f.write("        " + key + " = " + str(trd[key])+ "\n")
            f.write("    " + "}\n")

        f.write("}\n")
        f.write("venue = {\n")
        for key in venues.keys() :
            f.write("    " + key + " = {\n")
            s = venues[key]["s"]
            e = venues[key]["e"]

            sh = int(s.strip().split(" ")[1].split(":")[0])
            sm = int(s.strip().split(" ")[1].split(":")[1])
            eh = int(e.strip().split(" ")[1].split(":")[0])
            em = int(e.strip().split(" ")[1].split(":")[1])

            if sh > eh or (sh == eh and sm >= em) :
                sh = sh - 24

            f.write("        hours = [ " + str(sh) + ", " + str(sm) + ", " + str(eh) + ", " + str(em) + " ]\n")
            f.write("    }\n")
        f.write("}\n")

class SymbolMap :
    def __init__(self, xml_path = 'config/symbol', max_N = 2, assets_xml = "assets.xml") :
        self.xml_path = xml_path
        self.max_N = max_N
        self.assets_xml = assets_xml
        self.assets_root = None

    def update_config(self, today_yyyy_mm_dd, cfg_path = 'config', map_file = 'symbol_map.cfg', mts_symbols = None) :
        self._get_assets_root()
        t,v = update_symbol_map(today_yyyy_mm_dd, self.xml_path, self.max_N, self.assets_xml, mts_symbols = mts_symbols, root = self.assets_root)
        writeToConfigure(t, v, cfg_path, map_file)

    def get_tradable_map(self, today_yyyy_mm_dd, mts_key = False, mts_symbols = None) :
        """
        if mts_key is True, map have key on mts_symbol, otherwise, key is tradable
        return :
            dict with key being either tradable or mts_symbol, value being the tradable dict
        """
        self._get_assets_root()
        t,v = update_symbol_map(today_yyyy_mm_dd, self.xml_path, self.max_N, self.assets_xml, mts_symbols=mts_symbols, root=self.assets_root)
        smap = {} # a map from mts_symbol to a tradable
        for t0 in t :
            k = t0["mts_symbol"] if mts_key else t0["tradable"]
            smap[k] = copy.deepcopy(t0)
        return smap

    def list_symbol(self, today_yyyy_mm_dd = None, mts_symbol_list=None) :
        """
        return a list of symbol, such as WTI, SPX, from mts_symbol, such as WTI_N1. 
        if None then list all defined in assets.xml for today_yyyy_mm_dd
        """
        if today_yyyy_mm_dd is None :
            today_yyyy_mm_dd = datetime.datetime.now().strftime('%Y%m%d')
        smap = self.get_tradable_map(today_yyyy_mm_dd, mts_key = True)
        if mts_symbol_list is None:
            mts_symbol_list = smap.keys()

        symbols = []
        for k in mts_symbol_list:
            sym = smap[k]['symbol']
            if sym not in symbols: 
                symbols.append(sym)
        return symbols

    def get_tradable_from_mts_symbol(self, mts_symbol_arr, trade_day) :
        """
        get an array of tradables given mts_symbols and a trading day
        Note the returned array has same length with mts_symbol_arr.  If not a trading day
        for the corresponding mts_symbol, a 'None' is put in place
        """
        tm = self.get_tradable_map(trade_day, mts_key=True, mts_symbols = mts_symbol_arr)
        ret = []
        for ms in mts_symbol_arr:
            if ms not in tm.keys() :
                ret.append(None)
            else :
                ret.append(tm[ms]["tradable"])
        return ret

    def get_contract_from_symbol(self, mts_symbol_no_N, trade_day) :
        """
        mts_symbol_no_N: mts_symbol that doesn't have '_N', i.e. WTI 
        trade_day: a trading day in YYYYMMDD
        return: a list of all contracts that matches with the symbol on 
                the day, upto max_N defined in self.max_N
        """
        tm = self.get_tradable_map(trade_day, mts_key=True, mts_symbols = [ mts_symbol_no_N ])
        ret = []
        for ms in tm.keys() :
            symbol = tm[ms]["symbol"]
            if symbol in mts_symbol_no_N :
                ret.append(tm[ms]["contract_month"])
        return ret

    def get_symbol_contract_from_tradable(self, tradable, trade_day) :
        """
        return mts_symbol, symbol and  contract_month
        """
        tinfo = self.get_tradable_map(trade_day)[tradable]
        return tinfo['mts_symbol'], tinfo['symbol'], tinfo['contract_month']

    def get_mts_symbol_from_tt_venue(self, tt_venue_arr, trade_day, tt_venue_map_cfg='config/venue_map.cfg', N=1) :
        ttvm = self._load_tt_venue_map(tt_venue_map_cfg)
        tm = self.get_tradable_map(trade_day, mts_key=True)
        ret = []
        for ms in tm.keys() :
            ttv = ttvm[tm[ms]["venue"]]
            if tt_venue_arr is None or ttv in tt_venue_arr :
                if ms[-1] != str(N) :
                    continue
                ret.append([tm[ms]["symbol"], ttv])
        return ret

    def get_mts_symbol_from_mts_venue(self, mts_venue_arr, trade_day) :
        tm = self.get_tradable_map(trade_day, mts_key=True)
        ret = []
        for ms in tm.keys() :
            venue = tm[ms]["venue"]
            if venue in mts_venue_arr :
                ret.append(ms)
        return ret

    def get_mts_symbol_from_field(self, trade_day, field_values, field_name) :
        tm = self.get_tradable_map(trade_day, mts_key=True)
        ret = []
        for ms in tm.keys() :
            if field_name not in tm[ms].keys() :
                continue
            val = tm[ms][field_name]
            if val in field_values: 
                ret.append(ms)
        return ret

    def _load_tt_venue_map(self, tt_venue_map_cfg) :
        vm = {}
        try :
            with open(tt_venue_map_cfg, "r") as f :
                while True :
                    l = f.readline()
                    if len(l) ==0 :
                        break
                    l = l.strip()
                    if l[0] == '#' :
                        continue
                    la = l.split('=')
                    if len(la) == 2 :
                        vm[la[0].strip()] = la[1].strip()
        except :
            traceback.print_exc()
            print ("failed to load tt venue map!")
        return vm

    def _get_assets_root(self) :
        if self.assets_root is None :
            try :
                self.assets_root = gen_asset_root(self.xml_path, self.assets_xml)
            except :
                traceback.print_exc()
                raise RuntimeError("Cannot get symbol map from " + xml_path + ", " + assets_xml)

