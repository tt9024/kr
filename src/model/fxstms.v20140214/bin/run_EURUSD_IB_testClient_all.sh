#!/bin/bash

set -x 
TESTDIR=data/EURUSD/IB
OUTFILE=test/2014_trade_all.out

for (( i=1; i<260; i++ )); do
    TESTFILE=$TESTDIR/2013/day.$i.csv;
    bin/testClient.py $TESTFILE
done > $OUTFILE 2>&1


for (( i=260; i<280; i++ )); do
    TESTFILE=$TESTDIR/2014/day.$i.30.csv;
    bin/testClient.py $TESTFILE
done >> $OUTFILE 2>&1

