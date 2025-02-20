// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <stdint.h>
#include <cuda_fp16.h>
#include <curand_kernel.h>
#include <cstdio>
#include "contrib_ops/cpu/transformers/generation_shared.h"

namespace onnxruntime {
namespace contrib {
namespace cuda {

void LaunchInitKernel(
    float* beam_scores,
    int batch_size,
    int num_beams,
    cudaStream_t stream);

template <typename T>
void LaunchAddProbsKernel(T* log_probs,
                          T* cum_log_probs,
                          const int batch_size,
                          const int num_beams,
                          const int vocab_size,
                          cudaStream_t stream);

template <typename T>
void LaunchLogitsProcessKernel(
    T* next_token_scores,
    const int* vocab_mask,
    const int* prefix_vocab_mask,
    int* presence_mask,
    float presence_penalty,
    float temperature,
    int batch_size,
    int num_beams,
    int vocab_size,
    int padded_vocab_size,
    int demote_token_id,
    const int32_t* sequences,
    int max_sequence_length,
    int current_sequence_length,
    float repetition_penalty,
    int no_repeat_ngram_size,
    cudaStream_t stream);

struct HypothesisScore {
  const int32_t* hypothesis;
  int hypothesis_length;
  float score;

#ifdef DEBUG_GENERATION
  __device__ void Print() const {
    printf("HypothesisScore (hypothesis_length=%d, score=%f) \n", hypothesis_length, score);
    printf("  hypothesis:");
    if (hypothesis_length > 0 && hypothesis != NULL) {
      for (int i = 0; i < hypothesis_length; ++i) {
        printf("%d ", hypothesis[i]);
      }
    } else {
      printf("(empty)");
    }
    printf("\n");
  }
#endif
};

struct BeamHypotheses {
  HypothesisScore* beams_;  // Beam width sized array of hypotheses, sorted by highest scoring
  int beams_count_;
  int beams_used_;  // Number of elements used in beams_
  float length_penalty_;
  bool done_;

  // Add a new hypothesis
  __device__ void Add(const int32_t* hypothesis, int hypothesis_length, float sum_logprobs);

  // Return true if this beats the worst score in the hypothesis
  __device__ bool CanImprove(float best_sum_logprobs, int current_length) const;

  // Output results
  template <typename T>
  __device__ void Output(int top_k,             // number of sequences to return
                         int max_length,        // max sequence length
                         int pad_token_id,      // pad token
                         int32_t* sequences,    // buffer with pad token, shape (num_return_sequences, max_length)
                         T* sequences_scores);  // buffer for sequence scores, with shape (num_return_sequences)

#ifdef DEBUG_GENERATION
  __device__ void Print() const {
    printf("BeamHypotheses:\n");
    printf("  beams_count: %d\n", beams_count_);
    printf("  beams_used: %d\n", beams_used_);
    printf("  length_penalty: %f\n", length_penalty_);
    printf("  done: %s\n", done_ ? "true" : "false");

    printf("  beams:\n");
    for (int i = 0; i < beams_used_; ++i) {
      printf("    Beam %d:\n", i + 1);
      beams_[i].Print();
    }
  }
#endif
};

struct BeamScorerState {
  int batch_size_;
  int num_beams_;
  int max_length_;
  int num_return_sequences_;
  int pad_token_id_;
  int eos_token_id_;
  bool early_stopping_;
  int not_done_count_;          // When zero, every batch entry is done (starts at batch_size_)
  int hypothesis_buffer_used_;  // Offset of available buffer, or length of used buffer.

#ifdef DEBUG_GENERATION
  __host__ __device__ void Print(bool is_cpu) const {
    printf("BeamScorerState (cpu=%d) Dump:\n", is_cpu ? 1 : 0);
    printf("  batch_size_: %d\n", batch_size_);
    printf("  num_beams_: %d\n", num_beams_);
    printf("  max_length_: %d\n", max_length_);
    printf("  num_return_sequences_: %d\n", num_return_sequences_);
    printf("  pad_token_id_: %d\n", pad_token_id_);
    printf("  eos_token_id_: %d\n", eos_token_id_);
    printf("  early_stopping_: %s\n", early_stopping_ ? "true" : "false");
    printf("  not_done_count_: %d\n", not_done_count_);
    printf("  hypothesis_buffer_used_: %d\n", hypothesis_buffer_used_);
  }
#endif
};

void LaunchInitializeBeamHypotheses(gsl::span<BeamHypotheses> beam_hyps, float length_penalty, gsl::span<HypothesisScore> beams, int num_beams, cudaStream_t stream);

void LaunchBeamSearchScorer_Process(BeamScorerState& state_cpu,
                                    BeamScorerState& state,
                                    gsl::span<const int32_t> sequences,
                                    int sequence_length,
                                    gsl::span<BeamHypotheses> beam_hyps_,
                                    gsl::span<float> next_beam_scores_,
                                    gsl::span<int32_t> next_beam_tokens_,
                                    gsl::span<int32_t> next_beam_indices_,
                                    gsl::span<int32_t> hypothesis_buffer_,
                                    gsl::span<const float> next_scores,
                                    gsl::span<const int32_t> next_tokens,
                                    gsl::span<const int32_t> next_indices,
                                    cudaStream_t stream);

void LaunchBeamSearchScorer_AppendNextTokenToSequences(BeamScorerState& state_cpu,
                                                       BeamScorerState& state,
                                                       gsl::span<const int32_t> sequences,
                                                       gsl::span<int32_t> next_sequences,
                                                       int sequence_length,
                                                       gsl::span<int32_t> next_beam_tokens,
                                                       gsl::span<int32_t> next_beam_indices,
                                                       cudaStream_t stream);

template <typename T>
void LaunchBeamSearchScorer_Finalize(int batch_size,
                                     BeamScorerState& state,
                                     gsl::span<const int32_t> sequences,
                                     int sequence_length,
                                     gsl::span<BeamHypotheses> beam_hyps_,
                                     gsl::span<const float> final_beam_scores,
                                     gsl::span<int32_t> output,
                                     gsl::span<T> sequence_scores,
                                     cudaStream_t stream);

template <typename T>
void LaunchBeamSearchScoreCopy(gsl::span<const float> final_scores,
                               gsl::span<T> output_scores,
                               cudaStream_t stream);

void LaunchNextTokenKernel(const int64_t* next_token_indices,
                           int32_t* next_indices,
                           int32_t* next_tokens,
                           int batch_size,
                           int top_k,
                           int vocab_size,
                           cudaStream_t stream);

void LaunchUpdateGptKernel(const int32_t* old_mask_data,
                           int32_t* mask_data,
                           int32_t* next_positions,
                           int batch_beam_size,
                           int current_length,
                           cudaStream_t stream);

template <typename T>
void GetTempStorageSize(const T* d_keys_in,
                        const int* d_values_in,
                        int* d_offsets,
                        int num_items,
                        int num_segments,
                        cudaStream_t stream,
                        bool is_descending,
                        size_t& temp_storage_bytes);

void LaunchSetupParamsKernel(int* d_values_in,
                             int* d_offsets,
                             int batch_size,
                             int vocab_size,
                             cudaStream_t stream);

template <typename T>
void LaunchSortPairs(void* d_temp_storage,
                     size_t temp_storage_bytes,
                     const T* d_keys_in,
                     T* d_keys_out,
                     const int* d_values_in,
                     int* d_values_out,
                     int num_items,
                     int num_segments,
                     int* d_offsets,
                     cudaStream_t stream,
                     bool is_descending);

template <typename T>
void LaunchFilterLogitsKernel(float* d_sorted_logits_in,
                              const int* d_sorted_indices,
                              T* d_logits_in_out,
                              float top_p,
                              float filter_value,
                              int min_tokens_to_keep,
                              int batch_size,
                              int vocab_size,
                              cudaStream_t stream,
                              bool is_descending);

void TorchMultinomialKernelLauncher(float* d_input,
                                    float* d_sampled,
                                    int32_t* d_output,
                                    int batch_size,
                                    int vocab_size,
                                    int* d_presence_mask,
                                    cudaStream_t stream);

void UpdateDecoderMaskedMultiHeadAttentionCacheIndirection(int32_t* tgt_indir_cache,
                                                           const int32_t* src_indir_cache,
                                                           const int32_t* beam_ids,
                                                           int batch_size,
                                                           int beam_width,
                                                           int input_seq_length,
                                                           int max_seq_length,
                                                           int current_length,
                                                           cudaStream_t stream);

template <typename T>
void KeyCacheExpansionKernelLauncher(const T* key_cache,
                                     T* key_cache_expanded,
                                     int batch_size,
                                     int beam_width,
                                     int num_heads,
                                     int sequence_length,
                                     int max_seq_length,
                                     int head_size,
                                     cudaStream_t stream);

template <typename T>
void BufferExpansionKernelLauncher(const T* input,
                                   T* output,
                                   int batch_size,
                                   int beam_width,
                                   int chunk_size,
                                   cudaStream_t stream);

void ReorderPastStatesKernelLauncher(void* out_buffer,
                                     const void* in_buffer,
                                     int batch_size,
                                     int num_heads,
                                     int max_length,
                                     int head_size,
                                     int chunk_size,
                                     cudaStream_t stream);

void LaunchCopyCrossQKSingleDecodeStep(
    cudaStream_t stream,
    float* cross_qk_buffer_data,
    float** qk_layer_pointers,
    int token_index,
    int batchxbeam,
    int num_layers,
    int num_heads,
    int cross_qk_layer_head_pair_count,
    const int* cross_qk_layer_head_pairs,
    int frames,
    int max_length);

void LaunchFinalizeCrossQK(
    cudaStream_t stream,
    int iteration_number,
    int context_decoding_len,
    int batch_size,
    int num_beams,
    int max_length,
    int cross_qk_layer_head_pair_count,
    const int* cross_qk_layer_head_pairs,
    int frames_of_k,
    const float* cross_qk_buffer_data,
    float* cross_qk_output,
    int num_return_sequences,
    const int* cache_indir_data,
    const int32_t* beam_indices);

void LaunchForceDecodingIds(
    float* beam_scores,
    const int batch_size,
    const int num_beams,
    const int vocab_size,
    const int32_t* force_ids,
    int id_len,
    int step,
    cudaStream_t stream);

template <typename T>
void LaunchSaveNoSpeechProbs(
    T* result_no_speech_probs, /* [batch]*/
    const float* probs,        /* [batch, num_beams, vocab_size]*/
    const int batch_size,
    const int num_beams,
    const int vocab_size,
    const int no_speech_token_id,
    cudaStream_t stream);

}  // namespace cuda
}  // namespace contrib
}  // namespace onnxruntime
