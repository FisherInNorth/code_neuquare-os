import time
import pyautogui

def read_and_write_text(file_path, seconds):
    with open(file_path, 'r') as file:
        lines = file.readlines()

    time.sleep(seconds)
    for line in lines:
        # 移动到光标当前位置
        pyautogui.moveTo(pyautogui.position().x, pyautogui.position().y)
        # 模拟按键粘贴文本
        pyautogui.typewrite(line.strip())
        # 模拟敲击回车键
        pyautogui.press('enter')  
        # 等待
        # time.sleep(seconds)

file_path = './sifive_gdb_cmd.txt'
# file_path = './qemu_gdb_cmd.txt'
read_and_write_text(file_path, 2)