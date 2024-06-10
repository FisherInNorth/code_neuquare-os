#!/bin/bash

# 切换到目标目录
cd ./apps
# make busybox 
make all
cd -

# 执行make命令
make image
make
