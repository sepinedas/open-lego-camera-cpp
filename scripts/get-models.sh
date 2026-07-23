#!/usr/bin/env bash
# Fetch the facial-landmark model the dog filter needs. The Haar face detector
# ships with OpenCV (libopencv-dev), so only the LBF landmark model is fetched.
set -euo pipefail

dir="$(cd "$(dirname "$0")/.." && pwd)/models"
mkdir -p "$dir"
out="$dir/lbfmodel.yaml"
# Canonical LBF model from the OpenCV GSoC 2017 project (~54 MB).
url="https://github.com/kurnianggoro/GSOC2017/raw/master/data/lbfmodel.yaml"

if [ -f "$out" ]; then
  echo "already present: $out"
  exit 0
fi

echo "downloading LBF landmark model -> $out"
if command -v curl >/dev/null 2>&1; then
  curl -L --fail -o "$out" "$url"
elif command -v wget >/dev/null 2>&1; then
  wget -O "$out" "$url"
else
  echo "need curl or wget" >&2
  exit 1
fi

echo "done. Enable the filter with:  build/open-lego-camera --filter"
