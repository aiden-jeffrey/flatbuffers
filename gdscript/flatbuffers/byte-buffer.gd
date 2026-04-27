class_name FB__ByteBuffer

# TODO: add buffer identifier methods...
# TODO: add support for buffers with size prefix...

# TODO: do we need to do this??
extends RefCounted

var bytes: PackedByteArray
var position: int = 0

func _init(size: int) -> void:
  if size == 0 || (size & (size - 1)) != 0:
    printerr("invalid initial size %s, using 1024" % size)
    size = 1024

  bytes = _allocate(size)

func _allocate(size: int) -> PackedByteArray:
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
