// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/cpu/cpu_provider_factory.h"

#include <memory>

#include "core/providers/cpu/cpu_execution_provider.h"
#include "core/providers/cpu/cpu_provider_factory_creator.h"
#include "core/session/abi_session_options_impl.h"
#include "core/session/ort_apis.h"

namespace onnxruntime {

struct CpuProviderFactory : IExecutionProviderFactory {
  CpuProviderFactory(bool create_arena) : create_arena_(create_arena) {}
  ~CpuProviderFactory() override = default;
  std::unique_ptr<IExecutionProvider> CreateProvider() override;
  std::unique_ptr<IExecutionProvider> CreateProvider(const OrtSessionOptions& session_options,
                                                     const OrtLogger& session_logger) override;

 private:
  bool create_arena_;
};

std::unique_ptr<IExecutionProvider> CpuProviderFactory::CreateProvider() {
  CPUExecutionProviderInfo info;
  info.create_arena = create_arena_;
  return std::make_unique<CPUExecutionProvider>(info);
}

std::unique_ptr<IExecutionProvider> CpuProviderFactory::CreateProvider(const OrtSessionOptions& session_options,
                                                                       const OrtLogger& session_logger) {
  CPUExecutionProviderInfo info;
  info.create_arena = session_options.value.enable_cpu_mem_arena;

  auto cpu_ep = std::make_unique<CPUExecutionProvider>(info);
  cpu_ep->SetLogger(reinterpret_cast<const logging::Logger*>(&session_logger));
  return cpu_ep;
}

std::shared_ptr<IExecutionProviderFactory> CPUProviderFactoryCreator::Create(int use_arena) {
  return std::make_shared<onnxruntime::CpuProviderFactory>(use_arena != 0);
}

}  // namespace onnxruntime

ORT_API_STATUS_IMPL(OrtSessionOptionsAppendExecutionProvider_CPU, _In_ OrtSessionOptions* options, int use_arena) {
  options->provider_factories.push_back(onnxruntime::CPUProviderFactoryCreator::Create(use_arena));
  return nullptr;
}
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(disable : 26409)
#endif
ORT_API_STATUS_IMPL(OrtApis::CreateCpuMemoryInfo, enum OrtAllocatorType type, enum OrtMemType mem_type,
                    _Outptr_ OrtMemoryInfo** out) {
  *out = new OrtMemoryInfo(onnxruntime::CPU, type, OrtDevice(), mem_type);
  return nullptr;
}
