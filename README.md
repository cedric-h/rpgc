![image](https://github.com/cedric-h/rpgc/assets/25539554/7e685f20-5c93-47f5-92b3-46aae3ef0824)
![image](https://github.com/cedric-h/rpgc/assets/25539554/c35e5f1b-2596-4293-b766-954d7185e785)


# goal
sup nerds. this is a rewrite in C of [a game i wrote in rust a while ago](https://github.com/cedric-h/rpg).

## deps
`sudo apt-get install libgl-dev libx11-dev libxi-dev libxcursor-dev`

`git submodule update --init --depth=1`

`python3 json2flat.py`

## run
`./bake.sh && ./build/a.out`

## watch
`ls *.c | entr -s 'echo && ./bake.sh && ./build/a.out'`
