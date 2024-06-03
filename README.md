# pgext-analyzer
Python scripts for generating compatibility tables and source code analysis. This is a work in progress, and for now is badly documented :,) Feel free to submit PRs and offer feedback, we would love to hear it.

# Specifications
- Runs on machine with Ubuntu 22.04. Compatibility with other OSes is not supported.
- The tools use PostgreSQL 15.3. Compatibility with other versions of PostgreSQL is not supported.

# Dependencies
## Compatibility Analysis Dependencies
- readline
- flex (for pg_tle)
- mysql (for mysql_fdw), mysql runs with root
- libmysqlclient-dev (for mysql_fdw)
- create ~/.my.cnf file with root user and password
- libossp-uuid-dev (uuid-ossp)
- libipc-run-perl
- libperl-dev
- libtinfo5
- ninja-build
- libsybdb5 freetds-dev freetds-common (tds_fdw)

## Source Code Analysis Dependencies
- PMD CPD (https://github.com/pmd/pmd/releases/tag/pmd_releases/7.0.0-rc4)
- sctokenizer (https://pypi.org/project/sctokenizer/)

## Static Analysis Dependencies
- semgrep (pip install semgrep)

# Usage
## Compatibility Analysis
- Takes in four arguments, two which are mandatory.
- `--mode` (mandatory): A string value. Can be single (loads, installs, and runs tests on single extensions), pairwise (takes in a list of single extensions, generates pairs, and loads/installs/runs tests on them), pairwise-parallel (takes in a list of pairs of extensions, with a space after each other. e.g, "citus pg_cron" in this file will load and install both citus and pg_cron, then run respective tests.
- `--list`(mandatory): the text file containing a list of extensions. Must be compatible with mode argument. For instance, if you run compatibility_analysis.py with mode argument "single" but with pairwise list of extensions, the program won't work.
- `--port`: Port argument (default 5432). Will run PostgreSQL on a different port if needed. Probably useful if you're running something on port 5432...
- `--exit-flag`: If this argument is set, then this program will exit as soon as tests fail. It's mainly here for debugging purposes.

To run this program (as an example): (foo.txt doesn't exist)
```python
python3 compatibility_analysis.py --mode=pairwise-parallel --list=extn_list/foo.txt --port=5430
```

# extn_info Directory Structure
The `./extn_info` directory contains info on how Postgres extensions are downloaded, installed, and tested.

Each file is named after an extension, and contains the following information about the extension:
- "download_method": This field gives the method in which an extension should be downloaded. Possible values include "contrib", "tar", "zip", and "git". The latter three types indicate the inclusion of an extra key called "download_url", which gives the URL that should be used to extract the archive/git repository.
- "configure_options": Adds options that should be passed to the ./configure program when installing PostgreSQL
- "install_method": This field gives the method in which an extension should be installed. Possible values include "pgxs" and "shell_script". **Currently only "pgxs" is supported.**
- "test_method": This field gives the method that an extension should be tested. Possible values include "pg_regress" and "shell_script". **Currently only "pg_regress" is supported.**
- When "test_method" == "pg_regress", there's an extra key called "pg_regress", which contains 3 fields: "input_dir", which is where the sql and expected folders are, "options", a list containing extra options, and "test_list", indicating the tests that are to be ran and the order they should be ran in.
- "custom_config": This field is a list of strings that should be written to postgresql.conf before running tests.
- "no_load" and "no_preload" are Boolean fields that indicate whether an extension should not be preloaded (via shared_preload_libraries) or loaded (via CREATE EXTENSION).
- "before_test_scripts": Indicates a shell script to run before running tests.
- "after_test_scripts": Indicates a shell script to run after running tests (mostly for cleanup)
- "source_dir": Set for extensions that are not contrib extensions. Determines where the source code for this extension is located.
- "sql_dirs" and "sql_files": Contains a list of directories/files that contain SQL files that *define* the extension's functionality (e.g. defining functions or custom types)

# Acknowledgements
Huge shoutout to both Marco Slot and Erik Nordström for helping me with Citus + Timescale respectively. :)
