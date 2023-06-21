# Plays video frames in terminal

- downscales frames to match terminal size
- does not use ncurses
- descent fps

## How to use

Program takes one command line argument: directory containing frames.

The directory contains
1. bmp files of same sizes
2. `index.txt`

Video frames can be generated with
```sh
ffmpeg -i [video] -pix_fmt bgr24 [frames directory]/%d.bmp
```

`index.txt` is a comma separated list that has fields
1. size_t number of frames
2. float fps
3. size_t frame width
4. size_t frame height

Compile program with
```sh
make
```

and run with
```sh
make run ARGS="[directory]"
```
or
```sh
./target/main [directory]
```
