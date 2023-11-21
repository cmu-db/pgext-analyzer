# TODOs
Contains a list of personal notes and TODOs.

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