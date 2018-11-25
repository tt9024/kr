#!/usr/bin/bash

echo "find unzip'ed hist file and gzip them"
cd /cygdrive/e/ib/kisco
for f in `find hist -name *.csv -print` ; do echo $f ; gzip $f ; done
 
cd /cygdrive/e
#rsync -avz research/ bfu@192.168.1.235:/media/bfu/backup_2/research --exclude=log --exclude=.git --delete --exclude=*.pyc --exclude=repo*
#rsync -avz ib/kisco/hist/ bfu@192.168.1.235:/media/bfu/backup_2/kisco/hist --exclude=log --exclude=.git --delete --exclude=*.pyc --exclude=repo*
#rsync -avz ib/kisco/bar/ bfu@192.168.1.235:/media/bfu/backup_2/kisco/bar --exclude=log --exclude=.git --delete --exclude=*.pyc --exclude=repo*

rsync -avz ib/kisco/hist/ /cygdrive/c/zfu/kisco/hist --exclude=log --exclude=.git --exclude=*.pyc --exclude=repo*
rsync -avz ib/kisco/bar/  /cygdrive/c/zfu/kisco/bar  --exclude=log --exclude=.git --exclude=*.pyc --exclude=repo*
rsync -avz research/kdb/repo/ /cygdrive/c/zfu/kisco/repo

