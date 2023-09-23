./bootstrap -DPG_CONFIG=$PWD/../../pg-15-dist/bin/pg_config
mkdir build
cd build
make -j8
make install -j8