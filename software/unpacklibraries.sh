#!/usr/bin/env bash
set -euo pipefail

# Print usage/help
usage() {
  cat <<EOF
Usage: $0 [-c <cpp-library.zip>] [-a <arduino-library.zip>]

Options:
  -c <cpp-library.zip>       Path to the C++ library ZIP (unpacks to ./PC/ei_cpp_library)
  -a <arduino-library.zip>   Path to the Arduino library ZIP (only its src/ folder, unpacks to ./uC/lib/ei_arduino_library)
  -h                         Show this help message
EOF
}

CPP_ZIP=""
ARDUINO_ZIP=""

# Parse options
while getopts "c:a:h" opt; do
  case "$opt" in
    c) CPP_ZIP="$OPTARG" ;;
    a) ARDUINO_ZIP="$OPTARG" ;;
    h) usage; exit 0 ;;
    *) usage; exit 1 ;;
  esac
done

# Must pass at least one
if [[ -z "$CPP_ZIP" && -z "$ARDUINO_ZIP" ]]; then
  echo "Error: At least one of -c or -a must be provided."
  usage
  exit 1
fi

# Unpack C++ library
if [[ -n "$CPP_ZIP" ]]; then
  if [[ ! -f "$CPP_ZIP" ]]; then
    echo "Error: C++ archive '$CPP_ZIP' not found."
    exit 1
  fi

  CPP_DIR="./PC/ei_cpp_library"
  mkdir -p "$CPP_DIR"
  echo "Unpacking C++ library '$CPP_ZIP' into '$CPP_DIR'…"
  unzip -q "$CPP_ZIP" -d "$CPP_DIR"
fi

# Unpack Arduino library (only src/)
if [[ -n "$ARDUINO_ZIP" ]]; then
  if [[ ! -f "$ARDUINO_ZIP" ]]; then
    echo "Error: Arduino archive '$ARDUINO_ZIP' not found."
    exit 1
  fi

  ARDUINO_DIR="./uC/lib/ei_arduino_library"
  mkdir -p "$ARDUINO_DIR"

  echo "Clearing old Arduino library files (preserving library.json) in '$ARDUINO_DIR'…"
  (
    cd "$ARDUINO_DIR"
    for entry in * .*; do
      [[ "$entry" == "." || "$entry" == ".." || "$entry" == "library.json" ]] && continue
      rm -rf -- "$entry"
    done
  )

  echo "Extracting only the src/ folder from '$ARDUINO_ZIP' into '$ARDUINO_DIR'…"
  TMPDIR=$(mktemp -d)
  unzip -q "$ARDUINO_ZIP" -d "$TMPDIR"

  # find the one top-level directory that contains src/
  found=0
  for d in "$TMPDIR"/*/; do
    if [[ -d "$d/src" ]]; then
      cp -a "$d/src/." "$ARDUINO_DIR/"
      found=1
      break
    fi
  done

  if [[ "$found" -ne 1 ]]; then
    echo "Error: could not find a src/ directory inside '$ARDUINO_ZIP'."
    rm -rf "$TMPDIR"
    exit 1
  fi

  rm -rf "$TMPDIR"
fi
