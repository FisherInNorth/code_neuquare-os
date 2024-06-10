#!/bin/bash

./test

# 获取程序的退出状态码
exit_status=$?

# 检查退出状态码是否为8，表示成功
if [ $exit_status -eq 8 ]; then
    echo "Success!"
else
    echo "Program exited with non-zero status: $exit_status"
fi