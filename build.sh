#!/run/current-system/sw/bin/bash

export GLFW_PLATFORM=x11

mkdir -p build
cd build/
g++ ../src/handmade.cpp -o handmade -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 && \
cd ../ && \
./build/handmade
