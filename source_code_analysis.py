import csv
from datetime import datetime
import json
import os
import re
import subprocess
from sctokenizer import CppTokenizer

# Debug flag
DEBUG = True

# File paths (globals)
current_working_dir = os.getcwd()
extn_info_dir = "extn_info"
ext_work_dir = "pgextworkdir"
postgres_version = "15.3"
now = datetime.now()
date_time = now.strftime("%m-%d-%Y_%H:%M")
testing_output_dir = "testing-output-" + date_time
pmd_cpd_dir = "pmd-bin-7.0.0-rc4"
tmp_dir = "tmp"

# PMD argument globals
pmd_command = "./pmd-bin-7.0.0-rc4/bin/pmd cpd --minimum-tokens 100"
pmd_options = "  --language cpp  --no-fail-on-violation"

# Common C/C++ extensions
common_c_file_extns = ["h", "hh", "c", "cpp", "cc", "cxx", "cpp"]

# CPP Tokenizer
c_source_tokenizer = CppTokenizer()

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
  subprocess.run("mkdir " + tmp_dir, cwd=current_working_dir + "/" + testing_output_dir, shell=True)
  subprocess.run("touch terminal.txt", shell=True, cwd=current_working_dir + "/" + testing_output_dir)

def cleanup():
  subprocess.run("rm -r postgresql-" + postgres_version, shell=True, cwd=current_working_dir)
  subprocess.run("rm -rf " + ext_work_dir, shell=True, cwd=current_working_dir)
  subprocess.run("rm -rf " + tmp_dir, cwd=current_working_dir + "/" + testing_output_dir, shell=True)
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
    url = extn_entry["download_url"]
    base_name = os.path.basename(url)
    subprocess.run("wget " + url, shell=True, cwd=extension_dir, stdout=terminal_file, stderr=terminal_file)
    if download_type == "tar":
      subprocess.run("tar -xvf " + base_name, shell=True, cwd=extension_dir, stdout=terminal_file, stderr=terminal_file)
    elif download_type == "zip":
      subprocess.run("unzip " + base_name, shell=True, cwd=extension_dir, stdout=terminal_file, stderr=terminal_file)
    subprocess.run("rm " + base_name, shell=True, cwd=extension_dir, stdout=terminal_file, stderr=terminal_file)
  print("Finished downloading extension " + extn_name)

def parse_stats(stats):
  if DEBUG:
    print("Stats line: " + stats)
  stats_list = stats.split(" ")
  num_lines = int(stats_list[2])
  return num_lines

# The interval returned is inclusive: [s, e]
def parse_interval(line, num_lines):
  list_of_words = line.split(" ")
  key = list_of_words[-1]
  line_start = int(list_of_words[3]) - 1
  key_file = open(key, "r")
  key_file_lines = key_file.readlines()
  num_key_file_lines = len(key_file_lines)
  key_file.close()

  return key, (line_start, min(line_start + num_lines - 1, num_key_file_lines - 1))

# Number of lines, number of tokens
def process_err(extn_name, err):
  err_dict = {}
  list_of_lines = err.split('\n')
  first_pattern = r"Found a \d+ line \(\d+ tokens\) duplication in the following files:"
  second_pattern = r"Starting at line \d+ of .+"
  list_of_lines = list(filter(lambda x: re.match(first_pattern, x) or re.match(second_pattern, x), list_of_lines))
  list_of_lines.sort()
  num_lines = parse_stats(list_of_lines[0])

  parent_folder = "contrib" if extn_db[extn_name]["download_method"] == "contrib" else ext_work_dir
  efolder = "/" + parent_folder + "/" + extn_db[extn_name]["folder_name"] + "/"

  for elem in list_of_lines[1:]:
    key, (ls, le) = parse_interval(elem, num_lines)
    if efolder in key:
      if key not in err_dict:
        err_dict[key] = []
      err_dict[key].append([ls, le])

  return err_dict
  
def update_error_mapping(one_err_dict, err_mapping):
  for key in one_err_dict:
    if key not in err_mapping:
      err_mapping[key] = []
    err_mapping[key] += one_err_dict[key]
  return err_mapping

def get_num_tokens(line):
  tokens = c_source_tokenizer.tokenize(line)
  return len(tokens)

def convert_mapping_to_stats(err_mapping):
  sum_loc = 0
  sum_tokens = 0
  for key in err_mapping:
    intervals = err_mapping[key]
    key_file = open(key, "r")
    key_file_lines = key_file.readlines()

    if DEBUG:
      num_lines_in_file = len(key_file_lines)
      for itvl in intervals:
        if itvl[1] >= num_lines_in_file:
          raise RuntimeError("Index to file cannot be greater than num lines in file")

    intervals.sort(key=lambda tup: tup[0])
    ci_stack = []
    ci_stack.append(intervals[0])
    for itvl in intervals[1:]:
      if ci_stack[-1][0] <= itvl[0] and itvl[0] <= ci_stack[-1][1]:
        ci_stack[-1][1] = max(ci_stack[-1][1], itvl[1])
      else:
        ci_stack.append(itvl)
    
    # Now ci_stack contains the intervals we need to account for
    for itvl in ci_stack:
      for i in range(itvl[0], itvl[1] + 1):
        sum_loc += 1
        sum_tokens += get_num_tokens(key_file_lines[i])
    key_file.close()

  return sum_loc, sum_tokens

def get_total_loc(extn_name, source_dir):
  total_loc = 0
  print("Determining Total LOC for " + extn_name)
  for root, _, files in os.walk(source_dir):
    for name in files:
      _, file_ext = os.path.splitext(name)
      if file_ext[1:] in common_c_file_extns:
        tmp_source_file = open(os.path.join(source_dir, os.path.join(root, name)), "r")
        code_lines = tmp_source_file.readlines()
        total_loc += len(code_lines)
        tmp_source_file.close()
  return total_loc

# Return total_copied_loc/tokens, copied_postgres_loc/tokens, copied_extn_loc/tokens
def run_cpd_analysis(extn_name, source_dir):
  print("Running CPD analysis on " + extn_name)
  postgres_src_dir = current_working_dir + "/postgresql-" + postgres_version + "/src"
  postgres_command = pmd_command + (" --dir " + postgres_src_dir)
  postgres_command += " --dir " + source_dir
  postgres_command += pmd_options

  postgres_analysis = subprocess.run(postgres_command, shell=True, capture_output=True, cwd=current_working_dir)
  postgres_decoded_output = postgres_analysis.stdout.decode('utf-8')
  postgres_list_of_errors = postgres_decoded_output.split("=====================================================================")

  # Open errors file
  extn_error_file_name = extn_name + "_cpd.txt"
  subprocess.run("touch " + extn_error_file_name, shell=True, cwd=current_working_dir + "/" + testing_output_dir)
  extn_errors_terminal_file = open(testing_output_dir + "/" + extn_error_file_name, "w")

  total_num_instances = 0

  parent_folder = "contrib" if extn_db[extn_name]["download_method"] == "contrib" else ext_work_dir
  efolder = "/" + parent_folder + "/" + extn_db[extn_name]["folder_name"] + "/"
  postgres_err_mapping = {}
  extn_err_mapping = {}
  total_err_mapping = {}
  for err in postgres_list_of_errors:
    if efolder in err:
      err_dict = process_err(extn_name, err)
      if postgres_src_dir in err:
        update_error_mapping(err_dict, postgres_err_mapping)
      else:
        update_error_mapping(err_dict, extn_err_mapping)
      update_error_mapping(err_dict, total_err_mapping)
      total_num_instances += 1
      extn_errors_terminal_file.write(err)
      extn_errors_terminal_file.write("=====================================================================\n\n")

  extn_errors_terminal_file.close()
  if total_num_instances == 0:
    subprocess.run("rm " + extn_error_file_name, shell=True, cwd=current_working_dir + "/" + testing_output_dir)
    return (0, 0), (0, 0), (0, 0)
  
  tc_loc, tc_tokens = convert_mapping_to_stats(total_err_mapping)
  pc_loc, pc_tokens = convert_mapping_to_stats(postgres_err_mapping)
  ec_loc, ec_tokens = convert_mapping_to_stats(extn_err_mapping)
  return (tc_loc, tc_tokens), (pc_loc, pc_tokens), (ec_loc, ec_tokens)

# Versioning Analysis: Versioning LOC, Copied LOC between versions/tokens
def run_version_analysis(extn_name, source_dir):
  print("Running version analysis on " + extn_name)
  versioning_loc = 0
  versioning_files = []
  for root, _, files in os.walk(source_dir):
    for name in files:
      _, file_ext = os.path.splitext(name)
      if file_ext[1:] in common_c_file_extns:
        tmp_source_file = open(os.path.join(source_dir, os.path.join(root, name)), "r")
        code_lines = tmp_source_file.readlines()
        tmp_source_file.close()

        # Storing tuples with indexes and bool as to whether its a pg_version if
        pstack = []
        code_intervals = []
        for i, cl in enumerate(code_lines):
          if "#if" in cl:
            flag = "PG_VERSION_NUM" in cl
            pstack.append((i, flag))
          elif "#endif" in cl:
            if len(pstack) == 0:
              raise IndexError("No matching closing endif at line " + str(i) + " in file " + name)
            last_entry = pstack.pop() 
            if last_entry[1]:
              code_intervals.append([last_entry[0], i])

        if len(code_intervals) != 0:
          code_intervals.sort(key=lambda tup: tup[0])
          ci_stack = []
          ci_stack.append(code_intervals[0])
          for itvl in code_intervals[1:]:
            if ci_stack[-1][0] <= itvl[0] and itvl[0] <= ci_stack[-1][1]:
              ci_stack[-1][1] = max(ci_stack[-1][1], itvl[1])
            else:
              ci_stack.append(itvl)
        
          # In the tmp directory we create a file called tmp_name and copy all the code from these
          # intervals in this code
          tmp_version_code_file_name = current_working_dir + "/" + testing_output_dir + "/" + tmp_dir + "/" + "tmp_" + name
          tmp_version_code_file = open(tmp_version_code_file_name, "w")
          versioning_files.append(tmp_version_code_file_name)
        
          for ci in ci_stack:
            for j in range(ci[0], ci[1] + 1):
              tmp_version_code_file.write(code_lines[j])
            versioning_loc += ci[1] - ci[0]

          tmp_version_code_file.close()

  if versioning_loc == 0:
    return 0, (0, 0)

  # Running CPD analysis on just versioning code
  version_source_dir = current_working_dir + "/" + testing_output_dir + "/" + tmp_dir
  version_command = pmd_command + (" --dir " + version_source_dir)
  version_command += pmd_options

  version_analysis = subprocess.run(version_command, shell=True, capture_output=True, cwd=current_working_dir)
  version_decoded_output = version_analysis.stdout.decode('utf-8')
  
  if version_decoded_output == "":
    return (versioning_loc, (0, 0))

  version_list_of_errors = version_decoded_output.split("=====================================================================")
  err_mapping = {}
  for err in version_list_of_errors:
    err_dict = process_err(extn_name, err)
    update_error_mapping(err_dict, err_mapping)
  
  vc_loc, vc_tokens = convert_mapping_to_stats(err_mapping)

  # Clear temp for other extensions
  subprocess.run("rm -rf *", shell=True, cwd=version_source_dir)

  return versioning_loc, (vc_loc, vc_tokens)

def run_sca_analysis(extn_name, results_csv, versioning_csv):
  extn_entry = extn_db[extn_name]
  download_type = extn_entry["download_method"]
  if download_type == "downloaded":
    return 

  print("Running source code analysis on " + extn_name)
  source_dir ="" 
  if download_type == "contrib":
    source_dir = current_working_dir + "/postgresql-" + postgres_version + "/contrib/" + extn_entry["folder_name"]
  else:
    extension_dir = current_working_dir + "/" + ext_work_dir
    source_dir = extension_dir + "/" + extn_entry["folder_name"] + "/" + extn_entry["source_dir"]

  # Determine Total LOC in extension codebase
  total_loc = get_total_loc(extn_name, source_dir)

  # Run CPD analysis
  (tc_loc, tc_tokens), (pc_loc, pc_tokens), (ec_loc, ec_tokens) = run_cpd_analysis(extn_name, source_dir)

  versioning_loc, (vc_loc, vc_tokens) = run_version_analysis(extn_name, source_dir)
  
  results_csv.writerow([
    extn_name, 
    str(total_loc), 
    str(tc_loc),  
    str(pc_loc),
    str(ec_loc),
    str(tc_tokens),
    str(pc_tokens), 
    str(ec_tokens)])
  
  versioning_flag = "Yes" if versioning_loc > 0 else "No"
  versioning_csv.writerow([
    extn_name,
    str(total_loc),
    versioning_flag,
    str(versioning_loc),
    str(vc_loc), 
    str(vc_tokens)])

  print("Finished running source code analysis on " + extn_name)

if __name__ == '__main__':
  # Download Postgres 
  initial_setup()

  # Terminal file
  terminal_file = open(testing_output_dir + "/terminal.txt", "a")

  # CSV files
  sca_csv_file = open("source_code_analysis.csv", "w")
  sca_csv_file_writer = csv.writer(sca_csv_file)
  sca_csv_file_writer.writerow([
    "Extension Name", 
    "Total LOC", 
    "Total Copied LOC", 
    "Copied Postgres LOC",
    "Copied Extn LOC",
    "Total Copied Tokens",
    "Copied Postgres Tokens",
    "Copied Extn Tokens"])

  vers_csv_file = open("versioning.csv", "w")
  vers_csv_file_writer = csv.writer(vers_csv_file)
  vers_csv_file_writer.writerow([
    "Extension Name", 
    "Total LOC",
    "Versioning?", 
    "Versioning LOC", 
    "Copied Versioning LOC",
    "Copied Versioning Tokens"])
  
  if DEBUG:
    download_extn("cube", terminal_file)
    download_extn("citus", terminal_file)
    download_extn("hypopg", terminal_file)
    run_sca_analysis("cube", sca_csv_file_writer, vers_csv_file_writer)
    run_sca_analysis("citus", sca_csv_file_writer, vers_csv_file_writer)
    run_sca_analysis("hypopg", sca_csv_file_writer, vers_csv_file_writer)
  else:
    for extn in extn_db:
      download_extn(extn, terminal_file)
    
    # Determine the percentage of source code copied from Postgres
    extns_list = list(extn_db.keys())
    extns_list.sort()
    for extn in extns_list:
      run_sca_analysis(extn, sca_csv_file_writer, vers_csv_file_writer)

  terminal_file.close()
  sca_csv_file.close()
  vers_csv_file.close()
  cleanup()