# GdScript tests

For now there's a "test" runner shell script (gd_tests.sh) that generates code using flatc,
generates a reference binary using the ts bindings (for the simple schema - commented out
for now, and the binary has been committed) and runs some tests written in gdscript.
