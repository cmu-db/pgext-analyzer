POSTGRES_COMMAND="DROP TABLESPACE testts"
$PWD/../../pg-15-dist/bin/psql -d postgres -c "$POSTGRES_COMMAND"
rm -rf testts-tablespace