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

#include "grpc_c_service.h"
#include <protoc-c/c_helpers.h>
#include <google/protobuf/io/printer.h>

namespace google {
namespace protobuf {
namespace compiler {
namespace grpc_c {

GrpcCServiceGenerator::GrpcCServiceGenerator(const ServiceDescriptor* descriptor,
					     const string& dllexport_decl)
  : descriptor_(descriptor) {
  vars_["name"] = descriptor_->name();
  vars_["fullname"] = descriptor_->full_name();
  vars_["cname"] = c::FullNameToC(descriptor_->full_name());
  vars_["lcfullname"] = c::FullNameToLower(descriptor_->full_name());
  vars_["ucfullname"] = c::FullNameToUpper(descriptor_->full_name());
  vars_["lcfullpadd"] = c::ConvertToSpaces(vars_["lcfullname"]);
  vars_["package"] = descriptor_->file()->package();
  if (dllexport_decl.empty()) {
    vars_["dllexport"] = "";
  } else {
    vars_["dllexport"] = dllexport_decl + " ";
  }
}

GrpcCServiceGenerator::~GrpcCServiceGenerator() {}

// Header stuff.
void GrpcCServiceGenerator::GenerateMainHFile(io::Printer* printer)
{
  GenerateCallersDeclarations(printer);
}

void GrpcCServiceGenerator::GenerateCallersDeclarations(io::Printer* printer)
{
  /* Service and init */
  string lcfullname = c::FullNameToLower(descriptor_->full_name());

  /* Methods array */
  printer->Print(vars_, "\nextern const char *$lcfullname$__methods[];\n");

  printer->Print(vars_, "\nint $lcfullname$__service_init (grpc_c_server_t *server);\n");

  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    string lcname = c::CamelToLower(method->name());
    vars_["method"] = lcname;
    vars_["metpad"] = c::ConvertToSpaces(lcname);
    vars_["input_typename"] = c::FullNameToC(method->input_type()->full_name());
    vars_["output_typename"] = c::FullNameToC(method->output_type()->full_name());
    vars_["padddddddddddddddddd"] = c::ConvertToSpaces(lcfullname + "__" + lcname);

    /* For unary calls, we allow a blocking function */
    if (!method->client_streaming() && !method->server_streaming()) {
	printer->Print(vars_, 
		       "\nint $lcfullname$__$method$ (grpc_c_client_t *client, "
		       "grpc_c_metadata_array_t *array, uint32_t flags, $input_typename$ *input, "
		       "$output_typename$ **output, grpc_c_status_t *status, "
		       "long timeout);\n");
    }
    
    /* Sync request */
    printer->Print(vars_, 
		   "\nint $lcfullname$__$method$__sync (grpc_c_client_t *client, "
		   "grpc_c_metadata_array_t *array, uint32_t flags, grpc_c_context_t **context, $input_typename$ *input, long timeout);\n");

    /* Async request */
    printer->Print(vars_,
                   "\nint $lcfullname$__$method$__async (grpc_c_client_t *client, "
		   "grpc_c_metadata_array_t *array, uint32_t flags, $input_typename$ *input, \n"
		   "$padddddddddddddddddd$grpc_c_client_callback_t *cb, void *tag);\n");

    /* callback */
    printer->Print(vars_,
		   "\nvoid $lcfullname$__$method$_cb (grpc_c_context_t *context);\n");
  }
}

void GrpcCServiceGenerator::GenerateDescriptorDeclarations(io::Printer* printer)
{
  printer->Print(vars_, "extern const ProtobufCServiceDescriptor $lcfullname$__descriptor;\n");
}

// Service init
void GrpcCServiceGenerator::GenerateCServiceFile(io::Printer* printer)
{
  string lcfullname = c::FullNameToLower(descriptor_->full_name());
  vars_["method_count"] = c::SimpleItoa(descriptor_->method_count());
  printer->Print(vars_, 
		 "\nint\n"
		 "$lcfullname$__service_init (grpc_c_server_t *server)\n"
		 "{\n"
		 "    if (grpc_c_methods_alloc(server, $method_count$))\n"
		 "        return 1;\n\n");

  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    string lcname = c::CamelToLower(method->name());
    vars_["method"] = lcname;
    vars_["metpad"] = c::ConvertToSpaces(lcname);
    vars_["input_typename"] = c::FullNameToC(method->input_type()->full_name());
    vars_["input_lower"] = c::FullNameToLower(method->input_type()->full_name());
    vars_["output_typename"] = c::FullNameToC(method->output_type()->full_name());
    vars_["output_lower"] = c::FullNameToLower(method->output_type()->full_name());
    vars_["index"] = c::SimpleItoa(i);
    vars_["client_streaming"] = method->client_streaming() ? "1" : "0";
    vars_["server_streaming"] = method->server_streaming() ? "1" : "0";

    printer->Print(vars_,
		   "    grpc_c_register_method(server, $lcfullname$__methods[$index$], $client_streaming$, $server_streaming$, &$lcfullname$__$method$_cb,\n"
                   "                         &$input_lower$_packer, "
		   "&$input_lower$_unpacker, &$input_lower$_free,\n"
		   "                         &$output_lower$_packer, "
		   "&$output_lower$_unpacker, &$output_lower$_free);\n"
		   "\n");
  }
  printer->Print(vars_, 
		 "    return 0;\n"
		 "}\n");
}

// Source file stuff.
void GrpcCServiceGenerator::GenerateCFile(io::Printer* printer)
{
  GenerateCallersImplementations(printer);
}

struct MethodIndexAndName { unsigned i; const char *name; };
static int
compare_method_index_and_name_by_name (const void *a, const void *b)
{
  const MethodIndexAndName *ma = (const MethodIndexAndName *) a;
  const MethodIndexAndName *mb = (const MethodIndexAndName *) b;
  return strcmp (ma->name, mb->name);
}

void GrpcCServiceGenerator::GenerateServiceDescriptor(io::Printer* printer)
{
  int n_methods = descriptor_->method_count();
  MethodIndexAndName *mi_array = new MethodIndexAndName[n_methods];

  vars_["n_methods"] = c::SimpleItoa(n_methods);
  printer->Print(vars_, "static const ProtobufCMethodDescriptor $lcfullname$__method_descriptors[$n_methods$] =\n"
                       "{\n");
  for (int i = 0; i < n_methods; i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    vars_["method"] = method->name();
    vars_["input_descriptor"] = "&" + c::FullNameToLower(method->input_type()->full_name()) + "__descriptor";
    vars_["output_descriptor"] = "&" + c::FullNameToLower(method->output_type()->full_name()) + "__descriptor";
    printer->Print(vars_,
             "  { \"$method$\", $input_descriptor$, $output_descriptor$ },\n");
    mi_array[i].i = i;
    mi_array[i].name = method->name().c_str();
  }
  printer->Print(vars_, "};\n");

  qsort ((void*)mi_array, n_methods, sizeof (MethodIndexAndName),
         compare_method_index_and_name_by_name);
  printer->Print(vars_, "const unsigned $lcfullname$__method_indices_by_name[] = {\n");
  for (int i = 0; i < n_methods; i++) {
    vars_["i"] = c::SimpleItoa(mi_array[i].i);
    vars_["name"] = mi_array[i].name;
    vars_["comma"] = (i + 1 < n_methods) ? "," : " ";
    printer->Print(vars_, "  $i$$comma$        /* $name$ */\n");
  }
  printer->Print(vars_, "};\n");

  vars_["name"] = descriptor_->name();

  printer->Print(vars_, "const ProtobufCServiceDescriptor $lcfullname$__descriptor =\n"
                       "{\n"
		       "  PROTOBUF_C__SERVICE_DESCRIPTOR_MAGIC,\n"
		       "  \"$fullname$\",\n"
		       "  \"$name$\",\n"
		       "  \"$cname$\",\n"
		       "  \"$package$\",\n"
		       "  $n_methods$,\n"
		       "  $lcfullname$__method_descriptors,\n"
		       "  $lcfullname$__method_indices_by_name\n"
		       "};\n");

  delete[] mi_array;
}

void GrpcCServiceGenerator::GenerateCallersImplementations(io::Printer* printer)
{
  /* methods array */
  printer->Print(vars_, "\nconst char *$lcfullname$__methods[] = {\n");
  string fullname = descriptor_->full_name();
  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    vars_["method"] = method->name();
    printer->Print(vars_, "    \"/$fullname$/$method$\",\n");
  }
  printer->Print(vars_, "};\n");

  string lcfullname = c::FullNameToLower(fullname);

  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    string lcname = c::CamelToLower(method->name());
    string lcfullname = c::FullNameToLower(descriptor_->full_name());
    vars_["method"] = lcname;
    vars_["metpad"] = c::ConvertToSpaces(lcname);
    vars_["input_typename"] = c::FullNameToC(method->input_type()->full_name());
    vars_["input_lower"] = c::FullNameToLower(method->input_type()->full_name());
    vars_["output_typename"] = c::FullNameToC(method->output_type()->full_name());
    vars_["output_lower"] = c::FullNameToLower(method->output_type()->full_name());
    vars_["padddddddddddddddddd"] = c::ConvertToSpaces(lcfullname + "__" + lcname);
    vars_["index"] = c::SimpleItoa(i);
    vars_["client_streaming"] = method->client_streaming() ? "1" : "0";
    vars_["server_streaming"] = method->server_streaming() ? "1" : "0";

    /*
     * For unary calls, we allow a blocking function
     */
    if (!method->client_streaming() && !method->server_streaming()) {
	printer->Print(vars_,
		       "\nint \n"
		       "$lcfullname$__$method$ (grpc_c_client_t *client, grpc_c_metadata_array_t *array, uint32_t flags, $input_typename$ *input,\n"
		       "$padddddddddddddddddd$       $output_typename$ **output, grpc_c_status_t *status, long timeout)\n"
		       "{\n"
		       "    return grpc_c_client_request_unary(client, array, flags, $lcfullname$__methods[$index$], "
		       "(void *)input, (void **)output, status, $client_streaming$, $server_streaming$, "
		       "&$input_lower$_packer, &$input_lower$_unpacker, &$input_lower$_free, "
		       "&$output_lower$_packer, &$output_lower$_unpacker, &$output_lower$_free, timeout);\n"
		       "}\n");
    }

    /*
     * Sync and async functions for everyone
     */
    printer->Print(vars_,
                   "\nint \n"
		   "$lcfullname$__$method$__sync (grpc_c_client_t *client, grpc_c_metadata_array_t *array, uint32_t flags, grpc_c_context_t **context, $input_typename$ *input, long timeout)\n"
		   "{\n"
		   "    return grpc_c_client_request_sync(client, array, flags, context, $lcfullname$__methods[$index$], input, "
		   "$client_streaming$, $server_streaming$, "
		   "&$input_lower$_packer, &$input_lower$_unpacker, "
		   "&$input_lower$_free, &$output_lower$_packer,"
		   "&$output_lower$_unpacker, &$output_lower$_free, timeout);\n"
		   "}\n");

    printer->Print(vars_,
                   "\nint \n"
		   "$lcfullname$__$method$__async (grpc_c_client_t *client, \n"
                   "$padddddddddddddddddd$        grpc_c_metadata_array_t *array, uint32_t flags, \n"
                   "$padddddddddddddddddd$        $input_typename$ *input,\n"
		   "$padddddddddddddddddd$        grpc_c_client_callback_t *cb,\n"
                   "$padddddddddddddddddd$        void *tag)\n"
		   "{\n"
		   "    return grpc_c_client_request_async(client, array, flags, $lcfullname$__methods[$index$], "
		   "input, cb, tag, $client_streaming$, $server_streaming$, "
		   "&$input_lower$_packer, &$input_lower$_unpacker, "
		   "&$input_lower$_free, &$output_lower$_packer, "
		   "&$output_lower$_unpacker, &$output_lower$_free);\n"
		   "}\n");
  }
}


}  // namespace grpc_c
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
