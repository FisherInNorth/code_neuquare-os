import matplotlib.pyplot as plt
import numpy as np

def visualize_data(filename):
    sys_calls = []
    cnt = []
    time = []
    mm = []

    # 从文件中读取数据
    with open(filename, 'r') as file:
        lines = file.readlines()
        for line in lines:
            line = line.strip()
            if line.startswith("sys_"):
                parts = line.split(",")
                sys_calls.append(parts[0].strip())
                cnt.append(int(parts[1].split(":")[1].strip()))
                time.append(int(parts[2].split(":")[1].strip().split(" ")[0]))
                mm.append(int(parts[3].split(":")[1].strip().split(" ")[0]))

    # 按照次数从大到小排序
    sorted_indices_cnt = np.argsort(cnt)[::-1]
    sys_calls_sorted_cnt = [sys_calls[i] for i in sorted_indices_cnt]
    cnt_sorted = [cnt[i] for i in sorted_indices_cnt]

    # 绘制系统调用名-次数图表
    plt.figure(figsize=(12, 6))
    plt.bar(sys_calls_sorted_cnt, cnt_sorted)
    plt.xlabel('System Calls')
    plt.ylabel('Count')
    plt.title('System Calls - Count')
    plt.xticks(rotation=90)
    plt.tight_layout()
    plt.show()

    # 按照时间从大到小排序
    sorted_indices_time = np.argsort(time)[::-1]
    sys_calls_sorted_time = [sys_calls[i] for i in sorted_indices_time]
    time_sorted = [time[i] for i in sorted_indices_time]

    # 绘制系统调用名-时间图表
    plt.figure(figsize=(12, 6))
    plt.bar(sys_calls_sorted_time, time_sorted)
    plt.xlabel('System Calls')
    plt.ylabel('Time')
    plt.title('System Calls - Time')
    plt.xticks(rotation=90)
    plt.tight_layout()
    plt.show()

    # 按照花费的物理页数量从大到小排序
    sorted_indices_mm = np.argsort(mm)[::-1]
    sys_calls_sorted_mm = [sys_calls[i] for i in sorted_indices_mm]
    mm_sorted = [mm[i] for i in sorted_indices_mm]

    # 绘制系统调用名-花费的物理页数量图表
    plt.figure(figsize=(12, 6))
    plt.bar(sys_calls_sorted_mm, mm_sorted)
    plt.xlabel('System Calls')
    plt.ylabel('Memory Pages')
    plt.title('System Calls - Memory Pages')
    plt.xticks(rotation=90)
    plt.tight_layout()
    plt.show()

# 输入文件名并进行可视化
filename = "syscall_result.txt"
visualize_data(filename)