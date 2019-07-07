#!/bin/bash

a=`ps aux | grep csd | awk '{print $2}' | sed -n '1p'`
kill -9 $a
kill -9 $a
a=`ps aux | grep mgr | awk '{print $2}' | sed -n '1p'`
kill -9 $a