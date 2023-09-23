export PG_CONFIG=$PWD/../../pg-15-dist/bin/pg_config
./configure --prefix=$PWD/citus-dist
make -j8
make install -j8