# GIST Operating System

# Teammates

Kim Yooseung, Noh Janghan 

# How to Run

## Build Image

On this directory (where `Dockerfile` is located)

```
docker build -t pintos-env .
```

## Run container

```
docker run -it --privileged --name pintos \
  -v $(pwd)/pintos:/root/pintos \
  -v $(pwd)/bochs-2.6.2:/root/bochs-2.6.2 \
  -w /root/pintos \
  pintos-env /bin/bash

source ~/.bashrc
```

## Install Dependencies

```
sudo apt-get update && apt-get install -y \
    build-essential \
    gcc-4.4 g++ \
    qemu wget tar vim \
    libx11-dev libxpm-dev 

ln -sf /usr/bin/gcc-4.4 /usr/bin/gcc
```

## Make

On `pintos/src/utils` and `pintos/src/threads`

```
cd ~/pintos/src/utils
make clean
make 

cd ~/pintos/src/threads
make clean
make

cd ~
```

## Test

```
pintos -q run alarm-multiple
```

