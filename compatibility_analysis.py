import argparse
import csv
from datetime import datetime
import json
import os
import subprocess
import sys

# File paths (globals)
current_working_dir = os.getcwd()
pg_dist_dir = "pg-15-dist"
pg_data_dir = "pg-15-data"
ext_work_dir = "pgextworkdir"
extn_info_dir = "extn_info"
pg_config_path = current_working_dir + "/" + pg_dist_dir + "/bin/pg_config"
now = datetime.now()
date_time = now.strftime("%m-%d-%Y_%H:%M")
testing_output_dir = "testing-output-" + date_time
postgres_version = "15.3"
default_port_num = 5432
port_num = 5432
exit_flag = False

# Load extension database
extn_files = os.listdir(current_working_dir + "/" + extn_info_dir)
extn_db = {}
for file in extn_files:
  extn_info_file = open(current_working_dir + "/" + extn_info_dir + "/" + file, "r")
  extn_info_json = json.load(extn_info_file)
  key = os.path.splitext(file)[0]
  extn_db[key] = extn_info_json

#####################################################################
# UTILITY HELPER FUNCTIONS
#####################################################################

def get_terminal_file_name(first_extn, second_extn=""):
  test_extn_dir = first_extn if second_extn == "" else first_extn + "_" + second_extn
  subprocess.run("mkdir " + test_extn_dir, shell=True, cwd=current_working_dir + "/" + testing_output_dir)
  subprocess.run("touch terminal.txt", shell=True, cwd=current_working_dir + "/" + testing_output_dir + "/" + test_extn_dir)
  return test_extn_dir, current_working_dir + "/" + testing_output_dir + "/" + test_extn_dir + "/terminal.txt"

def get_terminal_file(first_extn, second_extn=""):
  test_extn_dir, terminal_file_name = get_terminal_file_name(first_extn, second_extn)
  terminal_file = open(terminal_file_name, "a")
  return test_extn_dir, terminal_file

def get_file_extns_list(file_extns_filename):
  f = open(file_extns_filename, "r")
  file_extns_list = f.readlines()
  file_extns_list = list(map(lambda x: x.strip("\n"),file_extns_list))
  f.close()
  return file_extns_list

def get_file_extn_pairs_list(file_extns_filename):
  f = open(file_extns_filename, "r")
  file_extns_list = f.readlines()
  file_extns_list = list(map(lambda x: tuple(x.strip("\n").split(" ")),file_extns_list))
  f.close()
  return file_extns_list

def get_dependencies(extn):
  dep_list = []
  if "dependencies" in extn_db[extn]:
    for dep in extn_db[extn]["dependencies"]:
      dep_list += get_dependencies(dep) + [dep]
  
  return dep_list

#####################################################################
# DOWNLOADING + INSTALLING POSTGRES EXTENSIONS HELPER FUNCTIONS
#####################################################################

def install_extn(extn_name, extn_entry, terminal_file):
  print("Installing " + extn_name)
  install_type = extn_entry["install_method"]

  if install_type == "installed":
    return
  elif install_type == "pgxs":
    install_extn_dir = current_working_dir + "/" + ext_work_dir + "/" + extn_entry["folder_name"]
    subprocess.run("make USE_PGXS=1 PG_CONFIG=" + pg_config_path + " -j8", shell=True, cwd=install_extn_dir, stdout=terminal_file, stderr=terminal_file)
    subprocess.run("make USE_PGXS=1 PG_CONFIG=" + pg_config_path + " install -j8", shell=True, cwd=install_extn_dir, stdout=terminal_file, stderr=terminal_file)
  elif install_type == "shell_script":
    # Copy shell script over to the installation directory and run it.
    install_extn_dir = current_working_dir + "/" + ext_work_dir + "/" + extn_entry["folder_name"]
    script_name = extn_entry["shell_script"]
    subprocess.run("cp ./extn_scripts/" + script_name + " " + install_extn_dir, shell=True, cwd=current_working_dir, stdout=terminal_file, stderr=terminal_file)
    subprocess.run("./" + script_name, shell=True, cwd=install_extn_dir, stdout=terminal_file, stderr=terminal_file)
  else:
    sys.exit("Could not install extension" + extn_name)

def download_install_extn(extn_name, extn_entry, terminal_file):
  print("Downloading extension " + extn_name)
  extension_dir = current_working_dir + "/" + ext_work_dir
  download_type = extn_entry["download_method"]

  if download_type == "contrib" or download_type == "downloaded":
    return
  elif download_type == "git":
    git_repo = extn_entry["download_url"]
    subprocess.run("git clone " + git_repo, shell=True, cwd=extension_dir, stdout=terminal_file, stderr=terminal_file)
    install_extn(extn_name, extn_entry, terminal_file)
  elif download_type == "tar" or download_type == "zip":
    url = extn_entry["download_url"]
    base_name = os.path.basename(url)
    subprocess.run("wget " + url, shell=True, cwd=extension_dir, stdout=terminal_file, stderr=terminal_file)
    if download_type == "tar":
      subprocess.run("tar -xvf " + base_name, shell=True, cwd=extension_dir, stdout=terminal_file, stderr=terminal_file)
    elif download_type == "zip":
      subprocess.run("unzip " + base_name, shell=True, cwd=extension_dir, stdout=terminal_file, stderr=terminal_file)
    subprocess.run("rm " + base_name, shell=True, cwd=extension_dir, stdout=terminal_file, stderr=terminal_file)
    install_extn(extn_name, extn_entry, terminal_file)
  else:
    sys.exit("Could not find download and install method")

def get_extns_to_install(extn_list):
  extns_to_install = []
  for extn in extn_list:
    potentials = get_dependencies(extn) + [extn]
    for p in potentials:
      if p not in extns_to_install:
        extns_to_install.append(p)

  return extns_to_install

def download_install_extn_list(extn_list):
  extns_to_install = get_extns_to_install(extn_list)
  subprocess.run("touch installation_terminal.txt", shell=True, cwd=current_working_dir + "/" + testing_output_dir)
  terminal_file = open(current_working_dir + "/" + testing_output_dir + "/installation_terminal.txt", "w")
  for extn in extns_to_install:
    download_install_extn(extn, extn_db[extn], terminal_file)

def post_install_extn(extn, terminal_file):
  # Post install: scripts ran after the database server has started.
  extn_entry = extn_db[extn]
  if "post_install_shell_script" in extn_entry:
    # Copy shell script over to the installation directory and run it.
    install_extn_dir = current_working_dir + "/" + ext_work_dir + "/" + extn_entry["folder_name"]
    script_name = extn_entry["post_install_shell_script"]
    subprocess.run("cp ./extn_scripts/" + script_name + " " + install_extn_dir, shell=True, cwd=current_working_dir, stdout=terminal_file, stderr=terminal_file)
    subprocess.run("./" + script_name, shell=True, cwd=install_extn_dir, stdout=terminal_file, stderr=terminal_file)

def post_install_extn_pair(first_extn, second_extn, terminal_file):
  extn_list = get_dependencies(first_extn) + [first_extn]
  for dep in get_dependencies(second_extn) + [second_extn]:
    if dep not in extn_list:
      extn_list.append(dep)

  for extn in extn_list:
    post_install_extn(extn, terminal_file)

#####################################################################
# INSTALLING POSTGRES HELPER FUNCTIONS
#####################################################################

def install_postgres(postgres_config_options = []):
  print("Installing Postgres " + postgres_version + "...")
  postgres_dir = current_working_dir + "/postgresql-" + postgres_version
  prefix = current_working_dir + "/" + pg_dist_dir
  config_options_str = ""

  for opt in postgres_config_options:
    config_options_str += opt + " "

  subprocess.run("./configure --prefix=" + prefix + " " + config_options_str, capture_output=True, shell=True, cwd=postgres_dir)
  subprocess.run("make clean", capture_output=True, shell=True, cwd=postgres_dir)
  subprocess.run("make world-bin -j8", capture_output=True, shell=True, cwd=postgres_dir)
  subprocess.run("make install-world-bin -j8", capture_output=True, shell=True, cwd=postgres_dir)
  print("Done installing Postgres " + postgres_version + "...")

def get_configure_options(extns_to_install):
  config_options = []
  for extn in extns_to_install:
    extn_entry = extn_db[extn]
    if "configure_options" in extn_entry:
      configure_options_list = extn_entry["configure_options"]
      for elem in configure_options_list:
        if elem not in config_options:
          config_options.append(elem)
  
  return config_options

def reinstall_postgres(extns_to_install, current_config_options):
  config_options = get_configure_options(extns_to_install)
  if set(config_options) == set(current_config_options):
    return current_config_options

  subprocess.run("rm -rf " + pg_dist_dir, cwd=current_working_dir, shell=True)
  install_postgres(config_options)
  return config_options

#####################################################################
# SETUP AND CLEANUP HELPER FUNCTIONS
#####################################################################

def initial_setup():
  url = "https://ftp.postgresql.org/pub/source/v" + postgres_version + "/postgresql-" + postgres_version + ".tar.gz"
  subprocess.run("wget " + url, cwd=current_working_dir, shell=True)
  subprocess.run("tar -xvf postgresql-" + postgres_version + ".tar.gz", cwd=current_working_dir, shell=True, capture_output=True)
  subprocess.run("mkdir " + ext_work_dir, cwd=current_working_dir, shell=True)
  subprocess.run("mkdir " + testing_output_dir, cwd=current_working_dir, shell=True)

def cleanup(delete_ext_dir=True):
  subprocess.run("rm -rf " + pg_data_dir, cwd=current_working_dir, shell=True)
  subprocess.run("rm logfile", cwd=current_working_dir, shell=True)
  if delete_ext_dir:
    subprocess.run("rm -rf *", cwd=current_working_dir + "/" + ext_work_dir, shell=True)

def final_cleanup():
  postgres_folder = "postgresql-" + postgres_version
  subprocess.run("rm -rf " + postgres_folder + " " + postgres_folder + ".tar.gz " + ext_work_dir + " " + pg_dist_dir, cwd=current_working_dir, shell=True)

#####################################################################
# POSTGRES COMMANDS HELPER FUNCTIONS
#####################################################################

def modify_postgresql_conf(extns_to_install):
  postgres_conf = open("./" + pg_data_dir + "/postgresql.conf", "a")
  if port_num != default_port_num:
    postgres_conf.write("port = " + str(port_num) + "\n")

  # Modify shared preload libraries
  extns_to_preload = []
  for extn in extns_to_install:
    extn_entry = extn_db[extn]
    if "no_preload" in extn_entry:
      continue
    elif "preload_name" in extn_entry:
      extns_to_preload.append(extn_entry["preload_name"])
    elif "preload_first" in extn_entry:
      extns_to_preload.insert(0, extn)
    else:
      extns_to_preload.append(extn)

  shared_preload_lib_str = ','.join(extns_to_preload)
  postgres_conf.write("shared_preload_libraries = '" + shared_preload_lib_str + "'" + "\n")

  # Write custom configuration
  for extn in extns_to_install:
    extn_entry = extn_db[extn]
    if "custom_config" in extn_entry:
      extra_config = extn_entry["custom_config"]
      for setting in extra_config:
        if "$PATH" in setting:
          extn_source_dir = current_working_dir + "/" + ext_work_dir + "/" + extn_entry["folder_name"]
          setting = setting.replace("$PATH", extn_source_dir)
        postgres_conf.write(setting + "\n")
  postgres_conf.close()

def init_db(terminal_file):
  # Run initdb
  print("Running initdb...")
  subprocess.run("./" + pg_dist_dir +  "/bin/initdb -D " + pg_data_dir, shell=True, cwd=current_working_dir, stdout=terminal_file, stderr=terminal_file)

def start_postgres(terminal_file):
  # Run Postgres database
  # pg-15-dist/bin/pg_ctl -D pg-15-data -l logfile start
  print("Starting Postgres...")
  subprocess.run("./" + pg_dist_dir + "/bin/pg_ctl -D " + pg_data_dir + " -l logfile start", cwd=current_working_dir, shell=True, stdout=terminal_file, stderr=terminal_file)

def stop_postgres(terminal_file):
  print("Stopping Postgres...")
  subprocess.run("./" + pg_dist_dir + "/bin/pg_ctl -D " + pg_data_dir + " -l logfile stop", cwd=current_working_dir, shell=True, stdout=terminal_file, stderr=terminal_file)

#####################################################################
# TESTING INFRASTRUCTURE FUNCTIONS
#####################################################################

def load_extn_str(test_extn, compat_extn, loaded_extns):
  load_ext_setting = ""
  for dep in get_dependencies(compat_extn) + [compat_extn]:
    if dep != test_extn and "no_create_extn" not in extn_db[dep] and dep not in loaded_extns:
      load_ext_setting += "--load-extension=" + dep + " "
  return load_ext_setting

def pg_regress_test(test_extn, compat_extn, test_extn_dir, terminal_file):
  print("Testing " + test_extn + "...")
  val = True
  file_path = ""
  test_extn_entry = extn_db[test_extn]
  if test_extn_entry["download_method"] == "contrib":
    file_path = "postgresql-" + postgres_version + "/contrib/" + test_extn_entry["folder_name"]
  else:
    file_path = ext_work_dir + "/" + test_extn_entry["folder_name"]

  test_pg_regress_entry = test_extn_entry["pg_regress"]
  if "input_dir" not in test_pg_regress_entry:
    sys.exit("Failed extn_db.json file for " + test_extn)

  input_dir = file_path + "/" + test_pg_regress_entry["input_dir"]  
  sql_dir = input_dir + "/sql"
  sql_files_list = os.listdir(current_working_dir + "/" + sql_dir)
  expected_dir = input_dir + "/expected" 
  if "expected_dir" in test_pg_regress_entry:
    expected_dir = file_path + "/" + test_pg_regress_entry["expected_dir"] + "/expected"
    subprocess.run("cp -R expected " + test_pg_regress_entry["input_dir"], cwd=current_working_dir + "/" + expected_dir + "/..", shell=True, stderr=terminal_file, stdout=terminal_file)
  expected_files_list = os.listdir(current_working_dir + "/" + expected_dir)

  test_list = test_pg_regress_entry["test_list"]
  if len(test_list) == 0:
    sys.exit("Testing cannot happen on extension " + test_extn + ", test_list length is 0")

  for elem in test_list:
    if elem + ".sql" not in sql_files_list:
      sys.exit(elem + ".sql file does not exist in SQL files list")
    if elem + ".out" not in expected_files_list:
      sys.exit(elem + ".out file does not exist in expected files list")

  pg_regress_cmd = current_working_dir + "/" + pg_dist_dir + "/lib/postgresql/pgxs/src/test/regress/pg_regress"
  output_dir = testing_output_dir + "/" + test_extn_dir
  input_dir_setting = "--inputdir=" + test_pg_regress_entry["input_dir"]
  bin_dir_setting = "--bindir=" + current_working_dir + "/" + pg_dist_dir + "/bin"

  loaded_extns = []
  custom_setting = ""
  if "options" in test_pg_regress_entry:
    for elem in test_pg_regress_entry["options"]:
      if elem.startswith("--launcher="):
        path_to_launcher = elem[16:]
        extn_source_dir = current_working_dir + "/" + ext_work_dir + "/" + test_extn_entry["folder_name"]
        path_to_launcher = extn_source_dir + path_to_launcher
        custom_setting += "--launcher=" + path_to_launcher + " "
      else:
        custom_setting += elem + " "

      if elem.startswith("--load-extension="):
        loaded_extn = elem[17:]
        loaded_extns.append(loaded_extn)

  load_ext_setting = ""
  if compat_extn != "":
    load_ext_setting = load_extn_str(test_extn, compat_extn, loaded_extns)

  test_list_str = ' '.join(test_list)
  total_command = pg_regress_cmd + " --outputdir=./ "
  total_command += bin_dir_setting + " " + input_dir_setting + " "
  total_command += custom_setting + " " + load_ext_setting + " " + test_list_str
  
  # Handle port
  if port_num != default_port_num:
    total_command += " --port=" + str(port_num) + " "

  # Handle path variable
  if "path" in test_pg_regress_entry:
    total_command = "PATH=" + test_pg_regress_entry["path"] + " " + total_command

  run_test_dir = current_working_dir + "/" + file_path
  before_test_scripts_txt = ""
  if "before_test_scripts" in test_extn_entry:
    before_test_scripts_txt = " && ".join(test_extn_entry["before_test_scripts"])
    total_command = before_test_scripts_txt + " && " + total_command
  
  if "env" in test_extn_entry:
    env_var_list = test_extn_entry["env"]
    env_var_list = list(map(lambda x: "export " + x, env_var_list))
    env_txt = " && ".join(env_var_list)
    total_command = env_txt + " && " + total_command

  test_res = subprocess.run(total_command, shell=True, cwd=run_test_dir, stdout=terminal_file, stderr=terminal_file)
  if test_res.returncode == 0:
    print("Tests for extension " + test_extn + " passed!")
  elif test_res.returncode == 1:
    print("Tests for extension " + test_extn + " failed!")
    val = False
    subprocess.run("cp -R results " + current_working_dir + "/" + output_dir, shell=True, cwd=run_test_dir,  stdout=terminal_file, stderr=terminal_file)
    os.rename(run_test_dir + "/regression.out", current_working_dir + "/" + output_dir + "/" + test_extn + ".out")
    os.rename(run_test_dir + "/regression.diffs", current_working_dir + "/" + output_dir + "/" + test_extn + ".diffs")
    subprocess.run("cp logfile " + current_working_dir + "/" + output_dir, shell=True, cwd=current_working_dir,  stdout=terminal_file, stderr=terminal_file)
    if exit_flag: 
      sys.exit("Exiting out of pgext-cli-python...")
  elif test_res.returncode == 2:
    print("Tests for extension " + test_extn + " could not run!")
    val = False
    subprocess.run("cp logfile " + current_working_dir + "/" + output_dir, shell=True, cwd=current_working_dir,  stdout=terminal_file, stderr=terminal_file)
    if exit_flag: 
      sys.exit("Exiting out of pgext-cli-python...")
  
  if "after_test_scripts" in test_extn_entry:
    after_test_scripts = test_extn_entry["after_test_scripts"]
    for ats in after_test_scripts:
      subprocess.run(ats, shell=True, cwd=run_test_dir,  stdout=terminal_file, stderr=terminal_file)
 
  return val

def custom_script_test(test_extn, compat_extn, test_extn_dir, terminal_file):
  # compare output to the expected
  # return true or false depending on this output
  # if wrong also copy logfile and the bad output
  test_extn_entry = extn_db[test_extn]
  extn_source_dir = current_working_dir + "/" + ext_work_dir + "/" + test_extn_entry["folder_name"]
  custom_test_script = test_extn_entry["custom_test_script"]
  test_script = custom_test_script["script"]
  expected_output_file_name = custom_test_script["expected"]
  output_dir = current_working_dir + "/" + testing_output_dir + "/" + test_extn_dir

  total_command = "./" + test_script

  env_var_list = []

  # Get compatible extension configs 
  if compat_extn != "":
    env_var_list += ["COMPATIBLE_EXTENSION="+compat_extn]

    compat_extn_entry = extn_db[compat_extn]
    if "custom_config" in compat_extn_entry:
      extra_config_string = ';'.join(compat_extn_entry["custom_config"])
      env_var_list += ["EXTRA_CONFIGS="+extra_config_string]

  # Determine environment variables
  if "env" in test_extn_entry:
    env_var_list += test_extn_entry["env"]

  if env_var_list:
    export_list = list(map(lambda x: "export " + x, env_var_list))
    env_txt = " && ".join(export_list)
    total_command = env_txt + " && " + total_command

  # Run tests
  subprocess.run("cp ./extn_scripts/" + test_script + " " + extn_source_dir, shell=True, cwd=current_working_dir, stdout=terminal_file, stderr=terminal_file)
  
  # Create the compatible extension if it exists
  if compat_extn != "":
    # Install compatible extensions onto template1
    test_extn_deps = get_dependencies(test_extn) + [test_extn]
    extns_to_load = []
    for dep in get_dependencies(compat_extn) + [compat_extn]:
      if dep not in test_extn_deps:
        extns_to_load.append(dep)

    for extn in extns_to_load:
      extn_entry = extn_db[extn]
      if "no_create_extn" not in extn_entry:
        subprocess.run("./" + pg_dist_dir + "/bin/psql --port=" + str(port_num) + " -c \"CREATE EXTENSION " + extn + ";\" template1", shell=True, cwd=current_working_dir, stdout=terminal_file, stderr=terminal_file)

  # Run testing command
  test_proc = subprocess.run(total_command, shell=True, cwd=extn_source_dir, capture_output=True)
  terminal_file.write(test_proc.stdout.decode('utf-8'))
  expected_output_file = open(current_working_dir + "/extn_test_results/" + expected_output_file_name, "r")
  expected_output = expected_output_file.read()
  test_output = test_proc.stdout.decode('utf-8')
  expected_location = custom_test_script["expected_location"]

  test_broken = True
  if expected_location == "beginning":
    if test_output.startswith(expected_output):
      test_broken = False
  elif expected_location == "end":
    if test_output.endswith(expected_output):
      test_broken = False
  elif expected_location == "all":
    if test_output == expected_output:
      test_broken = False

  if (test_broken):
    print("Tests for extension " + test_extn + " failed!")
    subprocess.run("cp logfile " + output_dir, shell=True, cwd=current_working_dir, stdout=terminal_file, stderr=terminal_file)
    fail_files = custom_test_script["fail_files"]
    fail_file_names = custom_test_script["fail_file_names"]

    for i in range(len(fail_files)):
      os.rename(extn_source_dir + "/" + fail_files[i], output_dir + "/" + fail_file_names[i])

    if exit_flag: 
      sys.exit("Exiting out of pgext-cli-python...")
    return False
  
  print("Tests for extension " + test_extn + " passed!")
  return True

def pgbench_test(test_extn, compat_extn, terminal_file):
   # Create and load database with extensions
  val = True
  subprocess.run("./" + pg_dist_dir + "/bin/createdb -p " + str(port_num) + " pgbench_test", shell=True, cwd=current_working_dir, stdout=terminal_file, stderr=terminal_file)
  extns_to_load = []
  for dep in get_dependencies(test_extn) + [test_extn]:
    if dep not in extns_to_load:
      extns_to_load.append(dep)

  for dep in get_dependencies(compat_extn) + [compat_extn]:
    if dep not in extns_to_load:
      extns_to_load.append(dep)

  for extn in extns_to_load:
    extn_entry = extn_db[extn]
    if "no_create_extn" not in extn_entry:
      subprocess.run("./" + pg_dist_dir + "/bin/psql --port=" + str(port_num) + " -c \"CREATE EXTENSION " + extn + ";\" pgbench_test", shell=True, cwd=current_working_dir, stdout=terminal_file, stderr=terminal_file)

  # Run pgbench
  subprocess.run("./" + pg_dist_dir + "/bin/pgbench -i -s 10 -p " + str(port_num) + " pgbench_test", shell=True, cwd=current_working_dir, stdout=terminal_file, stderr=terminal_file)
  res = subprocess.run("./" + pg_dist_dir + "/bin/pgbench -p " + str(port_num) + " --no-vacuum  -T 30 -j 8 pgbench_test", shell=True, cwd=current_working_dir, capture_output=True)
  if res.returncode == 0:
    res_output = res.stdout.splitlines()
    for line in res_output:
      line_str = line.decode('utf-8')
      if line_str.startswith("number of failed transactions:"):
        failed_txn_num = int(line_str.split(' ')[4])
        if failed_txn_num > 0:
          val = False
  else:
    val = False

  subprocess.run("./" + pg_dist_dir + "/bin/dropdb -p " + str(port_num) +  " pgbench_test", shell=True, cwd=current_working_dir, stdout=terminal_file, stderr=terminal_file)
  return val

def compatibility_test(first_extn, second_extn, test_extn_dir, terminal_file):
  print("Running compatibility testing for " + first_extn + " and " + second_extn)
  val = True

  # Post install scripts
  post_install_extn_pair(first_extn, second_extn, terminal_file)

  first_extn_entry = extn_db[first_extn]
  second_extn_entry = extn_db[second_extn]
  if "test_method" in first_extn_entry:
    first_test_type = first_extn_entry["test_method"]
    if first_test_type == "pg_regress":
      res = pg_regress_test(first_extn, second_extn, test_extn_dir, terminal_file)
      val = val and res
    elif first_test_type == "custom_test_script":
      res = custom_script_test(first_extn, second_extn, test_extn_dir, terminal_file)
      val = val and res

  if "test_method" in second_extn_entry:
    second_test_type = second_extn_entry["test_method"]
    if second_test_type == "pg_regress":
      res = pg_regress_test(second_extn, first_extn, test_extn_dir, terminal_file)
      val = val and res
    elif second_test_type == "custom_test_script":
      res = custom_script_test(second_extn, first_extn, test_extn_dir, terminal_file)
      val = val and res

  if not val:
    return False

  return pgbench_test(first_extn, second_extn, terminal_file) and pgbench_test(second_extn, first_extn, terminal_file)

# Returns (tests_exist, tests_pass)
def single_test(extn, extn_entry, test_extn_dir, terminal_file):
  print("Running single testing on " + extn)
  tests_pass = "not supported"
  tests_exist = "no"
  if "test_method" in extn_entry:
    tests_exist = "yes"
    test_method = extn_entry["test_method"]
    if test_method == "pg_regress":
      tests_pass = "yes" if pg_regress_test(extn, "", test_extn_dir, terminal_file) else "no"
    elif test_method == "custom_test_script":
      tests_pass = "yes" if custom_script_test(extn, "", test_extn_dir, terminal_file) else "no"

  return (tests_exist, tests_pass)

#####################################################################
# PAIRWISE TESTING MODE
#####################################################################
def delete_working_pairs(file_extn_pairs, extn_compat_list):
  for i in range(0, len(file_extn_pairs)):
    file_extn_pair = file_extn_pairs[i]
    test_extn_dir = file_extn_pair[0] + "_" + file_extn_pair[1]
    if extn_compat_list[i]:
      subprocess.run("rm -rf " + test_extn_dir, shell=True, cwd=current_working_dir + "/" + testing_output_dir)

def pairwise_validation_helper(file_extns_list):
  if len(file_extns_list) < 2:
    sys.exit("Must test compatibility of >= 2 extensions.")

  for extn in file_extns_list:
    if extn not in extn_db:
      sys.exit("Extension " + extn + " not in extension DB.")
    extn_entry = extn_db[extn]
    if "download_method" not in extn_entry:
      sys.exit("Extension " + extn + " cannot be downloaded.")
    if "install_method" not in extn_entry:
      sys.exit("Extension " + extn + " cannot be installed.")

def pairwise_testing_helper(file_extn_pairs):
  initial_setup()
  extn_compat_list = []
  current_configure_options = []
  install_postgres(current_configure_options)
  for (first_extn, second_extn) in file_extn_pairs:
    print("Determining compatibility betweeen " + first_extn + " and " + second_extn)
    
    # Get a list of extensions to download and install
    extns_to_install = get_extns_to_install([first_extn, second_extn])
    current_configure_options = reinstall_postgres(extns_to_install, current_configure_options)
    test_extn_dir, terminal_file = get_terminal_file(first_extn, second_extn)

    for extn in extns_to_install:
      download_install_extn(extn, extn_db[extn], terminal_file)

    init_db(terminal_file)
    modify_postgresql_conf(extns_to_install)
    start_postgres(terminal_file)

    extn_compat_list.append(compatibility_test(first_extn, second_extn, test_extn_dir, terminal_file))
    stop_postgres(terminal_file)
    terminal_file.close()
    cleanup()
  
  delete_working_pairs(file_extn_pairs, extn_compat_list)
  final_cleanup()
  return extn_compat_list

def pairwise_parallel_testing_helper(file_extn_pairs, file_extn_list):
  initial_setup()
  extn_compat_list = []
  install_postgres(get_configure_options(file_extn_list))
  download_install_extn_list(file_extn_list)

  for (first_extn, second_extn) in file_extn_pairs:
    print("Determining compatibility betweeen " + first_extn + " and " + second_extn)
    extns_to_install = get_extns_to_install([first_extn, second_extn])
    test_extn_dir, terminal_file = get_terminal_file(first_extn, second_extn)

    init_db(terminal_file)
    modify_postgresql_conf(extns_to_install)
    start_postgres(terminal_file)

    # Run tests
    extn_compat_list.append(compatibility_test(first_extn, second_extn, test_extn_dir, terminal_file))
    stop_postgres(terminal_file)
    terminal_file.close()
    cleanup(False)
  
  delete_working_pairs(file_extn_pairs, extn_compat_list)
  final_cleanup()
  return extn_compat_list

def pairwise_mode(file_extns_filename):
  file_extns_list = get_file_extns_list(file_extns_filename)
  pairwise_validation_helper(file_extns_list)

  # Get the pairs of extensions to test
  file_extn_pairs = []
  for first_item in file_extns_list:
    for second_item in file_extns_list:
      if first_item == second_item:
        continue
      else:
        file_extn_pairs.append((first_item, second_item))
  
  extn_compat_list = pairwise_testing_helper(file_extn_pairs)
  for i in range(len(extn_compat_list)):
    print(str(file_extn_pairs[i]) + ": " + str(extn_compat_list[i]))

  i = 0
  compat_csv_file = open("pairwise.csv", "w")
  writer = csv.writer(compat_csv_file)
  writer.writerow(["first =>>"] + file_extns_list)

  for extn in file_extns_list:
    row_to_write = [extn]
    for other_extn in file_extns_list:
      if other_extn == extn:
        row_to_write.append("n/a")
      else:
        val = "yes" if extn_compat_list[i] else "no"
        row_to_write.append(val)
        i += 1
    writer.writerow(row_to_write)
  
  compat_csv_file.close()

def pairwise_parallel_mode(file_extns_filename):
  file_extn_pairs = get_file_extn_pairs_list(file_extns_filename)
  file_extns_list = list(map(lambda x: [x[0], x[1]],file_extn_pairs))
  file_extns_list = [item for sublist in file_extns_list for item in sublist]
  file_extns_list = list(set(file_extns_list))
  pairwise_validation_helper(file_extns_list)
  extn_compat_list = pairwise_parallel_testing_helper(file_extn_pairs, file_extns_list)
  for i in range(len(extn_compat_list)):
    print(str(file_extn_pairs[i]) + ": " + str(extn_compat_list[i]))
  compat_csv_file = open("pairwise_parallel.csv", "w")
  writer = csv.writer(compat_csv_file)
  for i in range(0, len(extn_compat_list)):
    row_to_write = [file_extn_pairs[i][0], file_extn_pairs[i][1], str(extn_compat_list[i])]
    writer.writerow(row_to_write)
  
  compat_csv_file.close()

#####################################################################
# SINGLE TESTING MODE
#####################################################################

def single_mode(file_extns_filename):
  file_extns_list = get_file_extns_list(file_extns_filename)

  initial_setup()
  current_configure_options = []
  install_postgres(current_configure_options)
  single_csv_file = open("single.csv", "w")
  writer = csv.writer(single_csv_file)
  writer.writerow(["extension", "in extn db", "runs in driver", "tests exist", "tests pass"])

  for extn in file_extns_list:
    if extn not in extn_db:
      result_list = [extn, "no", "not supported", "not supported", "not supported"]
      writer.writerow(result_list)
      continue

    extn_entry = extn_db[extn]
    deps = extn_entry['dependencies']
    dep_not_supported = False
    for dep in deps:
      if dep not in extn_db:
        dep_not_supported = True
        break
    
    if dep_not_supported:
      tests_exist = "yes" if "test_method" in extn_entry else "no"
      result_list = [extn, "yes", "no", tests_exist, "not supported"]
      writer.writerow(result_list)
      continue

    if "download_method" not in extn_entry or "install_method" not in extn_entry:
      tests_exist = "yes" if "test_method" in extn_entry else "no"
      result_list = [extn, "yes", "no", tests_exist, "not supported"]
      writer.writerow(result_list)
      continue
        
    extns_to_install = deps + [extn]
    current_configure_options = reinstall_postgres(extns_to_install, current_configure_options)
    test_extn_dir, terminal_file_name = get_terminal_file_name(extn)
    terminal_file = open(terminal_file_name, "a")

    for extn in extns_to_install:
      download_install_extn(extn, extn_db[extn], terminal_file)

    init_db(terminal_file)
    modify_postgresql_conf(extns_to_install)
    start_postgres(terminal_file)
    result = single_test(extn, extn_entry, test_extn_dir, terminal_file)

    stop_postgres(terminal_file)
    terminal_file.close()
    cleanup()
    writer.writerow([extn, "yes", "yes"] + list(result))

  final_cleanup()

if __name__ == '__main__':
  # Argument parsing
  parser = argparse.ArgumentParser(
                    description='Runs compatibility testing.')
  parser.add_argument('-l', '--list', action='store', help='text file with list of extensions')
  parser.add_argument('-m', '--mode', action='store', help='Determine whether to run compatibility testing or single extension testing.')
  parser.add_argument('-p', '--port', action='store', help='Optional port number (default is 5432)')
  parser.add_argument('-x', '--exit-flag', action='store_true', help='Changes the value of the exit flag, which determines whether this program exits after failed tests.')
  args = parser.parse_args()
  args_dict = vars(args)
  extn_list_filename = args_dict['list']
  if extn_list_filename is None:
    sys.exit("No list argument parameter.")

  mode = args_dict['mode']
  if mode is None:
    sys.exit("No mode parameter.")

  port_str = args_dict['port']
  if port_str is not None:
    port_num = int(port_str)

  exit_flag_val = args_dict['exit_flag']
  if exit_flag_val is not None:
    exit_flag = exit_flag_val

  # Four modes will be supported.
  # Single: testing a single extension
  # Pairwise: testing pairs of extensions. Single machine.
  # Pairwise parallel: testing pairs of extensions, in parallel.
  # Combinatorial testing: testing via combinatorial table
  if mode == 'single':
    single_mode(extn_list_filename)
  elif mode == 'pairwise':
    pairwise_mode(extn_list_filename)
  elif mode == 'pairwise-parallel':
    pairwise_parallel_mode(extn_list_filename)
  elif mode == 'combinatorial':
    ### TODO: Support combinatorial mode
    print("Combinatorial mode not supported yet!")
