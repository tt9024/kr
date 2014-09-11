#!/bin/bash

set -x 


TESTCSVFN=data/EURUSD/IB/2014/day.279.30.csv

bin/testClient.py $TESTCSVFN 

