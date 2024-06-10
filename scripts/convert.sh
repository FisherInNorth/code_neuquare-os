#!/bin/bash

# 从标准输入读取内容，将多行转为单行，并去除空格
content=$(tr -d '\n' | tr -d ' ' )

echo "$content"
