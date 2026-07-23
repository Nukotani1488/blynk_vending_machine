Import("env")
import subprocess

def build_and_merge(source, target, env):
    build_dir = env.subst("$BUILD_DIR")

    # Build the filesystem image using the same environment, without recursing into "pio run"
    env.Execute("$PYTHONEXE -m platformio run -e esp32dev -t buildfs --disable-auto-clean")

    subprocess.run([
        "python", "-m", "esptool",
        "--chip", "esp32",
        "merge_bin",
        "-o", f"{build_dir}/merged-firmware.bin",
        "--flash_mode", "dio",
        "--flash_freq", "40m",
        "--flash_size", "4MB",
        "0x1000",  f"{build_dir}/bootloader.bin",
        "0x8000",  f"{build_dir}/partitions.bin",
        "0x10000", f"{build_dir}/firmware.bin",
        "0x210000", f"{build_dir}/littlefs.bin",
    ], check=True)

env.AddPostAction("$BUILD_DIR/firmware.bin", build_and_merge)