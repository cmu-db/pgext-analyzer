import csv
from datetime import datetime
import json
import os
import subprocess

# Debug flag
DEBUG = False

# File path globals
current_working_dir = os.getcwd()
extn_info_dir = "extn_info"
ext_work_dir = "pgextworkdir"
postgres_version = "15.3"
now = datetime.now()
date_time = now.strftime("%m-%d-%Y_%H:%M")
testing_output_dir = "testing-output-" + date_time

# Common C/C++ extensions
common_c_file_extns = ["h", "hh", "c", "cpp", "cc", "cxx", "cpp"]

# Info CSV Ordering
types_of_extns = [
  "Functions",
  "Types",
  "Index Access Methods",
  "Storage Managers",
  "Client Authentication",
  "Query Procesing",
  "Utility Commands"
]

types_of_mechanisms = [
  "Memory Allocation",
  "Background Workers",
  "Custom Configuration Variables"
]

# Function names: DefineCustomBLANKVariable
custom_variable_fns = [
  "Bool",
  "Int",
  "Real",
  "String",
  "Enum"
]

# List of hooks
misc_hooks = [
  "emit_log_hook",
  "shmem_startup_hook",
  "shmem_request_hook",
  "needs_fmgr_hook",
  "fmgr_hook",
  "post_parse_analyze_hook"
]

query_processing_hooks = [
  "explain_get_index_name_hook",
  "ExplainOneQuery_hook",
  "get_attavgwidth_hook",
  "get_index_stats_hook",
  "get_relation_info_hook",
  "get_relation_stats_hook",
  "planner_hook",
  "join_search_hook",
  "set_rel_pathlist_hook",
  "set_join_pathlist_hook",
  "create_upper_paths_hook",
  "ExecutorStart_hook",
  "ExecutorRun_hook",
  "ExecutorFinish_hook",
  "ExecutorEnd_hook",
]

utility_hooks = [
  "ProcessUtility_hook"
]

client_auth_hooks = [
  "check_password_hook",
  "ClientAuthentication_hook",
  "ExecutorCheckPerms_hook",
  "object_access_hook",
  "row_security_policy_hook_permissive",
  "row_security_policy_hook_restrictive"
]

# Types of Extensions

postgres_hooks = misc_hooks + query_processing_hooks + utility_hooks + client_auth_hooks

# Creating the extension DB with the JSON files in extn_info
extn_files = os.listdir(current_working_dir + "/" + extn_info_dir)
extn_db = {}
for file in extn_files:
  extn_info_file = open(current_working_dir + "/" + extn_info_dir + "/" + file, "r")
  extn_info_json = json.load(extn_info_file)
  key = os.path.splitext(file)[0]
  extn_db[key] = extn_info_json
  extn_info_file.close()

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

def does_hook_exist(cl : str, hook):
  return (cl.startswith(hook + "=") or cl.startswith(hook + " =")) and cl.endswith(";")

def does_utility_plugin_exist(cl : str):
  return "_PG_output_plugin_init" in cl 

def does_background_worker_exist(cl : str):
  return "RegisterDynamicBackgroundWorker" in cl

def does_config_option_exist(cl: str):
  for elem in custom_variable_fns:
    fn_name = "DefineCustom" + elem + "Variable"
    if fn_name in cl:
      return True 
  
  return False

def does_udf_exist(cl : str):
  udf1_str = "create function"
  udf2_str = "create or replace function"
  return udf1_str in cl.lower() or udf2_str in cl.lower()

def does_udt_exist(cl : str):
  udt1_str = "create type"
  udt2_str = "create or replace type"
  return udt1_str in cl.lower() or udt2_str in cl.lower()

def does_external_table_exist(cl : str):
  return "create foreign data wrapper" in cl.lower()

def does_access_method_exist(cl : str):
  return "create access method" in cl.lower()

def does_table_access_method_exist(cl : str):
  return does_access_method_exist(cl) and "type table" in cl.lower()

def does_index_access_method_exist(cl : str):
  return does_access_method_exist(cl) and "type index" in cl.lower()

def source_analysis(extn_name):
  extn_entry = extn_db[extn_name]
  download_type = extn_entry["download_method"]
  source_dir ="" 
  if download_type == "contrib":
    source_dir = current_working_dir + "/postgresql-" + postgres_version + "/contrib/" + extn_entry["folder_name"]
  else:
    extension_dir = current_working_dir + "/" + ext_work_dir
    source_dir = extension_dir + "/" + extn_entry["folder_name"] + "/" + extn_entry["source_dir"]

  hooks_map = {}
  features_map = {
    "Client Authentication": False,
    "Query Procesing": False,
    "Utility Commands": False
  }

  mechanisms_map = {}

  for hook in postgres_hooks:
    hooks_map[hook] = False

  for ty in types_of_mechanisms:
    mechanisms_map[ty] = False

  for root, _, files in os.walk(source_dir):
    for name in files:
      _, file_ext = os.path.splitext(name)
      if file_ext[1:] in common_c_file_extns:
        tmp_source_file = open(os.path.join(source_dir, os.path.join(root, name)), "r")
        code_lines = tmp_source_file.readlines()
        for cl in code_lines:
          processed_cl = " ".join(cl.strip().split())
          for hook in misc_hooks:
            if does_hook_exist(processed_cl, hook):
              hooks_map[hook] = True

          for hook in query_processing_hooks:
            if does_hook_exist(processed_cl, hook):
              hooks_map[hook] = True
              features_map["Query Procesing"] = True

          for hook in utility_hooks:
            if does_hook_exist(processed_cl, hook):
              hooks_map[hook] = True
              features_map["Utility Commands"] = True

          for hook in client_auth_hooks:
            if does_hook_exist(processed_cl, hook):
              hooks_map[hook] = True
              features_map["Client Authentication"] = True

          if does_utility_plugin_exist(processed_cl):
            features_map["Utility Commands"] = True
          
          if does_background_worker_exist(processed_cl):
            mechanisms_map["Background Workers"] = True

          if does_config_option_exist(processed_cl):
            mechanisms_map["Custom Configuration Variables"] = True

        tmp_source_file.close()

  if hooks_map["shmem_startup_hook"] and hooks_map["shmem_request_hook"]:
    mechanisms_map["Memory Allocation"] = True

  return hooks_map, features_map, mechanisms_map

def sql_analysis(extn_name):
  extn_entry = extn_db[extn_name]
  download_type = extn_entry["download_method"]
  codebase_dir ="" 
  if download_type == "contrib":
    codebase_dir = current_working_dir + "/postgresql-" + postgres_version + "/contrib/" + extn_entry["folder_name"]
  else:
    extension_dir = current_working_dir + "/" + ext_work_dir
    codebase_dir = extension_dir + "/" + extn_entry["folder_name"]

  features_map = {
    "Functions": False,
    "Types": False,
    "Index Access Methods": False,
    "Storage Managers": False
  }

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

  pg_catalog = False

  access_method_flag = False
  for file in sql_files_list:
    total_file_path = codebase_dir + "/" + file
    file_obj = open(total_file_path, "r")
    file_lines = file_obj.readlines()
    for fl in file_lines:
      if access_method_flag:
        if "table" in fl.lower():
          features_map["Storage Managers"] = True
        elif "index" in fl.lower():
          features_map["Index Access Methods"] = True
        access_method_flag = False
      
      if does_udf_exist(fl):
        features_map["Functions"] = True
      if does_udt_exist(fl):
        features_map["Types"] = True
      if does_external_table_exist(fl):
        features_map["Storage Managers"] = True

      if does_table_access_method_exist(fl):
        features_map["Storage Managers"] = True
      elif does_index_access_method_exist(fl):
        features_map["Index Access Methods"] = True
      elif does_access_method_exist(fl):
        access_method_flag = True

      if "pg_catalog" in fl.lower():
        pg_catalog = True

  if pg_catalog:
    print(extn_name)
    
  return features_map

def run_extension_info_analysis(extn_name, hooks_csv_file_writer, info_csv_file_writer, mechanisms_csv_file_writer):
  extn_entry = extn_db[extn_name]
  download_type = extn_entry["download_method"]
  if download_type == "downloaded":
    return 

  print("Running extension info analysis on " + extn_name)
  hook_map, features_map, mechanisms_map = source_analysis(extn_name)
  sql_features_map = sql_analysis(extn_name)
  features_map.update(sql_features_map)

  if DEBUG:
    print(hook_map)
    print(features_map)

  output_to_hooks_csv = [extn_name]
  for hook in postgres_hooks:
    output_val = "Yes" if hook_map[hook] else "No"
    output_to_hooks_csv.append(output_val)

  hooks_csv_file_writer.writerow(output_to_hooks_csv)

  output_to_info_csv = [extn_name]
  for ty in types_of_extns:
    output_val = "Yes" if features_map[ty] else "No"
    output_to_info_csv.append(output_val)

  info_csv_file_writer.writerow(output_to_info_csv)

  output_to_mechanisms_csv = [extn_name]
  for ty in types_of_mechanisms:
    output_val = "Yes" if mechanisms_map[ty] else "No"
    output_to_mechanisms_csv.append(output_val)

  mechanisms_csv_file_writer.writerow(output_to_mechanisms_csv)

if __name__ == '__main__':
  # Download Postgres 
  initial_setup()

  # Terminal file
  terminal_file = open(testing_output_dir + "/terminal.txt", "a")

  # CSV files
  hooks_csv_file = open("hooks.csv", "w")
  hooks_csv_file_writer = csv.writer(hooks_csv_file)
  total_hooks_list = ["Extension Name"] + postgres_hooks
  hooks_csv_file_writer.writerow(total_hooks_list)

  info_csv_file = open("info.csv", "w")
  info_csv_file_writer = csv.writer(info_csv_file)
  info_csv_file_writer.writerow(["Extension Name"] + types_of_extns)

  mechanisms_csv_file = open("mechanisms.csv", "w")
  mechanisms_csv_file_writer = csv.writer(mechanisms_csv_file)
  mechanisms_csv_file_writer.writerow(["Extension Name"] + types_of_mechanisms)

  if DEBUG:
    download_extn("postgis", terminal_file)
    run_extension_info_analysis("postgis", hooks_csv_file_writer, info_csv_file_writer, mechanisms_csv_file_writer)
  else:
    for extn in extn_db:
      download_extn(extn, terminal_file)
    
    # Determine the percentage of source code copied from Postgres
    extns_list = list(extn_db.keys())
    extns_list.sort()
    for extn in extns_list:
      run_extension_info_analysis(extn, hooks_csv_file_writer, info_csv_file_writer, mechanisms_csv_file_writer)

  cleanup()
  