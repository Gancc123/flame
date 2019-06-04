#!/usr/bin/env python3  
#-*- coding : utf-8 -*-  

import os
import sys
from math import ceil
from statistics import mean, median, stdev

_last_len = 0
def _state_print(s):
    global _last_len
    sys.stdout.write('\r%s' %(_last_len * ' '))
    sys.stdout.write('\r%s' %s)
    _last_len = len(s)

def array_from_file(fn):
    arr = []
    with open(fn, 'r') as f:
        cnt_line = f.readline() #cnt line
        _, cnt = cnt_line.split(':')
        cnt = int(cnt)
        f.readline() #first lat with connection establish.
        for i in range(cnt - 1): #ignore first line
            line = f.readline()
            num = float(line)
            arr.append(num)
            if i % 1000 == 0: 
                _state_print('%s/%s' %(i, cnt - 1))
    _state_print('%s/%s' %(cnt - 1, cnt - 1))
    print('')
    return arr

def percentile_num(arr, percent):
    index = ceil(len(arr) * percent)
    if index == len(arr):
        index = len(arr) - 1
    return arr[index], index

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print("Usage:\n\t%s result.txt" %sys.argv[0])
        exit(0)

    print('read file...')
    arr = array_from_file(sys.argv[1])

    print('process...')
    arr.sort()

    m = median(arr)
    print('median:\t%s' %m)

    a = mean(arr)
    print('avg:\t%s' %a)

    s = stdev(arr)
    print('stdev:\t%s' %s)
    
    
    n, i = percentile_num(arr, 0.99)
    print('99%%(%s):\t%s' %(i, n))
    n, i = percentile_num(arr, 0.999)
    print('99.9%%(%s):\t%s' %(i, n))
    n, i = percentile_num(arr, 0.9999)
    print('99.99%%(%s):\t%s' %(i, n))



