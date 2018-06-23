#!/usr/bin/bash
cd /cygdrive/e
rsync -avz research /cygdrive/f/research --exclude=log --exclude=.git
rsync -avz ib/kisco /cygdrive/f/kisco --exclude=ib/kisco/log --exclude=.git 
