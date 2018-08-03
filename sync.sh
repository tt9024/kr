#!/usr/bin/bash
cd /cygdrive/e
rsync -avz research /cygdrive/f/research --exclude=log --exclude=.git --delete
rsync -avz ib/kisco /cygdrive/f/kisco --exclude=log --exclude=.git --delete

