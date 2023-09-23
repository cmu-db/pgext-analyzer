# Usage: python3 list_to_pairs.py file_extns.txt prefix
import json
import sys
import os

script_args = sys.argv
f = open(script_args[1], "r")
file_extns_list = f.readlines()
file_extns_list = list(map(lambda x: x.strip("\n"),file_extns_list))
f.close()

# Load extension database
current_working_dir = os.getcwd()
extn_info_dir = "extn_info"
extn_files = os.listdir(current_working_dir + "/" + extn_info_dir)
extn_db = {}
for file in extn_files:
  extn_info_file = open(current_working_dir + "/" + extn_info_dir + "/" + file, "r")
  extn_info_json = json.load(extn_info_file)
  key = os.path.splitext(file)[0]
  extn_db[key] = extn_info_json

prefix = script_args[2]
file_extn_pairs_dict = {}
for extn1 in file_extns_list:
  for extn2 in file_extns_list:
    if extn1 == extn2:
      continue
    else:
      # Determine configure options
      def get_dependencies(extn):
        dep_list = []
        if "dependencies" in extn_db[extn]:
          for dep in extn_db[extn]["dependencies"]:
            dep_list += get_dependencies(dep) + [dep]
        return dep_list
      
      def collect_configure_options(extn_list):
        res = []
        for extn in extn_list:
          extn_entry = extn_db[extn]
          if "configure_options" in extn_entry:
            res += extn_entry["configure_options"]
        
        res = list(set(res))
        res.sort()
        return res
      
      extn_list =list(set(get_dependencies(extn1) + [extn1] + get_dependencies(extn2) + [extn2]))
      configure_options = collect_configure_options(extn_list)
      configure_options_key = ",".join(configure_options)
      if configure_options_key in file_extn_pairs_dict:
        file_extn_pairs_dict[configure_options_key] += [(extn1, extn2)]
      else:
        file_extn_pairs_dict[configure_options_key] = [(extn1, extn2)]

# Put them in prefix1.txt, prefix2.txt, etc
os.system("mkdir " + prefix)
i = 0
keys_list = list(file_extn_pairs_dict.keys())
for key in keys_list:
  filename = prefix  + "/" + prefix + str(i + 1) + ".txt"
  os.system("touch " + filename)
  sublist_file = open(filename, "w")
  tuples = file_extn_pairs_dict[key]
  for j in range(0, len(tuples)):
    sublist_file.write(tuples[j][0] + " " + tuples[j][1])
    if j != len(tuples) - 1:
      sublist_file.write("\n")
  i = i + 1
    