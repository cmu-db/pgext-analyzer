mkdir testts-tablespace
TESTTS_TABLESPACE="$PWD/testts-tablespace"
POSTGRES_COMMAND="CREATE TABLESPACE testts location '$TESTTS_TABLESPACE'"
$PWD/../../pg-15-dist/bin/psql -d postgres -c "$POSTGRES_COMMAND"