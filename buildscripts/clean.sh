#!/bin/bash -e

rm -rf ./prefix

for dir in deps/*/; do
    if [ -d "$dir/.git" ]; then
        echo "clean ${dir}";
        (cd "$dir" && git clean -fdx && echo "clean success ${dir}")
    fi
done