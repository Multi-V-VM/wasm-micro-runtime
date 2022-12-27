#!/usr/bin/env python3
#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#

"""
The script operates on such directories and files
|-- core
|   `-- deps
|       |-- emscripten
|       `-- wasi-sdk
|           `-- src
|               |-- llvm-project
|               `-- wasi-libc
`-- test-tools
    |-- build-wasi-sdk
    |   |-- build_wasi_sdk.py
    |   |-- include
    |   `-- patches
    `-- wasi-sdk
        |-- bin
        |-- lib
        `-- share
            `-- wasi-sysroot
"""

import hashlib
import logging
import os
import pathlib
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib
import urllib.request

logger = logging.getLogger("build_wasi_sdk")

external_repos = {
    "config": {
        "sha256": "6e29664a65277c10f73682893ad12a52e8ce8051a82ae839581d20c18da0d2cc",
        "store_dir": "core/deps/wasi-sdk/src/config",
        "strip_prefix": "config-c179db1b6f2ae484bfca1e9f8bae273e3319fa7d",
        "url": "https://git.savannah.gnu.org/cgit/config.git/snapshot/config-c179db1b6f2ae484bfca1e9f8bae273e3319fa7d.tar.gz",
    },
    "emscripten": {
        "sha256": "0f8b25cac5b2a55007a45c5bfa2b918add1df90bf624bb47510d8bc887c39901",
        "store_dir": "core/deps/emscripten",
        "strip_prefix": "emscripten-3.1.28",
        "url": "https://github.com/emscripten-core/emscripten/archive/refs/tags/3.1.28.tar.gz",
    },
    "llvm-project": {
        "sha256": "97db80c61c10ad7ffaffa2c14b413fb6e6537a523b574fd953c2ccd9be68d8bc",
        "store_dir": "core/deps/wasi-sdk/src/llvm-project",
        "strip_prefix": "llvm-project-088f33605d8a61ff519c580a71b1dd57d16a03f8",
        "url": "https://github.com/llvm/llvm-project/archive/088f33605d8a61ff519c580a71b1dd57d16a03f8.tar.gz",
    },
    "wasi-sdk": {
        "sha256": "0bccaaa16dfdf006ea4f704ac749db65eba701b382e38a2b152f1d6f2b54bc75",
        "store_dir": "core/deps/wasi-sdk",
        "strip_prefix": "wasi-sdk-b738c9d5530402ca145f2be495cda65b1e2a5389",
        "url": "https://github.com/WebAssembly/wasi-sdk/archive/b738c9d5530402ca145f2be495cda65b1e2a5389.tar.gz",
    },
    "wasi-libc": {
        "sha256": "0f4c49e34dfd0d9ec4822f3422aff019c6cfd18ac652b86092c4459a10eef5bc",
        "store_dir": "core/deps/wasi-sdk/src/wasi-libc",
        "strip_prefix": "wasi-libc-a00bf321eeeca836ee2a0d2d25aeb8524107b8cc",
        "url": "https://github.com/WebAssembly/wasi-libc/archive/a00bf321eeeca836ee2a0d2d25aeb8524107b8cc.tar.gz",
    },
}

# TOOD: can we use headers from wasi-libc and clang directly ?
emscripten_headers_src_dst = [
    ("include/compat/emmintrin.h", "sse/emmintrin.h"),
    ("include/compat/immintrin.h", "sse/immintrin.h"),
    ("include/compat/smmintrin.h", "sse/smmintrin.h"),
    ("include/compat/xmmintrin.h", "sse/xmmintrin.h"),
    ("lib/libc/musl/include/pthread.h", "libc/musl/pthread.h"),
    ("lib/libc/musl/include/signal.h", "libc/musl/signal.h"),
    ("lib/libc/musl/include/netdb.h", "libc/musl/netdb.h"),
    ("lib/libc/musl/include/sys/wait.h", "libc/musl/sys/wait.h"),
    ("lib/libc/musl/include/sys/socket.h", "libc/musl/sys/socket.h"),
    ("lib/libc/musl/include/setjmp.h", "libc/musl/setjmp.h"),
    ("lib/libc/musl/arch/emscripten/bits/setjmp.h", "libc/musl/bits/setjmp.h"),
]


def checksum(name, local_file):
    sha256 = hashlib.sha256()
    with open(local_file, "rb") as f:
        bytes = f.read(4096)
        while bytes:
            sha256.update(bytes)
            bytes = f.read(4096)

    return sha256.hexdigest() == external_repos[name]["sha256"]


def download(url, local_file):
    logger.debug(f"download from {url}")
    urllib.request.urlretrieve(url, local_file)
    return local_file.exists()


def unpack(tar_file, strip_prefix, dest_dir):
    # extract .tar.gz to /tmp, then move back without strippred prefix directories
    with tempfile.TemporaryDirectory() as tmp:
        with tarfile.open(tar_file) as tar:
            logger.debug(f"extract to {tmp}")
            def is_within_directory(directory, target):
                
                abs_directory = os.path.abspath(directory)
                abs_target = os.path.abspath(target)
            
                prefix = os.path.commonprefix([abs_directory, abs_target])
                
                return prefix == abs_directory
            
            def safe_extract(tar, path=".", members=None, *, numeric_owner=False):
            
                for member in tar.getmembers():
                    member_path = os.path.join(path, member.name)
                    if not is_within_directory(path, member_path):
                        raise Exception("Attempted Path Traversal in Tar File")
            
                tar.extractall(path, members, numeric_owner=numeric_owner) 
                
            
            safe_extract(tar, tmp)

        strip_prefix_dir = (
            pathlib.Path(tmp).joinpath(strip_prefix + os.path.sep).resolve()
        )
        if not strip_prefix_dir.exists():
            logger.error(f"extract {tar_file.name} failed")
            return False

        # mv /tmp/${strip_prefix} dest_dir/*
        logger.debug(f"move {strip_prefix_dir} to {dest_dir}")
        shutil.copytree(
            str(strip_prefix_dir),
            str(dest_dir),
            copy_function=shutil.move,
            dirs_exist_ok=True,
        )

    return True


def download_repo(name, root):
    if not name in external_repos:
        logger.error(f"{name} is not a known repository")
        return False

    store_dir = root.joinpath(f'{external_repos[name]["store_dir"]}').resolve()
    download_flag = store_dir.joinpath("DOWNLOADED")
    if store_dir.exists() and download_flag.exists():
        logger.info(
            f"keep using '{store_dir.relative_to(root)}'. Or to remove it and try again"
        )
        return True

    # download only when the target is neither existed nor broken
    download_dir = pathlib.Path("/tmp/build_wasi_sdk/")
    download_dir.mkdir(exist_ok=True)

    tar_name = pathlib.Path(external_repos[name]["url"]).name
    tar_file = download_dir.joinpath(tar_name)
    if tar_file.exists():
        if checksum(name, tar_file):
            logger.debug(f"use pre-downloaded {tar_file}")
        else:
            logger.debug(f"{tar_file} is broken, remove it")
            tar_file.unlink()

    if not tar_file.exists():
        if not download(external_repos[name]["url"], tar_file) or not checksum(
            name, tar_file
        ):
            logger.error(f"download {name} failed")
            return False

    # unpack and removing *strip_prefix*
    if not unpack(tar_file, external_repos[name]["strip_prefix"], store_dir):
        return False

    # leave a FLAG
    download_flag.touch()

    # leave download files in /tmp
    return True


def run_patch(patch_file, cwd):
    if not patch_file.exists():
        logger.error(f"{patch_file} not found")
        return False

    with open(patch_file, "r") as f:
        try:
            PATCH_DRY_RUN_CMD = "patch -f -p1 --dry-run"
            if subprocess.check_call(shlex.split(PATCH_DRY_RUN_CMD), stdin=f, cwd=cwd):
                logger.error(f"patch dry-run {cwd} failed")
                return False

            PATCH_CMD = "patch -f -p1"
            f.seek(0)
            if subprocess.check_call(shlex.split(PATCH_CMD), stdin=f, cwd=cwd):
                logger.error(f"patch {cwd} failed")
                return False
        except subprocess.CalledProcessError:
            logger.error(f"patch {cwd} failed")
            return False
    return True


def build_and_install_wasi_sdk(root):
    store_dir = root.joinpath(f'{external_repos["wasi-sdk"]["store_dir"]}').resolve()
    if not store_dir.exists():
        logger.error(f"{store_dir} does not found")
        return False

    # patch wasi-libc and wasi-sdk
    patch_flag = store_dir.joinpath("PATCHED")
    if not patch_flag.exists():
        if not run_patch(
            root.joinpath("test-tools/build-wasi-sdk/patches/wasi_libc.patch"),
            store_dir.joinpath("src/wasi-libc"),
        ):
            return False

        if not run_patch(
            root.joinpath("test-tools/build-wasi-sdk/patches/wasi_sdk.patch"), store_dir
        ):
            return False

        patch_flag.touch()
    else:
        logger.info("bypass the patch phase")

    # build
    build_flag = store_dir.joinpath("BUILDED")
    if not build_flag.exists():
        BUILD_CMD = "make build"
        if subprocess.check_call(shlex.split(BUILD_CMD), cwd=store_dir):
            logger.error(f"build wasi-sdk failed")
            return False

        build_flag.touch()
    else:
        logger.info("bypass the build phase")

    # install
    install_flag = store_dir.joinpath("INSTALLED")
    binary_path = root.joinpath("test-tools").resolve()
    if not install_flag.exists():
        shutil.copytree(
            str(store_dir.joinpath("build/install/opt").resolve()),
            str(binary_path),
            dirs_exist_ok=True,
        )

        # install headers
        emscripten_headers = (
            root.joinpath(external_repos["emscripten"]["store_dir"])
            .joinpath("system")
            .resolve()
        )
        wasi_sysroot_headers = binary_path.joinpath(
            "wasi-sdk/share/wasi-sysroot/include"
        ).resolve()
        for (src, dst) in emscripten_headers_src_dst:
            src = emscripten_headers.joinpath(src)
            dst = wasi_sysroot_headers.joinpath(dst)
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(src, dst)

        install_flag.touch()
    else:
        logger.info("bypass the install phase")

    return True


def main():
    console = logging.StreamHandler()
    console.setFormatter(logging.Formatter("%(asctime)s - %(message)s"))
    logger.setLevel(logging.INFO)
    logger.addHandler(console)
    logger.propagate = False

    # locate the root of WAMR
    current_file = pathlib.Path(__file__)
    if current_file.is_symlink():
        current_file = pathlib.Path(os.readlink(current_file))
    root = current_file.parent.joinpath("../..").resolve()
    logger.info(f"The root of WAMR is {root}")

    # download repos
    for repo in external_repos.keys():
        if not download_repo(repo, root):
            return False

    # build wasi_sdk and install
    if not build_and_install_wasi_sdk(root):
        return False

    # TODO install headers from emscripten

    return True


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
