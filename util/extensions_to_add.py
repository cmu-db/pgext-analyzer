import os
import json

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

# Get PGXN list of extensions
pgxn_path = "/home/abigalek/pgext-analyzer/pgxn/dist"
pgxn_files = os.listdir(pgxn_path)
pgxn_list = []
for file in pgxn_files:
  if file.endswith(".json"):
    pgxn_json = json.load(open(pgxn_path + "/" + file, "r"))
    if pgxn_json["name"] != None:
      pgxn_list.append(pgxn_json["name"])

extn_db_keys_set = set(extn_db.keys())
pgxn_list = list(map(lambda x: x.lower(), pgxn_list))
pgxn_list_set = set(pgxn_list)
set_diff = list(pgxn_list_set.difference(extn_db_keys_set))
set_diff.sort()

output = open("output.txt", "w")
for elem in set_diff:
  output.write(elem + "\n")

elem_json = {
    "download_method": "git",
    "download_url": "",
    "folder_name": "",
    "sql_files": [],
    "sql_dirs": [],
    "source_dir": "."
  }
elem_json_str = json.dumps(elem_json, indent=2)

new_extn_info_dir = "new_extn_info"
for elem in set_diff:
  elem_file = open(current_working_dir + "/" + new_extn_info_dir + "/" + elem + ".json", "w")
  elem_file.write(elem_json_str)
