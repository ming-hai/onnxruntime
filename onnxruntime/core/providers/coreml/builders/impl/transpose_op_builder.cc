// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/coreml/builders/helper.h"
#include "core/providers/coreml/builders/impl/base_op_builder.h"
#include "core/providers/coreml/builders/impl/builder_utils.h"
#include "core/providers/coreml/builders/model_builder.h"
#include "core/providers/coreml/builders/op_builder_factory.h"
#include "core/providers/coreml/shape_utils.h"
#include "core/providers/shared/utils/utils.h"

namespace onnxruntime {
namespace coreml {

class TransposeOpBuilder : public BaseOpBuilder {
  Status AddToModelBuilderImpl(ModelBuilder& model_builder, const Node& node,
                               const logging::Logger& logger) const override;

  bool SupportsMLProgram() const override { return true; }
};

Status TransposeOpBuilder::AddToModelBuilderImpl(ModelBuilder& model_builder,
                                                 const Node& node,
                                                 const logging::Logger& logger) const {
  NodeAttrHelper helper(node);
  std::vector<int64_t> perm = helper.Get("perm", std::vector<int64_t>());
  std::vector<int64_t> input_shape;
  ORT_RETURN_IF_NOT(GetShape(*node.InputDefs()[0], input_shape, logger), "Cannot get shape");
  auto input_dims = input_shape.size();
  if (perm.empty()) {
    for (int64_t i = input_dims - 1; i >= 0; i--)
      perm.push_back(i);
  } else {
    ORT_RETURN_IF_NOT(perm.size() == input_dims, "Perm and input should have same dimension");
  }

  if (model_builder.CreateMLProgram()) {
    using namespace CoreML::Specification::MILSpec;

    std::unique_ptr<Operation> op = model_builder.CreateOperation(node, "transpose");
    AddOperationInput(*op, "x", node.InputDefs()[0]->Name());
    AddOperationInput(*op, "perm", model_builder.AddConstant(op->type(), "perm", perm));
    AddOperationOutput(*op, *node.OutputDefs()[0]);
    model_builder.AddOperation(std::move(op));

  } else {
    std::unique_ptr<COREML_SPEC::NeuralNetworkLayer> layer = model_builder.CreateNNLayer(node);
    *layer->mutable_transpose()->mutable_axes() = {perm.cbegin(), perm.cend()};

    *layer->mutable_input()->Add() = node.InputDefs()[0]->Name();
    *layer->mutable_output()->Add() = node.OutputDefs()[0]->Name();

    model_builder.AddLayer(std::move(layer));
  }
  return Status::OK();
}

void CreateTransposeOpBuilder(const std::string& op_type, OpBuilderRegistrations& op_registrations) {
  op_registrations.builders.push_back(std::make_unique<TransposeOpBuilder>());
  op_registrations.op_builder_map.emplace(op_type, op_registrations.builders.back().get());
}

}  // namespace coreml
}  // namespace onnxruntime
