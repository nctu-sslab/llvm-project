#!/usr/bin/env python3
# This script take input from `diff`'s result
# Output diffed lines number of two file

import sys

InFileName = sys.argv[1]
OutFileName1st = "diffline1"
OutFileName2nd = "diffline2"
class Range():
    start = 0
    end = 0

def convertRange(line):
    ret = Range()
    if line.find(',') >= 0:
        pos = line.find(',')
        ret.start = int(line[0:pos])
        ret.end = int(line[pos+1:])
    else:
        ret.start = int(line)
        ret.end = ret.start
    return ret

def getRange(line, char):
    occur = line.find(char)
    range1 = line[0:occur]
    range2 = line[occur+1:]
    range1 = convertRange(range1)
    range2 = convertRange(range2)
    return range1, range2

f = open(InFileName)
raw = f.read()
lines = raw.split('\n')
fout = open(OutFileName2nd, "w+")
fout1d = open(OutFileName1st, "w+")
for line in lines:
    if len(line) == 0:
        break
    if line[0] == '>':
        pass
    elif line[0] == '<':
        pass
    elif line[0] == '-':
        pass
    elif line[0] == ' ':
        pass
    else:
        # TODO there is RcR(changing), RdL (delete) LaR (add)
        char  = ' '
        if line.find('c') >= 0 :
            char = 'c'
            pass
        elif line.find('d') >= 0:
            char = 'd'
            pass
        elif line.find('a') >= 0:
            char = 'a'
            pass
        else:
            print("Unknown format")
            print(line)
        line1, line2 = getRange(line, char)
        print("file1: {0}-{1}, file2: {2}-{3}".format(line1.start, line1.end, line2.start, line2.end))
        if line1 == -1:
            print("GG")
        for i in range(line1.start, line1.end + 1):
            fout1d.write(str(i))
            fout1d.write('\n')
        for i in range(line2.start, line2.end + 1):
            fout.write(str(i))
            fout.write('\n')
        continue
