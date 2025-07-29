#!/run/current-system/sw/bin/bash

mkdir -p build
cd build/
g++ ../src/handmade.cpp -o handmade -g -lX11 -levdev && \
cd ../ && \
./build/handmade
