import os
import json

current_working_dir = os.getcwd()
extn_info_dir = "extn_info"

# Creating the extension DB with the JSON files in extn_info
extn_files = os.listdir(current_working_dir + "/../" + extn_info_dir)
extn_db = {}
for file in extn_files:
  extn_info_file = open(current_working_dir + "/../" + extn_info_dir + "/" + file, "r")
  extn_info_json = json.load(extn_info_file)
  key = os.path.splitext(file)[0]
  extn_db[key] = extn_info_json
  extn_info_file.close()


def analysis(category : str):
  extensions_file = open("allow_lists/" + category + ".txt", "r")
  extensions = list(map(lambda x: x.strip("\n"), extensions_file.readlines()))

  total_count = len(extensions)
  total_in_extn_db = 0
  for e in extensions:
    if e in extn_db:
      total_in_extn_db += 1

  pct = round(total_in_extn_db * 100/total_count, 2)
    
  print(str(pct) + "% of " + category + " is in extn_db")

  pct1 = round(total_in_extn_db * 100/len(extn_db.keys()), 2)
  print(str(pct1) + "% of " "extn_db is part of " + category)


if __name__ == '__main__':
  analysis("contrib")
  analysis("aws")
  analysis("gcp")
  analysis("azure")