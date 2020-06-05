#!/usr/bin/env python3
import sys
import json
import numpy as np

def geo_mean(iterable):
    a = np.array(iterable)
    return a.prod()**(1.0/len(a))

def getFuncList(j):
    names = []
    for e in j:
        names.append(j[e]["Name"])
    if len(names) > len(set(names)):
        print("Warning!!! redundant functions")
    return names
def getEntryByName(j, name):
    for e in j:
        if name == j[e]["Name"]:
            return j[e]
    return None

file1=sys.argv[1] # 1d
file2= sys.argv[2]
print(file1)
print(file2)

with open(file1, "r") as f1:
    j1 = json.loads(f1.read())
with open(file2, "r") as f2:
    j2 = json.loads(f2.read())

names1 = set(getFuncList(j1))
names2 = set(getFuncList(j2))

if set(names1) != set(names2):
    print("Warning!! function list is not the same")

print(names1)
print(names2)

it1 = iter(names1)
it2 = iter(names2)
func1 = next(it1, None)
func2 = next(it2, None)


# avg
sum1 = 0
sum2 = 0
count1 = 0
count2 = 0
maxdiff = float('-inf')
mindiff = float('inf')
while 1 == 1:
    # FIXME count mean
    if func1 == None or func2 == None:
        if func1 != None:
            print("func2 run out, consume func1 all")
            hcm1 = getEntryByName(j1, func1)
            sum1 += hcm1["Difficulty"]
            count1 += 1
            func1 = next(it1, None)
            continue

        if func2 != None:
            hcm2 = getEntryByName(j2, func2)
            sum2 += hcm2["Difficulty"]
            count2 += 1
            func2 = next(it2, None)
            continue
            print("func1 run out, consume func2 all")
        break
    if func1 == func2:
        # Compare HCM of each function
        hcm1 = getEntryByName(j1, func1)
        hcm2 = getEntryByName(j2, func1)
        d1 = hcm1["Difficulty"]
        d2 = hcm2["Difficulty"]
        print("Entry " + func1 + " match " + str(d1) + ", " + str(d2))
        sum1 += d1
        sum2 += d2
        diff = hcm1["Difficulty"] - hcm2["Difficulty"]
        if diff > maxdiff:
            maxdiff = diff
        if diff < mindiff:
            mindiff = diff

        count1 += 1
        count2 += 1
        func1 = next(it1, None)
        func2 = next(it2, None)
    else:
        if func1 in names2:
            # func2 is unique
            hcm2 = getEntryByName(j2, func2)
            sum2 += hcm2["Difficulty"]
            count2 += 1
            func2 = next(it2, None)
            continue
        elif func2 in names1:
            # func1 is unique
            hcm1 = getEntryByName(j1, func1)
            sum1 += hcm1["Difficulty"]
            count1 += 1
            func1 = next(it1, None)
            continue
        else:
            # Consume hcm2 first
            hcm2 = getEntryByName(j2, func2)
            sum2 += hcm2["Difficulty"]
            count2 += 1
            func2 = next(it2, None)
            continue
print("Count1: " + str(count1))
print("Count2: " + str(count2))
if count1 == 0:
    avg1 = 0
else:
    avg1 = sum1 / count1
if count2 == 0:
    avg2 = 0
else:
    avg2 = sum2 / count2

print("Avg1: " + str(avg1))
print("Avg2: " + str(avg2))
if avg2 == 0:
    print("Ratio: " + str(0))
else:
    print("Ratio: " + str((avg1 - avg2)/avg2))
print("Maxdiff: " + str(maxdiff))
print("Mindiff: " + str(mindiff))
