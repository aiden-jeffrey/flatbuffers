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

#include "idl_gen_gdscript.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "codegen/idl_namer.h"
#include "flatbuffers/code_generators.h"
#include "flatbuffers/flatbuffers.h"
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
    /*namespace_seperator=*/".",
    /*object_prefix=*/"FB__",
    /*object_suffix=*/"T",
    /*keyword_prefix=*/"",
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
static const std::string Indent = "  ";

}  // namespace

class GdscriptGenerator : public BaseGenerator {
 public:
  GdscriptGenerator(const Parser& parser, const std::string& path,
                  const std::string& file_name)
      : BaseGenerator(parser, path, file_name, "" /* not used */,
                      "" /* not used */, "gd"),
        float_const_gen_("NAN", "INF", "-INF"),
        namer_(WithFlagOptions(kConfig, parser.opts, path), Keywords()) {
  }

  // Most field accessors need to retrieve and test the field offset first,
  // this is the prefix code for that.
  std::string OffsetPrefix(const FieldDef& field) const {
    return "\n" +
      Indent +
      "var voffset = self._get_voffset(" + NumToString(field.value.offset) + ")\n" +
      Indent +
      "if voffset != 0:\n";
  }

  // Begin a class declaration.
  void BeginClass(const StructDef& struct_def, std::string* code_ptr) const {
    std::string base_class = "FB__Table";
    auto& code = *code_ptr;
    code += "class_name " + namer_.Type(struct_def) + " extends " + base_class + "\n";
    code += "\n\n";
  }

  // Begin enum code with a class declaration.
  void BeginEnum(const EnumDef& enum_def, std::string* code_ptr) const {
    auto& code = *code_ptr;
    code += "class_name " + namer_.Type(enum_def) + "\n\n";
    code += "enum Enum { ";
  }

  void EndEnum(std::string* code_ptr) const {
    auto& code = *code_ptr;
    code += "}\n";
  }

  // Starts a new line and then indents.
  std::string GenIndents(int level) const {
    std::string out = "\n";
    int num = level - 1;
    for (int i = 0; i < num; i++) {
      out += Indent;
    }
    return out;
  }

  // A single enum member.
  void EnumMember(const EnumDef& enum_def, const EnumVal& ev,
                  std::string* code_ptr) const {
    auto& code = *code_ptr;
    code += namer_.Variant(ev);
    code += " = ";
    code += enum_def.ToString(ev) + ", ";
  }

  void GenTableStaticMethods(const StructDef& struct_def,
                             std::string* code_ptr) const {
    auto& code = *code_ptr;
    const std::string struct_type = namer_.Type(struct_def);

    // ctors from buffer
    // TODO: support root struct here...
    code += "static func get_root_as";
    code += "(p_buffer: FB__ByteBuffer) -> " + struct_type + ":\n";
    code += Indent + "var pos = p_buffer.position\n";
    code += Indent + "var root_offset = pos + p_buffer.bytes.decode_u32(pos)\n";
    code += Indent + "return " + struct_type + ".new(p_buffer, root_offset)\n";
    code += "\n";

    code += "static func get_size_prefixed_root_as";
    code += "(p_buffer: FB__ByteBuffer) -> " + struct_type + ":\n";
    code += Indent + "p_buffer.position += FB__Constants.FILE_IDENTIFIER_LENGTH\n";
    code += Indent + "var pos = p_buffer.position\n";
    code += Indent + "var root_offset = pos + p_buffer.bytes.decode_u32(pos)\n";
    code += Indent + "return " + struct_type + ".new(p_buffer, root_offset)\n";
    code += "\n";

    if (!struct_def.fixed && parser_.root_struct_def_ == &struct_def &&
        !parser_.file_identifier_.empty()) {
      // id checker
      code += "static func buffer_has_identifier";
      code += "(p_buffer: FB__ByteBuffer) -> bool:\n";
      code += Indent + "return p_buffer.has_identifier(\"";
      code += parser_.file_identifier_ + "\")\n";
      code += "\n";
    }
  }

  // Initialize an existing object with other data, to avoid an allocation.
  void GenConstructor(const StructDef& struct_def,
                          std::string* code_ptr) const {
    auto& code = *code_ptr;

    std::string is_struct = (struct_def.fixed ? "true" : "false");

    GenReceiver(struct_def, code_ptr);
    code += "_init(p_buffer: FB__ByteBuffer, p_offset: int) -> void:\n";
    code += Indent + "super(p_buffer, p_offset, " + is_struct + ")\n";
    code += "\n";
  }

  // Get the length of a vector.
  void GetVectorLen(const StructDef& struct_def, const FieldDef& field,
                    std::string* code_ptr) const {
    auto& code = *code_ptr;

    GenReceiver(struct_def, code_ptr);
    code += namer_.Method(field) + "_length()";
    code += " -> int:";
    if (!IsArray(field.value.type)) {
      // vector
      code += OffsetPrefix(field);
      code += Indent + Indent + Indent + "return self._vector_len(voffset)";
      code += GenIndents(2) + "return 0\n\n";
    } else {
      // fixed length array
      code += GenIndents(2) + "return " +
              NumToString(field.value.type.fixed_length) + "\n\n";
    }
  }

  // Determines whether a vector is none or not.
  void GetVectorIsNone(const StructDef& struct_def, const FieldDef& field,
                       std::string* code_ptr) const {
    auto& code = *code_ptr;

    GenReceiver(struct_def, code_ptr);
    code += namer_.Method(field) + "_is_null()";
    code += " -> bool:";
    if (!IsArray(field.value.type)) {
      code += GenIndents(2);
      code += "var voffset = self._get_voffset(" + NumToString(field.value.offset) + ")";
      code += GenIndents(2) + "return voffset == 0";
    } else {
      // assume that we always have an array as memory is preassigned
      code += GenIndents(2) + "return false";
    }
    code += "\n\n";
  }

  // Get the value of a struct's scalar.
  void GetScalarFieldOfStruct(const StructDef& struct_def,
                              const FieldDef& field,
                              std::string* code_ptr) const {
    auto& code = *code_ptr;
    std::string getter = GenGetter(field.value.type);
    GenReceiver(struct_def, code_ptr);
    code += namer_.Method(field);
    code += "() -> " + GdTypeName(field) + ":\n";
    code += Indent + "return " + getter + "self.start_offset + self._get_voffset(";
    code += NumToString(field.value.offset) + "))\n\n";
  }

  // Get the value of a table's scalar.
  void GetScalarFieldOfTable(const StructDef& struct_def, const FieldDef& field,
                             std::string* code_ptr) const {
    auto& code = *code_ptr;
    std::string getter = GenGetter(field.value.type);
    GenReceiver(struct_def, code_ptr);
    code += namer_.Method(field);
    code += "() -> " + GdTypeName(field) + ":";
    code += OffsetPrefix(field);
    getter += "voffset + self.start_offset)";
    auto is_bool = IsBool(field.value.type.base_type);
    auto is_enum = IsEnum(field.value.type);
    if (is_bool) {
      getter = "bool(" + getter + ")";
    } else if (is_enum) {
      getter += " as " + GdTypeName(field);
    }
    code += Indent + Indent + "return " + getter + "\n";
    std::string default_value;
    if (field.IsScalarOptional()) {
      default_value = "null";
    } else if (is_bool) {
      default_value = field.value.constant == "0" ? "false" : "true";
    } else {
      default_value = IsFloat(field.value.type.base_type)
                          ? float_const_gen_.GenFloatConstant(field)
                          : field.value.constant;
    }
    if (is_enum) {
      default_value += " as " + GdTypeName(field);
    }
    code += Indent + "return " + default_value + "\n\n";
  }

  // Get a struct by initializing an existing struct.
  // Specific to Struct.
  void GetStructFieldOfStruct(const StructDef& struct_def,
                              const FieldDef& field,
                              std::string* code_ptr) const {
    auto offset_str = "self.start_offset + " + NumToString(field.value.offset);
    auto& code = *code_ptr;
    GenReceiver(struct_def, code_ptr);
    code += namer_.Method(field);
    code += "(p_struct: " + TypeName(field) + " = null) -> " + TypeName(field) + ":\n";
    code += Indent + "if p_struct == null:\n";
    code += Indent + Indent + "return " + TypeName(field);
    code += ".new(self.buffer, " + offset_str + ")\n";
    code += Indent + "else:\n";
    code += Indent + Indent + "return self._sub_table(p_struct, " + offset_str + ")\n";
  }

  // Get the value of a fixed size array.
  void GetStructElemOfArray(const StructDef& struct_def, const FieldDef& field,
                        std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto vec_type = field.value.type.VectorType();

    auto offset_str = "self.start_offset + " + NumToString(field.value.offset);
    offset_str += " + p_index * " + NumToString(InlineSize(vec_type));

    GenReceiver(struct_def, code_ptr);
    code += namer_.Method(field);

    code += "(p_index: int, p_struct: " + TypeName(field) + " = null) -> " + TypeName(field) + ":\n";
    code += Indent + "if p_struct == null:\n";
    code += Indent + Indent + "return " + TypeName(field);
    code += ".new(self.buffer, " + offset_str + ")\n";
    code += Indent + "else:\n";
    code += Indent + Indent + "return self._sub_table(p_struct, " + offset_str + ")\n\n";
  }

  // Get the value of a vector's non-struct member. Uses a named return
  // argument to conveniently set the zero value for the result.
  void GetScalarElemOfArray(const StructDef& struct_def, const FieldDef& field,
                           std::string* code_ptr) const {
    auto& code = *code_ptr;
    GenReceiver(struct_def, code_ptr);
    code += namer_.Method(field);
    code += "(p_index: int) -> " + GdTypeName(field, false, true) +":\n";
    code += Indent + "return " + GenGetter(field.value.type);
    code += "self.start_offset + " + NumToString(field.value.offset);
    code += " + p_index * " + NumToString(InlineSize(field.value.type.VectorType())) + ")\n\n";
  }

  // Get a struct by initializing an existing struct.
  // Specific to Table.
  void GetStructFieldOfTable(const StructDef& struct_def, const FieldDef& field,
                             std::string* code_ptr) const {
    auto& code = *code_ptr;
    GenReceiver(struct_def, code_ptr);
    code += namer_.Method(field) + "() -> " + TypeName(field) + ":";

    code += OffsetPrefix(field);
    bool is_struct = field.value.type.struct_def->fixed;
    if (is_struct) {
      // struct so read directly
      code += Indent + Indent + "var offset = self.start_offset + voffset\n";
    } else {
      // table so build table
      code += Indent + Indent;
      code += "var offset = self._get_indirect(self.start_offset + voffset)\n";
    }

    code += Indent + Indent + "return " + TypeName(field);
    code += ".new(self.buffer, offset)\n";
    code += Indent + "return null\n\n";
  }

  // Get the value of a string.
  void GetStringField(const StructDef& struct_def, const FieldDef& field,
                      std::string* code_ptr) const {
    auto& code = *code_ptr;
    GenReceiver(struct_def, code_ptr);
    code += namer_.Method(field);

    code += "() -> String:";

    code += OffsetPrefix(field);
    code += Indent + Indent + "return " + GenGetter(field.value.type);
    code += "self.start_offset + voffset)\n";
    code += Indent + "return \"\"\n\n";
  }

  // Get the value of a union from an object.
  void GetUnionField(const StructDef& struct_def, const FieldDef& field,
                     std::string* code_ptr) const {

    // TODO: handle strings here...
    auto& code = *code_ptr;

    GenReceiver(struct_def, code_ptr);
    code += namer_.Method(field) + "(p_target: FB__Table) -> FB__Table:";
    code += OffsetPrefix(field);

    code += Indent + Indent;
    code += "var offset = self._get_indirect(self.start_offset + voffset)\n";

    code += Indent + Indent;
    code += "return self._sub_table(p_target, offset)\n";
    code += Indent + "return null\n\n";
  }


  template <typename T>
  std::string ModuleFor(const T* def) const {
    std::string filename =
        StripExtension(def->file) + parser_.opts.filename_suffix;
    if (parser_.file_being_parsed_ == def->file) {
      return "." + StripPath(filename);  // make it a "local" import
    }

    std::string module = parser_.opts.include_prefix + filename;
    std::replace(module.begin(), module.end(), '/', '.');
    return module;
  }

  // Generate the package reference when importing a struct or enum from its
  // module.
  std::string GenPackageReference(const Type& type) const {
    if (type.struct_def) return ModuleFor(type.struct_def);
    if (type.enum_def) return ModuleFor(type.enum_def);
    return "." + GenTypeGet(type);
  }

  // Get the value of a vector's struct member.
  void GetStructElemOfVector(const StructDef& struct_def,
                             const FieldDef& field, std::string* code_ptr) const {
    auto& code = *code_ptr;
    auto vectortype = field.value.type.VectorType();
    bool is_struct = vectortype.struct_def->fixed;

    GenReceiver(struct_def, code_ptr);
    code += namer_.Method(field);

    code += "(p_index: int)";
    code += " -> " + TypeName(field) + ":";
    code += OffsetPrefix(field);
    code += Indent + Indent;
    code += "var elem = self._vector_start(self.start_offset + voffset)\n";
    code += Indent + Indent;
    code += "elem += p_index * " + NumToString(InlineSize(vectortype)) + "\n";
    if (!is_struct) {
      // table, so it's a pointer
      code += Indent + Indent + "elem = self._get_indirect(elem)\n";
    }
    code += Indent + Indent;
    code += "return " + TypeName(field) + ".new(self.buffer, elem)\n";
    code += Indent + "return null\n\n";
  }

  // Get the value of a vector's non-struct member. Uses a named return
  // argument to conveniently set the zero value for the result.
  void GetScalarElemOfVector(const StructDef& struct_def,
                             const FieldDef& field,
                             std::string* code_ptr) const {
    auto& code = *code_ptr;
    auto vectortype = field.value.type.VectorType();

    GenReceiver(struct_def, code_ptr);
    code += namer_.Method(field);
    code += "(p_index: int)";
    code += " -> " + GdTypeName(field) + ":";
    code += OffsetPrefix(field);
    code += Indent + Indent;
    code += "var elem = self._vector_start(self.start_offset + voffset)\n";
    code += Indent + Indent;
    code += "elem += p_index * " + NumToString(InlineSize(vectortype)) + "\n";
    code += Indent + Indent;
    code += "return " + GenGetter(field.value.type) + "elem)\n";
    if (IsString(vectortype)) {
      code += Indent + "return \"\"\n";
    } else {
      // TODO: return null here??
      code += Indent + "return 0\n";
    }
    code += "\n";
  }

  // Returns a nested flatbuffer as itself.
  void GetVectorAsNestedFlatbuffer(const StructDef& struct_def,
                                   const FieldDef& field, std::string* code_ptr) const {
    auto nested = field.attributes.Lookup("nested_flatbuffer");
    if (!nested) {
      return;
    }  // There is no nested flatbuffer.

    auto& code = *code_ptr;
    (void)struct_def;
    code += "# TODO: implement / support nested flatbuffers";
  }

  // Begin the creator function signature.
  void BeginBuilderArgs(const StructDef& struct_def,
                        std::string* code_ptr) const {
    auto& code = *code_ptr;

    code += "\n";
    code += "static func create_" + namer_.Function(struct_def);
    code += "(p_builder: FB__Builder";
  }

  // Recursively generate arguments for a constructor, to deal with nested
  // structs.
  void StructBuilderArgs(const StructDef& struct_def,
                         const std::string nameprefix,
                         const std::string fieldname_suffix,
                         std::string* code_ptr, bool parent_struct_array = false) const {
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
        code += ": " + GdTypeName(field, parent_struct_array);
      }
    }
  }

  void TableBuilderArgs(const StructDef& struct_def,
                        const std::string nameprefix,
                        const std::string fieldname_suffix,
                        std::string* code_ptr, bool parent_struct_array = false) const {
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
        TableBuilderArgs(*field.value.type.struct_def, subprefix,
                          fieldname_suffix, code_ptr, is_array);
      } else {
        auto& code = *code_ptr;
        code += std::string(", ") + nameprefix;
        code += namer_.Field(field);

        if (!IsScalar(field.value.type.base_type)) {
          code += "_offset";
        }
        code += ": " + GdTypeName(field, parent_struct_array);
      }
    }
  }

  void TableBuilderBody(const StructDef& struct_def, const char* nameprefix,
                        std::string* code_ptr) const {
    auto& code = *code_ptr;
    (void)struct_def;
    (void)nameprefix;

    code += Indent + "## TODO flatbuffers: Implement table creator helpers\n";
  }

  // End the creator function signature.
  void EndBuilderArgs(std::string* code_ptr) const {
    auto& code = *code_ptr;
    code += ") -> int:\n";
  }

  // Recursively generate struct construction statements and instert manual
  // padding.

  void StructBuilderBody(const StructDef& struct_def, const char* nameprefix,
                         std::string* code_ptr, size_t index = 0,
                         bool in_array = false) const {
    auto& code = *code_ptr;
    std::string indent(index * 2, ' ');
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
        StructBuilderBody(*field_type.struct_def,
                          (nameprefix + (namer_.Field(field) + "_")).c_str(),
                          code_ptr, index, in_array);
      } else {
        const auto index_var = "_idx" + NumToString(index);
        if (IsArray(field_type)) {
          code += indent + "  for " + index_var + " in range(";
          code += NumToString(field_type.fixed_length);
          code += " , 0, -1):\n";
          in_array = true;
        }
        if (IsStruct(type)) {
          StructBuilderBody(*field_type.struct_def,
                            (nameprefix + (namer_.Field(field) + "_")).c_str(),
                            code_ptr, index + 1, in_array);
        } else {
          code += IsArray(field_type) ? "    " : "";
          code += indent + "  p_builder.write_" + GenMethod(field) + "(";
          code += nameprefix + namer_.Variable(field);
          size_t array_cnt = index + (IsArray(field_type) ? 1 : 0);
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
    code += "\n" + Indent + "return p_builder.offset()\n";
  }

  void GetStartOfTable(const StructDef& struct_def,
                       std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto struct_type = namer_.Type(struct_def);
    // Generate method with struct name.

    code += "\n## Builder static functions\n\n";

    code += "static func start(builder: FB__Builder) -> void:\n";
    code += Indent + "builder.start_table()\n\n";
  }

  void GenBufferFinish(const StructDef& struct_def,
                       std::string* code_ptr,
                       bool size_prefix) const {
    if (parser_.root_struct_def_ != &struct_def) {
      // only generate buffer finish for root table
      return;
    }

    auto& code = *code_ptr;
    const auto struct_type = namer_.Type(struct_def);
    std::string method_name = size_prefix ? "finish_buffer_size_prefixed" : "finish_buffer";

    code += "static func " + method_name + "(builder: FB__Builder, p_offset: int) -> void:\n";
    code += Indent + "builder.finish_buffer(p_offset";
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

  void GetEndOfTable(const StructDef& struct_def,
                     std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto struct_type = namer_.Type(struct_def);
    // Generate method with struct name.

    code += "static func finish(builder: FB__Builder) -> void:\n";
    code += Indent + "builder.finish_table()\n\n";
  }

  // Set the value of a table's field.
  void BuildFieldOfTable(const StructDef& struct_def, const FieldDef& field,
                         const size_t field_index, std::string* code_ptr) const {
    (void)struct_def;
    auto& code = *code_ptr;

    std::string default_value;
    bool is_bool = IsBool(field.value.type.base_type);
    bool is_pointer = !IsScalar(field.value.type.base_type);
    if (is_pointer) {
      default_value = "0";
    } else if (field.IsScalarOptional()) {
      default_value = "null";
    } else if (is_bool) {
      default_value = field.value.constant == "0" ? "false" : "true";
    } else {
      default_value = IsFloat(field.value.type.base_type)
        ? float_const_gen_.GenFloatConstant(field) : field.value.constant;
    }

    const std::string field_var = "p_" + namer_.Variable(field) + (is_pointer ? "_offset" : "");
    const std::string field_method = namer_.Method(field);
    const std::string field_ty = GenFieldTy(field);

    code += "static func add_" + field_method;
    code += "(p_builder: FB__Builder, " + field_var + ": " + GdTypeName(field) + ")";
    code += " -> void:\n";

    std::string next_block_indent = Indent;
    if (default_value != "null") {
      code += Indent + "var default_value = " + default_value + "\n\n";
      code += Indent + "if p_builder.force_defaults || (" + field_var + " != default_value):\n";
      next_block_indent += Indent;
    }

    code += next_block_indent + "p_builder.write_aligned_";
    if (is_pointer) {
      code += "offset";
    } else {
      code += namer_.Method(GenTypeGet(field.value.type));
    }
    code += "(" + field_var + ")\n";
    code += next_block_indent + "p_builder.curr_table.set_slot(";
    code += NumToString(field_index) + ", p_builder.offset())\n\n";
  }

  // Set the value of one of the members of a table's vector.
  void BuildVectorOfTable(const StructDef& struct_def, const FieldDef& field,
                          std::string* code_ptr) const {
    auto& code = *code_ptr;
    const std::string struct_type = namer_.Type(struct_def);
    const std::string field_method = namer_.Method(field);

    auto vector_type = field.value.type.VectorType();
    auto alignment = InlineAlignment(vector_type);
    auto elem_size = InlineSize(vector_type);

    // Generate method with struct name.
    const auto name = "start_" + field_method;
    code += "static func " + name;
    code += "_vector(p_builder: FB__Builder, p_num_elems: int):\n";

    code += Indent + "return p_builder.start_vector(";
    code += NumToString(elem_size);
    code += ", p_num_elems, " + NumToString(alignment);
    code += ")\n\n";
  }

  void BuildVectorCreationHelper(const StructDef& struct_def,
                                 const FieldDef& field, std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto vector_type = field.value.type.VectorType();
    const bool is_struct_vector = IsStruct(vector_type);
    const bool is_table_vector = IsTable(vector_type);
    if (is_struct_vector) {
      // don't generate helpers for struct vectors
      // TODO: revisit this...
      code += "## TODO flatbuffers: add creators for struct vectors\n";
      return;
    }
    const std::string struct_type = namer_.Type(struct_def);
    const std::string field_method = namer_.Method(field);

    auto write_method = is_table_vector ? "offset" : namer_.Method(GenTypeBasic(vector_type));

    code += "static func create_" + field_method + "_vector";
    code += "(p_builder: FB__Builder, data: Array[" + GdTypeName(field) + "]):\n";

    auto alignment = InlineAlignment(vector_type);
    auto elem_size = InlineSize(vector_type);
    code += Indent + "p_builder.start_vector(" + NumToString(elem_size);
    code += ", data.size(), " + NumToString(alignment) + ")\n";
    code += Indent + "for i in range(data.size(), 0, -1):\n";
    code += Indent + Indent + "var item = data[i - 1]\n";
    code += Indent + Indent + "p_builder.write_" + write_method + "(item)\n";
    code += Indent + "return p_builder.finish_vector()\n\n";
  }

  // Set the value of one of the members of a table's vector and fills in the
  // elements from a bytearray. This is for simplifying the use of nested
  // flatbuffers.
  void BuildVectorOfTableFromBytes(const StructDef& struct_def,
                                   const FieldDef& field,
                                   std::string* code_ptr) const {
    auto nested = field.attributes.Lookup("nested_flatbuffer");
    if (!nested) {
      return;
    }  // There is no nested flatbuffer.

    auto& code = *code_ptr;
    (void)struct_def;
    code += "## TODO flatbuffers: implement / support nested flatbuffers\n";
  }

  // Generate the receiver for function signatures.
  void GenReceiver(const StructDef& struct_def, std::string* code_ptr) const {
    auto& code = *code_ptr;
    code += "# " + namer_.Type(struct_def) + "\n";
    code += "func ";
  }

  // Generate a struct field, conditioned on its child type(s).
  void GenStructAccessor(const StructDef& struct_def, const FieldDef& field,
                         std::string* code_ptr) const {
    GenComment(field.doc_comment, code_ptr, &def_comment, Indent.c_str());
    if (IsScalar(field.value.type.base_type)) {
      if (struct_def.fixed) {
        GetScalarFieldOfStruct(struct_def, field, code_ptr);
      } else {
        GetScalarFieldOfTable(struct_def, field, code_ptr);
      }
    } else {
      switch (field.value.type.base_type) {
        case BASE_TYPE_STRUCT:
          if (struct_def.fixed) {
            GetStructFieldOfStruct(struct_def, field, code_ptr);
          } else {
            GetStructFieldOfTable(struct_def, field, code_ptr);
          }
          break;
        case BASE_TYPE_STRING:
          GetStringField(struct_def, field, code_ptr);
          break;
        case BASE_TYPE_VECTOR: {
          auto vectortype = field.value.type.VectorType();
          if (vectortype.base_type == BASE_TYPE_STRUCT) {
            GetStructElemOfVector(struct_def, field, code_ptr);
          } else {
            GetScalarElemOfVector(struct_def, field, code_ptr);
            GetVectorAsNestedFlatbuffer(struct_def, field, code_ptr);
          }
          break;
        }
        case BASE_TYPE_ARRAY: {
          auto vectortype = field.value.type.VectorType();
          if (vectortype.base_type == BASE_TYPE_STRUCT) {
            GetStructElemOfArray(struct_def, field, code_ptr);
          } else {
            GetScalarElemOfArray(struct_def, field, code_ptr);
            GetVectorAsNestedFlatbuffer(struct_def, field, code_ptr);
          }
          break;
        }
        case BASE_TYPE_UNION:
          GetUnionField(struct_def, field, code_ptr);
          break;
        default:
          FLATBUFFERS_ASSERT(0);
      }
    }
    if (IsVector(field.value.type) || IsArray(field.value.type)) {
      GetVectorLen(struct_def, field, code_ptr);
      GetVectorIsNone(struct_def, field, code_ptr);
    }
  }

  // Generate struct sizeof.
  void GenStructSizeOf(const StructDef& struct_def,
                       std::string* code_ptr) const {
    auto& code = *code_ptr;
    code += "static func size_of():\n";
    code +=
        Indent + "return " + NumToString(struct_def.bytesize) + "\n";
    code += "\n";
  }

  // Generate table constructors, conditioned on its members' types.
  void GenTableBuilders(const StructDef& struct_def, std::string* code_ptr) const {
    GetStartOfTable(struct_def, code_ptr);
    auto& code = *code_ptr;

    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto& field = **it;
      if (field.deprecated) continue;

      auto offset = it - struct_def.fields.vec.begin();
      code += "## " + namer_.Variable(field) + " functions\n";
      BuildFieldOfTable(struct_def, field, offset, code_ptr);
      if (IsVector(field.value.type)) {
        BuildVectorOfTable(struct_def, field, code_ptr);
        BuildVectorCreationHelper(struct_def, field, code_ptr);
        BuildVectorOfTableFromBytes(struct_def, field, code_ptr);
      }
    }
    GetEndOfTable(struct_def, code_ptr);
    GenBufferFinish(struct_def, code_ptr, true);
    GenBufferFinish(struct_def, code_ptr, false);

    BeginBuilderArgs(struct_def, code_ptr);
    TableBuilderArgs(struct_def, "p_", "_", code_ptr);
    EndBuilderArgs(code_ptr);
    TableBuilderBody(struct_def, "p_", code_ptr);

    EndBuilderBody(code_ptr);
  }

  // Generates struct or table methods.
  void GenStruct(const StructDef& struct_def, std::string* code_ptr) const {
    if (struct_def.generated) return;

    GenComment(struct_def.doc_comment, code_ptr, &def_comment);
    BeginClass(struct_def, code_ptr);
    if (!struct_def.fixed) {
      // Generate a special accessor for the table that has been declared as
      // the root type.
      GenTableStaticMethods(struct_def, code_ptr);
    } else {
      // Generates the SizeOf method for all structs.
      GenStructSizeOf(struct_def, code_ptr);
    }
    // Generates the ctor
    GenConstructor(struct_def, code_ptr);
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto& field = **it;
      if (field.deprecated) continue;

      GenStructAccessor(struct_def, field, code_ptr);
    }

    if (struct_def.fixed) {
      // creates a struct constructor function
      GenStructBuilder(struct_def, code_ptr);
    } else {
      // Creates a set of functions that allow table construction.
      GenTableBuilders(struct_def, code_ptr);
    }
  }

  void GenReceiverForObjectAPI(const StructDef& struct_def,
                               std::string* code_ptr) const {
    auto& code = *code_ptr;
    code += GenIndents(1) + "# " + namer_.ObjectType(struct_def);
    code += GenIndents(1) + "func ";
  }

  void BeginClassForObjectAPI(const StructDef& struct_def,
                              std::string* code_ptr) const {
    auto& code = *code_ptr;
    code += "\n";
    code += "class " + namer_.ObjectType(struct_def) + "(object):";
    code += "\n";
  }

  std::string GetBaseGDTypeForScalarAndString(const BaseType& base_type) const {
    if (IsBool(base_type)) {
      return "bool";
    } else if (IsFloat(base_type)) {
      return "float";
    } else if (IsInteger(base_type)) {
      return "int";
    } else if (base_type == BASE_TYPE_STRING) {
      return "String";
    } else {
      FLATBUFFERS_ASSERT(false && "base_type is not a scalar or string type.");
      return "";
    }
  }

  std::string GetDefaultValue(const FieldDef& field) const {
    BaseType base_type = field.value.type.base_type;
    if (field.IsScalarOptional()) {
      return "null";
    } else if (IsBool(base_type)) {
      return field.value.constant == "0" ? "false" : "true";
    } else if (IsFloat(base_type)) {
      return float_const_gen_.GenFloatConstant(field);
    } else if (IsInteger(base_type)) {
      return field.value.constant;
    } else {
      // For string, struct, and table.
      return "null";
    }
  }

  void GenUnionInit(const FieldDef& field, std::string* field_types_ptr,
                    std::set<std::string>* import_list,
                    std::set<std::string>* import_typing_list) const {
    // Gets all possible types in the union.
    import_typing_list->insert("Union");
    auto& field_types = *field_types_ptr;
    field_types = "Union[";

    std::string separator_string = ", ";
    auto enum_def = field.value.type.enum_def;
    for (auto it = enum_def->Vals().begin(); it != enum_def->Vals().end();
         ++it) {
      auto& ev = **it;
      // Union only supports string and table.
      std::string field_type;
      switch (ev.union_type.base_type) {
        case BASE_TYPE_STRUCT:
          field_type = namer_.ObjectType(*ev.union_type.struct_def);
          if (parser_.opts.include_dependence_headers) {
            auto package_reference = GenPackageReference(ev.union_type);
            field_type = package_reference + "." + field_type;
            import_list->insert("import " + package_reference);
          }
          field_type = "'" + field_type + "'";
          break;
        case BASE_TYPE_STRING:
          field_type += "str";
          break;
        case BASE_TYPE_NONE:
          field_type += "None";
          break;
        default:
          break;
      }
      field_types += field_type + separator_string;
    }

    // Removes the last separator_string.
    field_types.erase(field_types.length() - separator_string.size());
    field_types += "]";

    // Gets the import lists for the union.
    if (parser_.opts.include_dependence_headers) {
      const auto package_reference = GenPackageReference(field.value.type);
      import_list->insert("import " + package_reference);
    }
  }

  void GenStructInit(const FieldDef& field, std::string* out_ptr,
                     std::set<std::string>* import_list,
                     std::set<std::string>* import_typing_list) const {
    import_typing_list->insert("Optional");
    auto& output = *out_ptr;
    const Type& type = field.value.type;
    const std::string object_type = namer_.ObjectType(*type.struct_def);
    if (parser_.opts.include_dependence_headers) {
      auto package_reference = GenPackageReference(type);
      output = package_reference + "." + object_type + "]";
      import_list->insert("import " + package_reference);
    } else {
      output = object_type + "]";
    }
    output = "Optional[" + output;
  }

  void GenVectorInit(const FieldDef& field, std::string* field_type_ptr,
                     std::set<std::string>* import_list,
                     std::set<std::string>* import_typing_list) const {
    import_typing_list->insert("List");
    auto& field_type = *field_type_ptr;
    const Type& vector_type = field.value.type.VectorType();
    const BaseType base_type = vector_type.base_type;
    if (base_type == BASE_TYPE_STRUCT) {
      const std::string object_type =
          namer_.ObjectType(*vector_type.struct_def);
      field_type = object_type + "]";
      if (parser_.opts.include_dependence_headers) {
        auto package_reference = GenPackageReference(vector_type);
        field_type = package_reference + "." + object_type + "]";
        import_list->insert("import " + package_reference);
      }
      field_type = "Optional[List[" + field_type + "]";
    } else {
      field_type = "Optional[List[" +
                   GetBaseGDTypeForScalarAndString(base_type) + "]]";
    }
  }

  void GenInitialize(const StructDef& struct_def, std::string* code_ptr,
                     std::set<std::string>* import_list) const {
    std::string signature_params;
    std::string init_body;
    std::set<std::string> import_typing_list;

    signature_params += GenIndents(2) + "self,";

    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto& field = **it;
      if (field.deprecated) continue;

      // Determines field type, default value, and typing imports.
      auto base_type = field.value.type.base_type;
      std::string field_type;
      switch (base_type) {
        case BASE_TYPE_UNION: {
          GenUnionInit(field, &field_type, import_list, &import_typing_list);
          break;
        }
        case BASE_TYPE_STRUCT: {
          GenStructInit(field, &field_type, import_list, &import_typing_list);
          break;
        }
        case BASE_TYPE_VECTOR:
        case BASE_TYPE_ARRAY: {
          GenVectorInit(field, &field_type, import_list, &import_typing_list);
          break;
        }
        default:
          // Scalar or sting fields.
          field_type = GetBaseGDTypeForScalarAndString(base_type);
          if (field.IsScalarOptional()) {
            import_typing_list.insert("Optional");
            field_type = "Optional[" + field_type + "]";
          }
          break;
      }

      const auto default_value = GetDefaultValue(field);
      // Writes the init statement.
      const auto field_field = namer_.Field(field);

      // Build signature with keyword arguments, type hints, and default values.
      signature_params +=
          GenIndents(2) + field_field + " = " + default_value + ",";

      // Build the body of the __init__ method.
      init_body += GenIndents(2) + "self." + field_field + " = " + field_field +
                   "  # type: " + field_type;
    }

    // Writes __init__ method.
    auto& code_base = *code_ptr;
    GenReceiverForObjectAPI(struct_def, code_ptr);
    code_base += "__init__(" + signature_params + GenIndents(1) + "):";
    if (init_body.empty()) {
      code_base += GenIndents(2) + "pass";
    } else {
      code_base += init_body;
    }
    code_base += "\n";

    // Merges the typing imports into import_list.
    if (!import_typing_list.empty()) {
      // Adds the try statement.
      std::string typing_imports = "try:";
      typing_imports += GenIndents(1) + "from typing import ";
      std::string separator_string = ", ";
      for (auto it = import_typing_list.begin(); it != import_typing_list.end();
           ++it) {
        const std::string& im = *it;
        typing_imports += im + separator_string;
      }
      // Removes the last separator_string.
      typing_imports.erase(typing_imports.length() - separator_string.size());

      // Adds the except statement.
      typing_imports += "\n";
      typing_imports += "except:";
      typing_imports += GenIndents(1) + "pass";
      import_list->insert(typing_imports);
    }

    // Removes the import of the struct itself, if applied.
    auto struct_import = "import " + namer_.NamespacedType(struct_def);
    import_list->erase(struct_import);
  }

  void InitializeFromBuf(const StructDef& struct_def,
                         std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto struct_var = namer_.Variable(struct_def);
    const auto struct_type = namer_.Type(struct_def);

    code += GenIndents(1) + "static func init_from_buffer(buf, pos):";
    code += GenIndents(2) + struct_var + " = " + struct_type + "()";
    code += GenIndents(2) + struct_var + "._init(buf, pos)";
    code += GenIndents(2) + "return init_from_obj(" + struct_var + ")";
    code += "\n";
  }

  void InitializeFromPackedBuf(const StructDef& struct_def,
                               std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto struct_var = namer_.Variable(struct_def);
    const auto struct_type = namer_.Type(struct_def);

    code += GenIndents(1) + "static func init_from_packed(buf, pos=0):";
    code += GenIndents(2) +
            "n = flatbuffers.encode.Get(flatbuffers.packer.uoffset, buf, pos)";
    code += GenIndents(2) + "return init_from_buffer(buf, pos+n)";
    code += "\n";
  }

  void InitializeFromObjForObject(const StructDef& struct_def,
                                  std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto struct_var = namer_.Variable(struct_def);
    const auto struct_object = namer_.ObjectType(struct_def);

    code += GenIndents(1) + "static func init_from_obj(cls, " + struct_var + "):";
    code += GenIndents(2) + "x = " + struct_object + "()";
    code += GenIndents(2) + "x._UnPack(" + struct_var + ")";
    code += GenIndents(2) + "return x";
    code += "\n";
  }

  // TODO: reintroduce
  // void GenCompareOperator(const StructDef& struct_def,
  //                         std::string* code_ptr) const {
  //   auto& code = *code_ptr;
  //   code += GenIndents(1) + "def __eq__(self, other):";
  //   code += GenIndents(2) + "return type(self) == type(other)";
  //   for (auto it = struct_def.fields.vec.begin();
  //        it != struct_def.fields.vec.end(); ++it) {
  //     auto& field = **it;
  //     if (field.deprecated) continue;

  //     // Writes the comparison statement for this field.
  //     const auto field_name = namer_.Field(field);
  //     code += " and \\" + GenIndents(3) + "self." + field_name +
  //             " == " + "other." + field_name;
  //   }
  //   code += "\n";
  // }

  void GenUnPackForStruct(const StructDef& struct_def, const FieldDef& field,
                          std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto struct_var = namer_.Variable(struct_def);
    const auto field_field = namer_.Field(field);
    const auto field_method = namer_.Method(field);
    auto field_type = TypeName(field);

    if (parser_.opts.include_dependence_headers) {
      auto package_reference = GenPackageReference(field.value.type);
      field_type = package_reference + "." + TypeName(field);
    }

    code += GenIndents(2) + "if " + struct_var + "." + field_method + "(";
    // if field is a struct, we need to create an instance for it first.
    if (struct_def.fixed && field.value.type.base_type == BASE_TYPE_STRUCT) {
      code += field_type + "()";
    }
    code += ") is not None:";
    code += GenIndents(3) + "self." + field_field + " = " +
            namer_.ObjectType(field_type) + +".InitFromObj(" + struct_var +
            "." + field_method + "(";
    // A struct's accessor requires a struct buf instance.
    if (struct_def.fixed && field.value.type.base_type == BASE_TYPE_STRUCT) {
      code += field_type + "()";
    }
    code += "))";
  }

  void GenUnPackForUnion(const StructDef& struct_def, const FieldDef& field,
                         std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto field_field = namer_.Field(field);
    const auto field_method = namer_.Method(field);
    const auto struct_var = namer_.Variable(struct_def);
    const EnumDef& enum_def = *field.value.type.enum_def;
    auto union_fn = namer_.Function(enum_def);

    if (parser_.opts.include_dependence_headers) {
      union_fn = namer_.NamespacedType(enum_def) + "." + union_fn;
    }
    code += GenIndents(2) + "self." + field_field + " = " + union_fn +
            "Creator(" + "self." + field_field + "Type, " + struct_var + "." +
            field_method + "())";
  }

  void GenUnPackForStructVector(const StructDef& struct_def,
                                const FieldDef& field,
                                std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto field_field = namer_.Field(field);
    const auto field_method = namer_.Method(field);
    const auto struct_var = namer_.Variable(struct_def);

    code += GenIndents(2) + "if not " + struct_var + "." + field_method +
            "IsNone():";
    code += GenIndents(3) + "self." + field_field + " = []";
    code += GenIndents(3) + "for i in range(" + struct_var + "." +
            field_method + "Length()):";

    auto field_type = TypeName(field);
    auto one_instance = field_type + "_";
    one_instance[0] = CharToLower(one_instance[0]);
    if (parser_.opts.include_dependence_headers) {
      auto package_reference = GenPackageReference(field.value.type);
      field_type = package_reference + "." + TypeName(field);
    }
    code += GenIndents(4) + "if " + struct_var + "." + field_method +
            "(i) is None:";
    code += GenIndents(5) + "self." + field_field + ".append(None)";
    code += GenIndents(4) + "else:";
    code += GenIndents(5) + one_instance + " = " +
            namer_.ObjectType(field_type) + ".InitFromObj(" + struct_var + "." +
            field_method + "(i))";
    code +=
        GenIndents(5) + "self." + field_field + ".append(" + one_instance + ")";
  }

  void GenUnpackForTableVector(const StructDef& struct_def,
                               const FieldDef& field,
                               std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto field_field = namer_.Field(field);
    const auto field_method = namer_.Method(field);
    const auto struct_var = namer_.Variable(struct_def);

    code += GenIndents(2) + "if not " + struct_var + "." + field_method +
            "IsNone():";
    code += GenIndents(3) + "self." + field_field + " = []";
    code += GenIndents(3) + "for i in range(" + struct_var + "." +
            field_method + "Length()):";

    auto field_type = TypeName(field);
    auto one_instance = field_type + "_";
    one_instance[0] = CharToLower(one_instance[0]);
    if (parser_.opts.include_dependence_headers) {
      auto package_reference = GenPackageReference(field.value.type);
      field_type = package_reference + "." + TypeName(field);
    }
    code += GenIndents(4) + "if " + struct_var + "." + field_method +
            "(i) is None:";
    code += GenIndents(5) + "self." + field_field + ".append(None)";
    code += GenIndents(4) + "else:";
    code += GenIndents(5) + one_instance + " = " +
            namer_.ObjectType(field_type) + ".InitFromObj(" + struct_var + "." +
            field_method + "(i))";
    code +=
        GenIndents(5) + "self." + field_field + ".append(" + one_instance + ")";
  }

  void GenUnpackforScalarVectorHelper(const StructDef& struct_def,
                                      const FieldDef& field,
                                      std::string* code_ptr,
                                      int indents) const {
    auto& code = *code_ptr;
    const auto field_field = namer_.Field(field);
    const auto field_method = namer_.Method(field);
    const auto struct_var = namer_.Variable(struct_def);

    code += GenIndents(indents) + "self." + field_field + " = []";
    code += GenIndents(indents) + "for i in range(" + struct_var + "." +
            field_method + "Length()):";
    code += GenIndents(indents + 1) + "self." + field_field + ".append(" +
            struct_var + "." + field_method + "(i))";
  }

  void GenUnPackForScalarVector(const StructDef& struct_def,
                                const FieldDef& field,
                                std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto field_field = namer_.Field(field);
    const auto field_method = namer_.Method(field);
    const auto struct_var = namer_.Variable(struct_def);

    code += GenIndents(2) + "if not " + struct_var + "." + field_method +
            "IsNone():";

    // String does not have the AsNumpy method.
    if (!(IsScalar(field.value.type.VectorType().base_type))) {
      GenUnpackforScalarVectorHelper(struct_def, field, code_ptr, 3);
      return;
    }

    GenUnpackforScalarVectorHelper(struct_def, field, code_ptr, 3);
  }

  void GenUnPackForString(const StructDef& struct_def, const FieldDef& field,
                          std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto field_field = namer_.Field(field);
    const auto field_method = namer_.Method(field);
    const auto struct_var = namer_.Variable(struct_def);

    code += GenIndents(2) + "self." + field_field + " = " + struct_var + "." +
            field_method + "()";
    code += GenIndents(2) + "if self." + field_field + " is not None:";
    code += GenIndents(3) + "self." + field_field + " = self." + field_field +
            ".decode('utf-8')";
  }

  void GenUnPackForScalar(const StructDef& struct_def, const FieldDef& field,
                          std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto field_field = namer_.Field(field);
    const auto field_method = namer_.Method(field);
    const auto struct_var = namer_.Variable(struct_def);

    code += GenIndents(2) + "self." + field_field + " = " + struct_var + "." +
            field_method + "()";
  }

  // Generates the UnPack method for the object class.
  void GenUnPack(const StructDef& struct_def, std::string* code_ptr) const {
    std::string code;
    // Items that needs to be imported. No duplicate modules will be imported.
    std::set<std::string> import_list;

    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto& field = **it;
      if (field.deprecated) continue;

      switch (field.value.type.base_type) {
        case BASE_TYPE_STRUCT: {
          GenUnPackForStruct(struct_def, field, &code);
          break;
        }
        case BASE_TYPE_UNION: {
          GenUnPackForUnion(struct_def, field, &code);
          break;
        }
        case BASE_TYPE_ARRAY:
        case BASE_TYPE_VECTOR: {
          auto vectortype = field.value.type.VectorType();
          if (vectortype.base_type == BASE_TYPE_STRUCT) {
            GenUnPackForStructVector(struct_def, field, &code);
          } else {
            GenUnPackForScalarVector(struct_def, field, &code);
          }
          break;
        }
        case BASE_TYPE_STRING: {
          if (parser_.opts.python_decode_obj_api_strings) {
            GenUnPackForString(struct_def, field, &code);
          } else {
            GenUnPackForScalar(struct_def, field, &code);
          }
          break;
        }
        default:
          GenUnPackForScalar(struct_def, field, &code);
      }
    }

    // Writes import statements and code into the generated file.
    auto& code_base = *code_ptr;
    const auto struct_var = namer_.Variable(struct_def);

    GenReceiverForObjectAPI(struct_def, code_ptr);
    code_base += "_UnPack(self, " + struct_var + "):";
    code_base += GenIndents(2) + "if " + struct_var + " is None:";
    code_base += GenIndents(3) + "return";

    // Write the import statements.
    for (std::set<std::string>::iterator it = import_list.begin();
         it != import_list.end(); ++it) {
      code_base += GenIndents(2) + *it;
    }

    // Write the code.
    code_base += code;
    code_base += "\n";
  }

  void GenPackForStruct(const StructDef& struct_def,
                        std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto struct_fn = namer_.Function(struct_def);

    GenReceiverForObjectAPI(struct_def, code_ptr);
    code += "Pack(self, builder):";
    code += GenIndents(2) + "return Create" + struct_fn + "(builder";

    StructBuilderArgs(struct_def,
                      /* nameprefix = */ "selfGENPACK.",
                      /* fieldname_suffix = */ ".", code_ptr);
    code += ")\n";
  }

  void GenPackForStructVectorField(const StructDef& struct_def,
                                   const FieldDef& field,
                                   std::string* code_prefix_ptr,
                                   std::string* code_ptr) const {
    auto& code_prefix = *code_prefix_ptr;
    auto& code = *code_ptr;
    const auto field_field = namer_.Field(field);
    const auto struct_type = namer_.Type(struct_def);
    const auto field_method = namer_.Method(field);

    // Creates the field.
    code_prefix += GenIndents(2) + "if self." + field_field + " is not None:";
    if (field.value.type.struct_def->fixed) {
      code_prefix += GenIndents(3) + struct_type + "Start" + field_method +
                     "Vector(builder, len(self." + field_field + "))";
      code_prefix += GenIndents(3) + "for i in reversed(range(len(self." +
                     field_field + "))):";
      code_prefix +=
          GenIndents(4) + "self." + field_field + "[i].Pack(builder)";
      code_prefix += GenIndents(3) + field_field + " = builder.EndVector()";
    } else {
      // If the vector is a struct vector, we need to first build accessor for
      // each struct element.
      code_prefix += GenIndents(3) + field_field + "list = []";
      code_prefix += GenIndents(3);
      code_prefix += "for i in range(len(self." + field_field + ")):";
      code_prefix += GenIndents(4) + field_field + "list.append(self." +
                     field_field + "[i].Pack(builder))";

      code_prefix += GenIndents(3) + struct_type + "Start" + field_method +
                     "Vector(builder, len(self." + field_field + "))";
      code_prefix += GenIndents(3) + "for i in reversed(range(len(self." +
                     field_field + "))):";
      code_prefix += GenIndents(4) + "builder.PrependUOffsetTRelative" + "(" +
                     field_field + "list[i])";
      code_prefix += GenIndents(3) + field_field + " = builder.EndVector()";
    }

    // Adds the field into the struct.
    code += GenIndents(2) + "if self." + field_field + " is not None:";
    code += GenIndents(3) + struct_type + "Add" + field_method + "(builder, " +
            field_field + ")";
  }

  void GenPackForScalarVectorFieldHelper(const StructDef& struct_def,
                                         const FieldDef& field,
                                         std::string* code_ptr,
                                         int indents) const {
    auto& code = *code_ptr;
    const auto field_field = namer_.Field(field);
    const auto field_method = namer_.Method(field);
    const auto struct_type = namer_.Type(struct_def);
    const auto vectortype = field.value.type.VectorType();

    code += GenIndents(indents) + struct_type + "Start" + field_method +
            "Vector(builder, len(self." + field_field + "))";
    code += GenIndents(indents) + "for i in reversed(range(len(self." +
            field_field + "))):";
    code += GenIndents(indents + 1) + "builder.Prepend";

    std::string type_name;
    switch (vectortype.base_type) {
      case BASE_TYPE_BOOL:
        type_name = "Bool";
        break;
      case BASE_TYPE_CHAR:
        type_name = "Byte";
        break;
      case BASE_TYPE_UCHAR:
        type_name = "Uint8";
        break;
      case BASE_TYPE_SHORT:
        type_name = "Int16";
        break;
      case BASE_TYPE_USHORT:
        type_name = "Uint16";
        break;
      case BASE_TYPE_INT:
        type_name = "Int32";
        break;
      case BASE_TYPE_UINT:
        type_name = "Uint32";
        break;
      case BASE_TYPE_LONG:
        type_name = "Int64";
        break;
      case BASE_TYPE_ULONG:
        type_name = "Uint64";
        break;
      case BASE_TYPE_FLOAT:
        type_name = "Float32";
        break;
      case BASE_TYPE_DOUBLE:
        type_name = "Float64";
        break;
      case BASE_TYPE_STRING:
        type_name = "UOffsetTRelative";
        break;
      default:
        type_name = "VOffsetT";
        break;
    }
    code += type_name;
  }

  void GenPackForScalarVectorField(const StructDef& struct_def,
                                   const FieldDef& field,
                                   std::string* code_prefix_ptr,
                                   std::string* code_ptr) const {
    auto& code = *code_ptr;
    auto& code_prefix = *code_prefix_ptr;
    const auto field_field = namer_.Field(field);
    const auto field_method = namer_.Method(field);
    const auto struct_type = namer_.Type(struct_def);

    // Adds the field into the struct.
    code += GenIndents(2) + "if self." + field_field + " is not None:";
    code += GenIndents(3) + struct_type + "Add" + field_method + "(builder, " +
            field_field + ")";

    // Creates the field.
    code_prefix += GenIndents(2) + "if self." + field_field + " is not None:";
    // If the vector is a string vector, we need to first build accessor for
    // each string element. And this generated code, needs to be
    // placed ahead of code_prefix.
    auto vectortype = field.value.type.VectorType();
    if (IsString(vectortype)) {
      code_prefix += GenIndents(3) + field_field + "list = []";
      code_prefix +=
          GenIndents(3) + "for i in range(len(self." + field_field + ")):";
      code_prefix += GenIndents(4) + field_field +
                     "list.append(builder.CreateString(self." + field_field +
                     "[i]))";
      GenPackForScalarVectorFieldHelper(struct_def, field, code_prefix_ptr, 3);
      code_prefix += "(" + field_field + "list[i])";
      code_prefix += GenIndents(3) + field_field + " = builder.EndVector()";
      return;
    }

    GenPackForScalarVectorFieldHelper(struct_def, field, code_prefix_ptr, 3);
    code_prefix += "(self." + field_field + "[i])";
    code_prefix += GenIndents(3) + field_field + " = builder.EndVector()";
  }

  void GenPackForStructField(const StructDef& struct_def, const FieldDef& field,
                             std::string* code_prefix_ptr,
                             std::string* code_ptr) const {
    auto& code_prefix = *code_prefix_ptr;
    auto& code = *code_ptr;
    const auto field_field = namer_.Field(field);
    const auto field_method = namer_.Method(field);
    const auto struct_type = namer_.Type(struct_def);

    if (field.value.type.struct_def->fixed) {
      // Pure struct fields need to be created along with their parent
      // structs.
      code += GenIndents(2) + "if self." + field_field + " is not None:";
      code += GenIndents(3) + field_field + " = self." + field_field +
              ".Pack(builder)";
    } else {
      // Tables need to be created before their parent structs are created.
      code_prefix += GenIndents(2) + "if self." + field_field + " is not None:";
      code_prefix += GenIndents(3) + field_field + " = self." + field_field +
                     ".Pack(builder)";
      code += GenIndents(2) + "if self." + field_field + " is not None:";
    }

    code += GenIndents(3) + struct_type + "Add" + field_method + "(builder, " +
            field_field + ")";
  }

  void GenPackForUnionField(const StructDef& struct_def, const FieldDef& field,
                            std::string* code_prefix_ptr,
                            std::string* code_ptr) const {
    auto& code_prefix = *code_prefix_ptr;
    auto& code = *code_ptr;
    const auto field_field = namer_.Field(field);
    const auto field_method = namer_.Method(field);
    const auto struct_type = namer_.Type(struct_def);

    // TODO(luwa): TypeT should be moved under the None check as well.
    code_prefix += GenIndents(2) + "if self." + field_field + " is not None:";
    code_prefix += GenIndents(3) + field_field + " = self." + field_field +
                   ".Pack(builder)";
    code += GenIndents(2) + "if self." + field_field + " is not None:";
    code += GenIndents(3) + struct_type + "Add" + field_method + "(builder, " +
            field_field + ")";
  }

  void GenPackForTable(const StructDef& struct_def,
                       std::string* code_ptr) const {
    auto& code_base = *code_ptr;
    std::string code, code_prefix;
    const auto struct_var = namer_.Variable(struct_def);
    const auto struct_type = namer_.Type(struct_def);

    GenReceiverForObjectAPI(struct_def, code_ptr);
    code_base += "Pack(self, builder):";
    code += GenIndents(2) + struct_type + "Start(builder)";
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      auto& field = **it;
      if (field.deprecated) continue;

      const auto field_method = namer_.Method(field);
      const auto field_field = namer_.Field(field);

      switch (field.value.type.base_type) {
        case BASE_TYPE_STRUCT: {
          GenPackForStructField(struct_def, field, &code_prefix, &code);
          break;
        }
        case BASE_TYPE_UNION: {
          GenPackForUnionField(struct_def, field, &code_prefix, &code);
          break;
        }
        case BASE_TYPE_ARRAY:
        case BASE_TYPE_VECTOR: {
          auto vectortype = field.value.type.VectorType();
          if (vectortype.base_type == BASE_TYPE_STRUCT) {
            GenPackForStructVectorField(struct_def, field, &code_prefix, &code);
          } else {
            GenPackForScalarVectorField(struct_def, field, &code_prefix, &code);
          }
          break;
        }
        case BASE_TYPE_STRING: {
          code_prefix +=
              GenIndents(2) + "if self." + field_field + " is not None:";
          code_prefix += GenIndents(3) + field_field +
                         " = builder.CreateString(self." + field_field + ")";
          code += GenIndents(2) + "if self." + field_field + " is not None:";
          code += GenIndents(3) + struct_type + "Add" + field_method +
                  "(builder, " + field_field + ")";
          break;
        }
        default:
          // Generates code for scalar values. If the value equals to the
          // default value, builder will automatically ignore it. So we don't
          // need to check the value ahead.
          code += GenIndents(2) + struct_type + "Add" + field_method +
                  "(builder, self." + field_field + ")";
          break;
      }
    }

    code += GenIndents(2) + struct_var + " = " + struct_type + "End(builder)";
    code += GenIndents(2) + "return " + struct_var;

    code_base += code_prefix + code;
    code_base += "\n";
  }

  void GenStructForObjectAPI(const StructDef& struct_def,
                             std::string* code_ptr) const {
    if (struct_def.generated) return;

    std::set<std::string> import_list;
    std::string code;

    // Creates an object class for a struct or a table
    BeginClassForObjectAPI(struct_def, &code);

    GenInitialize(struct_def, &code, &import_list);

    InitializeFromBuf(struct_def, &code);

    InitializeFromPackedBuf(struct_def, &code);

    InitializeFromObjForObject(struct_def, &code);

    // TODO: reintroduce
    // if (parser_.opts.gen_compare) {
    //   GenCompareOperator(struct_def, &code);
    // }

    GenUnPack(struct_def, &code);

    if (struct_def.fixed) {
      GenPackForStruct(struct_def, &code);
    } else {
      GenPackForTable(struct_def, &code);
    }

    // Adds the imports at top.
    auto& code_base = *code_ptr;
    code_base += "\n";
    for (auto it = import_list.begin(); it != import_list.end(); it++) {
      auto im = *it;
      code_base += im + "\n";
    }
    code_base += code;
  }

  // TODO: finish obj api
  void GenUnionCreatorForStruct(const EnumDef& enum_def, const EnumVal& ev,
                                std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto union_type = namer_.Type(enum_def);
    const auto variant = namer_.Variant(ev);
    auto field_type = namer_.ObjectType(*ev.union_type.struct_def);

    code +=
        GenIndents(1) + "if unionType == " + union_type + "." + variant + ":";
    if (parser_.opts.include_dependence_headers) {
      auto package_reference = GenPackageReference(ev.union_type);
      code += GenIndents(2) + "import " + package_reference;
      field_type = package_reference + "." + field_type;
    }
    code += GenIndents(2) + "return " + field_type +
            ".InitFromBuf(table.Bytes, table.Pos)";
  }

  void GenUnionCreatorForString(const EnumDef& enum_def, const EnumVal& ev,
                                std::string* code_ptr) const {
    auto& code = *code_ptr;
    const auto union_type = namer_.Type(enum_def);
    const auto variant = namer_.Variant(ev);

    code +=
        GenIndents(1) + "if unionType == " + union_type + "()." + variant + ":";
    code += GenIndents(2) + "tab = Table(table.Bytes, table.Pos)";
    code += GenIndents(2) + "union = tab.String(table.Pos)";
    code += GenIndents(2) + "return union";
  }

  // Creates an union object based on union type.
  void GenUnionCreator(const EnumDef& enum_def, std::string* code_ptr) const {
    if (enum_def.generated) return;

    auto& code = *code_ptr;
    const auto enum_fn = namer_.Function(enum_def);

    code += "\n";
    code += "static func union_to_" + enum_fn;
    code += "(unionType: " + namer_.Type(enum_def) + ", parent: FB__Table) -> FB__Table: # NB: nullable";

    for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end(); ++it) {
      auto& ev = **it;
      // Union only supports string and table.
      switch (ev.union_type.base_type) {
        case BASE_TYPE_STRUCT:
          GenUnionCreatorForStruct(enum_def, ev, &code);
          break;
        case BASE_TYPE_STRING:
          GenUnionCreatorForString(enum_def, ev, &code);
          break;
        default:
          break;
      }
    }
    code += GenIndents(1) + "return null";
    code += "\n";
  }

  // Generate enum declarations.
  void GenEnum(const EnumDef& enum_def, std::string* code_ptr) const {
    if (enum_def.generated) return;

    GenComment(enum_def.doc_comment, code_ptr, &def_comment);
    BeginEnum(enum_def, code_ptr);
    for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end(); ++it) {
      auto& ev = **it;
      GenComment(ev.doc_comment, code_ptr, &def_comment);
      EnumMember(enum_def, ev, code_ptr);
    }
    EndEnum(code_ptr);
  }

  // Returns the function name that is able to read a value of the given type.
  std::string GenGetter(const Type& type) const {
    switch (type.base_type) {
      case BASE_TYPE_STRING:
        return "self._make_string(";
      case BASE_TYPE_UNION:
        return "self._sub_table(";
      case BASE_TYPE_VECTOR:
        return GenGetter(type.VectorType());
      default:
        return "self.buffer.bytes.decode_" +
               namer_.Method(GenTypeGet(type)) + "(";
    }
  }

  std::string GenFieldTy(const FieldDef& field) const {
    if (IsScalar(field.value.type.base_type) || IsArray(field.value.type)) {
      const std::string ty = GenTypeBasic(field.value.type);
      if (ty.find("int") != std::string::npos) {
        return "int";
      }

      if (ty.find("float") != std::string::npos) {
        return "float";
      }

      if (ty == "bool") {
        return "bool";
      }

      return "Any";
    } else {
      if (IsStruct(field.value.type)) {
        return "Any";
      } else {
        return "int";
      }
    }
  }

  // Returns the method name for use with add/put calls.
  std::string GenMethod(const FieldDef& field) const {
    return (IsScalar(field.value.type.base_type) || IsArray(field.value.type))
               ? namer_.Method(GenTypeBasic(field.value.type))
               : (IsStruct(field.value.type) ? "Struct" : "UOffsetTRelative");
  }

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
        return type.struct_def->name;
      case BASE_TYPE_UNION:
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

  std::string GdTypeName(const FieldDef& field, bool force_array = false,
                         bool force_raw_type = false) const {
    std::string field_type = TypeName(field);
    auto is_bool = IsBool(field.value.type.base_type);

    if (!force_raw_type && IsArray(field.value.type)) {
      return "Array[" + GdTypeName(field, false, true) + "]";
    }

    std::string out;

    if (is_bool) {
      out = "bool";
    } else if (field_type == "float" || field_type == "double") {
      out = "float";
    } else if (field.value.type.enum_def) {
      out = namer_.Type(*field.value.type.enum_def) + ".Enum";
    } else {
      out = "int";
    }

    if (force_array) {
      return "Array[" + out + "]";
    } else {
      return out;
    }
  }

  // Create a struct with a builder and the struct's arguments.
  void GenStructBuilder(const StructDef& struct_def,
                        std::string* code_ptr) const {
    BeginBuilderArgs(struct_def, code_ptr);
    StructBuilderArgs(struct_def,
                      /* nameprefix = */ "p_",
                      /* fieldname_suffix = */ "_", code_ptr);
    EndBuilderArgs(code_ptr);

    StructBuilderBody(struct_def, "p_", code_ptr);
    EndBuilderBody(code_ptr);
  }

  bool generate() {
    std::string one_file_code;
    if (!generateEnums()) return false;
    if (!generateStructs()) return false;

    return true;
  }

 private:
  bool generateEnums() const {
    for (auto it = parser_.enums_.vec.begin(); it != parser_.enums_.vec.end();
         ++it) {
      auto& enum_def = **it;
      std::string enumcode;
      GenEnum(enum_def, &enumcode);
      if (parser_.opts.generate_object_based_api & enum_def.is_union) {
        GenUnionCreator(enum_def, &enumcode);
      }

      const std::string mod =
          namer_.File(enum_def, SkipFile::SuffixAndExtension);

      if (!SaveType(namer_.File(enum_def, SkipFile::Suffix),
                    *enum_def.defined_namespace, enumcode)) {
        return false;
      }
    }
    return true;
  }

  bool generateStructs() const {
    for (auto it = parser_.structs_.vec.begin();
         it != parser_.structs_.vec.end(); ++it) {
      auto& struct_def = **it;
      std::string declcode;
      GenStruct(struct_def, &declcode);
      if (parser_.opts.generate_object_based_api) {
        GenStructForObjectAPI(struct_def, &declcode);
      }

      const std::string mod =
          namer_.File(struct_def, SkipFile::SuffixAndExtension);
      if (!SaveType(namer_.File(struct_def, SkipFile::Suffix),
                    *struct_def.defined_namespace, declcode)) {
        return false;
      }
    }
    return true;
  }

  // Begin by declaring namespace and imports.
  void BeginFile(const std::string& name_space_name,
                 std::string* code_ptr) const {
    auto& code = *code_ptr;
    code = code + "# " + FlatBuffersGeneratedWarning() + "\n\n";
    code += "# namespace: " + name_space_name + "\n\n";
  }

  bool SaveType(const std::string& defname, const Namespace& ns,
                const std::string& classcode) const {
    if (classcode.empty()) return true;

    std::string code = "";
    BeginFile(LastNamespacePart(ns), &code);
    code += classcode;

    const std::string directories = namer_.Directories(ns.components);
    EnsureDirExists(directories);

    const std::string filename = directories + defname;
    return parser_.opts.file_saver->SaveFile(filename.c_str(), code, false);
  }

 private:
  const SimpleFloatConstantGenerator float_const_gen_;
  const IdlNamer namer_;
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
