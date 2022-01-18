#!/bin/bash

if [ ! -d "build" ]; then
 mkdir build
fi

cd build

if [[ $(../sokol-tools-bin/bin/linux/sokol-shdc --input ../shaders.glsl --output shaders.glsl.h --slang glsl330 | tee /dev/tty) ]]; then
  exit 1
fi

# clang -fsanitize=undefined -g -O0 -L/usr/lib -lX11 -lXi -lXcursor -lGL -lasound -ldl -lm -lpthread ../main.c
gcc -g -O3 ../main.c -Wall -Werror -lX11 -lXi -lXcursor -lGL -ldl -lm -lpthread
