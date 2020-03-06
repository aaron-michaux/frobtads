#!/bin/bash

PROJECT="$(basename "$(dirname "$0")")"
PPWD="$(cd "$(dirname "$0")" ; pwd)"
T3MAKE="$PPWD/../../t3make"

# export TADSLIB="$PPWD/../../tads3/lib"
# export TADSLIB=/home/amichaux/Documents/CrashPlan/Development/frobtads
# ls "$TADSLIB"

OBJD="/tmp/t3make/$PROJECT/obj"
SYMD="/tmp/t3make/$PROJECT/sym"

mkdir -p "$OBJD"
mkdir -p "$SYMD"

cd "$PPWD"
"$T3MAKE" -Fo "$OBJD" -Fy "$SYMD" -f MyGame


