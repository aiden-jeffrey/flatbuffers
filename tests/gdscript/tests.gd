extends SceneTree

var simple_a = {
  "vec": [1, 2, 3],
  "arr": [10, 9, 8, 7],
  "e": Simple_MyFirstEnum.Enum.One,
  "str_": "this is a test string!",
}

func test_simple_end_to_end():
  var simple_a_fb = build_simple_a()
  parse_simple(simple_a_fb)
  # assert(false, "test failed")

func build_simple_a() -> PackedByteArray:
  var builder = FB__Builder.new(2048)
  # serialize

  var arr_off = Simple_SubTable.create_arr_vector(builder, [10, 9, 8, 7])
  # print("write_sting:")
  var str_off = builder.write_string("aiden")

  Simple_SubTable.begin(builder)
  Simple_SubTable.add_num(builder, 100)
  var vec_off = Simple_Vec.create_vec(builder, 1, 2, 3)
  Simple_SubTable.add_vec(builder, vec_off)

  Simple_SubTable.add_nint(builder, -90)
  Simple_SubTable.add_bint(builder, 12345)

  Simple_SubTable.add_arr(builder, arr_off)
  Simple_SubTable.add_e(builder, Simple_MyFirstEnum.Enum.One)
  Simple_SubTable.add_str_(builder, str_off)

  var data_off = Simple_SubTable.end(builder)
  var uid_off = builder.write_string("1234abcd")

  Simple_MyRoot.begin(builder)
  Simple_MyRoot.add_uid(builder, uid_off)
  Simple_MyRoot.add_data(builder, data_off)
  var root_off = Simple_MyRoot.end(builder)

  Simple_MyRoot.finish_buffer(builder, root_off)

  return builder.gen_trimmed_buffer()

func build_kitchen() -> PackedByteArray:
  var builder = FB__Builder.new()

  var say = builder.write_string("say something")
  var arr = Kitchen_FooBar.create_arr_vector(builder, [42, 42, 42])

  var str_arr_offsets: Array[int] = []
  for i in range(10):
    str_arr_offsets.append(builder.write_string("element #" + str(i)))

  var str_arr = Kitchen_FooBar.create_str_arr_vector(builder, str_arr_offsets)

  var num_vecs = 10
  Kitchen_FooBar.begin_vec_arr_vector(builder, num_vecs)
  for i in range(num_vecs, 0, -1):
    var ii = i - 1
    Kitchen_Vec.create_vec(builder, ii, ii, ii)
  var vec_arr = builder.end_vector()

  var a_arr_offsets: Array[int] = [
    Kitchen_TableA.create_table_a(builder, false, 100),
    Kitchen_TableA.create_table_a(builder, true, 200),
    Kitchen_TableA.create_table_a(builder, true, 300),
  ]

  var a_arr = Kitchen_FooBar.create_a_arr_vector(builder, a_arr_offsets)

  Kitchen_FooBar.begin_vec_arr_vector(builder, 5)
  for i in range(5, 0, -1):
    Kitchen_Vec.create_vec(builder, i, i, i)
  var boonion_vec_arr = builder.end_vector()

  var boonion = Kitchen_TableB.create_table_b(builder, 10, 1.6667,
    123456789.123456789, boonion_vec_arr)

  var a = Kitchen_TableA.create_table_a(builder, true, 1234)

  # got there
  Kitchen_FooBar.begin(builder)

  Kitchen_FooBar.add_thing(builder, Kitchen_Thing.Enum.Oven)
  Kitchen_FooBar.add_say(builder, say)
  Kitchen_FooBar.add_arr(builder, arr)
  Kitchen_FooBar.add_str_arr(builder, str_arr)
  Kitchen_FooBar.add_vec_arr(builder, vec_arr)
  var just_vec = Kitchen_Vec.create_vec(builder, 5, 6, 7)
  Kitchen_FooBar.add_just_vec(builder, just_vec)
  Kitchen_FooBar.add_a_arr(builder, a_arr)

  var data = Kitchen_Data.create_data(
    builder, 9, 9, 9, 8, 8, 8, 8, 16, [2, 4, 6],
    [11, 12], [11, 12], [11, 12])

  Kitchen_FooBar.add_data(builder, data)
  Kitchen_FooBar.add_boonion_type(builder, Kitchen_TestUnion.Enum.TableB)
  Kitchen_FooBar.add_boonion(builder, boonion)
  Kitchen_FooBar.add_a(builder, a)

  var foo_bar = Kitchen_FooBar.end(builder)
  Kitchen_FooBar.finish_buffer(builder, foo_bar)

  return builder.gen_trimmed_buffer()

func print_arr(p_name: String, p_arr_len: int, p_accessor: Callable):
  print(p_name + " len:", p_arr_len)

  var arr_printed = ""
  for i in range(p_arr_len):
    arr_printed += p_accessor.call(i) + ", "
  print(p_name + ": [" + arr_printed + "]")

func str_vec(vec: Kitchen_Vec):
  return "{" + str(vec.x()) + ", " + str(vec.y()) + ", " + str(vec.z()) + "}"

func str_table_a(table: Kitchen_TableA):
  return "TableA: " + str(table.a()) + ", " + str(table.b())

func str_table_b(table: Kitchen_TableB):
  return "TableB: " + str(table.g()) + ", " + str(table.h()) + ", " + str(table.dbl());

func parse_simple(fb: PackedByteArray):
  var bb = FB__ByteBuffer.new(fb)
  var my_root = Simple_MyRoot.get_root_as(bb)
  var data = my_root.data()

  print("uid: ", my_root.uid())
  print("num: ", data.num())
  print("nint: ", data.nint())
  print("bint: ", data.bint())
  var vec: Simple_Vec = data.vec()
  print("vec:", vec.x(), ", ", vec.y(), ", ", vec.z())
  print("str:", data.str_())

  print_arr("arr", data.arr_length(),func (i): return str(data.arr(i)))

  print("e:", data.e())

func parse_kitchen(fb: PackedByteArray):
  var bb = FB__ByteBuffer.new(fb)
  var foo_bar = Kitchen_FooBar.get_root_as(bb)

  var thing = foo_bar.thing()
  print("thing: ", thing)
  print("say: ", foo_bar.say())
  print_arr("arr", foo_bar.arr_length(), func (i): return str(foo_bar.arr(i)))
  print_arr("str_arr", foo_bar.str_arr_length(), func (i): return foo_bar.str_arr(i))

  print_arr("vec_arr", foo_bar.vec_arr_length(),
    func (i): return str_vec(foo_bar.vec_arr(i)))

  print("just_vec: ", str_vec(foo_bar.just_vec()))

  print_arr("a_arr", foo_bar.a_arr_length(), func (i): return str_table_a(foo_bar.a_arr(i)))
  print("data:")
  var data = foo_bar.data()
  print("  v: ", str_vec(data.v()))
  var q = data.q()
  print("  q: ", str_vec(q.v()), " [", q.w(), "]")
  print("  uid: ", data.uid())
  print_arr("  height", data.height_length(), func (i): return str(data.height(i)))
  print_arr("  v_arr", data.v_arr_length(), func (i): return str_vec(data.v_arr(i)))

  var boon = Kitchen_TableB.from(foo_bar.boonion())
  print("boonion: ", foo_bar.boonion_type() as Kitchen_TestUnion.Enum,  " => ", str_table_b(boon))
  print("a: ", str_table_a(foo_bar.a()))

func check_ts_bin(path: String):
  var data = FileAccess.get_file_as_bytes(path)

  parse_simple(data)

func _init() -> void:
  print("##########SIMPLE##########")
  # test_simple_end_to_end()
  print("------checking ts binary------")
  var ts_data = FileAccess.get_file_as_bytes("res://ts_check/out/ts-out.bin")
  parse_simple(ts_data)

  print("------checking our own buffer------")
  var built = build_simple_a()
  var gd_fp = "res://out/gd-out.bin"
  var fp = FileAccess.open(gd_fp, FileAccess.WRITE)
  fp.store_buffer(built)

  parse_simple(built)

  print("##########KITCHEN##########")
  var kitchen = build_kitchen()
  parse_kitchen(kitchen)

  quit()
