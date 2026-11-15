#!/bin/bash
# Usage: ./build.sh kernel       -> boot.sd with the vcamera kernel config
#        ./build.sh image        -> full SD image with the small core reading the mic

VCAMERA_DIR=$(cd "$(dirname "$0")" && pwd)
REPOSITORY_DIR=$(dirname "$VCAMERA_DIR")
SDK_DIR=$REPOSITORY_DIR/duo-buildroot-sdk
BOARD=milkv-duo-sd
LINUX_DEFCONFIG=$SDK_DIR/build/boards/cv180x/cv1800b_milkv_duo_sd/linux/cvitek_cv1800b_milkv_duo_sd_defconfig

# duo-examples/envsetup.sh (loaded by .envrc) exports these for userland builds; the SDK uses CHIP itself
unset CC CFLAGS LDFLAGS CHIP TOOLCHAIN_PREFIX

fail()
{
    echo "build.sh: $*" >&2
    exit 1
}

prepareSdk()
{
    git -C "$REPOSITORY_DIR" submodule update --init duo-buildroot-sdk || fail "cannot fetch the SDK"
    if [ ! -d "$SDK_DIR/host-tools" ]; then
        git clone --depth 1 https://github.com/milkv-duo/host-tools.git "$SDK_DIR/host-tools" || fail "cannot fetch host-tools"
    fi

    # Applying twice must be a no-op so the script can be rerun
    for patch in "$VCAMERA_DIR"/sdk/*.patch; do
        if ! git -C "$SDK_DIR" apply --reverse --check "$patch" 2>/dev/null; then
            git -C "$SDK_DIR" apply "$patch" || fail "$(basename "$patch") does not apply"
        fi
    done
    while read -r line; do
        if ! grep -qxF "$line" "$LINUX_DEFCONFIG"; then
            echo "$line" >> "$LINUX_DEFCONFIG"
        fi
    done < "$VCAMERA_DIR/sdk/linux.config"
}

buildKernel()
{
    cd "$SDK_DIR" || fail "no SDK"
    source build/envsetup_milkv.sh list || fail "cannot load the SDK environment"
    check_board "$BOARD" || fail "cannot select $BOARD"
    # pack_boot runs u-boot's mkimage, which only exists after a u-boot build
    (_build_uboot_env && cd "$BUILD_PATH" && make u-boot-build) || fail "u-boot build failed"
    build_kernel || fail "kernel build failed"
    echo "built $OUTPUT_DIR/rawimages/boot.sd"
}

buildImage()
{
    export VCAMERA_DIR
    cd "$SDK_DIR" && ./build.sh "$BOARD" || fail "image build failed"
}

case "$1" in
    kernel)
        prepareSdk
        buildKernel
        ;;
    image)
        prepareSdk
        buildImage
        ;;
    *)
        fail "usage: $0 kernel | image"
        ;;
esac
