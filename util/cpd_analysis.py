# Heuristic script to help me determine potential full functions to put in 
# the sca_functions text files folder.

import os

important_chars = ["{", "}", "(", ")", ";"]

current_working_dir = os.getcwd()
sca_work_dir = "sca_analysis_output"
sca_fn_dir = "sca_analysis_fns"
sca_postgres_dir = "sca_analysis_postgres"
breaker = "====================================================================="
path = current_working_dir + "/" + sca_work_dir

for file in os.listdir(path):
  # extnname_cpd
  file_name = os.path.splitext(file)[0]
  extn_name = file_name[:-4]

  # Open two files
  postgres_err_file = open(current_working_dir + "/" + sca_postgres_dir + "/" + extn_name + "_postgres.txt", "w")
  fn_err_file = open(current_working_dir + "/" + sca_fn_dir + "/" + extn_name + "_fn.txt", "w")

  file_obj = open(path + "/" + file, "r")
  file_lines = file_obj.readlines()
  file_str = "".join(file_lines)
  errors_list = file_str.split(breaker)
  print("Determining potential functions for " + file)
  for err in errors_list:
    flag = True
    for ic in important_chars:
      if ic not in err:
        flag = False
    if "postgresql-15.3/src" in err:
      postgres_err_file.write(err)
      if flag:
        fn_err_file.write(err)

  postgres_err_file.close()
  fn_err_file.close()