#!/usr/bin/python 

import fileinput, copy

class DefaultDict(dict):
    #"""Dictionary with a default value for unknown keys."""
    def __init__(self, default):
        self.default = default

    def __getitem__(self, key):
        if key in self: 
            return self.get(key)
        else:
            ## Need copy in case self.default is something like []
            return self.setdefault(key, copy.deepcopy(self.default))
            
    def __copy__(self):
        copy = DefaultDict(self.default)
        copy.update(self)
        return copy

# create geometric bins with the recurrenc b[n] = b[n-2] * 2 for  n > 2
# [1,2,3,4,6,8,12,16,...]
bins = [10,20,30]
for i in range(50):
    bins.append(2*bins[-2])
zeroBins = [0]*len(bins)
functions = dict()
counts = DefaultDict(zeroBins)
active = set()

for line in fileinput.input():
    parts = line.split()
#    print "\t".join(parts)
    if len(parts) == 3:
        [address, image, name] = parts
        functions[int(address)] = [name,image,0]
    if len(parts) == 2:
        (t, address) = parts
        if (t == "0"):
            functions[int(address)][2] += 1
            active.add(int(address))
        else:
            functions[int(address)][2] -= 1
            if functions[int(address)][2] == 0:
                active.remove(int(address))
    if len(parts) == 1:
        distance = int(parts[0])
        reuseBin = 0
        for i in range(len(bins)):
            if distance < bins[i]:
                reuseBin = i
                break
        for f in active:
            counts[f][reuseBin] += 1 

# print the results
print "\t".join(["valid","address","name","image","bin","count"])
for key in counts:
    [name, image, stack] = functions[key]
    valid = 0
    if stack == 0:
        valid = 1
    for i in range(len(bins)):
        bin = bins[i]
        count = counts[key][i]
        print "\t".join(map(str,[valid, key, name, image, bin, count]))
    
