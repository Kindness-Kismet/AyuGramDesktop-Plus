#!/usr/bin/env bash
set -euo pipefail

libraries_dir=${1:-Libraries}
test -d "$libraries_dir"

before=$(du -sk "$libraries_dir" | cut -f1)

find "$libraries_dir" \
  '(' \
    '(' \
      ! '(' \
        -name '*.a' \
        -o -name '*.h' \
        -o -name '*.hpp' \
        -o -name '*.inc' \
        -o -name '*.cmake' \
        -o -name '*.pc' \
        -o -path '*/include/*' \
        -o -path '*/objects-*' \
        -o -path '*/cache_keys/*' \
        -o -path '*/patches/*' \
        -o -perm +111 \
      ')' \
      -type f \
    ')' \
    -o -empty \
  ')' \
  -delete

# 保留库和对象文件原样，避免 strip 在依赖中的旧架构文件上崩溃。

after=$(du -sk "$libraries_dir" | cut -f1)
echo "Libraries cache: $((before * 1024)) -> $((after * 1024)) bytes"
