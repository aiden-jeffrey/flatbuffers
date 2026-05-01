class_name FB__Table

# TODO: extend RefCounted??

# Tables are flexible flatbuffer objects
# A table is structured:
# - soffset (i32) - subtracted from the table start_offset to determine the start_offset of the vtable
# - fields (with padding to maintain alignment)

# The vtable is made of voffsets (u16) and is structured:
# - size of vtable
# - size of object
# - n number of field offsets into the table
# therefore a vtable is made of n + 2 voffsets, and the field offsets are added to the table
# start_offset to get the absolute field start_offset

var buffer: FB__ByteBuffer
var start_offset: int
var is_struct: bool

func _init(p_byte_buffer: FB__ByteBuffer, p_offset: int = 0, p_is_struct: bool = false) -> void:
  self.buffer = p_byte_buffer
  self.start_offset = p_offset
  self.is_struct = p_is_struct

func _make_string(p_offset: int) -> String:
  var offset = self._get_indirect(p_offset)
  var str_len = self.buffer.bytes.decode_u32(offset)
  offset += 4;

  var slice = self.buffer.bytes.slice(offset, offset + str_len)
  return slice.get_string_from_utf8()

# Copy
# TODO: can i just return a table and cast it afterwards??
func _sub_table(p_target: FB__Table, p_offset: int) -> FB__Table:
  p_target.buffer = self.buffer
  p_target.start_offset = p_offset

  return p_target

func _vector_len(p_offset: int) -> int:
  return self.buffer.bytes.decode_u32(self._get_indirect(p_offset))

func _vector_start(p_offset: int) -> int:
  return self._get_indirect(p_offset) + 4

# Query the vtable for a given field voffset (start_offset + voffset is the address of the field)
# - if the field index is outside the vtable, the field is missing
func _get_voffset(p_vtable_offset: int) -> int:
  var vtable = self.start_offset - self.buffer.bytes.decode_s32(self.start_offset)
  var vtable_size = self.buffer.bytes.decode_u16(vtable)
  if p_vtable_offset < vtable_size:
    return self.buffer.bytes.decode_u16(vtable + p_vtable_offset)
  else:
    return 0

# Read address at p_offset and return the resulting absolute address
func _get_indirect(p_offset: int) -> int:
  return p_offset + self.buffer.bytes.decode_u32(p_offset)
