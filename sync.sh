#!/usr/bin/bash

echo "find unzip'ed hist file and gzip them"
cd /cygdrive/e/ib/kisco
for f in `find hist -name *.csv -print` ; do echo $f ; gzip $f ; done
 
cd /cygdrive/e
rsync -avz research/ /cygdrive/f/research --exclude=log --exclude=.git --delete --exclude=*.pyc
rsync -avz ib/kisco/ /cygdrive/f/kisco --exclude=log --exclude=.git --delete --exclude=*.pyc

