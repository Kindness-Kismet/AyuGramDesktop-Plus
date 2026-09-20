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

# Release 构建不会发布依赖调试信息，缓存前移除它以免挤占 Actions 配额。
find "$libraries_dir" -type f \( -name '*.a' -o -name '*.o' \) -print0 \
  | xargs -0 -n 64 strip -S

after=$(du -sk "$libraries_dir" | cut -f1)
echo "Libraries cache: $((before * 1024)) -> $((after * 1024)) bytes"
