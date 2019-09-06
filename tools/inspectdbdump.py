#!/usr/bin/python3
# Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Gather stats on a rocksdb dump. Prepare the dump files with:
#
#  rocksdb-5.7.3/ldb --db=<rocksdb directory> \
#                    --path=<file in directory> \
#                    --hex dump > <dump file>
#  inspectdb.py <dump file>
#
# Also supports directly piping ldb output to stdin input.
#
# Suggested improvements:
#   - use a real histogram implementation (numpy?)
#   - directly open the rocksdb database, instead of reading a dump

import argparse
import math
import re
import sys

parser = argparse.ArgumentParser(description="Analyze data from an ldb dump")
parser.add_argument("filename",
                    help="The filename of the ldb dump (stdin if omitted)",
                    nargs='?', type=argparse.FileType('r'), default=sys.stdin)
parser.add_argument("-p","--prefix", help="Only analyze keys with this prefix",
                    default="", dest="prefix")
args = parser.parse_args()

# some dumps look like:
#   0x1234 ==> 0x567890
# others look like:
#   '1234' seq:0, type:1 => '567890'
pattern = re.compile("(?:0x)?'?([0-9A-Fa-f]*).*=> (?:0x)?'?([0-9A-Fa-f]*)'?")

def key_type(key):
   return key[:2]

class Histogram:
   '''
   Overly simple histogram implementation. It buckets values into
   log-base-2 bins (1, 2, 4, 8, ...). It also keeps rough track of any
   items that were too large for the largest bin.
   '''
   def __init__(self):
      self._bins = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
      self._over_count = 0
      self._over_min = sys.maxsize
      self._over_max = 0

   def update(self, value):
      index = math.ceil(math.log(value, 2))
      if index < len(self._bins):
         self._bins[index] += 1
      else:
         self._over_count += 1
         self._over_min = min(self._over_min, value)
         self._over_max = max(self._over_max, value)

   def __str__(self):
      '''
      Representation is each bin on a line by itself, with the upper bin
      limit followed by the count of items in that bin.
      '''
      result = ""
      limit = 1
      for v in self._bins:
         result += "<={:>4}: {:>5}\n".format(limit, v)
         limit *= 2
      result += " >{:>4}: {:>5} ({} - {})".format(
         limit, self._over_count, self._over_min, self._over_max)
      return result

class DBStats:
   '''
   Overly simple stats about the sizes of values for each key
   type. Stores a count of the number of items for that type, the sum of
   the value sizes for that type, and a histogram of the value sizes.
   '''
   def __init__(self):
      self._stats = {}

   def update(self, key, value):
      if not key.startswith(args.prefix):
         return

      ktype = key_type(key[len(args.prefix):])
      if ktype in self._stats:
         key_stats = self._stats[ktype]
      else:
         key_stats = {"count": 0, "size": 0, "histo": Histogram()}

      key_stats["count"] += 1
      key_stats["size"] += len(value)//2
      key_stats["histo"].update(len(value)//2)
      self._stats[ktype] = key_stats

   def __str__(self):
      '''
      Representation is a block of lines for each type, including the
      key type (prefix), count, value size sum, and histogram.
      '''
      result = ""
      for k in self._stats.keys():
         result += "Type: {}{}\n".format(args.prefix, k)
         result += "  count: {:>10}\n".format(self._stats[k]["count"])
         result += "    sum: {:>10}\n".format(self._stats[k]["size"])
         result += "  histo:\n"
         histo_result = str(self._stats[k]["histo"])
         for l in histo_result.split("\n"):
            result += "    {}\n".format(l)
      return result

stats = DBStats()

for line in args.filename:
   match = pattern.match(line)
   if match:
      stats.update(match.group(1), match.group(2))

print(stats)
