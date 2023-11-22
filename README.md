# pgext-cli-python
Python scripts for generating compatibility tables (and maybe other things).

# Compatibility Analysis Dependencies
- readline
- flex (for pg_tle)
- mysql (for mysql_fdw), mysql runs with root w password hello123
- libmysqlclient-dev (for mysql_fdw)
- create ~/.my.cnf file with root user and password
- libossp-uuid-dev (uuid-ossp)
- libipc-run-perl
- libperl-dev
- libtinfo5
- ninja-build
- libsybdb5 freetds-dev freetds-common (tds_fdw)

# Source Code Analysis Dependencies
- PMD CPD (https://github.com/pmd/pmd/releases/tag/pmd_releases/7.0.0-rc4)
- sctokenizer (https://pypi.org/project/sctokenizer/)

# Static Analysis Dependencies
- semgrep (pip install semgrep)

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