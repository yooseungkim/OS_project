# Dockerfile
FROM --platform=linux/386 ubuntu:14.04

COPY ./config/.bashrc /root/.bashrc
COPY ./config/.vimrc /root/.vimrc

ENV PATH="/root/pintos/src/utils:${PATH}"

WORKDIR /root/pintos