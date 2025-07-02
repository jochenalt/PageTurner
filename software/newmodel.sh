#!/usr/bin/env bash
echo on

  
./unpacklibraries.sh -a model/ei-pageturner* -c model/pageturner*
cd PC/inference
make clean
make -j
