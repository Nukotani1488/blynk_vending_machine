# merge_wokwi.py
Import("env")
import subprocess

def merge_for_wokwi(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    subprocess.run([
        "python", "-m", "esptool",
        "--chip", "esp32",
        "merge-bin",
        "-o", f"{build_dir}/merged-firmware.bin",
        "--flash-mode", "dio",
        "--flash-freq", "40m",
        "--flash-size", "4MB",
        "0x1000",  f"{build_dir}/bootloader.bin",
        "0x8000",  f"{build_dir}/partitions.bin",
        "0x10000", f"{build_dir}/firmware.bin",
        "0x210000", f"{build_dir}/littlefs.bin",
    ], check=True)

env.AddPostAction("$BUILD_DIR/firmware.bin", merge_for_wokwi)