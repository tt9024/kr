import numpy as np
import mts_util
import dill
import Pop
import ar1_model
import ar1_eval
import datetime
import yaml
import copy
import Outliers as OT
import sys

def get_coef_scale(coef,first_gran=3) :
    """
    coef: shape [n, nh], i.e. c_0[:,0,:]
    return approximate scale captured at each n-by-n fcst table
    """
    n,nh=coef.shape
    scale0=[]
    for k in np.arange(n).astype(int):
        #over_night_scl=np.sum(np.abs(coef[k,:nh-2*n+k+1]))
        over_night_scl=0
        c0 = np.abs(coef[k,:])
        c0[-2*n:-n]*=np.sqrt(first_gran)
        scale0.append(c0[nh-2*n+k+1:nh-n+k+1]+over_night_scl)
    return np.array(scale0)[:,::-1]

def smooth_shp(shp_ni, iter=1) :
    # shp_ni shape (ni, n, nf)
    # smooth vertically and diagonally
    shp_out = []
    for shp in shp_ni:
        shp0 = shp.copy()
        for i in np.arange(iter) :
            shp0[1:,:] = shp[1:,:]*0.9 + shp[:-1,:]*0.1
            shp0[:-1,:] = shp[:-1,:]*0.9 + shp[1:,:]*0.1
            shp0[1:,1:] = shp[1:,1:]*0.8 + shp[:-1,:-1]*0.2
            shp0[:-1,:-1] = shp[:-1,:-1]*0.8 + shp[1:,1:]*0.2
            shp = shp0.copy()
        shp_out.append(shp0)
    return np.array(shp_out)

def ni_pick_w(shp_ni, ixf, shp_threshold=0.05, smooth_iter=1):
    ni, ndays, n, nf = shp_ni.shape
    shp = np.mean(shp_ni, axis=1)/np.std(shp_ni, axis=1)
    ixd = ixf-np.r_[-1,ixf[:-1]]
    shp /= np.sqrt(ixd)
    shp_s = smooth_shp(shp, iter=smooth_iter)
    ix = np.nonzero(shp_s>shp_threshold)
    w = np.zeros((shp_s.shape))
    w[ix] = 1
    return w

def f_from_fni(fni, shp_ni, ixf, shp_threshold=0.05) :
    w = ni_pick_w(shp_ni, ixf, shp_threshold=shp_threshold)
    ni, nndays, nf = fni.shape
    ni0, ndays, n, nf0 = shp_ni.shape
    assert ndays*n == nndays and ni==ni0 and nf == nf0

    fni_out=fni.copy()
    for d in np.arange(ndays).astype(int):
        fni_out[:,d*n:(d+1)*n,:]*=w
    return fni_out

def fcst_table_model(model_name, md_dict, symbol='CL', fm_in=None, fni_arr_in=None):
    n6_rt = dill.load(open(model_name, 'rb'))
    if fm_in is None:
        fm_in = n6_rt['fm']
    fm = copy.deepcopy(fm_in)
    f,v,_=fm.run_days(md_dict)

    fm = copy.deepcopy(fm_in)
    fni, shp_ni, lrf = ar1_eval.get_fni(fm, md_dict, symbol=symbol)
    ni,nndays,nf=fni.shape
    n=fm.fcst_obj.n

    ixf=fm.fcst_obj.ixf
    lr=md_dict[symbol][:,:,0].flatten()
    ft,tshp,fnx_,tshp_ni=ar1_eval.fcst_table_shp(fni,v,n,ixf,lr, fni_arr_in=fni_arr_in)
    return f,v,fni,shp_ni,ft,tshp,tshp_ni,ixf,n6_rt['ind']['spec'],fm

def collect_all(model_name_list, md_dict, symbol='CL', get_stdvh=False, stdvh_ni=0, fm_dict_in={}, fni_dict_in={}):
    # stdvh_ni is a scalar, ni for the trading instrument, usually 0, i.e. ni=0 is return of WTI, trading WTI
    fni=[]
    tshp=[]
    tshp_ni=[]
    ixf_fn=[]
    spec_fn=[]
    stdvh=[]
    vnf = []
    fm_dict={}
    for fn0 in model_name_list:
        fnkey = fn0.split('/')[-1].split('.')[0]
        fm_in = fm_dict_in[fnkey] if fnkey in fm_dict_in.keys() else None
        fni_in = fni_dict_in[fnkey] if fnkey in fni_dict_in.keys() else None
        _,v,fni0,_,_,tshp0,tshp_ni0,ixf,ni_spec,fm=fcst_table_model(fn0, md_dict, fm_in=fm_in,fni_arr_in=fni_in)
        fni.append(fni0)
        tshp.append(tshp0)
        tshp_ni.append(tshp_ni0)
        ixf_fn.append(ixf)
        spec_fn.append(ni_spec)
        vnf.append(v)
        fm_dict[fnkey]=fm

        # this is slow but need to be done once
        if get_stdvh:
            stdvh.append(ar1_eval.get_stdvh_days(fn0, md_dict, ni_ix=stdvh_ni, fm_in=fm_in))
    tshp_dict={'fn':model_name_list, 'fni':fni, 'tshp':tshp, 'tshp_ni':tshp_ni, 'stdvh':stdvh, 'vnf':vnf, 'ixf':ixf_fn, 'spec':spec_fn, 'fm_dict':fm_dict, 'md_dict':md_dict, 'symbol':symbol, 'stdvh_ni':stdvh_ni}
    return tshp_dict

def update_tshp_dict(tshp_dict, md_dict):
    model_name_list = tshp_dict['fn']
    symbol = tshp_dict['symbol']
    stdvh_ni = tshp_dict['stdvh_ni']
    fm_dict_in = tshp_dict['fm_dict']
    ndays, n = tshp_dict['stdvh'][0].shape

    # construct fni_in
    fni_dict_in={}
    for i, mn in enumerate(model_name_list):
        fnkey = mn.split('/')[-1].split('.')[0]
        fni_dict_in[fnkey] = tshp_dict['fni'][i][:,-n:,:]

    tshp_dict2 = collect_all(model_name_list, md_dict, symbol=symbol, get_stdvh=True, stdvh_ni=stdvh_ni, fm_dict_in = fm_dict_in, fni_dict_in=fni_dict_in)
    return tshp_dict2

def merge_tshp_dict(tshp_dict, tshp_dict2):
    """ tshp_dict will be updated in place

        The keys in both dict:
        ['fn', 'fni', 'tshp', 'tshp_ni', 'stdvh', 'vnf', 'ixf', 'spec', 'fm_dict', 'md_dict', 'symbol', 'stdvh_ni']

        'fni','tshp','tshp_ni','stdvh','vnf','fm_dict','md_dict' are updated

    """
    for i, fn in enumerate(tshp_dict['fn']):
        ni, nndays, nf = tshp_dict['fni'][i].shape
        # merge fni, tshp_ni
        for k in ['fni','tshp_ni']:
            x = []
            for ni0 in np.arange(ni).astype(int):
                x.append(np.vstack((tshp_dict[k][i][ni0], tshp_dict2[k][i][ni0])))
            tshp_dict[k][i]=np.array(x)
            #v0 = tshp_dict[k][i].reshape((ni, nndays*nf))
            #v1 = tshp_dict2[k][i].reshape((ni, nndays2*nf))
            #tshp_dict[k][i] = np.hstach((v0,v1)).reshape((ni, (nndays+nndays2), nf))

        # merge tshp, stdvh, vnf
        for k in ['tshp', 'vnf']:
            tshp_dict[k][i] = np.vstack((tshp_dict[k][i], tshp_dict2[k][i]))

        # stdvh has one more at the end
        tshp_dict['stdvh'][i] = np.vstack((tshp_dict['stdvh'][i][:-1,:], tshp_dict2['stdvh'][i]))

    tshp_dict['fm_dict'] = tshp_dict2['fm_dict']
    for k in tshp_dict['md_dict'].keys():
        tshp_dict['md_dict'][k]=np.vstack((tshp_dict['md_dict'][k], tshp_dict2['md_dict'][k]))

    return tshp_dict

def tshp_dict_summary(tshp_dict, lpx, tcost=0.05, pnl_tcost=0.015, contract_size=1000):
    """
    tshp_dict: {'fn','tshp',tshp_ni','stdvh','fni'}
    1. print 25 day shp
        for f,ts in zip(tshp_dict['fn'], tshp_dict['tshp']):
            sp0=[]
            for ix0, ix1 in zip([25, 50, 75], [75, 100, 125]):
                sp0.append(np.mean(ts[ix0:ix1,:,:])/np.std(ts[ix0:ix1,:,:]))
            print ('%60s:%.5f,%.5f,%.5f'%(f,sp0[0],sp0[1],sp0[2]))
    2. daily pnl from each model
    """

    pp_fn=[]
    pnl_fn=[]
    for fn, ixf, fni, stdvh in zip(tshp_dict['fn'], tshp_dict['ixf'], tshp_dict['fni'], tshp_dict['stdvh']):
        print('getting position for ',fn)
        f=np.sum(fni,axis=0)
        _,n=stdvh.shape
        v=ar1_eval.get_v_from_stdvh(stdvh[:-1,:],ixf) #stdvh has one more day than f,v
        pp0,pnl_cst=ar1_eval.p0_from_fnx(n,f,v,lpx,tcost,pnl_tcost=pnl_tcost, contract_size=contract_size)
        pp_fn.append(pp0)
        pnl_fn.append(pnl_cst)

    return pp_fn, pnl_fn
    
def print_ni_shp_consistency(tshp_dict, model_idx, thres_list=[-0.5,-0.3,0,0.01,0.02,0.03,0.05,0.1,0.15,0.3], ix0=[-75,-50], ix1=-25, ix2=-1, out_file=None):
    """
    The unfiltered sharp versus filtered.  The filter uses ix0:ix1, and check on ix1:ix2
    return 
        ni_thres_idx: [ (niname, [ ( thres, m2_1, m2_1d, tix1, m2_2, tix2 ) ], corrm21d, corrm22d ) ]
    """
    if out_file is not None:
        fp=open(out_file, 'at')
    else :
        fp = sys.stdout

    fp.write('\nmodel: [%s] in-samples (%d to %d and %d to %d) out-sample(%d to %d) \n\tcolumns: unfiltered_shp, filtered_shp1(cnt%%), filtered_shp2(cnt%%), col3-col1, col3-col2\n'\
            %(tshp_dict['fn'][model_idx].split('/')[-1], ix0[0], ix1, ix0[1], ix1, ix1, ix2))

    nm = len(tshp_dict['tshp_ni'][model_idx])
    ni_thres_idx=[]
    for i, tni in enumerate(np.vstack((tshp_dict['tshp_ni'][model_idx], [tshp_dict['tshp'][model_idx]]))):
        """
        shp0_1=np.mean(tni[ix0[0]:ix1,:,:],axis=0)/np.std(tni[ix0[0]:ix1,:,:])
        shp0_2=np.mean(tni[ix0[1]:ix1,:,:],axis=0)/np.std(tni[ix0[1]:ix1,:,:])
        shp1=np.mean(tni[ix1:ix2,:,:],axis=0)/np.std(tni[ix1:ix2,:,:])
        """

        shp0_1=np.median(tni[ix0[0]:ix1,:,:],axis=0)/np.std(tni[:ix1,:,:],axis=0)
        shp0_2=np.median(tni[ix0[1]:ix1,:,:],axis=0)/np.std(tni[:ix1,:,:],axis=0)

        #shp1=np.mean(tni[ix1:ix2,:,:],axis=0)/np.std(tni[ix1:ix2,:,:],axis=0)
        shp1=np.median(tni[ix1:ix2,:,:],axis=0)/np.std(tni[:ix2,:,:],axis=0)
        if i == nm:
            fp.write ("=== model\n")
            niname="model"
        else :
            niname=str(tshp_dict['spec'][model_idx][i%len(tshp_dict['spec'][model_idx])])+'_'+str(i)
            fp.write('=== ni(%d: %s)\n'%(i, niname))
        thres_idx=[]
        m2d = []
        m2d2 =[]
        for thres in thres_list:
            n0,n1=shp1.shape
            tix1=np.nonzero(shp0_1>thres)
            tix2=np.nonzero(shp0_2>thres)
            m1=np.mean(shp1)
            m2_1=0
            m2_2=0
            if len(tix1[0]) > 0:
                m2_1=np.mean(shp1[tix1])
            if len(tix2[0]) > 0:
                m2_2=np.mean(shp1[tix2])
            fp.write('   thres(%.3f):\t%.4f,%.4f(%02d%%),%.4f(%02d%%),%.4f,%.4f\n'\
                    %(thres,m1, m2_1, (200*len(tix1[0])+n0*n1)/(2*n0*n1),m2_2, (200*len(tix2[0])+n0*n1)/(2*n0*n1),m2_1-m1, m2_2-m1))

            idx_item = (thres, m2_1, m2_1-m1, tix1, m2_2, tix2)
            m2d.append (m2_1-m1)
            m2d2.append(m2_2-m1)
            thres_idx.append(idx_item)
        corr_m2d = np.corrcoef(thres_list[:-1],m2d[:-1])[0,1]
        corr_m2d2= np.corrcoef(thres_list[:-1],m2d2[:-1])[0,1]
        fp.write ("Corr: %.2f, %.2f\n"%(corr_m2d, corr_m2d2))
        ni_thres_idx.append((niname, copy.deepcopy(thres_idx), corr_m2d, corr_m2d2))
    if fp != sys.stdout:
        fp.close()
    return ni_thres_idx

def pick_ni(n,ni_thres_idx, corr_thres=0.1, shp_thres=0.01):
    """
    ni_thres_idx: [ (niname, [ ( thres, m2_1, m2_1d, tix1, m2_2, tix2 ) ], corrm21d, corrm22d ) ]
    return 
        [(i, shp_tbl, tix)] of the chosen tix with
    """
    picks = []
    for i, (niname, thres_idx, corr1, corr2) in enumerate(ni_thres_idx):
        #if corr1 < corr_thres:
        # demand both corr to be positive enough
        if corr1 < corr_thres or corr2 < corr_thres:
            continue

        pk=set()
        shp_tbl=np.zeros((n,n))
        for thres, m2, m2d, tix, m2_2, tix2 in thres_idx:
            if len(tix[0]) == 0:
                continue
            if m2 > shp_thres:
                # add the set
                ix0,ix1=tix
                for i0, i1 in zip(ix0,ix1):
                    pk.add(i0*n+i1)
                    shp_tbl[i0,i1]=m2
            elif m2 < shp_thres:
                # remove from the set
                ix0,ix1=tix
                for i0, i1 in zip(ix0,ix1):
                    try :
                        pk.remove(i0*n+i1)
                        shp_tbl[i0,i1]=0
                    except:
                        pass
        ix0=[]
        ix1=[]
        for ix in pk:
            ix0.append(ix//n)
            ix1.append(ix%n)
        picks.append((i, shp_tbl, (np.array(ix0), np.array(ix1))))

    return picks

def create_pop3():
    """
    just pick n9, n10 and n12 model, plus n7_zf_ng's zf and ng, n11
    """

def mask(tshp0, tshp_ni0, dix0, dix1, dix2):
    """get consistent score from period dix0 to dix1 and dix1 to dix2
    and see if this 
    """
def score01(tshp_mask0, tshp_ni_mask0, tshp1, tshp_ni1):
    pass

####
# Picking procedures
# 1. collect a set of potential models, i.e. n4_rt from release dill
# 2. for each model, 
#    2.1 get a shp_table for model
#    2.2 pick areas that are consistently good

def pick_search(tshp, tshp_ni, fdix0, fdix1, tdix0, tdix1):
    """
        ni_shp_pick = pick_search(tshp, tshp_ni, fdix0, fdix1, tdix0, tdix1)
    input: 
        fdix0/fdix1: fit day start/end index. fdix1 exclusive
        tdix0/tdix1: test day start/end index. tdix1 exclusive
    return:
        ni_shp_pick: shape(ni+1, n, n) pick of each fni, with last one
                     being the model (tshp)
    """
    pass





#def ni_pick(fni_array, shp_ni_array, lrf) :
    """
    generate fni, shp_ni for each of the fm and perform a ni pick for each
    nxnf

    fni_arr: shape (mc x ni, nndays, nf)
    shp_ni:  shape  (mc x ni, ndays,  n, nf)
    lrf:     shape (nndays, nf), ready to be multiplied by f for sharp

    return 
    niarr: shape (n,nf) of ni selection
    fni:  the final f
    shp_ni: the (n,nf) of sharp
    """

    # 1. for each nnf, pick a best positive shp f and iterate
    #    1. pick another and search for best weight
    #    2. if that improves the previous shp
    #    3. iterate until not improving
    # 2. scale the resulting f according to stdv

#    mcni, nndays, nf = fni_array.shape
#    mcni, ndays, n, nf = shp_ni_array.shape
#    w = []

#    ixd = np.arange(ndays).astype(int)
#    for k in np.arange(n).astype(int) :
#        for j in np.arange(nf).astype(int) :
#            # the bar k at forecast j
#            fni = fni_array[:,ixd+k,:]
#            shp = shp_ni_array[:,:,k,i]


