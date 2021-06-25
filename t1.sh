#!/bin/bash

#phat_601
cd jobs_test/test1/build && cmake .. && make -j 14 &
cd jobs_test/test2/build && cmake .. && make -j 14 &
cd jobs_test/test3/build && cmake .. && make -j 14 &