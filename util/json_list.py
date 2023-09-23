# Takes a string and separates it into a list of strings,
# separated by space

input1 = input()
words_list = input1.split()

for word in words_list:
  print("\"" + word + "\",")