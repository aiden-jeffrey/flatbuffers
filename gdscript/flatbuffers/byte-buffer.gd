class_name FB__ByteBuffer

# TODO: add buffer identifier methods...
# TODO: add support for buffers with size prefix...

# TODO: do we need to do this??
extends RefCounted

var bytes: PackedByteArray
var position: int = 0

func _init(buffer: PackedByteArray) -> void:
  self.bytes = buffer

static func make_blank(p_capacity: int) -> FB__ByteBuffer:
  if p_capacity == 0 || (p_capacity & (p_capacity - 1)) != 0:
    printerr("invalid initial size %s, using 1024" % p_capacity)
    p_capacity = 1024

  return FB__ByteBuffer.new(_allocate(p_capacity))

static func _allocate(size: int) -> PackedByteArray:
  var out = PackedByteArray()
  out.resize(size)
  # TODO: is this required...?
  out.fill(0x00)
  return out

func clear() -> void:
  position = 0

# Capacity of underlying PackedByteArray
func capacity() -> int:
  return bytes.size()

# Doubles the size of the buffer and copies the contents to the end
func double_capacity() -> bool:
  var _capacity = self.capacity()
  if _capacity >= FB__Constants.MAX_BUFFER_SIZE:
    printerr("flatbuffers: cannot double_capacity beyond 2GB")
    return false

  var new_buffer = _allocate(_capacity)

  self.bytes = new_buffer + bytes
  self.position = _capacity

  return true

func has_identifier(p_id: String) -> bool:
  if p_id.length() != FB__Constants.FILE_IDENTIFIER_LENGTH:
    printerr("invalid ident string %s", p_id)
    return false

  for i in range(0, FB__Constants.FILE_IDENTIFIER_LENGTH):
    if ord(p_id[i]) != self.bytes.decode_u8(self.position + 4 + i):
      return false

  return true
