#!/usr/bin/env python3

# merge result of `wdiff -3 file1 file2`
import sys
import re

def merge(substrs):
    final = ""
    for s in substrs:
        final += s + "\n"
    final = final
    final = final.replace("[-", " ")
    final = final.replace("-]", " ")
    final = final.replace("{+", " ")
    final = final.replace("+}", " ")
    return final

def extract_new(text):
    text = text.replace('\r', ' ').replace('\n', ' ')
    x = re.findall("\[\-.*?\-\]", text)
    x2 = re.findall("\{\+.*?\+\}", text)
    print(merge(x) + merge(x2))


if __name__ == "__main__":
    wdiff_file = sys.argv[1]
    with open(wdiff_file, 'r') as f:
        text = f.read()
        extract_new(text)

