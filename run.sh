#!/bin/bash

set -e

cd "$(dirname "$0")"
ninja
emrun --no_browser --port 8080 www

