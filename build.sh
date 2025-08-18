#!/run/current-system/sw/bin/bash

mkdir -p build
pushd build/ > /dev/null
# Add -m32 to compile for 32bits
clang++ ../src/linux_handmade.cpp -DHANDMADE_SLOW -DHANDMADE_INTERNAL -std=c++17 -o handmade -g3 -O0 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wno-gnu-anonymous-struct -Wno-nested-anon-types -lX11 -levdev -lasound && \
./handmade
popd > /dev/null
