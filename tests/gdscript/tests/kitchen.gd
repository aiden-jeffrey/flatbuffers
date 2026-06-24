class_name Test__Kitchen

static func build_kitchen() -> PackedByteArray:
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

  Kitchen_TableB.begin_vec_arr_vector(builder, 5)
  Kitchen_Vec.create_vec(builder, 11, 11, 11)
  Kitchen_Vec.create_vec(builder, 12, 12, 12)
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
    builder, 1, 11, 111, 2, 4, 6, 8, 16, [3, 6, 9],
    [11, 12], [11, 12], [11, 12])

  Kitchen_FooBar.add_data(builder, data)
  Kitchen_FooBar.add_boonion_type(builder, Kitchen_TestUnion.Enum.TableB)
  Kitchen_FooBar.add_boonion(builder, boonion)
  Kitchen_FooBar.add_a(builder, a)

  var foo_bar = Kitchen_FooBar.end(builder)
  Kitchen_FooBar.finish_buffer(builder, foo_bar)

  return builder.gen_trimmed_buffer()

static func str_vec(vec: Kitchen_Vec):
  return "{" + str(vec.x()) + ", " + str(vec.y()) + ", " + str(vec.z()) + "}"

static func str_table_a(table: Kitchen_TableA):
  return "TableA: " + str(table.a()) + ", " + str(table.b())

static func str_table_b(table: Kitchen_TableB):
  return "TableB: " + str(table.g()) + ", " + str(table.h()) + ", " + str(table.dbl());


static func print_kitchen(fb: PackedByteArray):
  var bb = FB__ByteBuffer.new(fb)
  var foo_bar = Kitchen_FooBar.get_root_as(bb)

  var thing = foo_bar.thing()
  GdTest.debug_print("thing: ", thing)
  GdTest.debug_print("say: ", foo_bar.say())
  GdTest.debug_print_arr("arr", foo_bar.arr_length(), func (i): return str(foo_bar.arr(i)))
  GdTest.debug_print_arr("str_arr", foo_bar.str_arr_length(), func (i): return foo_bar.str_arr(i))

  GdTest.debug_print_arr("vec_arr", foo_bar.vec_arr_length(),
    func (i): return str_vec(foo_bar.vec_arr(i)))

  GdTest.debug_print("just_vec: ", str_vec(foo_bar.just_vec()))

  GdTest.debug_print_arr("a_arr", foo_bar.a_arr_length(), func (i): return str_table_a(foo_bar.a_arr(i)))
  GdTest.debug_print("data:")
  var data = foo_bar.data()
  GdTest.debug_print("  v: ", str_vec(data.v()))
  var q = data.q()
  GdTest.debug_print("  q: ", str_vec(q.v()), " [", q.w(), "]")
  GdTest.debug_print("  uid: ", data.uid())
  GdTest.debug_print_arr("  height", data.height_length(), func (i): return str(data.height(i)))
  GdTest.debug_print_arr("  v_arr", data.v_arr_length(), func (i): return str_vec(data.v_arr(i)))

  var boon = Kitchen_TableB.from(foo_bar.boonion())
  GdTest.debug_print("boonion: ", foo_bar.boonion_type() as Kitchen_TestUnion.Enum,  " => ", str_table_b(boon))
  GdTest.debug_print("a: ", str_table_a(foo_bar.a()))

static func assert_vec_equal(a: Kitchen_Vec, b: Kitchen_Vec):
  GdTest.assert_equal("  x:", a.x(), b.x())
  GdTest.assert_equal("  y:", a.y(), b.y())
  GdTest.assert_equal("  z:", a.z(), b.z())

static func assert_table_a_equal(a: Kitchen_TableA, b: Kitchen_TableA):
  GdTest.assert_equal("  a", a.a(), b.a())
  GdTest.assert_equal("  b", a.b(), b.b())

static func assert_table_b_equal(a: Kitchen_TableB, b: Kitchen_TableB):
  GdTest.assert_equal("  g", a.g(), b.g())
  GdTest.assert_almost_equal("  h", a.h(), b.h())
  GdTest.assert_almost_equal("  dbl", a.dbl(), b.dbl())

  GdTest.assert_equal("vec_arr:", a.vec_arr_length(), b.vec_arr_length())
  for i in range(a.vec_arr_length()):
    GdTest.debug_print("  [%s]:" % i)
    assert_vec_equal(a.vec_arr(i), b.vec_arr(i))

static func assert_kitchen_equal(a: Kitchen_FooBar, b: Kitchen_FooBar):
  GdTest.assert_equal("thing", a.thing(), b.thing())
  GdTest.assert_equal("say", a.say(), b.say())

  GdTest.assert_equal("arr:", a.arr_length(), b.arr_length())
  for i in range(a.arr_length()):
    GdTest.assert_equal("  [%s]:" % i, a.arr(i), b.arr(i))

  GdTest.assert_equal("str_arr:", a.str_arr_length(), b.str_arr_length())
  for i in range(a.str_arr_length()):
    GdTest.assert_equal("  [%s]:" % i, a.str_arr(i), b.str_arr(i))

  GdTest.assert_equal("vec_arr:", a.vec_arr_length(), b.vec_arr_length())
  for i in range(a.vec_arr_length()):
    GdTest.debug_print("  [%s]:" % i)
    assert_vec_equal(a.vec_arr(i), b.vec_arr(i))

  GdTest.debug_print("just_vec:")
  assert_vec_equal(a.just_vec(), b.just_vec())

  GdTest.assert_equal("a_arr:", a.a_arr_length(), b.a_arr_length())
  for i in range(a.a_arr_length()):
    GdTest.debug_print("  [%s]:" % i)
    assert_table_a_equal(a.a_arr(i), b.a_arr(i))


static func test_kitchen():
  var arr: Array[int] = [42, 42, 42]
  var str_arr: Array[String] = []
  for i in range(10):
    str_arr.append("element #" + str(i))
  var vec_arr: Array[Kitchen_Vec.ObjectType] = []
  var num_vecs = 10
  for i in range(num_vecs):
    var ii = i
    vec_arr.append(Kitchen_Vec.ObjectType.new(ii, ii, ii))

  var a_arr: Array[Kitchen_TableA.ObjectType] = [
    Kitchen_TableA.ObjectType.new(false, 100),
    Kitchen_TableA.ObjectType.new(true, 200),
    Kitchen_TableA.ObjectType.new(true, 300),
  ]

  var common_vec_arr: Array[Kitchen_Vec.ObjectType] = []
  common_vec_arr.append(Kitchen_Vec.ObjectType.new(11, 11, 11))
  common_vec_arr.append(Kitchen_Vec.ObjectType.new(12, 12, 12))
  var data = Kitchen_Data.ObjectType.new(
    Kitchen_Vec.ObjectType.new(1, 11, 111),
    Kitchen_Quat.ObjectType.new(Kitchen_Vec.ObjectType.new(2, 4, 6), 8),
    16,
    [3, 6, 9],
    common_vec_arr
  )

  var boonion = Kitchen_TableB.ObjectType.new(10, 1.6667, 123456789.123456789, common_vec_arr)

  # NB: this should be the same data as written directly in build_kitchen
  var kitchen_t = Kitchen_FooBar.ObjectType.new(
    Kitchen_Thing.Enum.Oven,
    "say something",
    arr,
    str_arr,
    vec_arr,
    Kitchen_Vec.ObjectType.new(5, 6, 7),
    a_arr,
    data,
    Kitchen_TestUnion.Enum.TableB,
    boonion,
    Kitchen_TableA.ObjectType.new(true, 1234)
  )

  var kitchen = build_kitchen()
  var bb = FB__ByteBuffer.new(kitchen)
  var kitchen_fb = Kitchen_FooBar.get_root_as(bb)
  # print_kitchen(kitchen)

  # print("\n++++++++++++++++++++++\n")

  var builder = FB__Builder.new()
  var root_off = kitchen_t.pack(builder)
  Kitchen_FooBar.finish_buffer(builder, root_off)
  var packed = builder.gen_trimmed_buffer()
  # print_kitchen(packed)
  var bb_check = FB__ByteBuffer.new(packed)
  var kitchen_check_fb = Kitchen_FooBar.get_root_as(bb_check)

  assert_kitchen_equal(kitchen_fb, kitchen_check_fb)

  return true


static func all():
  return [test_kitchen]
