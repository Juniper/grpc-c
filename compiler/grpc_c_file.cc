/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 */

// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
// http://code.google.com/p/protobuf/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Author: kenton@google.com (Kenton Varda)
//  Based on original Protocol Buffers design by
//  Sanjay Ghemawat, Jeff Dean, and others.

// Copyright (c) 2008-2014, Dave Benson and the protobuf-c authors.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Modified to implement C code by Dave Benson.

#include "grpc_c_file.h"
#include "grpc_c_helpers.h"
#include "grpc_c_message.h"
#include <protoc-c/c_enum.h>
#include <protoc-c/c_service.h>
#include <protoc-c/c_extension.h>
#include <protoc-c/c_helpers.h>
#include <protoc-c/c_message.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/descriptor.pb.h>


#include "protobuf-c.h"


namespace google {
namespace protobuf {
namespace compiler {
namespace c {
  class MessageGenerator;
}
namespace grpc_c {

// ===================================================================

FileGenerator::FileGenerator(const FileDescriptor* file,
                             const string& dllexport_decl)
  : file_(file),
    message_generators_(
      new scoped_ptr<c::MessageGenerator>[file->message_type_count()]),
    grpc_c_message_generators_(
      new scoped_ptr<GrpcCMessageGenerator>[file->message_type_count()]),
    message_pack_unpack_generators_(
      new scoped_ptr<MessagePackUnpackGenerator>[file->message_type_count()]),
    enum_generators_(
      new scoped_ptr<c::EnumGenerator>[file->enum_type_count()]),
    service_generators_(
      new scoped_ptr<GrpcCServiceGenerator>[file->service_count()]),
    extension_generators_(
      new scoped_ptr<c::ExtensionGenerator>[file->extension_count()]) {

  for (int i = 0; i < file->message_type_count(); i++) {
    message_generators_[i].reset(
      new c::MessageGenerator(file->message_type(i), dllexport_decl));
  }

  for (int i = 0; i < file->message_type_count(); i++) {
    grpc_c_message_generators_[i].reset(
      new GrpcCMessageGenerator(file->message_type(i), dllexport_decl));
  }

  for (int i = 0; i < file->message_type_count(); i++) {
    message_pack_unpack_generators_[i].reset(
      new MessagePackUnpackGenerator(file->message_type(i), dllexport_decl));
  }

  for (int i = 0; i < file->enum_type_count(); i++) {
    enum_generators_[i].reset(
      new c::EnumGenerator(file->enum_type(i), dllexport_decl));
  }

  for (int i = 0; i < file->service_count(); i++) {
    service_generators_[i].reset(
      new GrpcCServiceGenerator(file->service(i), dllexport_decl));
  }

  for (int i = 0; i < file->extension_count(); i++) {
    extension_generators_[i].reset(
      new c::ExtensionGenerator(file->extension(i), dllexport_decl));
  }

  c::SplitStringUsing(file_->package(), ".", &package_parts_);
}

FileGenerator::~FileGenerator() {}

void FileGenerator::GenerateHeader(io::Printer* printer) {
  string filename_identifier = c::FilenameIdentifier(file_->name());

  static const int min_header_version = 1000000;

  // Generate top of header.
  printer->Print(
    "/* Generated by the protocol buffer compiler.  DO NOT EDIT! */\n"
    "/* Generated from: $filename$ */\n"
    "\n"
    "#ifndef PROTOBUF_C_$filename_identifier$__INCLUDED\n"
    "#define PROTOBUF_C_$filename_identifier$__INCLUDED\n"
    "\n"
    "#include <protobuf-c/protobuf-c.h>\n"
    "\n"
    "#include <grpc/grpc.h>\n"
    "#include <grpc/byte_buffer_reader.h>\n"
    "#include <grpc/support/alloc.h>\n"
    "#include <grpc/support/log.h>\n"
    "#include <grpc-c/grpc-c.h>\n"
    "\n"
    "PROTOBUF_C__BEGIN_DECLS\n"
    "\n",
    "filename", file_->name(),
    "filename_identifier", filename_identifier);

  // Verify the protobuf-c library header version is compatible with the
  // protoc-c version before going any further.
  printer->Print(
    "#if PROTOBUF_C_VERSION_NUMBER < $min_header_version$\n"
    "# error This file was generated by a newer version of protoc-c which is "
    "incompatible with your libprotobuf-c headers. Please update your headers.\n"
    "#elif $protoc_version$ < PROTOBUF_C_MIN_COMPILER_VERSION\n"
    "# error This file was generated by an older version of protoc-c which is "
    "incompatible with your libprotobuf-c headers. Please regenerate this file "
    "with a newer version of protoc-c.\n"
    "#endif\n"
    "\n",
    "min_header_version", c::SimpleItoa(min_header_version),
    "protoc_version", c::SimpleItoa(PROTOBUF_C_VERSION_NUMBER));

  for (int i = 0; i < file_->dependency_count(); i++) {
    printer->Print(
      "#include \"$dependency$.grpc-c.h\"\n",
      "dependency", c::StripProto(file_->dependency(i)->name()));
  }

  printer->Print("\n");

  // Generate forward declarations of classes.
  for (int i = 0; i < file_->message_type_count(); i++) {
    message_generators_[i]->GenerateStructTypedef(printer);
    grpc_c_message_generators_[i]->GenerateStructTypedef(printer);
  }

  printer->Print("\n");

  // Generate enum definitions.
  printer->Print("\n/* --- enums --- */\n\n");
  for (int i = 0; i < file_->message_type_count(); i++) {
    message_generators_[i]->GenerateEnumDefinitions(printer);
  }
  for (int i = 0; i < file_->enum_type_count(); i++) {
    enum_generators_[i]->GenerateDefinition(printer);
  }

  // Generate class definitions.
  printer->Print("\n/* --- messages --- */\n\n");
  for (int i = 0; i < file_->message_type_count(); i++) {
    message_generators_[i]->GenerateStructDefinition(printer);
  }

  for (int i = 0; i < file_->message_type_count(); i++) {
    message_generators_[i]->GenerateHelperFunctionDeclarations(printer, false);
  }

  // Generate data packers and unpackers
  printer->Print("\n/* --- data packers and unpackers -- */\n");
  for (int i = 0; i < file_->message_type_count(); i++) {
    message_pack_unpack_generators_[i]->GenerateHelperFunctionDeclarations(printer);
  }

  // Generate service definitions.
  printer->Print("\n\n/* --- services --- */\n");
  for (int i = 0; i < file_->service_count(); i++) {
    service_generators_[i]->GenerateMainHFile(printer);
  }

  // Declare extension identifiers.
  for (int i = 0; i < file_->extension_count(); i++) {
    extension_generators_[i]->GenerateDeclaration(printer);
  }

  printer->Print("\n/* --- descriptors --- */\n\n");
  for (int i = 0; i < file_->enum_type_count(); i++) {
    enum_generators_[i]->GenerateDescriptorDeclarations(printer);
  }
  for (int i = 0; i < file_->message_type_count(); i++) {
    message_generators_[i]->GenerateDescriptorDeclarations(printer);
  }

  printer->Print(
    "\n"
    "PROTOBUF_C__END_DECLS\n"
    "\n\n#endif  /* PROTOBUF_C_$filename_identifier$__INCLUDED */\n",
    "filename_identifier", filename_identifier);
}

void FileGenerator::GenerateServiceSource(io::Printer* printer) {
  printer->Print(
    "/* Generated by the protocol buffer compiler.  DO NOT EDIT! */\n"
    "/* Generated from: $filename$ */\n"
    "\n"
    "/* Do not generate deprecated warnings for self */\n"
    "#ifndef PROTOBUF_C__NO_DEPRECATED\n"
    "#define PROTOBUF_C__NO_DEPRECATED\n"
    "#endif\n"
    "\n"
    "#include \"$basename$.grpc-c.h\"\n",
    "filename", file_->name(),
    "basename", c::StripProto(file_->name()));

  for (int i = 0; i < file_->service_count(); i++) {
    service_generators_[i]->GenerateCServiceFile(printer);
  }

}

void FileGenerator::GenerateSource(io::Printer* printer) {
  printer->Print(
    "/* Generated by the protocol buffer compiler.  DO NOT EDIT! */\n"
    "/* Generated from: $filename$ */\n"
    "\n"
    "/* Do not generate deprecated warnings for self */\n"
    "#ifndef PROTOBUF_C__NO_DEPRECATED\n"
    "#define PROTOBUF_C__NO_DEPRECATED\n"
    "#endif\n"
    "\n"
    "#include \"$basename$.grpc-c.h\"\n",
    "filename", file_->name(),
    "basename", c::StripProto(file_->name()));

  for (int i = 0; i < file_->message_type_count(); i++) {
    message_generators_[i]->GenerateHelperFunctionDefinitions(printer, false);
  }
  for (int i = 0; i < file_->message_type_count(); i++) {
    message_generators_[i]->GenerateMessageDescriptor(printer);
  }
  for (int i = 0; i < file_->message_type_count(); i++) {
    message_pack_unpack_generators_[i]->GenerateHelperFunctionDefinitions(printer);
  }
  for (int i = 0; i < file_->enum_type_count(); i++) {
    enum_generators_[i]->GenerateEnumDescriptor(printer);
  }
  for (int i = 0; i < file_->service_count(); i++) {
    service_generators_[i]->GenerateCFile(printer);
  }

}

}  // namespace grpc_c
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
