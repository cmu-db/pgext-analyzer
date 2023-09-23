# pgext-cli-python
Python scripts for generating compatibility tables (and maybe other things).

# Dependencies
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

# extn_info Directory Structure
The `./extn_info` directory contains info on how Postgres extensions are downloaded, installed, and tested.

Each file is named after an extension, and contains the following information about the extension:
- "download_method": This field gives the method in which an extension should be downloaded. Possible values include "contrib", "tar", "zip", and "git". The latter three types indicate the inclusion of an extra key called "download_url", which gives the URL that should be used to extract the archive/git repository.
- configure options: Adds options that should be passed to the ./configure program when installing PostgreSQL
- "install_method": This field gives the method in which an extension should be installed. Possible values include "pgxs" and "shell_script". **Currently only "pgxs" is supported.**
- "test_method": This field gives the method that an extension should be tested. Possible values include "pg_regress" and "shell_script". **Currently only "pg_regress" is supported.**
- When "test_method" == "pg_regress", there's an extra key called "pg_regress", which contains 3 fields: "input_dir", which is where the sql and expected folders are, "options", a list containing extra options, and "test_list", indicating the tests that are to be ran and the order they should be ran in.
- "custom_config": This field is a list of strings that should be written to postgresql.conf before running tests.
- "no_load" and "no_preload" are Boolean fields that indicate whether an extension should not be preloaded (via shared_preload_libraries) or loaded (via CREATE EXTENSION).
- "before_test_scripts": Indicates a shell script to run before running tests.
- "after_test_scripts": Indicates a shell script to run after running tests (mostly for cleanup)

# TODOs
## Dev
- Debug tests.
- Support git branching.
- Document the code.
- Support different testing methods (pgTAP)
- anon requires faker (python dependency)

# Unsupported Extensions (so far)
### Integrated Extensions
- pltcl
- test_parser

Test modules/extensions (test_parser).

### Rust Codebase List
- citus: Must use Ubuntu machine (should move dev env.)
- pg_tiktoken: This one doesn't build in general, outside of rust.
- aqo: is a fork, not an extension (patches header files)
- pg_auto_failover

The rest of these have errors when building on Mac and therefore should be fixed by potentially patching the extension source code.

### AWS List
- aws_commons
- aws_lambda
- aws_s3
- flow_control
- icu
- log_fdw
- pg_transport
- plcoffee
- plls
- rds_tools
- plrust

A lot of these (aws*, flow_control, rds_tools, pg_transport) are closed source, and there's no way to access it except through AWS RDS. Some of these are language extensions (e.g. pllcoffee, plls). plrust is built via cargo and this module doesn't support cargo pgrx yet. Lastly, I can't figure out how to install icu.

### Google Cloud SQL List
- pgoutput

I can't figure out how to install it. 

## Unsupported Testing Frameworks
- pgTAP: used by pgjwt, pg_stat_monitor, pg_partman
- Some extensions (pgtap, anon, rdkit) have customized test suites. Some might be configurable though.

## Unsupported pg_regress problem children
- anon (lots of setup problems)
- mysql_fdw (setup hell)
- oracle_fdw (setup hell)
- pg_querylog (I can't figure out the steps for testing)

# Quirks
- cmudb-demo4 uses port 5432, cmudb-demo5 uses port 5433, cmudb-demo2 uses port 5434