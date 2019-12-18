#!/bin/bash

set -e

QUICKJS_VERSION=2019-10-27

if [[ -e quickjs.tar.xz ]]; then
    rm -f quickjs.tar.xz
fi
curl -L -o quickjs.tar.xz https://bellard.org/quickjs/quickjs-${QUICKJS_VERSION}.tar.xz

if [[ -d quickjs ]]; then
    rm -rf quickjs
fi
mkdir quickjs
tar Jxf quickjs.tar.xz --strip-components=1 -C quickjs
rm -f quickjs.tar.xz
