#!/bin/bash
set -euo pipefail

uefi_root=$(cd "$(dirname "$0")/../.." && pwd)
output_dir="$uefi_root/Build/gts7fewifiPkg/recovery-packages"
stock_recovery=${T733_STOCK_RECOVERY:-}
payload_image=${1:-}
artifact_tag=${2:-}
recovery_image="$uefi_root/Mu-gts7fewifi.img"
python_command=${PYTHON:-python3}

if [ -z "$payload_image" ] || [ -z "$stock_recovery" ]; then
    printf 'Usage: T733_STOCK_RECOVERY=/path/recovery.img bash %s PAYLOAD_IMG [TAG]\n' "$0" >&2
    exit 1
fi

for input_image in "$payload_image" "$stock_recovery"; do
    if [ ! -r "$input_image" ]; then
        printf 'Cannot read input image: %s\n' "$input_image" >&2
        exit 1
    fi
done

if [ -n "$artifact_tag" ]; then
    output_dir="$output_dir-$artifact_tag"
    recovery_image="$uefi_root/Mu-gts7fewifi-$artifact_tag.img"
fi

if [ -e "$output_dir" ]; then
    printf 'Output directory already exists; select a new tag: %s\n' "$output_dir" >&2
    exit 1
fi

mkdir -p "$output_dir/uefi" "$output_dir/stock"
"$python_command" "$uefi_root/Resources/Scripts/append_gts7fewifi_payload.py" \
    "$recovery_image" "$payload_image" "$output_dir/uefi/recovery.img"
cp "$stock_recovery" "$output_dir/stock/recovery.img"

# Samsung's flashing client selects the recovery partition by this member name.
COPYFILE_DISABLE=1 tar --format=ustar -cf "$output_dir/gts7fewifi-uefi-recovery.tar" \
    -C "$output_dir/uefi" recovery.img
COPYFILE_DISABLE=1 tar --format=ustar -cf "$output_dir/gts7fewifi-stock-recovery.tar" \
    -C "$output_dir/stock" recovery.img

printf 'UEFI recovery package: %s\n' "$output_dir/gts7fewifi-uefi-recovery.tar"
printf 'Stock recovery package: %s\n' "$output_dir/gts7fewifi-stock-recovery.tar"
