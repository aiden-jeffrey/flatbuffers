class_name Test__Simple

static func assert_simple_equal_unpacked(a: Simple_MyRoot.ObjectType, b: Simple_MyRoot.ObjectType):
  GdTest.assert_equal("uid", a.uid, b.uid)

  var a_data = a.data
  var b_data = b.data
  GdTest.assert_almost_equal("num", a_data.num, b_data.num)
  GdTest.debug_print("vec:")
  GdTest.assert_equal("  x", a_data.vec.x, b_data.vec.x)
  GdTest.assert_equal("  y", a_data.vec.y, b_data.vec.y)
  GdTest.assert_equal("  z", a_data.vec.z, b_data.vec.z)
  GdTest.assert_equal("nint", a_data.nint, b_data.nint)

  GdTest.debug_print("arr:")
  for i in range(a_data.arr.size()):
    GdTest.assert_equal("  [%s]:" % i, a_data.arr[i], b_data.arr[i])

  GdTest.assert_equal("e", a_data.e, b_data.e)
  GdTest.assert_equal("str_", a_data.str_, b_data.str_)
  GdTest.assert_equal("bint", a_data.bint, b_data.bint)

static func assert_simple_equal(a: Simple_MyRoot, b: Simple_MyRoot.ObjectType):
  GdTest.assert_equal("uid", a.uid(), b.uid)

  var a_data = a.data()
  var b_data = b.data
  GdTest.assert_almost_equal("num", a_data.num(), b_data.num)
  GdTest.debug_print("vec:")
  GdTest.assert_equal("  x", a_data.vec().x(), b_data.vec.x)
  GdTest.assert_equal("  y", a_data.vec().y(), b_data.vec.y)
  GdTest.assert_equal("  z", a_data.vec().z(), b_data.vec.z)
  GdTest.assert_equal("nint", a_data.nint(), b_data.nint)

  GdTest.debug_print("arr:")
  for i in range(a_data.arr_length()):
    GdTest.assert_equal("  [%s]:" % i, a_data.arr(i), b_data.arr[i])

  GdTest.assert_equal("e", a_data.e(), b_data.e)
  GdTest.assert_equal("str_", a_data.str_(), b_data.str_)
  GdTest.assert_equal("bint", a_data.bint(), b_data.bint)

static func print_simple(p_root: Simple_MyRoot):
  var data = p_root.data()

  GdTest.debug_print("uid: ", p_root.uid())
  GdTest.debug_print("num: ", data.num())
  GdTest.debug_print("nint: ", data.nint())
  GdTest.debug_print("bint: ", data.bint())
  var vec: Simple_Vec = data.vec()
  GdTest.debug_print("vec:", vec.x(), ", ", vec.y(), ", ", vec.z())
  GdTest.debug_print("str:", data.str_())

  GdTest.debug_print_arr("arr", data.arr_length(), func (i): return str(data.arr(i)))

  GdTest.debug_print("e:", data.e())

static func build_simple(p_simple_t: Simple_MyRoot.ObjectType) -> PackedByteArray:
  var builder = FB__Builder.new(2048)
  # serialize
  #
  var data = p_simple_t.data

  var arr_off = Simple_SubTable.create_arr_vector(builder, data.arr)
  var str_off = builder.write_string(data.str_)

  Simple_SubTable.begin(builder)
  Simple_SubTable.add_num(builder, data.num)
  var vec_data = data.vec
  var vec_off = Simple_Vec.create_vec(builder, vec_data.x, vec_data.y, vec_data.z)
  Simple_SubTable.add_vec(builder, vec_off)

  Simple_SubTable.add_nint(builder, data.nint)
  Simple_SubTable.add_bint(builder, data.bint)

  Simple_SubTable.add_arr(builder, arr_off)
  Simple_SubTable.add_e(builder, data.e)
  Simple_SubTable.add_str_(builder, str_off)

  var data_off = Simple_SubTable.end(builder)
  var uid_off = builder.write_string(p_simple_t.uid)

  Simple_MyRoot.begin(builder)
  Simple_MyRoot.add_uid(builder, uid_off)
  Simple_MyRoot.add_data(builder, data_off)
  var root_off = Simple_MyRoot.end(builder)

  Simple_MyRoot.finish_buffer(builder, root_off)

  return builder.gen_trimmed_buffer()

# tests
static func test_simple_end_to_end():
  var simple_t = Simple_MyRoot.ObjectType.new(
    "1234abcd",
    Simple_SubTable.ObjectType.new(
      100,
      Simple_Vec.ObjectType.new(1, 2, 3),
      -90,
      [12, 13, 14, 15, 16],
      Simple_MyFirstEnum.Enum.Hundy,
      "some test string",
      123456,
    )
  )

  var fb = build_simple(simple_t)
  var bb = FB__ByteBuffer.new(fb)
  var my_root = Simple_MyRoot.get_root_as(bb)

  assert_simple_equal(my_root, simple_t)
  GdTest.assert_("identifier", Simple_MyRoot.buffer_has_identifier(bb))

  return true

static func test_simple_object_api_unpack():
  var simple_t = Simple_MyRoot.ObjectType.new(
    "uidstring",
    Simple_SubTable.ObjectType.new(
      12.57,
      Simple_Vec.ObjectType.new(0, 0, 0),
      122345,
      [],
      Simple_MyFirstEnum.Enum.Two,
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa+@\n",
      -1,
    )
  )

  var fb = build_simple(simple_t)
  var bb = FB__ByteBuffer.new(fb)
  var my_root = Simple_MyRoot.get_root_as(bb)

  var unpacked = Simple_MyRoot.ObjectType.unpack(my_root)
  assert_simple_equal_unpacked(simple_t, unpacked)

  return true

static func test_simple_from_ts():
  var ts_data = FileAccess.get_file_as_bytes("res://data/bin/ts-out.bin")
  # NB: if you want to generate this file in the test run, you can uncomment below and in
  #     the yarn stuff in the gd_script file
  # var ts_data = FileAccess.get_file_as_bytes("res://ts_check/out/ts-out.bin")

  var bb = FB__ByteBuffer.new(ts_data)
  var my_root = Simple_MyRoot.get_root_as(bb)
  print_simple(my_root)

  return true

static func test_simple_io():
  var simple_t = Simple_MyRoot.ObjectType.new(
    "1a2b3c4d",
    Simple_SubTable.ObjectType.new(
      1.6666666,
      Simple_Vec.ObjectType.new(123, 2, 1),
      100,
      [],
      Simple_MyFirstEnum.Enum.One,
      "writing this one to file",
      0,
    )
  )

  var fb = build_simple(simple_t)
  GdTest.debug_print(fb)

  # var gd_fp = "res://out/gd-out.bin"
  var fp = FileAccess.create_temp(FileAccess.READ_WRITE, "", "", true)
  var tmp_path = fp.get_path()
  fp.store_buffer(fb)
  fp.close()

  var read_fb = FileAccess.get_file_as_bytes(tmp_path)

  GdTest.assert_equal("fb", read_fb.size(), fb.size())
  for i in range(read_fb.size()):
    GdTest.assert_equal("  [%s]" % i, read_fb[i], fb[i])

  var bb = FB__ByteBuffer.new(fb)
  var my_root = Simple_MyRoot.get_root_as(bb)

  var unpacked = Simple_MyRoot.ObjectType.unpack(my_root)
  assert_simple_equal_unpacked(simple_t, unpacked)

  return true


static func test_simple_bad():
  var b = Simple_MyRoot.ObjectType.new(
    "1234abcd",
    Simple_SubTable.ObjectType.new(
      100,
      Simple_Vec.ObjectType.new(300, 2, 3),
      -90,
      [12, 13, 14, 15, 16],
      Simple_MyFirstEnum.Enum.Hundy,
      "some test string",
      123456,
    )
  )

  var fb = build_simple(b)
  var bb = FB__ByteBuffer.new(fb)
  var a = Simple_MyRoot.get_root_as(bb)

  GdTest.assert_equal("uid", a.uid(), b.uid)

  var a_data = a.data()
  var b_data = b.data
  GdTest.assert_almost_equal("num", a_data.num(), b_data.num)
  GdTest.debug_print("vec:")
  # overflow of u8, so should fail
  # TODO: do we need to warn on build??
  GdTest.assert_not_equal("  x", a_data.vec().x(), b_data.vec.x)
  GdTest.assert_equal("  y", a_data.vec().y(), b_data.vec.y)
  GdTest.assert_equal("  z", a_data.vec().z(), b_data.vec.z)
  GdTest.assert_equal("nint", a_data.nint(), b_data.nint)

  GdTest.debug_print("arr:")
  for i in range(a_data.arr_length()):
    GdTest.assert_equal("  [%s]:" % i, a_data.arr(i), b_data.arr[i])

  GdTest.assert_equal("e", a_data.e(), b_data.e)
  GdTest.assert_equal("str_", a_data.str_(), b_data.str_)
  GdTest.assert_equal("bint", a_data.bint(), b_data.bint)

  return true

static func all():
  return [
    test_simple_end_to_end,
    test_simple_object_api_unpack,
    test_simple_from_ts,
    test_simple_io,
    test_simple_bad]
