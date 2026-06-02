class_name FB__Object

# Objects are used in the object API, they can pack into a buffer
# and unpack (see NB below re: unpack) from an FB__Table

func pack(_p_builder: FB__Builder) -> int:
  printerr("abstract class needs inheriting")
  return -1

## NB: can't actually specify this because gdscript won't let us narrow
##     the p_table type in generated implementations, here just as documentation
# static func unpack(_p_table: FB__Table) -> FB__Object:
#   printerr("abstract class needs inheriting")
#   return null
