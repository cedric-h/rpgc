#!/bin/bash

if [ ! -d "build" ]; then
 mkdir build
fi

cd build

if [[ $OSTYPE == 'darwin'* ]]; then
  if [[ $(../sokol-tools-bin/bin/osx/sokol-shdc --input ../shaders.glsl --output shaders.glsl.h --slang metal_macos | tee /dev/tty) ]]; then
    exit 1
  fi
  
  gcc -g -O0 -ObjC ../main.c -Wall -Werror -framework QuartzCore -framework Cocoa -framework MetalKit -framework Metal -framework OpenGL -framework AudioToolbox
else
  if [[ $(../sokol-tools-bin/bin/linux/sokol-shdc --input ../shaders.glsl --output shaders.glsl.h --slang glsl330 | tee /dev/tty) ]]; then
    exit 1
  fi
  
clang -fsanitize=undefined -g -O0 -L/usr/lib -lX11 -lXi -lXcursor -lGL -ldl -lm -lpthread ../main.c
#  gcc -g -O0 ../main.c -Wall -Werror -lX11 -lXi -lXcursor -lGL -ldl -lm -lpthread
fi
