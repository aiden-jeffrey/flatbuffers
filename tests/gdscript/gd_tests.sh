#!/bin/bash

set -eu

pushd "$(dirname $0)" >/dev/null
test_dir="$(pwd)"
gen_code_dir=${test_dir}/generated
schema_dir=${test_dir}/data/schemas
out_dir=${test_dir}/out

mkdir -p "${out_dir}"
mkdir -p "${gen_code_dir}"

# gen_ts_code_dir=${test_dir}/ts_check/src/generated
# mkdir -p "${gen_ts_code_dir}"

pushd "../.."

./flatc --gdscript --gen-object-api -o "${gen_code_dir}" "${schema_dir}"/simple.fbs
./flatc --gdscript --gen-object-api -o "${gen_code_dir}" "${schema_dir}"/kitchen_sink.fbs
./flatc --gdscript --gen-object-api --gen-all -o "${gen_code_dir}" "${schema_dir}"/wincludes/base.fbs

# NB: uncomment to generate reference bindings and test binaries using ts generator
# ./flatc --ts --gen-object-api --ts-no-import-ext -o "${gen_ts_code_dir}" "${schema_dir}"/simple.fbs
# ./flatc --ts --gen-object-api --ts-no-import-ext -o "${gen_ts_code_dir}" "${schema_dir}"/kitchen_sink.fbs

popd

# pushd "ts_check"

# yarn start

# popd

godot -s --headless tests/main.gd "$@"

echo "TESTS PASS, GD SUCCESS!"
