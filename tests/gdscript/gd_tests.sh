#!/bin/bash

set -eu

pushd "$(dirname $0)" >/dev/null
test_dir="$(pwd)"
gen_code_dir=${test_dir}/generated
gen_ts_code_dir=${test_dir}/ts_check/src/generated
# gen_py_code_dir=${test_dir}/py_check/src/generated
data_dir=${test_dir}/data
out_dir=${test_dir}/out

mkdir -p "${out_dir}"
mkdir -p "${gen_code_dir}"
mkdir -p "${gen_ts_code_dir}"

pushd "../.."

./flatc --gdscript --gen-object-api -o "${gen_code_dir}" "${data_dir}"/simple.fbs
./flatc --gdscript --gen-object-api -o "${gen_code_dir}" "${data_dir}"/kitchen_sink.fbs
./flatc --gdscript --gen-object-api --gen-all -o "${gen_code_dir}" "${data_dir}"/wincludes/base.fbs
./flatc --ts --gen-object-api --ts-no-import-ext -o "${gen_ts_code_dir}" "${data_dir}"/simple.fbs
./flatc --ts --gen-object-api --ts-no-import-ext -o "${gen_ts_code_dir}" "${data_dir}"/kitchen_sink.fbs

popd

pushd "ts_check"

yarn start

popd

godot -s --headless tests.gd

echo "SUCCESS!"
