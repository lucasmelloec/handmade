#!/run/current-system/sw/bin/bash

mkdir -p build
pushd build/
g++ ../src/linux_handmade.cpp -DHANDMADE_SLOW -DHANDMADE_INTERNAL -o handmade -g -O0 -Wall -lX11 -levdev -lasound && \
popd && \
./build/handmade
