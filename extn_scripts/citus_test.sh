# Citus creates its own postgres instances, but we can ask it to
# load additional extensions by slightly abusing the EXTRA_TESTS
# variable, which is injected into the pg_regress command line.
#
# We currently only run the basic test suite (check-multi) and filter
# the tests passed line from the output, because the total output
# contains absolute paths and timings.

EXTRA_TESTS=

if [[ ! -z "${EXTRA_CONFIGS}" ]]; then
  # Expand server configs variable into command line options for pg_multi_regress.pl
  echo "${EXTRA_CONFIGS}" | sed -e 's/;/\n/' > /tmp/citus-pgext-cli.conf
  EXTRA_TESTS="--temp-config=/tmp/citus-pgext-cli.conf"
fi

if [[ ! -z "${COMPATIBLE_EXTENSION}" ]]; then
  # pairwise test
  export EXTRA_TESTS="--load-extension=${COMPATIBLE_EXTENSION} ${EXTRA_TESTS}" 
  make -C src/test/regress check-multi | tee src/test/regress/check-multi.log | grep "tests passed"
else
  # single test
  make -C src/test/regress check-multi | tee src/test/regress/check-multi.log | grep "tests passed"
fi
