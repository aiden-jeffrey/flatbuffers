extends SceneTree



func _init() -> void:
  var args_with_prog = OS.get_cmdline_user_args()
  if args_with_prog.has("-v"):
    GdTest.VERBOSE = true

  GdTest.print_underlined("simple schema tests")
  for test in Test__Simple.all():
    GdTest.run(test)

  GdTest.print_underlined("kitchen schema tests")
  for test in Test__Kitchen.all():
    GdTest.run(test)

  quit(0 if GdTest.SUCCESS else 1)
