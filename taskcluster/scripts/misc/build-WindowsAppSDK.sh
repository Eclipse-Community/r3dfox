#!/bin/bash
set -x -e -v

artifact=$(basename "$TOOLCHAIN_ARTIFACT")
target_folder_name=${artifact%.tar.*}
ARCH_SUFFIX=$1
SHORT_VERSION=$2

cd $MOZ_FETCHES_DIR
unzip MSIX/win10-${ARCH_SUFFIX}/Microsoft.WindowsAppRuntime.${SHORT_VERSION}.msix

mkdir $target_folder_name
# We just extract the few DLLs that Firefox needs instead of the
# whole SDK.
cp CoreMessagingXP.dll $target_folder_name
cp marshal.dll $target_folder_name
cp Microsoft.InputStateManager.dll $target_folder_name
cp Microsoft.Internal.FrameworkUdk.dll $target_folder_name
cp Microsoft.UI.Composition.OSSupport.dll $target_folder_name
cp Microsoft.UI.Input.dll $target_folder_name
cp Microsoft.UI.Windowing.Core.dll $target_folder_name
cp Microsoft.UI.Windowing.dll $target_folder_name

mkdir -p $UPLOAD_DIR

tar -caf $target_folder_name.tar.zst $target_folder_name
mv $target_folder_name.tar.zst $UPLOAD_DIR