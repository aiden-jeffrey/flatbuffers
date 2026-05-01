class_name FB__Builder

var buffer: FB__ByteBuffer
var head: int

var min_align        : int = 1
var curr_vector_size : int = -1
var curr_vtable      : CurrentTable = null
var vtable_offsets   : Array[int] = []
var force_defaults   : bool = false
# TODO: add shared strings...

enum ErrorState {OK, BUFFER_GROW_FAILURE, WRITE_STATE_IMPROPER_USE, FINISH_INVALID_FILE_ID}

var error_state: ErrorState = ErrorState.OK

# NB: mostly just ported directly from the ts bindings
func _init(p_initial_size: int = 1024, p_force_defaults: bool = false) -> void:
  self.buffer = FB__ByteBuffer.new(p_initial_size)
  self.head = self.buffer.capacity()
  self.force_defaults = p_force_defaults

# ensure there is head and pads HEAD to elem_size
func prep(elem_size: int, additional_bytes: int) -> bool:
  if elem_size > self.min_align:
    self.min_align = elem_size

  # NB: I have no idea how this works
  var align_size = (~(self.buffer.capacity() - self.head + additional_bytes) + 1) & (elem_size - 1)

  # TODO: i think this allocated too much head
  while (self.head < align_size + elem_size + additional_bytes):
    var old_cap = self.buffer.capacity()
    if not self.buffer.double_capacity():
      printerr("failure to grow buffer")
      return false
    self.head += self.buffer.capacity() - old_cap

  self.pad(align_size)
  return true

func pad(size: int) -> void:
  # TODO: could write larger sizes here...
  for i in range(size):
    self.write_s8(0)

func offset() -> int:
  # TODO: can this ever be negative??
  return self.buffer.capacity() - self.head

# write methods just write to the current HEAD
func write_s8(value: int) -> void:
  self.head -= 1
  self.buffer.bytes.encode_s8(self.head, value)

func write_u8(value: int) -> void:
  self.head -= 1
  self.buffer.bytes.encode_u8(self.head, value)

func write_s16(value: int) -> void:
  self.head -= 2
  self.buffer.bytes.encode_s16(self.head, value)

func write_u16(value: int) -> void:
  self.head -= 2
  self.buffer.bytes.encode_u16(self.head, value)

func write_s32(value: int) -> void:
  self.head -= 4
  self.buffer.bytes.encode_s32(self.head, value)

func write_u32(value: int) -> void:
  self.head -= 4
  self.buffer.bytes.encode_u32(self.head, value)

func write_s64(value: int) -> void:
  self.head -= 8
  self.buffer.bytes.encode_s64(self.head, value)

# TODO: add bigint here... currently this will cause overflow for numbers > u32.MAX
func write_u64(value: int) -> void:
  self.head -= 8
  self.buffer.bytes.encode_u64(self.head, value)

func write_float(value: float) -> void:
  self.head -= 4
  self.buffer.bytes.encode_float(self.head, value)

func write_double(value: float) -> void:
  self.head -= 8
  self.buffer.bytes.encode_double(self.head, value)

## add methods write with alignment
# TODO: is my error handling performant enough??
func write_aligned_s8(value: int) -> void:
  if not self.prep(1, 0):
    error_state = ErrorState.BUFFER_GROW_FAILURE
    return
  self.write_s8(value)

func write_aligned_u8(value: int) -> void:
  if not self.prep(1, 0):
    error_state = ErrorState.BUFFER_GROW_FAILURE
    return
  self.write_u8(value)

func write_aligned_u16(value: int) -> void:
  if not self.prep(2, 0):
    error_state = ErrorState.BUFFER_GROW_FAILURE
    return
  self.write_u16(value)

func write_aligned_s16(value: int) -> bool:
  if not self.prep(2, 0):
    return false
  self.write_s16(value)
  return true

func write_aligned_s32(value: int) -> void:
  if not self.prep(4, 0):
    error_state = ErrorState.BUFFER_GROW_FAILURE
    return
  self.write_s32(value)

func write_aligned_u32(value: int) -> void:
  if not self.prep(4, 0):
    error_state = ErrorState.BUFFER_GROW_FAILURE
    return
  self.write_u32(value)

func write_aligned_s64(value: int) -> void:
  if not self.prep(8, 0):
    error_state = ErrorState.BUFFER_GROW_FAILURE
    return
  self.write_s64(value)

func write_aligned_u64(value: int) -> void:
  if not self.prep(8, 0):
    error_state = ErrorState.BUFFER_GROW_FAILURE
    return
  # TODO: resolve TODO against write_u64
  self.write_u64(value)

func write_aligned_float(value: float) -> void:
  if not self.prep(4, 0):
    error_state = ErrorState.BUFFER_GROW_FAILURE
    return
  self.write_float(value)

func write_aligned_double(value: float) -> void:
  if not self.prep(8, 0):
    error_state = ErrorState.BUFFER_GROW_FAILURE
    return
  self.write_double(value)

func write_aligned_offset(p_offset: int) -> void:
  if not self.prep(4, 0):
    error_state = ErrorState.BUFFER_GROW_FAILURE
    return
  # TODO: understand how this works...
  self.write_u32(self.offset() - p_offset + 4)

func begin_table() -> bool:
  if self.curr_vtable != null:
    error_state = ErrorState.WRITE_STATE_IMPROPER_USE
    printerr("can't build nested tables")
    return false

  self.curr_vtable = CurrentTable.new(self.offset())

  return self.error_state != ErrorState.OK

func end_table() -> int:
  if self.curr_vtable == null:
    error_state = ErrorState.WRITE_STATE_IMPROPER_USE
    printerr("not building a table, can't finish")
    return 0

  var start_offset = self.offset()
  var table_len = (self.curr_vtable.end_offset - start_offset) * 2
  var vtable_len = (self.curr_vtable.num_fields + 2) * 2

  # NB: we write buffers backwards in case you're confused
  # TODO: why is this required? to ensure alinment??
  self.write_aligned_u32(0)

  # TODO could probably get away with writing non-aligned here...
  for i in range(self.curr_vtable.num_fields, 0, -1):
    var voffset_abs = self.curr_vtable.slots[i - 1]
    if (voffset_abs != null && voffset_abs != 0):
      self.write_aligned_u16(start_offset - voffset_abs)
    else:
      self.write_aligned_u16(0)

  self.write_aligned_u16(table_len)
  self.write_aligned_u16(vtable_len)

  # now we search for an identical vtable to reuse
  # we do this now because the table we just wrote has all the offsets applied
  var our_vtable = self.head
  var vtable_match: int = 0
  for vtable in self.vtable_offsets:
    var other_vtable = self.buffer.capacity() - vtable
    if vtables_equal(our_vtable, other_vtable):
      vtable_match = vtable
      break

  if vtable_match != 0:
    # reset head to table (future writes will overwrite the void vtable)
    self.head = self.buffer.capacity() - start_offset
    self.buffer.bytes.encode_s32(self.head, vtable_match - start_offset)
  else:
    self.vtable_offsets.append(self.offset())
    self.buffer.bytes.encode_s32(self.buffer.capacity() - start_offset, self.offset - start_offset)

  self.curr_vtable = null

  if self.error_state == ErrorState.OK:
    return start_offset
  else:
    return 0

func start_vector(elem_size: int, num_elems: int, alignment: int) -> bool:
  if self.curr_vtable != null:
    error_state = ErrorState.WRITE_STATE_IMPROPER_USE
    printerr("can't build vector while building table")
    return false

  if self.error_state != ErrorState.OK:
    return false

  self.curr_vector_size = num_elems
  self.prep(4, elem_size * num_elems)
  self.prep(alignment, elem_size * num_elems)
  return true

func finish_vector() -> int:
  if self.curr_vector_size == -1:
    printerr("no table to finish")
    error_state = ErrorState.WRITE_STATE_IMPROPER_USE
    return false

  self.write_u32(self.curr_vector_size)
  return self.offset()

func finish(p_root_table: int, p_opt_file_id: String = "", p_opt_size_prefix: bool = false) -> bool:
  if self.error_state != ErrorState.OK:
    printerr("builder in error state %s, can't finish" % self.error_state)
    return false

  var size_prefix = 4 if p_opt_size_prefix else 0
  if (p_opt_file_id != ""):
    const req_len = FB__Constants.FILE_IDENTIFIER_LENGTH
    if p_opt_file_id.length() == req_len:
      self.prep(self.min_align, 4 + req_len + size_prefix)
      for i in range(req_len - 1, -1, -1):
        self.write_u8(ord(p_opt_file_id[i]))
    else:
      printerr("invalid file_id: %s" % p_opt_file_id)
      error_state = ErrorState.FINISH_INVALID_FILE_ID

  self.prep(self.min_align, 4 + size_prefix)
  self.write_aligned_offset(p_root_table)
  if size_prefix:
    self.write_aligned_u32(self.buffer.capacity() - self.head)
  self.buffer.position = self.head

  if self.error_state == ErrorState.OK:
    return true
  else:
    printerr("builder error %s" % self.error_state)
    return false

func write_string(p_str: String) -> int:
  var byte_array = p_str.to_utf8_buffer()
  self.write_aligned_u8(0)
  self.start_vector(1, byte_array.size(), 1)

  # TODO: this is mega slow - see the best way to copy chunks
  for i in range(byte_array.size(), 0, -1):
    self.write_u16(byte_array[i - 1])
  return self.finish_vector()

## inner classes
# TODO: tbh this should probably live in the FB__Table class...
class CurrentTable:
  var slots: Array[int]
  var num_fields: int
  var end_offset: int

  func _init(p_end_offset: int) -> void:
    self.slots = []
    self.num_fields = 0
    self.end_offset = p_end_offset

  func set_slot(p_field_index: int, p_offset: int) -> void:
    if self.num_fields < p_field_index + 1:
      self.num_fields = p_field_index + 1
    if self.slots.size() < self.num_fields:
      self.slots.resize(self.num_fields)
    self.slots[p_field_index] = p_offset

func vtables_equal(va: int, vb: int) -> bool:
  var vtable_len = self.buffer.bytes.decode_u16(va)
  if vtable_len != self.buffer.bytes.decode_u16(vb):
    return false

  for i in range(1, vtable_len):
    if self.buffer.bytes.decode_u16(va + (i * 2)) != self.buffer.bytes.decode_u16(vb + (i * 2)):
      return false

  return true
