#!/bin/bash

cmake -S .. -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS_DEBUG="-g -O0" \
    -DTARGET_ENVIRONMENT:string=gtest 

make -C build -j8

./build/test/lib/test_rpmsg_lite
