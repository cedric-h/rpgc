# goal
sup nerds. this is a rewrite in C of [a game i wrote in rust a while ago](https://github.com/cedric-h/rpg). trying to figure out how much smaller C WASM bundles can be in a much more real-world way. also trying to figure out why it is C feels so much more ergonomic to write. figuring out a real world something and figuring out something about myself along the way. dope.

## deps
`sudo apt-get install libgl-dev libx11-dev libxi-dev libxcursor-dev`

`git submodule update --init --depth=1`

`python3 json2flat.py`

## run
`./bake.sh && ./build/a.out`

## watch
`ls *.c | entr -s 'echo && ./bake.sh && ./build/a.out'`
