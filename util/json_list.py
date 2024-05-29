# This script takes a string and separates it into a list of strings, separated
# by space. I used this tool to get lists of pg_regress tests from the 
# extensions' Makefiles.

input1 = input()
words_list = input1.split()

for word in words_list:
  print("\"" + word + "\",")