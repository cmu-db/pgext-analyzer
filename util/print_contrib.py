# Utility program that takes the extensions in the extension DB and
# prints the ones in the contrib directory.

import json
import os
import sys

# File paths
current_working_dir = os.getcwd()
extn_info_dir = "extn_info"

# Load extension database
extn_files = os.listdir(current_working_dir + "/" + extn_info_dir)
extn_db = {}
for file in extn_files:
  extn_info_file = open(current_working_dir + "/" + extn_info_dir + "/" + file, "r")
  extn_info_json = json.load(extn_info_file)
  key = os.path.splitext(file)[0]
  extn_db[key] = extn_info_json

script_args = sys.argv

f = open(script_args[1], "r")
file_extns_list = f.readlines()
file_extns_list = list(map(lambda x: x.strip("\n"),file_extns_list))
f.close()

for extn in file_extns_list:
  extn_entry = extn_db[extn]
  if "download_method" in extn_entry and extn_entry["download_method"] == "contrib":
    print(extn)