/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <google/protobuf/compiler/plugin.h>
#include "grpc_c_generator.h"


int main(int argc, char* argv[]) {
  google::protobuf::compiler::grpc_c::GrpcCGenerator grpc_c_generator;
  return google::protobuf::compiler::PluginMain(argc, argv, &grpc_c_generator);
}
