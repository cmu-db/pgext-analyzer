import csv
from datetime import datetime
import json
import os
import subprocess
import sys

DEBUG = False

# File path globals
current_working_dir = os.getcwd()
extn_info_dir = "extn_info"
ext_work_dir = "pgextworkdir"
postgres_version = "15.3"
now = datetime.now()
date_time = now.strftime("%m-%d-%Y_%H:%M")
testing_output_dir = "testing-output-" + date_time

# Creating the extension DB with the JSON files in extn_info
extn_files = os.listdir(current_working_dir + "/" + extn_info_dir)
extn_db = {}
for file in extn_files:
  extn_info_file = open(current_working_dir + "/" + extn_info_dir + "/" + file, "r")
  extn_info_json = json.load(extn_info_file)
  key = os.path.splitext(file)[0]
  extn_db[key] = extn_info_json
  extn_info_file.close()

# Language list:
language_list = ["c", "plpgsql", "sql", "internal", "plv8"]

def initial_setup():
  url = "https://ftp.postgresql.org/pub/source/v" + postgres_version + "/postgresql-" + postgres_version + ".tar.gz"
  subprocess.run("wget " + url, cwd=current_working_dir, shell=True)
  subprocess.run("tar -xvf postgresql-" + postgres_version + ".tar.gz", cwd=current_working_dir, shell=True, capture_output=True)
  subprocess.run("mkdir " + ext_work_dir, cwd=current_working_dir, shell=True)
  subprocess.run("mkdir " + testing_output_dir, cwd=current_working_dir, shell=True)
  subprocess.run("touch terminal.txt", shell=True, cwd=current_working_dir + "/" + testing_output_dir)

def cleanup():
  subprocess.run("rm -r postgresql-" + postgres_version, shell=True, cwd=current_working_dir)
  subprocess.run("rm -rf " + ext_work_dir, shell=True, cwd=current_working_dir)
  subprocess.run("rm postgresql-" + postgres_version + ".tar.gz", shell=True, cwd=current_working_dir)

def download_extn(extn_name, terminal_file):
  extn_entry = extn_db[extn_name]
  print("Downloading extension " + extn_name)
  extension_dir = current_working_dir + "/" + ext_work_dir
  download_type = extn_entry["download_method"]
  if download_type == "git":
    git_repo = extn_entry["download_url"]
    subprocess.run("git clone " + git_repo, shell=True, cwd=extension_dir, stdout=terminal_file, stderr=terminal_file)
  elif download_type == "tar" or download_type == "zip":
    if "download_url" in extn_entry:
      url = extn_entry["download_url"]
      base_name = os.path.basename(url)
      subprocess.run("wget " + url, shell=True, cwd=extension_dir, stdout=terminal_file, stderr=terminal_file)
    elif "pgxn_location" in extn_entry:
      archive_name = current_working_dir + "/pgxn/dist/" + extn_entry["pgxn_location"]
      subprocess.run("cp " + archive_name + " " + extension_dir, shell=True, cwd=extension_dir, stdout=terminal_file, stderr=terminal_file)
      base_name = os.path.basename(extn_entry["pgxn_location"])
    
    if download_type == "tar":
      subprocess.run("tar -xvf " + base_name, shell=True, cwd=extension_dir, stdout=terminal_file, stderr=terminal_file)
    elif download_type == "zip":
      subprocess.run("unzip " + base_name, shell=True, cwd=extension_dir, stdout=terminal_file, stderr=terminal_file)
    subprocess.run("rm " + base_name, shell=True, cwd=extension_dir, stdout=terminal_file, stderr=terminal_file)

  print("Finished downloading extension " + extn_name)

def does_udf_exist(cl : str):
  udf1_str = "create function"
  udf2_str = "create or replace function"
  return cl.lower().startswith(udf1_str) or cl.lower().startswith(udf2_str)

def does_language_exist(cl: str):
  return "language " in cl

def not_comment(cl: str):
  return not cl.strip().startswith("/*") and not cl.strip().startswith("--")

def function_analysis(extn_name):
  language_dict = {}

  for vl in language_list:
    language_dict[vl] = 0

  extn_entry = extn_db[extn_name]
  download_type = extn_entry["download_method"]

  if download_type == "downloaded":
    return language_dict

  codebase_dir ="" 
  if download_type == "contrib":
    codebase_dir = current_working_dir + "/postgresql-" + postgres_version + "/contrib/" + extn_entry["folder_name"]
  else:
    extension_dir = current_working_dir + "/" + ext_work_dir
    codebase_dir = extension_dir + "/" + extn_entry["folder_name"]

  sql_files_list = []

  if "sql_files" in extn_entry:
    sql_files_list = extn_entry["sql_files"]

  if "sql_dirs" in extn_entry:
    sql_dirs_list = extn_entry["sql_dirs"]
    for dir in sql_dirs_list:
      for file_path in os.listdir(codebase_dir + "/" + dir):
        _, file_extension = os.path.splitext(file_path)
        if file_extension == ".sql":
          sql_files_list.append(dir + "/" + file_path)

  if DEBUG:
    print(sql_files_list)

  for file in sql_files_list:
    total_file_path = codebase_dir + "/" + file
    file_obj = open(total_file_path, "r")
    file_lines = file_obj.readlines()
    file_lines = list(filter(not_comment, file_lines))

    index = 0
    while index < len(file_lines):
      fl = file_lines[index]
      if does_udf_exist(fl):
        if DEBUG:
          print(fl)
        udf_index = index
        while True:
          if index >= len(file_lines):
            print("BUG: no method syntax in function spotted...")
            print(file)
            print(extn_name)
            break
          
          language_str = file_lines[index].lower()
          if does_udf_exist(language_str) and udf_index != index:
            print("BUG: no method syntax in function spotted...")
            print(file)
            print(extn_name)
            break

          if does_language_exist(language_str.lower()):
            if DEBUG:
              print(language_str)

            update_flag = False
            lang_list = language_str.split()
            for (i, l) in list(enumerate(lang_list)):
              if l == "language":
                language_idx = i+1
                language_name = lang_list[language_idx]
                language_name = language_name.strip("';")

                # Language verification
                for verified_lang in language_list:
                  if language_name == verified_lang:
                    language_dict[language_name] = language_dict[language_name] + 1
                    update_flag = True
                    break
            
            if update_flag:
              index += 1
              break
            else:
              index += 1
          else:
            index += 1
      else:
        index += 1
          
  return language_dict

if __name__ == '__main__':
  # Download Postgres 
  initial_setup()
  # Terminal file
  terminal_file = open(testing_output_dir + "/terminal.txt", "a")

  # Function analysis CSV file
  functions_csv_file = open("functions.csv", "w")
  functions_csv_file_writer = csv.writer(functions_csv_file)
  functions_csv_file_writer.writerow(["Extension Name"] + language_list)

  extns_list = list(extn_db.keys())
  extns_list.sort()
  
  for extn in extns_list:
    print("Analyzing " + extn + "...")
    download_extn(extn, terminal_file)
    language_dict = function_analysis(extn)
    output_list = []
    for verified_lang in language_list:
      output_list.append(str(language_dict[verified_lang]))
    functions_csv_file_writer.writerow([extn] + output_list)

  cleanup()