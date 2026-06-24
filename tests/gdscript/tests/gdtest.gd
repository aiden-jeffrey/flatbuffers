class_name GdTest

static var VERBOSE = false
static var SUCCESS = true

static func print_underlined(txt: String):
  print(txt)
  print("=".repeat(txt.length()) + "\n")

static func debug_print(...txt):
  if VERBOSE:
    print.callv(["[] "] + txt)

static func debug_print_arr(p_name: String, p_arr_len: int, p_accessor: Callable):
  debug_print("%s len: %s" % [p_name, p_arr_len])

  var arr_printed = ""
  for i in range(p_arr_len):
    arr_printed += p_accessor.call(i) + ", "
  debug_print(p_name + ": [" + arr_printed + "]")

static func assert_(txt: String, cond: bool):
  if !cond:
    push_error("%s %s" % [txt, cond])
    SUCCESS = false
  debug_print("%s %s" % [txt, cond])

static func assert_equal(txt: String, a, b):
  if a != b:
    push_error("%s %s != %s" % [txt, a, b])
    SUCCESS = false
  debug_print("%s %s == %s" % [txt, a, b])

static func assert_almost_equal(txt: String, a, b):
  if !is_equal_approx(a, b):
    push_error("%s %s != %s" % [txt, a, b])
    SUCCESS = false
  debug_print("%s %s == %s" % [txt, a, b])

static func assert_not_equal(txt: String, a, b):
  if a == b:
    push_error("%s %s == %s" % [txt, a, b])
    SUCCESS = false
  debug_print("%s %s != %s" % [txt, a, b])

static func run(p_test: Callable):
  SUCCESS = true
  var name = str(p_test).split("::")[1]
  print_underlined("running " + name)
  var success = p_test.call() && SUCCESS
  if success:
    print("=> " + name + "... OK!\n")
  else:
    print("=> " + name + "... FAILED!\n")
    SUCCESS = false

  return success
