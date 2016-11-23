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

// Copyright (c) 2008-2013, Dave Benson.  All rights reserved.
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

#ifndef GRPC_C_INTERNAL_COMPILER_C_MESSAGE_H
#define GRPC_C_INTERNAL_COMPILER_C_MESSAGE_H

#include <string>
#include <google/protobuf/stubs/common.h>
#include <protoc-c/c_field.h>
#include <protoc-c/c_message.h>

namespace google {
namespace protobuf {
  namespace io {
    class Printer;             // printer.h
  }
}

namespace protobuf {
namespace compiler {
namespace grpc_c {

class MessagePackUnpackGenerator {
 public:
  // See generator.cc for the meaning of dllexport_decl.
  explicit MessagePackUnpackGenerator(const Descriptor* descriptor,
				      const string& dllexport_decl);
  ~MessagePackUnpackGenerator();

  // Header stuff.

  // Generate standard helper functions declarations for this message.
  void GenerateHelperFunctionDeclarations(io::Printer* printer);

  // Source file stuff.

  // Generate code that initializes the global variable storing the message's
  // descriptor.
  void GenerateHelperFunctionDefinitions(io::Printer* printer);

 private:

  string GetDefaultValueC(const FieldDescriptor *fd);

  const Descriptor* descriptor_;
  string dllexport_decl_;

  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(MessagePackUnpackGenerator);
};

class GrpcCMessageGenerator {
 public:
  explicit GrpcCMessageGenerator(const Descriptor* descriptor, 
			    const string& dllexport_decl);

  ~GrpcCMessageGenerator();

  void GenerateStructTypedef(io::Printer* printer);

 private:
  const Descriptor* descriptor_;
  string dllexport_decl_;
  scoped_array<scoped_ptr<GrpcCMessageGenerator> > grpc_c_nested_generators_;
};

}  // namespace grpc_c
}  // namespace compiler
}  // namespace protobuf

}  // namespace google
#endif  // GPRC_C_INTERNAL_COMPILER_C_MESSAGE_H
