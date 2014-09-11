#!/bin/bash

set -x 

CONFFILE=model/conf/EURUSD.IB.20140214.conf
TESTDIR=data/EURUSD/IB/2014

PROPFILE=`grep propFileName $CONFFILE | perl -ane 'chomp;s/.*=//;s/\s//g;print'`;
MODELFILE=`grep modelFileName $CONFFILE | perl -ane 'chomp;s/.*=//;s/\s//g;print'`;

TIMESTAMP=`date "+%s"`

OUTFILE=test/ClassifierDecoder.model.EURUSD.IB.2013.day.all.30.out
for (( i=260; i<280; i++ )); do
    TESTFILE=$TESTDIR/day.$i.30.dat;
    TESTCMD="java -cp lib/java/stanford-classifier-3.3.0.jar:lib/java/fxstms.jar ClassifierDecoder $PROPFILE $MODELFILE $TESTFILE";
    $TESTCMD;
done > $OUTFILE 2>&1

grep -P '\t0.0\t' $OUTFILE | lib/perl/computeGain.pl $CONFFILE


###grep -P '\t0.0\t' $OUTFILE | perl -ne '$probthreshold=$ENV{"probthreshold"};$action=$ENV{"action"};$c=0;$r=0;$g=0;while(<>){chomp;@t=split(/\s+/,$_);if($t[22] eq $action && $t[23] > $probthreshold){$c++; if($t[0] eq $action){$r++;} $g += $t[21];};} if($c == 0){print "$r/$c $g\n"}else{ $g2=$g-$c*0.00004; print "$r/$c ", sprintf("%.4f", $r/$c)," $g $g2\n"}' 


