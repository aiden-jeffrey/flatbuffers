/*
 * Copyright 2014 Google Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// independent from idl_parser, since this code is not needed for most clients

// TODO: known limitations:
// TODO: support strings and structs in unions

#include "idl_gen_gdscript.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "codegen/idl_namer.h"
#include "flatbuffers/code_generators.h"
#include "flatbuffers/idl.h"
#include "flatbuffers/util.h"

namespace flatbuffers {
namespace gdscript {

namespace {

std::set<std::string> Keywords() {
  // List of keywords retrieved from here:
  // https://github.com/godotengine/godot/blob/master/modules/gdscript/gdscript_tokenizer.cpp
  return {
    "as",
    "assert",
    "await",
    "breakpoint",
    "class",
    "class_name",
    "const",
    "enum",
    "extends",
    "func",
    "in",
    "is",
    "namespace",
    "preload",
    "self",
    "signal",
    "static",
    "super",
    "trait",
    "var",
    "void",
    "yield",
  };
}

static const Namer::Config kConfig = {
    /*types=*/Case::kKeep,
    /*constants=*/Case::kScreamingSnake,
    /*methods=*/Case::kSnake2,
    /*functions=*/Case::kSnake2,
    /*fields=*/Case::kSnake2,
    /*variable=*/Case::kSnake2,
    /*variants=*/Case::kKeep,
    /*enum_variant_seperator=*/".",
    /*escape_keywords=*/Namer::Config::Escape::AfterConvertingCase,
    /*namespaces=*/Case::kKeep,
    /*namespace_seperator=*/"_",
    /*object_prefix=*/"FB__",
    /*object_suffix=*/"T",
    /*keyword_prefix=*/"_",
    /*keyword_suffix=*/"_",
    /*keywords_casing=*/Namer::Config::KeywordsCasing::CaseSensitive,
    /*filenames=*/Case::kKeep,
    /*directories=*/Case::kKeep,
    /*output_path=*/"",
    /*filename_suffix=*/"",
    /*filename_extension=*/".gd",
};

// Hardcode spaces per indentation.
static const CommentConfig def_comment = {nullptr, "#", nullptr};

// NB: copying this limitation from the ts bindings - presumably can't create a table
//     that contains structs because they need to be added inline...
static bool CanCreateFactoryMethod(const StructDef &struct_def) {
  return struct_def.fields.vec.size() < 2 ||
    std::all_of(std::begin(struct_def.fields.vec),
                std::end(struct_def.fields.vec),
                [](const FieldDef *f) -> bool {
                  FLATBUFFERS_ASSERT(f != nullptr);
                  return f->value.type.base_type != BASE_TYPE_STRUCT;
                });
}

}  // namespace

class GdscriptGenerator : public BaseGenerator {
 public:
  GdscriptGenerator(const Parser& parser, const std::string& path,
                  const std::string& file_name)
      : BaseGenerator(parser, path, file_name, "", "", "gd"),
        float_const_gen_("NAN", "INF", "-INF"),
        namer_(WithFlagOptions(kConfig, parser.opts, path), Keywords()) {
  }

  bool generate() {
    std::string one_file_code;
    if (!GenAllEnums()) return false;
    if (!GenAllStructs()) return false;

    return true;
  }

 private:
  const SimpleFloatConstantGenerator float_const_gen_;
  const IdlNamer namer_;

  // structs and tables ******
  void GenTableStaticMethods(const StructDef& struct_def, std::string* code_ptr) const {
    auto& code = *code_ptr;
    const std::string struct_type = namer_.NamespacedType(struct_def);

    if (struct_def.fixed) {
      code += "static func size_of():\n";
      code += "  return " + NumToString(struct_def.bytesize) + "\n";
      code += "\n";
      return;
    }

    // ctors from buffer
    // TODO: support root struct here...
    code += "static func get_root_as(p_buffer: FB__ByteBuffer) -> " + struct_type + ":\n";
    code += "  var pos: int = p_buffer.position\n";
    code += "  var root_offset: int = pos + p_buffer.bytes.decode_u32(pos)\n";
    code += "  return " + struct_type + ".new(p_buffer, root_offset)\n";
    code += "\n";

    code += "static func get_size_prefixed_root_as(p_buffer: FB__ByteBuffer) -> " + struct_type + ":\n";
    code += "  p_buffer.position += FB__Constants.FILE_IDENTIFIER_LENGTH\n";
    code += "  var pos: int = p_buffer.position\n";
    code += "  var root_offset: int = pos + p_buffer.bytes.decode_u32(pos)\n";
    code += "  return " + struct_type + ".new(p_buffer, root_offset)\n";
    code += "\n";

    if (parser_.root_struct_def_ == &struct_def && !parser_.file_identifier_.empty()) {
      // id checker
      code += "static func buffer_has_identifier(p_buffer: FB__ByteBuffer) -> bool:\n";
      code += "  return p_buffer.has_identifier(\"";
      code += parser_.file_identifier_ + "\")\n";
      code += "\n";
    }

  }

  void GenTableCtors(const StructDef& struct_def, std::string* code_ptr) const {
    auto& code = *code_ptr;

    std::string is_struct = (struct_def.fixed ? "true" : "false");
    const auto type_name = namer_.NamespacedType(struct_def);

    code += "func _init(p_buffer: FB__ByteBuffer, p_offset: int) -> void:\n";
    code += "  super(p_buffer, p_offset, " + is_struct + ")\n";
    code += "\n";

    code += "static func from(p_table: FB__Table) -> " + type_name + ":\n";
    code += "  return " + type_name + ".new(p_table.buffer, p_table.start_offset)\n";
    code += "\n";
  }

  // Accessor method generators
  void GenAccessorVectorUtils(const FieldDef& field, std::string* code_ptr) const {
    auto& code = *code_ptr;

    // length()
    code += "func " + namer_.Method(field) + "_length() -> int:\n";
    if (!IsArray(field.value.type)) {
      // vector
      code += "  var voffset: int = self._get_voffset(" + NumToString(field.value.offset) + ")\n";
      code += "  if voffset != 0:\n";
      code += "    return self._vector_len(self.start_offset + voffset)\n";
      code += "  return 0\n";
    } else {
      // fixed length array
      code += "  return " + NumToString(field.value.type.fixed_length) + "\n";
    }
    code += "\n";

    // is_null()
    code += "func " + namer_.Method(field) + "_is_null() -> bool:\n";
    if (!IsArray(field.value.type)) {
      code += "  var voffset: int = self._get_voffset(" + NumToString(field.value.offset) + ")\n";
      code += "  return voffset == 0\n";
    } else {
      // assume that we always have an array as memory is preassigned
      code += "  return false\n";
    }
    code += "\n";
  }

  void GenAccessorScalar(const StructDef& struct_def,
                          const FieldDef& field,
                         std::string* code_ptr) const {
    const auto type_name = GdTypeName(field);
    if (struct_def.fixed) {
      // struct member
      auto& code = *code_ptr;
      std::string getter = GenGetter(field.value.type);
      code += "func " + namer_.Method(field) + "() -> " + type_name + ":\n";
      code += "  return " + getter + "(self.start_offset";
      if (field.value.offset != 0) {
        code += " + " + NumToString(field.value.offset);
      }
      code += ")\n\n";
    } else {
      // table member
      std::string value = GenGetter(field.value.type) + "(voffset + self.start_offset)";
      auto is_bool = IsBool(field.value.type.base_type);
      auto is_enum = IsEnum(field.value.type);
      if (is_bool) {
        value = "bool(" + value + ")";
      } else if (is_enum) {
        value += " as " + type_name;
      }
      std::string default_value = GetDefaultValue(field);
      if (is_enum) {
        default_value += " as " + type_name;
      }

      auto& code = *code_ptr;
      code += "func " + namer_.Method(field) + "() -> " + type_name + ":\n";
      code += "  var voffset: int = self._get_voffset(" + NumToString(field.value.offset) + ")\n";
      code += "  if voffset != 0:\n";
      code += "    return " + value + "\n";
      code += "  return " + default_value + "\n\n";
    }
  }

  void GenAccessorStruct(const StructDef& struct_def,
                          const FieldDef& field,
                          std::string* code_ptr) const {
    if (struct_def.fixed) {
      // struct member
      std::string offset_str = "self.start_offset";
      if (field.value.offset != 0) {
        offset_str += " + " + NumToString(field.value.offset);
      }
      auto& code = *code_ptr;
      code += "func " + namer_.Method(field) + "() -> " + TypeName(field) + ":\n";
      code += "  return " + TypeName(field) + ".new(self.buffer, " + offset_str + ")\n\n";
    } else {
      // table member
      auto& code = *code_ptr;
      code += "func " + namer_.Method(field) + "() -> " + TypeName(field) + ":\n";

      code += "  var voffset: int = self._get_voffset(" + NumToString(field.value.offset) + ")\n";
      code += "  if voffset != 0:\n";
      if (field.value.type.struct_def->fixed) {
        // struct so read directly
        code += "    var offset = self.start_offset + voffset\n";
      } else {
        // table so build table
        code += "    var offset = self._get_indirect(self.start_offset + voffset)\n";
      }

      code += "    return " + TypeName(field) + ".new(self.buffer, offset)\n";
      code += "  return null\n\n";
    }
  }

  // Get the value of a string.
  void GenAccessorString(const FieldDef& field, std::string* code_ptr) const {
    auto& code = *code_ptr;
    code += "func " +namer_.Method(field) + "() -> String:\n";

    code += "  var voffset: int = self._get_voffset(" + NumToString(field.value.offset) + ")\n";
    code += "  if voffset != 0:\n";
    code += "    return " + GenGetter(field.value.type) + "(self.start_offset + voffset)\n";
    code += "  return \"\"\n\n";
  }

  // Get the value of a vector's struct member.
  void GenVectorAccessorStruct(const FieldDef& field, std::string* code_ptr) const {
    auto& code = *code_ptr;
    auto vectortype = field.value.type.VectorType();

    code += "func " + namer_.Method(field) + "(p_index: int) -> " + TypeName(field) + ":\n";
    code += "  var voffset: int = self._get_voffset(" + NumToString(field.value.offset) + ")\n";
    code += "  if voffset != 0:\n";
    code += "    var elem: int = self._vector_start(self.start_offset + voffset)\n";
    code += "    elem += p_index * " + NumToString(InlineSize(vectortype)) + "\n";
    if (!vectortype.struct_def->fixed) {
      // table, so it's a pointer
      code += "    elem = self._get_indirect(elem)\n";
    }
    code += "    return " + TypeName(field) + ".new(self.buffer, elem)\n";
    code += "  return null\n\n";
  }

  // Get the value of a vector's non-struct member.
  void GenVectorAccessorScalar(const FieldDef& field,
                                std::string* code_ptr) const {
    auto& code = *code_ptr;
    auto vectortype = field.value.type.VectorType();
    // workaround my type naming system which is bad
    auto type_name = IsSeries(field.value.type) && IsString(field.value.type.VectorType())
      ? "String" : GdTypeName(field);

    code += "func " + namer_.Method(field) + "(p_index: int)" + " -> " + type_name + ":\n";
    code += "  var voffset: int = self._get_voffset(" + NumToString(field.value.offset) + ")\n";
    code += "  if voffset != 0:\n";
    code += "    var elem: int = self._vector_start(self.start_offset + voffset)\n";
    code += "    elem += p_index * " + NumToString(InlineSize(vectortype)) + "\n";
    code += "    return " + GenGetter(field.value.type) + "(elem)\n";
    if (IsString(vectortype)) {
      code += "  return \"\"\n";
    } else {
      code += "  return 0\n";
    }
    code += "\n\n";
  }

  // Returns a nested flatbuffer as itself.
  void GenVectorAccessorAsNested(const FieldDef& field, std::string* code_ptr) const {
    auto nested = field.attributes.Lookup("nested_flatbuffer");
    if (!nested) {
      return;
    }  // There is no nested flatbuffer.

    auto& code = *code_ptr;
    code += "# TODO: implement / support nested flatbuffers\n\n";
  }


  // Get the value of a fixed size array.
  void GenFixedArrayAccessorStruct(const FieldDef& field, std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto vec_type = field.value.type.VectorType();

    auto offset_str = "self.start_offset + " + NumToString(field.value.offset) +
      " + (p_index * " + NumToString(InlineSize(vec_type)) + ")";

    code += "func " + namer_.Method(field) + "(p_index: int) -> " + TypeName(field) + ":\n";
    code += "  return " + TypeName(field) + ".new(self.buffer, " + offset_str + ")\n\n";
  }

  // Get the value of a vector's non-struct member. Uses a named return
  // argument to conveniently set the zero value for the result.
  void GenFixedArrayAccessorScalar(const FieldDef& field, std::string* code_ptr) const {
    auto& code = *code_ptr;
    code += "func " + namer_.Method(field) + "(p_index: int) -> " + GdTypeName(field) +":\n";
    code += "  return " + GenGetter(field.value.type) +
      "(self.start_offset + " + NumToString(field.value.offset) +
      " + (p_index * " + NumToString(InlineSize(field.value.type.VectorType())) + "))\n\n";
  }

  // Get the value of a union from an object.
  void GenAccessorUnion(const FieldDef& field, std::string* code_ptr) const {

    // TODO: handle strings here...
    auto& code = *code_ptr;

    code += "func " + namer_.Method(field) + "() -> FB__Table:\n";
    code += "  var voffset: int = self._get_voffset(" + NumToString(field.value.offset) + ")\n";
    code += "  if voffset != 0:\n";
    code += "    var offset = self._get_indirect(self.start_offset + voffset)\n";
    code += "    return FB__Table.new(self.buffer, offset)\n";
    code += "  return null\n\n";
  }

  // Generate a struct field, conditioned on its child type(s).
  void GenTableAccessors(const StructDef& struct_def, const FieldDef& field,
                         std::string* code_ptr) const {
    GenComment(field.doc_comment, code_ptr, &def_comment, "  ");
    if (IsScalar(field.value.type.base_type)) {
      GenAccessorScalar(struct_def, field, code_ptr);
    } else {
      switch (field.value.type.base_type) {
        case BASE_TYPE_STRUCT:
          GenAccessorStruct(struct_def, field, code_ptr);
          break;
        case BASE_TYPE_STRING:
          GenAccessorString(field, code_ptr);
          break;
        case BASE_TYPE_VECTOR: {
          auto vectortype = field.value.type.VectorType();
          if (vectortype.base_type == BASE_TYPE_STRUCT) {
            GenVectorAccessorStruct(field, code_ptr);
          } else {
            GenVectorAccessorScalar(field, code_ptr);
            GenVectorAccessorAsNested(field, code_ptr);
          }
          break;
        }
        case BASE_TYPE_ARRAY: {
          auto vectortype = field.value.type.VectorType();
          if (vectortype.base_type == BASE_TYPE_STRUCT) {
            GenFixedArrayAccessorStruct(field, code_ptr);
          } else {
            GenFixedArrayAccessorScalar(field, code_ptr);
            GenVectorAccessorAsNested(field, code_ptr);
          }
          break;
        }
        case BASE_TYPE_UNION:
          GenAccessorUnion(field, code_ptr);
          break;
        default:
          FLATBUFFERS_ASSERT(0);
      }
    }
    if (IsSeries(field.value.type)) {
      GenAccessorVectorUtils(field, code_ptr);
    }
  }

  void GenStructOrTable(const StructDef& struct_def, std::string* code_ptr) const {
    if (struct_def.generated) return;

    GenComment(struct_def.doc_comment, code_ptr, &def_comment);

    auto& code = *code_ptr;
    code += "class_name " + namer_.NamespacedType(struct_def) + " extends FB__Table\n";
    code += "\n\n";

    GenTableStaticMethods(struct_def, code_ptr);
    // Generates the ctor
    GenTableCtors(struct_def, code_ptr);

    code += "#### ACCESSORS ####\n\n";

    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto& field = **it;
      if (field.deprecated) continue;

      GenTableAccessors(struct_def, field, code_ptr);
    }

    if (struct_def.fixed) {
      // creates a struct constructor function
      GenStructBuilder(struct_def, code_ptr);
    } else {
      // Creates a set of functions that allow table construction.
      GenTableBuilder(struct_def, code_ptr);
    }
  }

  bool GenAllStructs() const {
    for (auto it = parser_.structs_.vec.begin();
         it != parser_.structs_.vec.end(); ++it) {
      auto& struct_def = **it;
      std::string declcode;
      GenStructOrTable(struct_def, &declcode);
      if (parser_.opts.generate_object_based_api) {
        GenObjectAPI(struct_def, &declcode);
      }

      const std::string mod =
          namer_.File(struct_def, SkipFile::SuffixAndExtension);
      if (!WriteTypeToFile(namer_.File(struct_def, SkipFile::Suffix),
                           *struct_def.defined_namespace, declcode)) {
        return false;
      }
    }
    return true;
  }

  /* ENUM GENERATION*/

  void GenEnum(const EnumDef& enum_def, std::string* code_ptr) const {
    if (enum_def.generated) return;
    auto& code = *code_ptr;

    GenComment(enum_def.doc_comment, &code, &def_comment);
    code += "class_name " + namer_.NamespacedType(enum_def) + "\n\n";
    code += "enum Enum { ";
    for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end(); ++it) {
      auto& ev = **it;
      GenComment(ev.doc_comment, &code, &def_comment);

      // enum member
      code += namer_.Variant(ev);
      code += " = ";
      code += enum_def.ToString(ev) + ", ";
    }
    code += "}\n\n";

    if (parser_.opts.generate_object_based_api && enum_def.is_union) {
      // make util ObjectType unpacking function
      // TODO: support struct and strings in unions
      code += "static func unpack_union_to_object_type(p_kind: Enum, p_value: FB__Table) -> FB__Object:\n";
      code += "  match p_kind:\n";
      auto it = enum_def.Vals().begin();
      // skip default value
      it++;
      for (; it != enum_def.Vals().end(); ++it) {
        auto& ev = **it;
        code += "    Enum." + namer_.Variant(ev) + ":\n";
        code += "      return " + GenTypeGet(ev.union_type) + ".ObjectType.unpack(p_value)\n";
      }
      code += "    Enum.NONE:\n";
      code += "      return null\n";
      code += "    _:\n";
      code += "      printerr(\"unknown union type\")\n";
      code += "      return null\n\n";
    }
  }


  bool GenAllEnums() const {
    for (auto it = parser_.enums_.vec.begin(); it != parser_.enums_.vec.end();
         ++it) {
      auto& enum_def = **it;
      std::string enumcode;
      GenEnum(enum_def, &enumcode);

      const std::string mod =
          namer_.File(enum_def, SkipFile::SuffixAndExtension);

      if (!WriteTypeToFile(namer_.File(enum_def, SkipFile::Suffix),
                           *enum_def.defined_namespace, enumcode)) {
        return false;
      }
    }
    return true;
  }


  /* BUILDER */

  void GenBuilderStart(const StructDef& struct_def,
                       std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto struct_type = namer_.NamespacedType(struct_def);
    // Generate method with struct name.

    code += "\n#### BUILDER ####\n\n";

    code += "static func begin(p_builder: FB__Builder) -> void:\n";
    code += "  p_builder.begin_table()\n\n";
  }

  void GenBuilderRootFinish(std::string* code_ptr) const {
    for (const bool size_prefix : {false, true}) {
      std::string method_name = size_prefix ? "finish_buffer_size_prefixed" : "finish_buffer";

      auto& code = *code_ptr;
      code += "static func " + method_name + "(builder: FB__Builder, p_offset: int) -> void:\n";
      code += "  builder.finish(p_offset";
      if (!parser_.file_identifier_.empty()) {
        code += ", \"" + parser_.file_identifier_ + "\"";
      }
      if (size_prefix) {
        if (parser_.file_identifier_.empty()) {
          code += ", \"\"";
        }
        code += ", true";
      }
      code += ")\n\n";
    }
  }

  void GenBuilderEnd(std::string* code_ptr) const {
    auto& code = *code_ptr;
    // Generate method with struct name.

    code += "static func end(builder: FB__Builder) -> int:\n";
    code += "  return builder.end_table()\n\n";
  }

  // Set the value of a table's field.
  void GenBuilderField(const FieldDef& field, const size_t field_index, std::string* code_ptr) const {
    bool is_pointer = !IsScalar(field.value.type.base_type);
    std::string default_value = is_pointer ? "0" : GetDefaultValue(field);

    const std::string field_var = "p_" + namer_.Variable(field) + (is_pointer ? "_offset" : "");
    const std::string field_method = namer_.Method(field);
    const std::string write_method = "write_aligned_" +
      (is_pointer ? "offset" : namer_.Method(GenTypeGet(field.value.type)));

    auto& code = *code_ptr;
    code += "static func add_" + field_method +
      "(p_builder: FB__Builder, " + field_var + ": " +
      (is_pointer ? "int" : GdTypeName(field)) + ") -> void:\n";

    // std::string next_block_indent = _ind;
    if (default_value != "null") {
      code += "  var default_value = " + default_value + "\n\n";
      code += "  if not (p_builder.force_defaults || (" + field_var + " != default_value)):\n";
      code += "    return\n\n";
    }

    if (!IsStruct(field.value.type)) {
      code += "  p_builder." + write_method + "(" + field_var + ")\n";
    } else {
      code += "  # structs are written inline, so data should already be written\n";
    }
    code += "  p_builder.curr_vtable.set_slot(" +
      NumToString(field_index) + ", p_builder.offset())\n";

    code += "\n\n";
  }

  // Set the value of one of the members of a table's vector.
  void GenBuilderVectorStart(const FieldDef& field, std::string* code_ptr) const {
    const std::string field_method = namer_.Method(field);

    auto vector_type = field.value.type.VectorType();
    auto alignment = InlineAlignment(vector_type);
    auto elem_size = InlineSize(vector_type);

    // Generate method with struct name.
    auto& code = *code_ptr;
    code += "static func begin_" + field_method +
      "_vector(p_builder: FB__Builder, p_num_elems: int):\n";

    code += "  return p_builder.begin_vector(" + NumToString(elem_size) +
      ", p_num_elems, " + NumToString(alignment) + ")\n\n";
  }

  void GenBuilderVectorCreateHelper(const FieldDef& field, std::string* code_ptr) const {
    const auto vector_type = field.value.type.VectorType();
    const bool is_struct_vector = IsStruct(vector_type);
    const bool is_table_vector = IsTable(vector_type);

    auto& code = *code_ptr;
    if (is_struct_vector) {
      // don't generate helpers for struct vectors
      // TODO: revisit this...
      code += "## TODO flatbuffers: add creators for struct vectors\n\n";
      return;
    }

    const std::string field_method = namer_.Method(field);
    auto write_method = is_table_vector || IsString(vector_type)
      ? "aligned_offset"
      : namer_.Method(GenTypeBasic(vector_type));
    auto alignment = InlineAlignment(vector_type);
    auto elem_size = InlineSize(vector_type);

    code += "static func create_" + field_method + "_vector" +
      "(p_builder: FB__Builder, p_data: Array[" + GdTypeName(field) + "]) -> int:\n";
    code += "  p_builder.begin_vector(" + NumToString(elem_size) +
      ", p_data.size(), " + NumToString(alignment) + ")\n";
    code += "  for i in range(p_data.size(), 0, -1):\n";
    code += "    var item = p_data[i - 1]\n";
    code += "    p_builder.write_" + write_method + "(item)\n";
    code += "  return p_builder.end_vector()\n\n";
  }

  // Set the value of one of the members of a table's vector and fills in the
  // elements from a bytearray. This is for simplifying the use of nested
  // flatbuffers.
  void GenBuilderVectorNested(const FieldDef& field, std::string* code_ptr) const {
    auto nested = field.attributes.Lookup("nested_flatbuffer");
    if (!nested) {
      return;
    }  // There is no nested flatbuffer.

    auto& code = *code_ptr;
    code += "## TODO flatbuffers: implement / support nested flatbuffers\n\n";
  }

  // Generate table builder
  void GenTableBuilder(const StructDef& struct_def, std::string* code_ptr) const {
    GenBuilderStart(struct_def, code_ptr);

    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto& field = **it;
      if (field.deprecated) continue;

      auto offset = it - struct_def.fields.vec.begin();
      GenBuilderField(field, offset, code_ptr);
      if (IsVector(field.value.type)) {
        GenBuilderVectorStart(field, code_ptr);
        GenBuilderVectorCreateHelper(field, code_ptr);
        GenBuilderVectorNested(field, code_ptr);
      }
    }
    GenBuilderEnd(code_ptr);
    if (parser_.root_struct_def_ == &struct_def) {
      // only generate buffer finish for root table
      GenBuilderRootFinish(code_ptr);
    }

    if (CanCreateFactoryMethod(struct_def)) {
      BeginBuilderArgs(struct_def, code_ptr);
      TableBuilderArgs(struct_def, "p_", code_ptr);
      EndBuilderArgs(code_ptr);
      TableBuilderBody(struct_def, "p_", code_ptr);
    }
  }

  // Create a struct with a builder and the struct's arguments.
  void GenStructBuilder(const StructDef& struct_def,
                        std::string* code_ptr) const {
    BeginBuilderArgs(struct_def, code_ptr);
    StructBuilderArgs(struct_def, "p_", "_", code_ptr);
    EndBuilderArgs(code_ptr);

    StructBuilderBody(struct_def, "p_", code_ptr);
    EndBuilderBody(code_ptr);
  }

  // Begin the creator function signature.
  void BeginBuilderArgs(const StructDef& struct_def,
                        std::string* code_ptr) const {
    auto& code = *code_ptr;

    code += "\n#### BUILDER ####\n\n";
    code += "static func create_" + namer_.Function(struct_def);
    code += "(p_builder: FB__Builder";
  }

  void StructBuilderArgs(const StructDef& struct_def,
                         const std::string nameprefix,
                         const std::string fieldname_suffix,
                         std::string* code_ptr,
                         bool parent_struct_array = false) const {
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto& field = **it;
      const auto& field_type = field.value.type;
      const auto is_array = IsArray(field_type);
      const auto& type = is_array ? field_type.VectorType() : field_type;
      if (IsStruct(type)) {
        // Generate arguments for a struct inside a struct. To ensure names
        // don't clash, and to make it obvious these arguments are constructing
        // a nested struct, prefix the name with the field name.
        auto subprefix = nameprefix;
        subprefix += namer_.Field(field) + fieldname_suffix;

        StructBuilderArgs(*field.value.type.struct_def, subprefix,
                          fieldname_suffix, code_ptr, is_array);
      } else {
        auto& code = *code_ptr;
        code += std::string(", ") + nameprefix;
        code += namer_.Field(field);
        code += ": " + (parent_struct_array || is_array
          ? "Array"
          : GdTypeName(field));
      }
    }
  }

  std::string TableBuilderFieldName(const FieldDef& field, const std::string nameprefix) const {
    std::string out = nameprefix + namer_.Field(field);

    if (!IsScalar(field.value.type.base_type)) {
      out += "_offset";
    }
    return out;
  }

  void TableBuilderArgs(const StructDef& struct_def,
                        const std::string nameprefix,
                        std::string* code_ptr) const {
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto& field = **it;
      auto& code = *code_ptr;
      code += std::string(", ") + TableBuilderFieldName(field, nameprefix);
      code += ": " + (IsScalar(field.value.type.base_type) ? GdTypeName(field) : "int");
    }
  }

  void TableBuilderBody(const StructDef& struct_def, const char* nameprefix,
                        std::string* code_ptr) const {
    const std::string struct_name = namer_.NamespacedType(struct_def);

    auto& code = *code_ptr;
    code += "  " + struct_name + ".begin(p_builder)\n";
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto& field = **it;
      auto arg_name = TableBuilderFieldName(field, nameprefix);
      const auto field_name = namer_.Method(field);
      // TODO: add deprecated checks across the board

      if (field.IsScalarOptional()) {
        code += "  if " + arg_name + " != null:\n";
        code += "  ";
      }
      code += "  " + struct_name + ".add_" + field_name + "(" +
        "p_builder, " + arg_name + ")\n";
    }
    code += "  return " + struct_name + ".end(p_builder)\n";
  }

  void EndBuilderArgs(std::string* code_ptr) const {
    auto& code = *code_ptr;
    code += ") -> int:\n";
  }

  void StructBuilderBody(const StructDef& struct_def, const char* nameprefix,
                         std::string* code_ptr, size_t level = 0,
                         bool in_array = false) const {
    auto& code = *code_ptr;
    std::string indent(level * 2, ' ');
    code +=
        indent + "  p_builder.prep(" + NumToString(struct_def.minalign) + ", ";
    code += NumToString(struct_def.bytesize) + ")\n";
    for (auto it = struct_def.fields.vec.rbegin();
         it != struct_def.fields.vec.rend(); ++it) {
      auto& field = **it;
      const auto& field_type = field.value.type;
      const auto& type =
          IsArray(field_type) ? field_type.VectorType() : field_type;
      if (field.padding) {
        code +=
          indent + "  p_builder.pad(" + NumToString(field.padding) + ")\n";
      }
      if (IsStruct(field_type)) {
        // TODO: just call create methods here instead, i.e.:
        // code += indent + "  " + namer_.NamespacedType(struct_def) + ".create_" + namer_.Variable(field) +
        //   "(p_builder, ...)\n";

        StructBuilderBody(*field_type.struct_def,
                          (nameprefix + (namer_.Field(field) + "_")).c_str(),
                          code_ptr, level, in_array);
      } else {
        const auto index_var = "_idx" + NumToString(level);
        if (IsArray(field_type)) {
          code += indent + "  for " + index_var + " in range(";
          code += NumToString(field_type.fixed_length);
          code += " , 0, -1):\n";
          in_array = true;
        }
        if (IsStruct(type)) {
          StructBuilderBody(*field_type.struct_def,
                            (nameprefix + (namer_.Field(field) + "_")).c_str(),
                            code_ptr, level + 1, in_array);
        } else {
          code += IsArray(field_type) ? "  " : "";
          code += indent + "  p_builder.write_" + GenMethod(field) + "(";
          code += nameprefix + namer_.Variable(field);
          size_t array_cnt = level + (IsArray(field_type) ? 1 : 0);
          for (size_t i = 0; in_array && i < array_cnt; i++) {
            code += "[_idx" + NumToString(i) + "-1]";
          }
          code += ")\n";
        }
      }
    }
  }

  void EndBuilderBody(std::string* code_ptr) const {
    auto& code = *code_ptr;
    code += "\n";
    code += "  return p_builder.offset()\n";
  }

  /* UTILS AND HELPERS */

  bool WriteTypeToFile(const std::string& defname, const Namespace& ns,
                       const std::string& classcode) const {
    if (classcode.empty()) return true;

    std::string code = "";
    code = code + "# " + FlatBuffersGeneratedWarning() + "\n\n";
    code += "# namespace: " + LastNamespacePart(ns) + "\n\n";
    code += classcode;

    const std::string directories = namer_.Directories(ns.components);
    EnsureDirExists(directories);

    const std::string filename = directories + defname;
    return parser_.opts.file_saver->SaveFile(filename.c_str(), code, false);
  }

  std::string GetDefaultValue(const FieldDef& field) const {
    BaseType base_type = field.value.type.base_type;
    if (field.IsScalarOptional()) {
      return "null";
    } else if (IsBool(base_type)) {
      return field.value.constant == "0" ? "false" : "true";
    } else if (IsFloat(base_type)) {
      return float_const_gen_.GenFloatConstant(field);
    } else if (IsEnum(field.value.type)) {
      const EnumDef& enum_def = *field.value.type.enum_def;
      return namer_.NamespacedType(enum_def) + ".Enum." + namer_.Variant(*enum_def.Vals().front());
    } else if (IsInteger(base_type)) {
      return field.value.constant;
    } else if (IsString(field.value.type)) {
      return "\"\"";
    } else if (IsArray(field.value.type) || IsVector(field.value.type)) {
      return "[]";
    } else {
      // For string, struct, and table.
      return "null";
    }
  }

  // Returns the function name that is able to read a value of the given type.
  std::string GenGetter(const Type& type) const {
    switch (type.base_type) {
      case BASE_TYPE_STRING:
        return "self._make_string";
      case BASE_TYPE_VECTOR:
        return GenGetter(type.VectorType());
      default:
        return "self.buffer.bytes.decode_" + namer_.Method(GenTypeGet(type));
    }
  }

  // Returns the method name for use with add/put calls.
  std::string GenMethod(const FieldDef& field) const {
    return (IsScalar(field.value.type.base_type) || IsArray(field.value.type))
      ? namer_.Method(GenTypeBasic(field.value.type))
      : "u32";
  }

  /* TYPE NAME HELPERS - A REAL MESS */

  // TODO: tidy this up...
  std::string GenTypeBasic(const Type& type) const {
    // clang-format off
    static const char *ctypename[] = {
      #define FLATBUFFERS_TD(ENUM, IDLTYPE, \
                             CTYPE, JTYPE, GTYPE, NTYPE, PTYPE, KTYPE, RTYPE, \
                             STYPE, GDTYPE, ...)                                     \
        #GDTYPE,
        FLATBUFFERS_GEN_TYPES(FLATBUFFERS_TD)
      #undef FLATBUFFERS_TD
    };
    // clang-format on
    return ctypename[IsArray(type) ? type.VectorType().base_type
                                   : type.base_type];
  }

  std::string GenTypePointer(const Type& type) const {
    switch (type.base_type) {
      case BASE_TYPE_STRING:
        return "String";
      case BASE_TYPE_VECTOR:
        // fall through
      case BASE_TYPE_ARRAY:
        return GenTypeGet(type.VectorType());
      case BASE_TYPE_STRUCT:
        return namer_.NamespacedType(*type.struct_def);
      case BASE_TYPE_UNION:
        return namer_.NamespacedType(*type.enum_def);
        // fall through
      default:
        return "FB__Table";
    }
  }

  std::string GenTypeGet(const Type& type) const {
    return IsScalar(type.base_type) ? GenTypeBasic(type) : GenTypePointer(type);
  }

  std::string TypeName(const FieldDef& field) const {
    return GenTypeGet(field.value.type);
  }

  std::string GdObjectApiTypeName(const FieldDef& field) const {
    std::string out = "";
    bool is_array = IsArray(field.value.type) || IsVector(field.value.type);
    auto field_type = is_array ? field.value.type.VectorType() : field.value.type;
    std::string field_name = TypeName(field);

    if (is_array) {
      out += "Array[";
    }

    if (IsString(field_type)) {
      out += "String";
    } else if (IsEnum(field_type)) {
      out += namer_.NamespacedType(*field_type.enum_def) + ".Enum";
    } else if (IsUnion(field_type)) {
      // just use base class
      out += "FB__Object";
    } else if (IsStruct(field_type) || IsTable(field_type)) {
      out += field_name + ".ObjectType";
    } else if (IsBool(field_type.base_type)) {
      out += "bool";
    } else if (field_name == "float" || field_name == "double") {
        out += "float";
    } else if (IsInteger(field_type.base_type)) {
      out += "int";
    } else {
      out += "unknown_type";
    }

    if (is_array) {
      out += "]";
    }
    return out;
  }

  std::string GdArrayTypeName(const FieldDef& field,
                              bool allow_struct = false,
                              bool get_object = false) const {
    return "Array[" + GdTypeName(field, allow_struct, get_object) + "]";
  }

  std::string GdTypeName(const FieldDef& field,
                         bool allow_struct = false,
                         bool get_object = false) const {
    std::string field_name = TypeName(field);
    auto field_type = field.value.type;
    auto is_bool = IsBool(field_type.base_type);

    std::string out;
    bool is_struct = field_type.base_type == BASE_TYPE_STRUCT ||
      (field_type.base_type == BASE_TYPE_ARRAY &&
       (field_type.VectorType().base_type == BASE_TYPE_STRUCT));

    if (is_bool) {
      out = "bool";
    } else if (field_name == "float" || field_name == "double") {
      out = "float";
    } else if (IsEnum(field_type)) {
      out = namer_.NamespacedType(*field_type.enum_def) + ".Enum";
    } else if (allow_struct && is_struct) {
      return get_object ? field_name + ".ObjectType" : field_name;
    } else if (IsString(field_type)) {
      return "String";
    } else {
      out = "int";
    }

    return out;
  }


  /* OBJECT API */

  void GenObjAPIInit(const StructDef& struct_def, std::string* code_ptr) const {
    std::string init_body;

    std::string signature_params;

    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto& field = **it;
      if (field.deprecated) continue;

      // Determines field type, default value, and typing imports.
      const auto field_type = GdObjectApiTypeName(field);

      const auto default_value = GetDefaultValue(field);
      // Writes the init statement.
      const auto field_field = namer_.Field(field);

      // Build signature with keyword arguments, type hints, and default values.
      if (!signature_params.empty()) {
        signature_params += ", ";
      }
      signature_params += "p_" + field_field + ": " + field_type;
      signature_params += " = " + default_value;

      // Build the body of the __init__ method.
      init_body += "    self." + field_field + " = p_" + field_field + "\n";
    }

    // Writes __init__ method.
    auto& code_base = *code_ptr;
    code_base += "  func _init(" + signature_params + "):\n";
    if (init_body.empty()) {
      code_base += "    pass";
    } else {
      code_base += init_body;
    }
    code_base += "\n";
  }

  void GenUnPackForUnion(const StructDef& struct_def,
                         const FieldDef& field,
                         std::string* args_ptr,
                         std::string* code_prefix_ptr) const {
    auto& args = *args_ptr;
    auto& code_prefix = *code_prefix_ptr;
    const auto table_name = "p_" + namer_.Variable(struct_def);
    const auto field_field = namer_.Field(field);
    const auto var_name = "_" + field_field;
    const auto field_method = namer_.Method(field);
    const auto field_type = TypeName(field);

    code_prefix += "    var " + var_name + " = " + table_name + "." + field_method + "()\n";

    const auto kind = table_name + "." + field_field + "_type()";
    // auto field_func = ;
    args += "\n      ";
    args += "(" + field_type + ".unpack_union_to_object_type(" + kind + ", " + var_name + "))" +
      " if (" + var_name + " != null) else null,";
  }

  void GenUnPackForVector(const StructDef& struct_def,
                          const FieldDef& field,
                          std::string* args_ptr,
                          std::string* code_prefix_ptr) const {
    const auto vector_type = field.value.type.VectorType();
    const bool is_table_or_struct = vector_type.base_type == BASE_TYPE_STRUCT;

    auto& args = *args_ptr;
    auto& code_prefix = *code_prefix_ptr;
    const auto table_name = "p_" + namer_.Variable(struct_def);
    const auto var_name = "_" + namer_.Field(field);
    const auto field_method = namer_.Method(field);
    const auto struct_var = namer_.Variable(struct_def);
    // const auto field_type = GdTypeName(field);

    const auto field_type = IsScalar(vector_type.base_type)
      ? GdTypeName(field, true)
      : TypeName(field);
    const auto api_field_type = GdObjectApiTypeName(field);

    // for vectors the length is there at runtime, for arrays it's known at compile
    const std::string arr_len = IsArray(field.value.type)
      ? NumToString(field.value.type.fixed_length)
      : table_name + "." + namer_.Field(field) + "_length()";

    code_prefix += "    var " + var_name + ": " + api_field_type + " = []\n";
    code_prefix += "    for i in range(" + arr_len +"):\n";
    code_prefix += "      var val: " + field_type + " = " + table_name + "." + field_method +
      "(i)\n";
    code_prefix += "      if val != null:\n";
    code_prefix += "        " + var_name + ".append(";
    if (is_table_or_struct) {
      code_prefix += field_type + ".ObjectType.unpack(val)";
    } else {
      code_prefix += "val";
    }
    code_prefix += ")\n";

    args += "\n      ";
    args += var_name + ",";
  }

  void GenUnPackForStruct(const StructDef& struct_def,
                          const FieldDef& field,
                          std::string* args_ptr,
                          std::string* code_prefix_ptr) const {
    auto& args = *args_ptr;
    auto& code_prefix = *code_prefix_ptr;
    const auto table_name = "p_" + namer_.Variable(struct_def);
    const auto var_name = "_" + namer_.Field(field);
    const auto field_method = namer_.Method(field);
    const auto field_type = TypeName(field);

    code_prefix += "    var " + var_name + " = " + table_name + "." + field_method + "()\n";

    // auto field_func = ;
    args += "\n      ";
    args += "(" + field_type + ".ObjectType.unpack(" + var_name + "))" +
      " if (" + var_name + " != null) else null,";
  }

  void GenUnPackForScalar(const StructDef& struct_def, const FieldDef& field,
                          std::string* args_ptr) const {
    auto& args = *args_ptr;
    const auto field_field = namer_.Field(field);
    const auto field_method = namer_.Method(field);
    const auto struct_var = namer_.Variable(struct_def);
    const auto table_name = "p_" + namer_.Variable(struct_def);

    args += "\n      ";
    args += table_name + "." + field_method + "(),";
  }

  // Generates the UnPack method for the object class.
  void GenUnPack(const StructDef& struct_def, std::string* code_ptr) const {
    std::string args_str, prefix_str;

    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto& field = **it;
      if (field.deprecated) continue;

      switch (field.value.type.base_type) {
        case BASE_TYPE_STRUCT: {
          GenUnPackForStruct(struct_def, field, &args_str, &prefix_str);
          break;
        }
        case BASE_TYPE_UNION: {
          GenUnPackForUnion(struct_def, field, &args_str, &prefix_str);
          break;
        }
        case BASE_TYPE_ARRAY:
        case BASE_TYPE_VECTOR: {
          GenUnPackForVector(struct_def, field,
                             &args_str, &prefix_str);
          break;
        }
        case BASE_TYPE_STRING:
        default:
          GenUnPackForScalar(struct_def, field, &args_str);
          break;
      }
    }

    // Writes import statements and code into the generated file.
    auto& code_base = *code_ptr;
    const auto struct_var = namer_.Variable(struct_def);

    const std::string return_type = "ObjectType";
    const std::string table_type = namer_.NamespacedType(struct_def);
    const std::string table_name = "p_" + namer_.Variable(struct_def);

    code_base += "  static func unpack(" + table_name + ": " + table_type + ") -> " + return_type + ":\n";
    code_base += "    if " + table_name + " == null:\n";
    code_base += "      return null\n";
    code_base += prefix_str + "\n";
    code_base += "    return " + return_type + ".new(";

    // Write the args.
    code_base += args_str;
    code_base += ")\n\n";
  }

  bool StructPackArgs(const StructDef& struct_def,
                      const std::string nameprefix,
                      const std::string fieldname_suffix,
                      std::string* code_ptr) const {
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto& field = **it;
      const auto& field_type = field.value.type;
      const auto is_array = IsArray(field_type);
      const auto& type = is_array ? field_type.VectorType() : field_type;
      if (IsStruct(type)) {
        // TODO: check how this works with arrays of structs that themselves contain arrays of
        //       structs
        if (is_array) {
          // we have to map into arrays to extract the fields
          auto subprefix = nameprefix + "." + namer_.Field(field) + ".map(" +
            "func(elem): return elem"; // .something

          StructPackArgs(*field.value.type.struct_def, subprefix, ") as Array[int]", code_ptr);
        } else {
          // just extract the field directly
          auto subprefix = nameprefix + "." + namer_.Field(field);

          StructPackArgs(*field.value.type.struct_def, subprefix, fieldname_suffix, code_ptr);
        }
      } else {
        auto& code = *code_ptr;
        code += std::string(", ") + nameprefix + "." + namer_.Field(field) + fieldname_suffix;
      }
    }
    return true;
  }

  void GenPackForStruct(const StructDef& struct_def,
                        std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto struct_type = namer_.NamespacedType(struct_def);
    const auto struct_fn = namer_.Function(struct_def);

    std::string args_str;
    StructPackArgs(struct_def, "self", "", &args_str);

    code += "  func pack(p_builder: FB__Builder) -> int:\n";
    code += "    return " + struct_type +".create_" + struct_fn;
    code += "(p_builder" + args_str + ")\n\n";

  }

  void GenPackForVectorField(const StructDef& struct_def,
                             const FieldDef& field,
                             std::string* code_prefix_ptr,
                             std::string* code_ptr) const {
    auto& code_prefix = *code_prefix_ptr;
    auto& code = *code_ptr;
    const auto field_field = namer_.Field(field);
    const auto field_method = namer_.Method(field);
    const auto struct_type = namer_.NamespacedType(struct_def);

    auto vectortype = field.value.type.VectorType();
    if (IsString(vectortype)) {
      code_prefix += "    var " + field_field + "_offset: int = " +
        struct_type + ".create_" + field_method + "_vector(p_builder, " +
        "p_builder.pack_string_array(self." + field_field + "))\n";
    } else if (IsTable(vectortype)) {
      code_prefix += "    var " + field_field + "_offset: int = " +
        struct_type + ".create_" + field_method + "_vector(p_builder, " +
        "p_builder.pack_object_array(self." + field_field + " as Array[FB__Object]))\n";
    } else if (IsStruct(vectortype)) {
      const auto rev_var = field_field + "_reversed";
      code_prefix +=  "\n";
      code_prefix += "    var " + rev_var + ": Array[FB__Object]\n";
      code_prefix += "    " + rev_var + ".assign(self." + field_field + ".duplicate())\n";
      code_prefix += "    " + rev_var + ".reverse()\n";
      code_prefix += "    " + struct_type + ".begin_" + field_field +
        "_vector(p_builder, " + rev_var + ".size())\n";
      code_prefix += "    p_builder.pack_object_array(" + rev_var + ")\n";
      code_prefix += "    var " + field_field + "_offset: int = " + "p_builder.end_vector()\n";
      code_prefix +=  "\n";
    } else {
      // scalar
      code_prefix += "    var " + field_field + "_offset: int = " +
        struct_type + ".create_" + field_method + "_vector(p_builder, " + "self." + field_field + ")\n";
    }

    code += "    " + struct_type + ".add_" + field_method + "(p_builder, " +
            field_field + "_offset)\n";

  }

  void GenPackForStructField(const StructDef& struct_def, const FieldDef& field,
                             std::string* code_decl_ptr,
                             std::string* code_prefix_ptr,
                             std::string* code_ptr) const {
    auto& code_decl = *code_decl_ptr;
    auto& code_prefix = *code_prefix_ptr;
    auto& code = *code_ptr;
    const auto field_field = namer_.Field(field);
    const auto field_method = namer_.Method(field);
    const auto struct_type = namer_.NamespacedType(struct_def);

    if (field.value.type.struct_def->fixed) {
      // Pure struct fields need to be created along with their parent
      // structs.
      code += "    if self." + field_field + " != null:\n";
      code += "      var " + field_field + "_offset: int = self." + field_field + ".pack(p_builder)\n";
    } else {
      code_decl += "    var " + field_field + "_offset: int = 0\n";
      // Tables need to be created before their parent structs are created.
      code_prefix += "    if self." + field_field + " != null:\n";
      code_prefix += "      " + field_field + "_offset = self." + field_field +
                     ".pack(p_builder)\n";
      code += "    if " + field_field + "_offset != 0:\n";
    }

    code += "      " + struct_type + ".add_" + field_method + "(p_builder, " +
            field_field + "_offset)\n";
  }

  void GenPackForUnionField(const StructDef& struct_def, const FieldDef& field,
                             std::string* code_decl_ptr,
                            std::string* code_prefix_ptr,
                            std::string* code_ptr) const {
    auto& code_decl = *code_decl_ptr;
    auto& code_prefix = *code_prefix_ptr;
    auto& code = *code_ptr;
    const auto field_field = namer_.Field(field);
    const auto field_method = namer_.Method(field);
    const auto struct_type = namer_.NamespacedType(struct_def);

    code_decl += "    var " + field_field + "_offset: int = 0\n";
    code_prefix += "    if self." + field_field + " != null:\n";
    code_prefix += "      " + field_field + "_offset = self." + field_field +
                   ".pack(p_builder)\n";
    code += "    " + struct_type + ".add_" + field_method + "(p_builder, " +
      field_field + "_offset)\n";
  }

  void GenPackForString(const StructDef& struct_def, const FieldDef& field,
                        std::string* code_decl_ptr,
                        std::string* code_prefix_ptr,
                        std::string* code_ptr) const {
    auto& code_decl = *code_decl_ptr;
    auto& code_prefix = *code_prefix_ptr;
    auto& code = *code_ptr;
    const auto field_field = namer_.Field(field);
    const auto field_method = namer_.Method(field);
    const auto struct_type = namer_.NamespacedType(struct_def);

    code_decl += "    var " + field_field + "_offset: int = 0\n";
    code_prefix += "    if self." + field_field + " != \"\":\n";
    code_prefix += "      " + field_field + "_offset = p_builder.write_string(self." + field_field + ")\n";
    code += "    " + struct_type + ".add_" + field_method +
            "(p_builder, " + field_field + "_offset)\n";
  }

  void GenPackForTable(const StructDef& struct_def,
                       std::string* code_ptr) const {
    auto& code_base = *code_ptr;
    std::string code, code_prefix, code_decl;
    const auto struct_var = namer_.Variable(struct_def);
    const auto struct_type = namer_.NamespacedType(struct_def);

    code_base += "  func pack(p_builder: FB__Builder) -> int:\n";
    code += "    " + struct_type + ".begin(p_builder)\n";
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto& field = **it;
      if (field.deprecated) continue;

      const auto field_method = namer_.Method(field);
      const auto field_field = namer_.Field(field);

      switch (field.value.type.base_type) {
        case BASE_TYPE_STRUCT: {
          GenPackForStructField(struct_def, field, &code_decl,
                                &code_prefix, &code);
          break;
        }
        case BASE_TYPE_UNION: {
          GenPackForUnionField(struct_def, field, &code_decl,
                               &code_prefix, &code);
          break;
        }
        case BASE_TYPE_ARRAY:
        case BASE_TYPE_VECTOR: {
          GenPackForVectorField(struct_def, field, &code_prefix, &code);
          break;
        }
        case BASE_TYPE_STRING: {
          GenPackForString(struct_def, field, &code_decl, &code_prefix, &code);
          break;
        }
        default:
          // pack for scalar
          code += "    "  + struct_type + ".add_" + field_method +
                  "(p_builder, self." + field_field + ")\n";
          break;
      }
    }

    code += "    return " + struct_type + ".end(p_builder)\n";

    code_base += code_decl + code_prefix + code;
    code_base += "\n";
  }

  void GenObjectAPI(const StructDef& struct_def,
                    std::string* code_ptr) const {
    if (struct_def.generated) return;

    auto& code = *code_ptr;

    code += "\n\n#### OBJECT API ####\n";
    code += "\nclass ObjectType extends FB__Object:\n";

    // generate Object member variables
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto& field = **it;

      const auto field_type = GdObjectApiTypeName(field);

      const auto default_value = GetDefaultValue(field);

      code += "  var " + namer_.Field(field) + ": " +
        field_type + " = " + default_value + "\n";
    }

    code += "\n";

    GenObjAPIInit(struct_def, &code);

    GenUnPack(struct_def, &code);

    if (struct_def.fixed) {
      GenPackForStruct(struct_def, &code);
    } else {
      GenPackForTable(struct_def, &code);
    }
  }
};

}  // namespace gdscript

static const char* GenerateGdscript(const Parser& parser, const std::string& path,
                                  const std::string& file_name) {
  gdscript::GdscriptGenerator generator(parser, path, file_name);
  if (!generator.generate()) return "could not generate GDScript code";

  return nullptr;
}

namespace {

class GdscriptCodeGenerator : public CodeGenerator {
 public:
  Status GenerateCode(const Parser& parser, const std::string& path,
                      const std::string& filename) override {
    auto err = GenerateGdscript(parser, path, filename);
    if (err) {
      status_detail = " " + std::string(err);
      return Status::ERROR;
    }
    return Status::OK;
  }

  Status GenerateCode(const uint8_t*, int64_t, const CodeGenOptions&) override {
    return Status::NOT_IMPLEMENTED;
  }

  Status GenerateMakeRule(const Parser& parser, const std::string& path,
                          const std::string& filename,
                          std::string& output) override {
    (void)parser;
    (void)path;
    (void)filename;
    (void)output;
    return Status::NOT_IMPLEMENTED;
  }

  Status GenerateGrpcCode(const Parser& parser, const std::string& path,
                          const std::string& filename) override {
    (void)parser;
    (void)path;
    (void)filename;
    return Status::NOT_IMPLEMENTED;
  }

  Status GenerateRootFile(const Parser& parser,
                          const std::string& path) override {
    (void)parser;
    (void)path;
    return Status::NOT_IMPLEMENTED;
  }

  bool IsSchemaOnly() const override { return true; }

  bool SupportsBfbsGeneration() const override { return false; }
  bool SupportsRootFileGeneration() const override { return false; }

  IDLOptions::Language Language() const override { return IDLOptions::kGDScript; }

  std::string LanguageName() const override { return "GDScript"; }
};
}  // namespace

std::unique_ptr<CodeGenerator> NewGdscriptCodeGenerator() {
  return std::unique_ptr<GdscriptCodeGenerator>(new GdscriptCodeGenerator());
}

}  // namespace flatbuffers
