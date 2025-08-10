#!/run/current-system/sw/bin/bash

mkdir -p build
cd build/
g++ ../src/linux_handmade.cpp -o handmade -g -O0 -Wall -lX11 -levdev -lasound && \
cd ../ && \
./build/handmade
