# llama.cpp server and DLL build (CPU inference only)
#
# llama.cpp is an amazing project, but its build system is poor and
# growing worse. It's never properly built llama.dll under any compiler,
# and DLL builds have been unsupported by w64dk for some time. This
# makefile is a replacement build system that produces llama.dll and
# llama-server.exe using w64dk. No source file changes are needed.
#
# The DLL exports the public API and no more, and is readily usable as a
# component in another project (game engine, etc.). The server EXE is
# fully functional on Windows 7 or later. It is not linked against the
# DLL, since that's not useful, but can be made to do so with a small
# tweak to this makefile.
#
# Invoke this makefile in the llama.cpp source tree:
#
#   $ make -j$(nproc) -f path/to/w64devkit/contrib/llama.mak
#
# Incremental builds are unsupported, so clean rebuild after pulling. It
# was last tested at b4404, and an update will inevitably break it.

CROSS    =
CPPFLAGS = -w -O2
LDFLAGS  = -s

.SUFFIXES: .c .cpp .o
def = -DGGML_USE_CPU
inc = -I. -Icommon -Iinclude -Iggml/include -Iggml/src -Iggml/src/ggml-cpu
%.c.o: %.c
	$(CROSS)gcc -c -Wa,-mbig-obj -o $@ $(inc) $(def) $(CPPFLAGS) $<
%.cpp.o: %.cpp
	$(CROSS)g++ -c -Wa,-mbig-obj -o $@ $(inc) $(def) $(CPPFLAGS) $<

dll = \
  ggml/src/ggml-alloc.c.o \
  ggml/src/ggml-backend-reg.cpp.o \
  ggml/src/ggml-backend.cpp.o \
  ggml/src/ggml-cpu/ggml-cpu-aarch64.cpp.o \
  ggml/src/ggml-cpu/ggml-cpu-quants.c.o \
  ggml/src/ggml-cpu/ggml-cpu-traits.cpp.o \
  ggml/src/ggml-cpu/ggml-cpu.c.o \
  ggml/src/ggml-cpu/ggml-cpu.cpp.o \
  ggml/src/ggml-cpu/llamafile/sgemm.cpp.o \
  ggml/src/ggml-opt.cpp.o \
  ggml/src/ggml-quants.c.o \
  ggml/src/ggml-threading.cpp.o \
  ggml/src/ggml.c.o \
  src/llama-grammar.cpp.o \
  src/llama-sampling.cpp.o \
  src/llama-vocab.cpp.o \
  src/llama.cpp.o \
  src/unicode-data.cpp.o \
  src/unicode.cpp.o

exe = \
  common/arg.cpp.o \
  common/w64dk-build-info.cpp.o \
  common/common.cpp.o \
  common/console.cpp.o \
  common/json-schema-to-grammar.cpp.o \
  common/log.cpp.o \
  common/ngram-cache.cpp.o \
  common/sampling.cpp.o \
  common/speculative.cpp.o \
  examples/server/server.cpp.o

all: llama.dll llama-server.exe

llama-server.exe: $(exe) $(dll)
	$(CROSS)g++ $(LDFLAGS) -o $@ $(exe) $(dll) -lws2_32

llama.dll: $(dll) llama.def
	$(CROSS)g++ -shared $(LDFLAGS) -o $@ $(dll) llama.def

clean:
	rm -f $(dll) $(exe) llama.def llama.dll llama-server.exe \
	   examples/server/index.html.gz.hpp examples/server/loading.html.hpp \
	   common/w64dk-build-info.cpp

common/arg.cpp.o: common/arg.cpp
common/common.cpp.o: common/common.cpp
common/console.cpp.o: common/console.cpp
common/json-schema-to-grammar.cpp.o: common/json-schema-to-grammar.cpp
common/log.cpp.o: common/log.cpp
common/ngram-cache.cpp.o: common/ngram-cache.cpp
common/sampling.cpp.o: common/sampling.cpp
common/speculative.cpp.o: common/speculative.cpp
ggml/src/ggml-alloc.c.o: ggml/src/ggml-alloc.c
ggml/src/ggml-backend-reg.cpp.o: ggml/src/ggml-backend-reg.cpp
ggml/src/ggml-backend.cpp.o: ggml/src/ggml-backend.cpp
ggml/src/ggml-cpu/ggml-cpu-aarch64.cpp.o: ggml/src/ggml-cpu/ggml-cpu-aarch64.cpp
ggml/src/ggml-cpu/ggml-cpu-quants.c.o: ggml/src/ggml-cpu/ggml-cpu-quants.c
ggml/src/ggml-cpu/ggml-cpu-traits.c.o: ggml/src/ggml-cpu/ggml-cpu-traits.c
ggml/src/ggml-cpu/ggml-cpu.c.o: ggml/src/ggml-cpu/ggml-cpu.c
ggml/src/ggml-cpu/ggml-cpu.cpp.o: ggml/src/ggml-cpu/ggml-cpu.cpp
ggml/src/ggml-cpu/llamafile/sgemm.cpp.o: ggml/src/ggml-cpu/llamafile/sgemm.cpp
ggml/src/ggml-opt.cpp.o: ggml/src/ggml-opt.cpp
ggml/src/ggml-quants.c.o: ggml/src/ggml-quants.c
ggml/src/ggml-threading.cpp.o: ggml/src/ggml-threading.cpp
ggml/src/ggml.c.o: ggml/src/ggml.c
src/llama-grammar.cpp.o: src/llama-grammar.cpp
src/llama-sampling.cpp.o: src/llama-sampling.cpp
src/llama-vocab.cpp.o: src/llama-vocab.cpp
src/llama.cpp.o: src/llama.cpp
src/unicode-data.cpp.o: src/unicode-data.cpp
src/unicode.cpp.o: src/unicode.cpp

.ONESHELL:  # needed for heredocs

# NOTE: produces valid C++ even if Git is unavailable
common/w64dk-build-info.cpp:
	cat >$@ <<EOF
	int         LLAMA_BUILD_NUMBER = {$$(git rev-list  --count HEAD)};
	char const *LLAMA_COMMIT       = "$$(git rev-parse --short HEAD)";
	char const *LLAMA_COMPILER     = "gcc (GCC) $$(gcc -dumpversion)";
	char const *LLAMA_BUILD_TARGET = "$$(gcc -dumpmachine)";
	EOF

common/w64dk-build-info.cpp.o: common/w64dk-build-info.cpp

examples/server/index.html.gz.hpp: examples/server/public/index.html.gz
	cd examples/server/public/ && xxd -i index.html.gz >../index.html.gz.hpp
examples/server/loading.html.hpp: examples/server/public/loading.html
	cd examples/server/public/ && xxd -i loading.html >../loading.html.hpp
examples/server/server.cpp.o: \
  examples/server/server.cpp \
  examples/server/index.html.gz.hpp \
  examples/server/loading.html.hpp

llama.def:
	@cat >$@ <<EOF
	LIBRARY llama
	EXPORTS
	ggml_abort
	ggml_abs
	ggml_abs_inplace
	ggml_acc
	ggml_acc_inplace
	ggml_add
	ggml_add1
	ggml_add1_inplace
	ggml_add_cast
	ggml_add_inplace
	ggml_add_rel_pos
	ggml_add_rel_pos_inplace
	ggml_arange
	ggml_are_same_shape
	ggml_are_same_stride
	ggml_argmax
	ggml_argsort
	ggml_backend_alloc_buffer
	ggml_backend_alloc_ctx_tensors
	ggml_backend_alloc_ctx_tensors_from_buft
	ggml_backend_buffer_clear
	ggml_backend_buffer_free
	ggml_backend_buffer_get_alignment
	ggml_backend_buffer_get_alloc_size
	ggml_backend_buffer_get_base
	ggml_backend_buffer_get_max_size
	ggml_backend_buffer_get_size
	ggml_backend_buffer_get_type
	ggml_backend_buffer_get_usage
	ggml_backend_buffer_init_tensor
	ggml_backend_buffer_is_host
	ggml_backend_buffer_name
	ggml_backend_buffer_reset
	ggml_backend_buffer_set_usage
	ggml_backend_buft_alloc_buffer
	ggml_backend_buft_get_alignment
	ggml_backend_buft_get_alloc_size
	ggml_backend_buft_get_device
	ggml_backend_buft_get_max_size
	ggml_backend_buft_is_host
	ggml_backend_buft_name
	ggml_backend_compare_graph_backend
	ggml_backend_cpu_buffer_from_ptr
	ggml_backend_cpu_buffer_type
	ggml_backend_dev_backend_reg
	ggml_backend_dev_buffer_from_host_ptr
	ggml_backend_dev_buffer_type
	ggml_backend_dev_by_name
	ggml_backend_dev_by_type
	ggml_backend_dev_count
	ggml_backend_dev_description
	ggml_backend_dev_get
	ggml_backend_dev_get_props
	ggml_backend_dev_host_buffer_type
	ggml_backend_dev_init
	ggml_backend_dev_memory
	ggml_backend_dev_name
	ggml_backend_dev_offload_op
	ggml_backend_dev_supports_buft
	ggml_backend_dev_supports_op
	ggml_backend_dev_type
	ggml_backend_event_free
	ggml_backend_event_new
	ggml_backend_event_record
	ggml_backend_event_synchronize
	ggml_backend_event_wait
	ggml_backend_free
	ggml_backend_get_alignment
	ggml_backend_get_default_buffer_type
	ggml_backend_get_device
	ggml_backend_get_max_size
	ggml_backend_graph_compute
	ggml_backend_graph_compute_async
	ggml_backend_graph_copy
	ggml_backend_graph_copy_free
	ggml_backend_graph_plan_compute
	ggml_backend_graph_plan_create
	ggml_backend_graph_plan_free
	ggml_backend_guid
	ggml_backend_init_best
	ggml_backend_init_by_name
	ggml_backend_init_by_type
	ggml_backend_load
	ggml_backend_load_all
	ggml_backend_name
	ggml_backend_offload_op
	ggml_backend_reg_by_name
	ggml_backend_reg_count
	ggml_backend_reg_dev_count
	ggml_backend_reg_dev_get
	ggml_backend_reg_get
	ggml_backend_reg_get_proc_address
	ggml_backend_reg_name
	ggml_backend_sched_alloc_graph
	ggml_backend_sched_free
	ggml_backend_sched_get_backend
	ggml_backend_sched_get_buffer_size
	ggml_backend_sched_get_n_backends
	ggml_backend_sched_get_n_copies
	ggml_backend_sched_get_n_splits
	ggml_backend_sched_get_tensor_backend
	ggml_backend_sched_graph_compute
	ggml_backend_sched_graph_compute_async
	ggml_backend_sched_new
	ggml_backend_sched_reserve
	ggml_backend_sched_reset
	ggml_backend_sched_set_eval_callback
	ggml_backend_sched_set_tensor_backend
	ggml_backend_sched_synchronize
	ggml_backend_supports_buft
	ggml_backend_supports_op
	ggml_backend_synchronize
	ggml_backend_tensor_alloc
	ggml_backend_tensor_copy
	ggml_backend_tensor_copy_async
	ggml_backend_tensor_get
	ggml_backend_tensor_get_async
	ggml_backend_tensor_memset
	ggml_backend_tensor_set
	ggml_backend_tensor_set_async
	ggml_backend_unload
	ggml_backend_view_init
	ggml_bf16_to_fp32
	ggml_bf16_to_fp32_row
	ggml_blck_size
	ggml_build_backward_expand
	ggml_build_forward_expand
	ggml_can_repeat
	ggml_cast
	ggml_clamp
	ggml_concat
	ggml_cont
	ggml_cont_1d
	ggml_cont_2d
	ggml_cont_3d
	ggml_cont_4d
	ggml_conv_1d
	ggml_conv_1d_ph
	ggml_conv_2d
	ggml_conv_2d_s1_ph
	ggml_conv_2d_sk_p0
	ggml_conv_transpose_1d
	ggml_conv_transpose_2d_p0
	ggml_cos
	ggml_cos_inplace
	ggml_count_equal
	ggml_cpy
	ggml_cross_entropy_loss
	ggml_cross_entropy_loss_back
	ggml_cycles
	ggml_cycles_per_ms
	ggml_diag
	ggml_diag_mask_inf
	ggml_diag_mask_inf_inplace
	ggml_diag_mask_zero
	ggml_diag_mask_zero_inplace
	ggml_div
	ggml_div_inplace
	ggml_dup
	ggml_dup_inplace
	ggml_dup_tensor
	ggml_element_size
	ggml_elu
	ggml_elu_inplace
	ggml_exp
	ggml_exp_inplace
	ggml_flash_attn_back
	ggml_flash_attn_ext
	ggml_flash_attn_ext_get_prec
	ggml_flash_attn_ext_set_prec
	ggml_fopen
	ggml_format_name
	ggml_fp16_to_fp32
	ggml_fp16_to_fp32_row
	ggml_fp32_to_bf16
	ggml_fp32_to_bf16_row
	ggml_fp32_to_bf16_row_ref
	ggml_fp32_to_fp16
	ggml_fp32_to_fp16_row
	ggml_free
	ggml_ftype_to_ggml_type
	ggml_gallocr_alloc_graph
	ggml_gallocr_free
	ggml_gallocr_get_buffer_size
	ggml_gallocr_new
	ggml_gallocr_new_n
	ggml_gallocr_reserve
	ggml_gallocr_reserve_n
	ggml_gelu
	ggml_gelu_inplace
	ggml_gelu_quick
	ggml_gelu_quick_inplace
	ggml_get_data
	ggml_get_data_f32
	ggml_get_first_tensor
	ggml_get_max_tensor_size
	ggml_get_mem_buffer
	ggml_get_mem_size
	ggml_get_name
	ggml_get_next_tensor
	ggml_get_no_alloc
	ggml_get_rel_pos
	ggml_get_rows
	ggml_get_rows_back
	ggml_get_tensor
	ggml_get_type_traits
	ggml_get_unary_op
	ggml_graph_add_node
	ggml_graph_clear
	ggml_graph_cpy
	ggml_graph_dump_dot
	ggml_graph_dup
	ggml_graph_get_grad
	ggml_graph_get_grad_acc
	ggml_graph_get_tensor
	ggml_graph_n_nodes
	ggml_graph_node
	ggml_graph_nodes
	ggml_graph_overhead
	ggml_graph_overhead_custom
	ggml_graph_print
	ggml_graph_reset
	ggml_graph_size
	ggml_group_norm
	ggml_group_norm_inplace
	ggml_guid_matches
	ggml_hardsigmoid
	ggml_hardswish
	ggml_im2col
	ggml_im2col_back
	ggml_init
	ggml_is_3d
	ggml_is_contiguous
	ggml_is_contiguous_0
	ggml_is_contiguous_1
	ggml_is_contiguous_2
	ggml_is_empty
	ggml_is_matrix
	ggml_is_permuted
	ggml_is_quantized
	ggml_is_scalar
	ggml_is_transposed
	ggml_is_vector
	ggml_leaky_relu
	ggml_log
	ggml_log_inplace
	ggml_log_set
	ggml_map_binary_f32
	ggml_map_binary_inplace_f32
	ggml_map_custom1
	ggml_map_custom1_f32
	ggml_map_custom1_inplace
	ggml_map_custom1_inplace_f32
	ggml_map_custom2
	ggml_map_custom2_f32
	ggml_map_custom2_inplace
	ggml_map_custom2_inplace_f32
	ggml_map_custom3
	ggml_map_custom3_f32
	ggml_map_custom3_inplace
	ggml_map_custom3_inplace_f32
	ggml_map_unary_f32
	ggml_map_unary_inplace_f32
	ggml_mean
	ggml_mul
	ggml_mul_inplace
	ggml_mul_mat
	ggml_mul_mat_id
	ggml_mul_mat_set_prec
	ggml_n_dims
	ggml_nbytes
	ggml_nbytes_pad
	ggml_neg
	ggml_neg_inplace
	ggml_nelements
	ggml_new_buffer
	ggml_new_graph
	ggml_new_graph_custom
	ggml_new_tensor
	ggml_new_tensor_1d
	ggml_new_tensor_2d
	ggml_new_tensor_3d
	ggml_new_tensor_4d
	ggml_norm
	ggml_norm_inplace
	ggml_nrows
	ggml_op_desc
	ggml_op_name
	ggml_op_symbol
	ggml_opt_dataset_data
	ggml_opt_dataset_free
	ggml_opt_dataset_get_batch
	ggml_opt_dataset_init
	ggml_opt_dataset_labels
	ggml_opt_dataset_shuffle
	ggml_opt_default_params
	ggml_opt_epoch
	ggml_opt_epoch_callback_progress_bar
	ggml_opt_fit
	ggml_opt_forward
	ggml_opt_forward_backward
	ggml_opt_free
	ggml_opt_get_default_optimizer_params
	ggml_opt_grad_acc
	ggml_opt_init
	ggml_opt_inputs
	ggml_opt_labels
	ggml_opt_loss
	ggml_opt_ncorrect
	ggml_opt_outputs
	ggml_opt_pred
	ggml_opt_reset
	ggml_opt_result_accuracy
	ggml_opt_result_free
	ggml_opt_result_init
	ggml_opt_result_loss
	ggml_opt_result_ndata
	ggml_opt_result_pred
	ggml_opt_result_reset
	ggml_opt_step_adamw
	ggml_out_prod
	ggml_pad
	ggml_pad_reflect_1d
	ggml_permute
	ggml_pool_1d
	ggml_pool_2d
	ggml_pool_2d_back
	ggml_print_object
	ggml_print_objects
	ggml_quantize_chunk
	ggml_quantize_free
	ggml_quantize_init
	ggml_quantize_requires_imatrix
	ggml_relu
	ggml_relu_inplace
	ggml_repeat
	ggml_repeat_back
	ggml_reset
	ggml_reshape
	ggml_reshape_1d
	ggml_reshape_2d
	ggml_reshape_3d
	ggml_reshape_4d
	ggml_rms_norm
	ggml_rms_norm_back
	ggml_rms_norm_inplace
	ggml_rope
	ggml_rope_back
	ggml_rope_custom
	ggml_rope_custom_inplace
	ggml_rope_ext
	ggml_rope_ext_inplace
	ggml_rope_inplace
	ggml_rope_yarn_corr_dims
	ggml_row_size
	ggml_rwkv_wkv6
	ggml_scale
	ggml_scale_inplace
	ggml_set
	ggml_set_1d
	ggml_set_1d_inplace
	ggml_set_2d
	ggml_set_2d_inplace
	ggml_set_inplace
	ggml_set_input
	ggml_set_loss
	ggml_set_name
	ggml_set_no_alloc
	ggml_set_output
	ggml_set_param
	ggml_set_zero
	ggml_sgn
	ggml_sgn_inplace
	ggml_sigmoid
	ggml_sigmoid_inplace
	ggml_silu
	ggml_silu_back
	ggml_silu_inplace
	ggml_sin
	ggml_sin_inplace
	ggml_soft_max
	ggml_soft_max_back
	ggml_soft_max_back_inplace
	ggml_soft_max_ext
	ggml_soft_max_inplace
	ggml_sqr
	ggml_sqr_inplace
	ggml_sqrt
	ggml_sqrt_inplace
	ggml_ssm_conv
	ggml_ssm_scan
	ggml_status_to_string
	ggml_step
	ggml_step_inplace
	ggml_sub
	ggml_sub_inplace
	ggml_sum
	ggml_sum_rows
	ggml_tallocr_alloc
	ggml_tallocr_new
	ggml_tanh
	ggml_tanh_inplace
	ggml_tensor_overhead
	ggml_threadpool_params_default
	ggml_threadpool_params_init
	ggml_threadpool_params_match
	ggml_time_init
	ggml_time_ms
	ggml_time_us
	ggml_timestep_embedding
	ggml_top_k
	ggml_transpose
	ggml_type_name
	ggml_type_size
	ggml_type_sizef
	ggml_unary
	ggml_unary_inplace
	ggml_unary_op_name
	ggml_unravel_index
	ggml_upscale
	ggml_upscale_ext
	ggml_used_mem
	ggml_validate_row_data
	ggml_view_1d
	ggml_view_2d
	ggml_view_3d
	ggml_view_4d
	ggml_view_tensor
	ggml_win_part
	ggml_win_unpart
	gguf_add_tensor
	gguf_find_key
	gguf_find_tensor
	gguf_free
	gguf_get_alignment
	gguf_get_arr_data
	gguf_get_arr_n
	gguf_get_arr_str
	gguf_get_arr_type
	gguf_get_data
	gguf_get_data_offset
	gguf_get_key
	gguf_get_kv_type
	gguf_get_meta_data
	gguf_get_meta_size
	gguf_get_n_kv
	gguf_get_n_tensors
	gguf_get_tensor_name
	gguf_get_tensor_offset
	gguf_get_tensor_type
	gguf_get_val_bool
	gguf_get_val_data
	gguf_get_val_f32
	gguf_get_val_f64
	gguf_get_val_i16
	gguf_get_val_i32
	gguf_get_val_i64
	gguf_get_val_i8
	gguf_get_val_str
	gguf_get_val_u16
	gguf_get_val_u32
	gguf_get_val_u64
	gguf_get_val_u8
	gguf_get_version
	gguf_init_empty
	gguf_init_from_file
	gguf_remove_key
	gguf_set_arr_data
	gguf_set_arr_str
	gguf_set_kv
	gguf_set_tensor_data
	gguf_set_tensor_type
	gguf_set_val_bool
	gguf_set_val_f32
	gguf_set_val_f64
	gguf_set_val_i16
	gguf_set_val_i32
	gguf_set_val_i64
	gguf_set_val_i8
	gguf_set_val_str
	gguf_set_val_u16
	gguf_set_val_u32
	gguf_set_val_u64
	gguf_set_val_u8
	gguf_type_name
	gguf_write_to_file
	llama_add_bos_token
	llama_add_eos_token
	llama_attach_threadpool
	llama_backend_free
	llama_backend_init
	llama_batch_free
	llama_batch_get_one
	llama_batch_init
	llama_chat_apply_template
	llama_chat_builtin_templates
	llama_context_default_params
	llama_control_vector_apply
	llama_copy_state_data
	llama_decode
	llama_detach_threadpool
	llama_detokenize
	llama_encode
	llama_free
	llama_free_model
	llama_get_embeddings
	llama_get_embeddings_ith
	llama_get_embeddings_seq
	llama_get_kv_cache_token_count
	llama_get_kv_cache_used_cells
	llama_get_logits
	llama_get_logits_ith
	llama_get_model
	llama_get_state_size
	llama_kv_cache_can_shift
	llama_kv_cache_clear
	llama_kv_cache_defrag
	llama_kv_cache_seq_add
	llama_kv_cache_seq_cp
	llama_kv_cache_seq_div
	llama_kv_cache_seq_keep
	llama_kv_cache_seq_pos_max
	llama_kv_cache_seq_rm
	llama_kv_cache_update
	llama_kv_cache_view_free
	llama_kv_cache_view_init
	llama_kv_cache_view_update
	llama_load_model_from_file
	llama_load_session_file
	llama_log_set
	llama_lora_adapter_clear
	llama_lora_adapter_free
	llama_lora_adapter_init
	llama_lora_adapter_remove
	llama_lora_adapter_set
	llama_max_devices
	llama_model_decoder_start_token
	llama_model_default_params
	llama_model_desc
	llama_model_has_decoder
	llama_model_has_encoder
	llama_model_is_recurrent
	llama_model_meta_count
	llama_model_meta_key_by_index
	llama_model_meta_val_str
	llama_model_meta_val_str_by_index
	llama_model_n_params
	llama_model_quantize
	llama_model_quantize_default_params
	llama_model_size
	llama_n_batch
	llama_n_ctx
	llama_n_ctx_train
	llama_n_embd
	llama_n_head
	llama_n_layer
	llama_n_seq_max
	llama_n_threads
	llama_n_threads_batch
	llama_n_ubatch
	llama_n_vocab
	llama_new_context_with_model
	llama_numa_init
	llama_perf_context
	llama_perf_context_print
	llama_perf_context_reset
	llama_perf_sampler
	llama_perf_sampler_print
	llama_perf_sampler_reset
	llama_pooling_type
	llama_print_system_info
	llama_rope_freq_scale_train
	llama_rope_type
	llama_sampler_accept
	llama_sampler_apply
	llama_sampler_chain_add
	llama_sampler_chain_default_params
	llama_sampler_chain_get
	llama_sampler_chain_init
	llama_sampler_chain_n
	llama_sampler_chain_remove
	llama_sampler_clone
	llama_sampler_free
	llama_sampler_get_seed
	llama_sampler_init_dist
	llama_sampler_init_dry
	llama_sampler_init_grammar
	llama_sampler_init_greedy
	llama_sampler_init_infill
	llama_sampler_init_logit_bias
	llama_sampler_init_min_p
	llama_sampler_init_mirostat
	llama_sampler_init_mirostat_v2
	llama_sampler_init_penalties
	llama_sampler_init_softmax
	llama_sampler_init_temp
	llama_sampler_init_temp_ext
	llama_sampler_init_top_k
	llama_sampler_init_top_p
	llama_sampler_init_typical
	llama_sampler_init_xtc
	llama_sampler_name
	llama_sampler_reset
	llama_sampler_sample
	llama_save_session_file
	llama_set_abort_callback
	llama_set_causal_attn
	llama_set_embeddings
	llama_set_n_threads
	llama_set_state_data
	llama_split_path
	llama_split_prefix
	llama_state_get_data
	llama_state_get_size
	llama_state_load_file
	llama_state_save_file
	llama_state_seq_get_data
	llama_state_seq_get_size
	llama_state_seq_load_file
	llama_state_seq_save_file
	llama_state_seq_set_data
	llama_state_set_data
	llama_supports_gpu_offload
	llama_supports_mlock
	llama_supports_mmap
	llama_supports_rpc
	llama_synchronize
	llama_time_us
	llama_token_bos
	llama_token_cls
	llama_token_eos
	llama_token_eot
	llama_token_fim_mid
	llama_token_fim_pad
	llama_token_fim_pre
	llama_token_fim_rep
	llama_token_fim_sep
	llama_token_fim_suf
	llama_token_get_attr
	llama_token_get_score
	llama_token_get_text
	llama_token_is_control
	llama_token_is_eog
	llama_token_middle
	llama_token_nl
	llama_token_pad
	llama_token_prefix
	llama_token_sep
	llama_token_suffix
	llama_token_to_piece
	llama_tokenize
	llama_vocab_type
	EOF
