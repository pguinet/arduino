# Script PlatformIO (extra_scripts, post) : apres chaque build, fusionne
# bootloader + partitions + boot_app0 + application en un seul binaire,
# a flasher a l'offset 0x0 depuis un flasheur web (esptool-js dans Chrome).
#
# Sortie : firmware/Claude_Dashboard_merged.bin (ignore par git : il contient
# les identifiants de credentials.h).

import os

Import("env")  # noqa: F821 (fourni par SCons)

MERGED_NAME = "Claude_Dashboard_merged.bin"


def merge_firmware(source, target, env):
    out_dir = os.path.join(env.subst("$PROJECT_DIR"), "firmware")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, MERGED_NAME)

    images = []
    for offset, image in env.get("FLASH_EXTRA_IMAGES", []):
        images += [offset, env.subst(image)]
    images += [env.subst("$ESP32_APP_OFFSET"), str(target[0])]

    esptool = os.path.join(
        env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
    cmd = [
        env.subst("$PYTHONEXE"), esptool,
        "--chip", env.BoardConfig().get("build.mcu"),
        "merge_bin", "-o", out,
        # Meme mode que l'upload PlatformIO : qio -> dio dans l'en-tete, la
        # ROM ne sait pas charger le bootloader en QIO (boucle ets_loader.c 78).
        "--flash_mode", env.subst("${__get_board_flash_mode(__env__)}"),
        "--flash_freq", env.subst("${__get_board_f_image(__env__)}"),
        "--flash_size", env.BoardConfig().get("upload.flash_size"),
    ] + images
    env.Execute(" ".join('"%s"' % c for c in cmd))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_firmware)  # noqa: F821
