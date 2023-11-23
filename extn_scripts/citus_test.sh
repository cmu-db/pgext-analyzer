#!/bin/bash
# Citus creates its own postgres instances, but we can ask it to
# load additional extensions by slightly abusing the EXTRA_TESTS
# variable, which is injected into the pg_regress command line.
#
# We currently only run the basic test suite (check-multi) and filter
# the tests passed line from the output, because the total output
# contains absolute paths and timings.

if [[ ! -z "${CREATE_EXTENSIONS}" ]]; then
  EXTRA_TESTS=$(echo ${CREATE_EXTENSIONS} | sed -e 's/,/ --load-extension=/')
  EXTRA_TESTS="--load-extension=${EXTRA_TESTS}"
fi

# Also inject the extension-specific configurations and shared_preload_libraries
# via the temp-config argument.
export EXTRA_TESTS="--temp-config=../../pg-15-data/postgresql.conf ${EXTRA_TESTS}"
echo $EXTRA_TESTS > /tmp/extra.txt

make -C src/test/regress check-multi | tee src/test/regress/check-multi.log | grep "tests passed"
