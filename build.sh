#!/run/current-system/sw/bin/bash

mkdir -p build
pushd build/
g++ ../src/linux_handmade.cpp -o handmade -g -O0 -Wall -lX11 -levdev -lasound && \
popd && \
./build/handmade
