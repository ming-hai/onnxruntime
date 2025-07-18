// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <memory>

#include "core/common/logging/logging.h"
#include "core/graph/constants.h"
#include "core/graph/graph.h"
#include "core/graph/graph_viewer.h"
#include "core/providers/coreml/coreml_provider_factory_creator.h"
#include "core/providers/coreml/coreml_provider_factory.h"
#include "core/session/inference_session.h"
#include "core/session/onnxruntime_cxx_api.h"
#include "test/common/tensor_op_test_utils.h"
#include "test/framework/test_utils.h"
#include "test/util/include/asserts.h"
#include "test/util/include/current_test_name.h"
#include "test/util/include/default_providers.h"
#include "test/util/include/inference_session_wrapper.h"
#include "test/util/include/test_environment.h"
#include "test/util/include/test_utils.h"
#include "core/graph/onnx_protobuf.h"

#if !defined(ORT_MINIMAL_BUILD)
// if this is a full build we need the provider test utils
#include "test/providers/provider_test_utils.h"
#endif  // !(ORT_MINIMAL_BUILD)

#include "gtest/gtest.h"
#include "gmock/gmock.h"

using namespace ONNX_NAMESPACE;
using namespace ::onnxruntime::logging;

// defined in test_main.cc
extern std::unique_ptr<Ort::Env> ort_env;

namespace onnxruntime {
namespace test {

static std::unordered_map<std::string, std::string> MakeCoreMLProviderOptions(std::string ModelFormat = "NeuralNetwork",
                                                                              std::string ComputeUnits = "CPUOnly",
                                                                              std::string ModelCacheDirectory = "") {
  std::unordered_map<std::string, std::string> provider_options = {{kCoremlProviderOption_MLComputeUnits, ComputeUnits},
                                                                   {kCoremlProviderOption_ModelFormat, ModelFormat},
                                                                   {kCoremlProviderOption_ModelCacheDirectory,
                                                                    ModelCacheDirectory}};
  return provider_options;
}

static std::unique_ptr<IExecutionProvider> MakeCoreMLExecutionProvider(
    std::string ModelFormat = "NeuralNetwork", std::string ComputeUnits = "CPUOnly", std::string ModelCacheDirectory = "") {
  std::unordered_map<std::string, std::string> provider_options = MakeCoreMLProviderOptions(ModelFormat,
                                                                                            ComputeUnits,
                                                                                            ModelCacheDirectory);
  return CoreMLProviderFactoryCreator::Create(provider_options)->CreateProvider();
}

#if !defined(ORT_MINIMAL_BUILD)

TEST(CoreMLExecutionProviderTest, TestAddEpUsingPublicApi) {
  auto session_has_ep = [](Ort::Session& session) -> bool {
    // Access the underlying InferenceSession.
    const OrtSession* ort_session = session;
    const InferenceSession* s = reinterpret_cast<const InferenceSession*>(ort_session);
    bool has_ep = false;

    for (const auto& provider : s->GetRegisteredProviderTypes()) {
      if (provider == kCoreMLExecutionProvider) {
        has_ep = true;
        break;
      }
    }
    return has_ep;
  };

  const ORTCHAR_T* model_file_name = ORT_TSTR("testdata/constant_floats.onnx");
  auto provider_options = MakeCoreMLProviderOptions("NeuralNetwork", "CPUOnly", "./tmp");

  {
    // Test C++ API to add CoreML EP with the short name 'CoreML'.
    Ort::SessionOptions so;
    so.AppendExecutionProvider("CoreML", provider_options);
    Ort::Session session(*ort_env, model_file_name, so);
    ASSERT_TRUE(session_has_ep(session)) << "CoreML EP was not found in registered providers for session.";
  }

  {
    // Test C++ API to add CoreML EP with the long canonical name 'CoreMLExecutionProvider'.
    Ort::SessionOptions so;
    so.AppendExecutionProvider(kCoreMLExecutionProvider, provider_options);
    Ort::Session session(*ort_env, model_file_name, so);
    ASSERT_TRUE(session_has_ep(session)) << "CoreML EP was not found in registered providers for session.";
  }
}

TEST(CoreMLExecutionProviderTest, FunctionTest) {
  const ORTCHAR_T* model_file_name = ORT_TSTR("coreml_execution_provider_test_graph.onnx");

  {  // Create the model with 2 add nodes
    onnxruntime::Model model("graph_1", false, DefaultLoggingManager().DefaultLogger());
    auto& graph = model.MainGraph();
    std::vector<onnxruntime::NodeArg*> inputs;
    std::vector<onnxruntime::NodeArg*> outputs;

    // FLOAT tensor.
    ONNX_NAMESPACE::TypeProto float_tensor;
    float_tensor.mutable_tensor_type()->set_elem_type(ONNX_NAMESPACE::TensorProto_DataType_FLOAT);
    float_tensor.mutable_tensor_type()->mutable_shape()->add_dim()->set_dim_value(1);
    float_tensor.mutable_tensor_type()->mutable_shape()->add_dim()->set_dim_value(1);
    float_tensor.mutable_tensor_type()->mutable_shape()->add_dim()->set_dim_value(3);
    float_tensor.mutable_tensor_type()->mutable_shape()->add_dim()->set_dim_value(2);

    auto& input_arg_1 = graph.GetOrCreateNodeArg("X", &float_tensor);
    auto& input_arg_2 = graph.GetOrCreateNodeArg("Y", &float_tensor);
    inputs.push_back(&input_arg_1);
    inputs.push_back(&input_arg_2);
    auto& output_arg = graph.GetOrCreateNodeArg("node_1_out_1", &float_tensor);
    outputs.push_back(&output_arg);
    graph.AddNode("node_1", "Add", "node 1.", inputs, outputs);

    auto& input_arg_3 = graph.GetOrCreateNodeArg("Z", &float_tensor);
    inputs.clear();
    inputs.push_back(&output_arg);
    inputs.push_back(&input_arg_3);
    auto& output_arg_2 = graph.GetOrCreateNodeArg("M", &float_tensor);
    outputs.clear();
    outputs.push_back(&output_arg_2);
    graph.AddNode("node_2", "Add", "node 2.", inputs, outputs);

    ASSERT_STATUS_OK(graph.Resolve());
    ASSERT_STATUS_OK(onnxruntime::Model::Save(model, model_file_name));
  }

#if defined(__APPLE__)
  std::vector<int64_t> dims_mul_x = {1, 1, 3, 2};
  std::vector<float> values_mul_x = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  OrtValue ml_value_x;

  AllocatorPtr allocator = CPUAllocator::DefaultInstance();
  CreateMLValue<float>(allocator, dims_mul_x, values_mul_x, &ml_value_x);
  OrtValue ml_value_y;
  CreateMLValue<float>(allocator, dims_mul_x, values_mul_x, &ml_value_y);
  OrtValue ml_value_z;
  CreateMLValue<float>(allocator, dims_mul_x, values_mul_x, &ml_value_z);

  NameMLValMap feeds;
  feeds.insert(std::make_pair("X", ml_value_x));
  feeds.insert(std::make_pair("Y", ml_value_y));
  feeds.insert(std::make_pair("Z", ml_value_z));

  RunAndVerifyOutputsWithEP(model_file_name, CurrentTestName(),
                            MakeCoreMLExecutionProvider(),
                            feeds);
#else
  TestModelLoad(model_file_name, MakeCoreMLExecutionProvider(), ExpectedEPNodeAssignment::Some);
#endif
}

// CoreML EP currently handles a special case for supporting ArgMax op:
// An ArgMax followed by a Cast to int32 type.
// Please see in <repo_root>/onnxruntime/core/providers/coreml/builders/impl/argmax_op_builder.cc
// and /cast_op_builder.cc. We have the following UT test here for this special case
// This test case can also be shared later if we want to support similar cases in NNAPI
TEST(CoreMLExecutionProviderTest, ArgMaxCastTest) {
  const ORTCHAR_T* model_file_name = ORT_TSTR("testdata/coreml_argmax_cast_test.onnx");

#if defined(__APPLE__)
  std::vector<int64_t> dims_mul_x = {3, 2, 2};
  std::vector<float> values_mul_x = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f};
  OrtValue ml_value_x;
  AllocatorPtr allocator = CPUAllocator::DefaultInstance();
  CreateMLValue<float>(allocator, dims_mul_x, values_mul_x, &ml_value_x);

  NameMLValMap feeds;
  feeds.insert(std::make_pair("X", ml_value_x));

  EPVerificationParams verification_params{};
  verification_params.ep_node_assignment = ExpectedEPNodeAssignment::All;

  RunAndVerifyOutputsWithEP(model_file_name, CurrentTestName(),
                            MakeCoreMLExecutionProvider(),
                            feeds,
                            verification_params);
  RunAndVerifyOutputsWithEP(model_file_name, CurrentTestName(),
                            MakeCoreMLExecutionProvider("MLProgram"),
                            feeds,
                            verification_params);
#else
  TestModelLoad(model_file_name, MakeCoreMLExecutionProvider(), ExpectedEPNodeAssignment::All);
#endif
}

TEST(CoreMLExecutionProviderTest, ArgMaxUnsupportedCastTest) {
  const ORTCHAR_T* model_file_name = ORT_TSTR("testdata/coreml_argmax_unsupported_cast_test.onnx");

#if defined(__APPLE__)
  std::vector<int64_t> dims_mul_x = {3, 2, 2};
  std::vector<float> values_mul_x = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f};
  OrtValue ml_value_x;
  AllocatorPtr allocator = CPUAllocator::DefaultInstance();
  CreateMLValue<float>(allocator, dims_mul_x, values_mul_x, &ml_value_x);

  NameMLValMap feeds;
  feeds.insert(std::make_pair("X", ml_value_x));

  const std::function<void(const Graph&)> graph_verifier = [](const Graph& graph) {
    GraphViewer graph_viewer{graph};
    const auto& node_indices_in_order = graph_viewer.GetNodesInTopologicalOrder();
    ASSERT_EQ(node_indices_in_order.size(), size_t{2});
    // second node should be an unsupported Cast
    const auto* cast_node = graph.GetNode(node_indices_in_order[1]);
    ASSERT_NE(cast_node, nullptr);
    ASSERT_EQ(cast_node->OpType(), "Cast");
    ASSERT_EQ(cast_node->GetExecutionProviderType(), kCpuExecutionProvider);
  };

  EPVerificationParams verification_params{};
  verification_params.ep_node_assignment = ExpectedEPNodeAssignment::Some;
  verification_params.graph_verifier = &graph_verifier;

  RunAndVerifyOutputsWithEP(model_file_name, CurrentTestName(),
                            MakeCoreMLExecutionProvider(),
                            feeds,
                            verification_params);

  RunAndVerifyOutputsWithEP(model_file_name, CurrentTestName(),
                            MakeCoreMLExecutionProvider("MLProgram"),
                            feeds,
                            verification_params);
#else
  TestModelLoad(model_file_name, MakeCoreMLExecutionProvider(), ExpectedEPNodeAssignment::Some);
#endif
}

TEST(CoreMLExecutionProviderTest, GatherWithScalarIndices) {
  // For scalar inputs, the input shape is modified from [] -> [1] before passing the input to CoreML.
  // This won't work for Gather because the output shape depends on the `indices` input shape which could be a scalar.
  // Currently, we expect the CoreML EP to only take the Shape node in this graph (Gather -> Shape).
  const auto model_file_name = ORT_TSTR("testdata/gather_with_scalar_indices_then_shape.onnx");

#if defined(__APPLE__)
  RandomValueGenerator gen{1234};
  std::vector<int64_t> X_shape = {5, 3, 4};
  std::vector<float> X_data = gen.Uniform<float>(X_shape, 0.0f, 1.0f);
  OrtValue X = CreateInputOrtValueOnCPU<float>(X_shape, X_data);
  OrtValue indices = CreateInputOrtValueOnCPU<int64_t>(AsSpan<int64_t>({}), AsSpan<int64_t>({1}));

  RunAndVerifyOutputsWithEP(model_file_name, CurrentTestName(),
                            MakeCoreMLExecutionProvider(),
                            {{"X", X}, {"indices", indices}});
#else
  TestModelLoad(model_file_name, MakeCoreMLExecutionProvider(), ExpectedEPNodeAssignment::Some);
#endif
}

TEST(CoreMLExecutionProviderTest, ShapeThenSliceAndGather) {
  // This is a simple test model that provides the output of Shape to Slice and Gather.
  // We expect the CoreML EP to support shape manipulations like this.
  const auto model_file_name = ORT_TSTR("testdata/shape_then_slice_and_gather.onnx");

#if defined(__APPLE__)
  RandomValueGenerator gen{1234};
  std::vector<int64_t> X_shape = {5, 3, 4, 1, 2};
  std::vector<float> X_data = gen.Uniform<float>(X_shape, 0.0f, 1.0f);
  OrtValue X = CreateInputOrtValueOnCPU<float>(X_shape, X_data);

  RunAndVerifyOutputsWithEP(model_file_name, CurrentTestName(),
                            MakeCoreMLExecutionProvider(),
                            {{"X", X}},
                            EPVerificationParams{ExpectedEPNodeAssignment::All});
#else
  TestModelLoad(model_file_name, MakeCoreMLExecutionProvider(), ExpectedEPNodeAssignment::All);
#endif
}

#endif  // !(ORT_MINIMAL_BUILD)

TEST(CoreMLExecutionProviderTest, TestOrtFormatModel) {
  // mnist model that has only had basic optimizations applied. CoreML should be able to take at least some of the nodes
  const ORTCHAR_T* model_file_name = ORT_TSTR("testdata/mnist.basic.ort");

#if defined(__APPLE__)
  RandomValueGenerator random{};
  const std::vector<int64_t> dims = {1, 1, 28, 28};
  std::vector<float> data = random.Gaussian<float>(dims, 0.0f, 1.f);

  OrtValue ml_value;
  CreateMLValue<float>(TestCPUExecutionProvider()->CreatePreferredAllocators()[0], dims, data, &ml_value);

  NameMLValMap feeds;
  feeds.insert(std::make_pair("Input3", ml_value));

  RunAndVerifyOutputsWithEP(model_file_name, CurrentTestName(),
                            MakeCoreMLExecutionProvider(),
                            feeds);
#else
  TestModelLoad(model_file_name, MakeCoreMLExecutionProvider(), ExpectedEPNodeAssignment::Some);
#endif
}

#if defined(USE_COREML)
// Names in CoreML cannot start with [0-9] or contain anything but "[a-z][A-Z][0-9]_"
// Test that we fix invalid names in model inputs, initializers and outputs.
// This is only enforced for ML Program, so we only do name sanitization when creating an ML Program format model.
TEST(CoreMLExecutionProviderTest, TestNameSanitization) {
  OpTester test("Clip", 11);

  std::vector<int64_t> dims{3, 3};
  test.AddInput<float>("0", dims,
                       {-1.0f, 0.0f, 1.0f,
                        -6.0f, 0.0f, 6.0f,
                        -5.4f, 2.0f, 6.0f});
  test.AddInput<float>("1.min", {}, {-5}, true);  // add as initializers
  test.AddInput<float>("2/max", {}, {5}, true);
  test.AddOutput<float>("3", dims,
                        {-1.0f, 0.0f, 1.0f,
                         -5.0f, 0.0f, 5.0f,
                         -5.0f, 2.0f, 5.0f});

  // TensorRT does not support Clip opset 11 yet.
  test.Run(OpTester::ExpectResult::kExpectSuccess, "", {kTensorrtExecutionProvider});
}
#endif

TEST(CoreMLExecutionProviderTest, TestModelCache) {
  const ORTCHAR_T* model_file_name = ORT_TSTR("testdata/coreml_argmax_cast_test.onnx");

  onnx::ModelProto model;
  {
    std::ifstream in(model_file_name, std::ios_base::binary);
    model.ParseFromIstream(&in);
    in.close();
  }

  std::string out_string;
#if defined(__APPLE__)
  std::vector<int64_t> dims_mul_x = {3, 2, 2};
  std::vector<float> values_mul_x = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f};
  OrtValue ml_value_x;
  AllocatorPtr allocator = CPUAllocator::DefaultInstance();
  CreateMLValue<float>(allocator, dims_mul_x, values_mul_x, &ml_value_x);

  NameMLValMap feeds;
  feeds.insert(std::make_pair("X", ml_value_x));
  std::string subgraph_name;
  const std::function<void(const Graph&)> graph_verifier = [&subgraph_name](const Graph& graph) {
    GraphViewer graph_viewer{graph};
    const auto& node_indices_in_order = graph_viewer.GetNodesInTopologicalOrder();
    const auto* node = graph.GetNode(node_indices_in_order[0]);
    auto _first = node->Name().find('_') + 1;
    auto _second = node->Name().find('_', _first);
    subgraph_name = node->Name().substr(_first, _second - _first);
  };
  EPVerificationParams verification_params{.graph_verifier = &graph_verifier};

  auto* metadata_props = model.add_metadata_props();
  metadata_props->set_key(kCOREML_CACHE_KEY);
  {  // test with valid model cache directory
    metadata_props->set_value("legalhash123");
    model.SerializeToString(&out_string);
    gsl::span<const std::byte> model_data{reinterpret_cast<const std::byte*>(out_string.data()), out_string.size()};
    RunAndVerifyOutputsWithEP(model_data, CurrentTestName(),
                              MakeCoreMLExecutionProvider("MLProgram", "CPUOnly", ORT_TSTR("./tmp/")),
                              feeds,
                              verification_params);
    ASSERT_EQ(std::filesystem::exists("./tmp/legalhash123"), true);
  }
  {
    // test with invalid model cache directory, only alphanumeric characters are allowed
    out_string.clear();
    metadata_props->set_key(kCOREML_CACHE_KEY);
    metadata_props->set_value("illegalhash__123");
    model.SerializeToString(&out_string);
    gsl::span<const std::byte> model_data{reinterpret_cast<const std::byte*>(out_string.data()), out_string.size()};
    RunAndVerifyOutputsWithEP(model_data, CurrentTestName(),
                              MakeCoreMLExecutionProvider("MLProgram", "CPUOnly", ORT_TSTR("./tmp")),
                              feeds,
                              verification_params);
    ASSERT_EQ(std::filesystem::exists("./tmp/illegalhash__123"), false);
    // the cache folder name should be the first part of the subgraph name
    ASSERT_EQ(std::filesystem::exists("./tmp/" + subgraph_name), true);
  }
  {
    // test with invalid model cache directory,  more than 64 characters
    out_string.clear();
    metadata_props->set_key(kCOREML_CACHE_KEY);
    metadata_props->set_value("modelhashwithmorethan64charactersmodelhashwithmorethan64charactersmodelhashwithmorethan64characters");
    model.SerializeToString(&out_string);
    gsl::span<const std::byte> model_data{reinterpret_cast<const std::byte*>(out_string.data()), out_string.size()};
    RunAndVerifyOutputsWithEP(model_data, CurrentTestName(),
                              MakeCoreMLExecutionProvider("MLProgram", "CPUOnly", ORT_TSTR("./tmp")),
                              feeds,
                              verification_params);
    ASSERT_EQ(std::filesystem::exists("./tmp/modelhashwithmorethan64charactersmodelhashwithmorethan64charactersmodelhashwithmorethan64characters"), false);
    // the cache folder name should be the first part of the subgraph name
    ASSERT_EQ(std::filesystem::exists("./tmp/" + subgraph_name), true);
  }
  {
    // test with invalid model cache directory,  empty
    out_string.clear();
    metadata_props->set_key(kCOREML_CACHE_KEY);
    metadata_props->set_value("");
    model.SerializeToString(&out_string);
    gsl::span<const std::byte> model_data{reinterpret_cast<const std::byte*>(out_string.data()), out_string.size()};
    RunAndVerifyOutputsWithEP(model_data, CurrentTestName(),
                              MakeCoreMLExecutionProvider("MLProgram", "CPUOnly", ORT_TSTR("./tmp")),
                              feeds,
                              verification_params);
    // the cache folder name should be the first part of the subgraph name
    ASSERT_EQ(std::filesystem::exists("./tmp/" + subgraph_name), true);
  }
  {
    // test with invalid model cache directory, caching shall be disabled
    out_string.clear();
    metadata_props->set_key(kCOREML_CACHE_KEY);
    metadata_props->set_value("");
    model.SerializeToString(&out_string);
    gsl::span<const std::byte> model_data{reinterpret_cast<const std::byte*>(out_string.data()), out_string.size()};
    RunAndVerifyOutputsWithEP(model_data, CurrentTestName(),
                              MakeCoreMLExecutionProvider("MLProgram", "CPUOnly", ORT_TSTR("/")),
                              feeds,
                              verification_params);
    // this folder can't be created
    ASSERT_EQ(std::filesystem::exists("/" + subgraph_name), false);
  }
#else
  model.SerializeToString(&out_string);
  gsl::span<const std::byte> model_data{reinterpret_cast<const std::byte*>(out_string.data()), out_string.size()};
  TestModelLoad(model_data, MakeCoreMLExecutionProvider(), ExpectedEPNodeAssignment::All);
#endif
}
}  // namespace test
}  // namespace onnxruntime
