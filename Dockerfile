# Dockerfile
FROM --platform=linux/386 ubuntu:14.04

COPY ./config/.bashrc /root/.bashrc
COPY ./config/.vimrc /root/.vimrc

RUN source ~/.bashrc
WORKDIR /root/pintos