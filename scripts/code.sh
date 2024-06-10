#!/bin/bash

read -p "请输入数字: " input

output=""

# 使用循环遍历输入数字的每个字符
for ((i=0; i<${#input}; i++)); do
    digit="${input:i:1}"

    # 在每两个数字之前添加'0x'
    if (( i % 2 == 0 )); then
        output+="0x"
    fi

    # 添加当前数字
    output+="$digit"

    # 在第二个数字后面添加','
    if (( (i + 1) % 2 == 0 )); then
        output+=","
    fi
done

echo "$output"
