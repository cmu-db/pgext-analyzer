import csv
from datetime import datetime
import json
import os
import subprocess

# Debug flag
DEBUG = False

# File paths (globals)
current_working_dir = os.getcwd()
semgrep_dir = "semgrep_analysis_input"
input_fn_dir = semgrep_dir + "/sca_analysis_fns"
now = datetime.now()
date_time = now.strftime("%m-%d-%Y_%H:%M")
testing_output_dir = "testing-output-" + date_time
tmp_output_file_name = current_working_dir + "/" + testing_output_dir + "/tmp_output.txt"

################################################################
# HELPER FUNCTIONS
################################################################

def initial_setup():
  subprocess.run("mkdir " + testing_output_dir, cwd=current_working_dir, shell=True)

################################################################
# PROCESSING FUNCTIONS
################################################################
def get_extn_name(file):
  return file[:-7]

def get_fn_info(result_obj):
  if "extra" in result_obj:
    extra_obj = result_obj["extra"]
    if "metavars" in extra_obj:
      metavars_obj = extra_obj["metavars"]
      if "$FUNC" in metavars_obj:
        func_obj = metavars_obj["$FUNC"]
        if "abstract_content" in func_obj:
          fn_name = func_obj["abstract_content"]
          if "start" in func_obj:
            start_obj = func_obj["start"]
            if "line" in start_obj:
              return fn_name, start_obj["line"]
  
  raise RuntimeError("Could not extract function name from results object")

def get_semgrep_command(source_file, yml_file):
  semgrep_command = "semgrep scan --json"
  semgrep_command += " --config=" + semgrep_dir + "/" + yml_file
  semgrep_command += " --output=" + tmp_output_file_name
  full_file_name = current_working_dir + "/" + input_fn_dir + "/" + source_file
  semgrep_command += " " + full_file_name
  return semgrep_command

# This function exists so that random segfaults in semgrep are handled properly.
def run_semgrep_command(semgrep_command):
  if DEBUG:
    print(semgrep_command)
  while True:
    res = subprocess.run(semgrep_command, shell=True, cwd=current_working_dir, capture_output=True)
    if "Segmentation fault (core dumped)" not in res.stdout.decode('utf-8'):
      break

def store_state_results(extn_name, results_json):
  results_file_name = extn_name + "_state_results.json"
  subprocess.run(
    "touch " + results_file_name, 
    shell=True, 
    cwd=current_working_dir + "/" + testing_output_dir, 
    capture_output=True)
  with open(testing_output_dir + "/" + results_file_name, "w") as results_file:
    json.dump(results_json, results_file, indent=2)
    results_file.close()

def get_csv_output(file):
  extn_name = get_extn_name(file)
  print("Running function analysis on " + extn_name)

  # Get Total LOC of file
  input_file_name =  current_working_dir + "/" + input_fn_dir + "/" + file
  input_file_obj = open(input_file_name, "r")
  total_loc = len(input_file_obj.readlines())
  input_file_obj.close()

  # Get number of copied functions
  fn_semgrep_command = get_semgrep_command(file, "function.yml")
  run_semgrep_command(fn_semgrep_command)

  results = open(tmp_output_file_name, "r")
  results_json = json.load(results)
  results.close()
  fns_list = results_json["results"]
  num_copied_fns = len(fns_list)

  # Get number of state modified functions + instances
  state_semgrep_command = get_semgrep_command(file, "state.yml")
  run_semgrep_command(state_semgrep_command)

  results = open(tmp_output_file_name, "r")
  results_json = json.load(results)
  results.close()

  store_state_results(extn_name, results_json)

  state_list = results_json["results"]
  num_state_instances = len(state_list)
  state_fns_set = set()
  for robj in state_list:
    fn_name, fn_line = get_fn_info(robj)
    state_fns_set.add((fn_name, fn_line))

  num_state_fns = len(state_fns_set)
  return extn_name, total_loc, num_copied_fns, num_state_fns, num_state_instances

################################################################
# MAIN ROUTINE
################################################################

if __name__ == '__main__':
  initial_setup()
  test_files_dir = current_working_dir + "/" + input_fn_dir

  # Create CSV
  csv_file = open("function_analysis.csv", "w")
  csv_file_writer = csv.writer(csv_file)
  csv_file_writer.writerow([
    "Extension Name",
    "Total LOC",
    "Num Copied Fns",
    "Num Copied + Modified Fns",
    "Num State Modifications"
  ])

  file_list = os.listdir(test_files_dir)
  file_list.sort()
  for file in file_list:
    extn_name, num_lines, copied_fns, state_fns, state_instances = get_csv_output(file)
    output_list = [extn_name, str(num_lines), str(copied_fns), str(state_fns), str(state_instances)]
    csv_file_writer.writerow(output_list)

  csv_file.close()