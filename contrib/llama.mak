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
# was last tested at b5509, and an update will inevitably break it.

CROSS    =
CPPFLAGS = -w -O2
LDFLAGS  = -s

.SUFFIXES: .c .cpp .o
def = -DGGML_USE_CPU
inc = \
  -I. \
  -Icommon \
  -Iggml/include \
  -Iggml/src \
  -Iggml/src/ggml-cpu \
  -Iinclude \
  -Itools/mtmd
%.c.o: %.c
	$(CROSS)gcc -c -o $@ $(inc) $(def) $(CPPFLAGS) $<
%.cpp.o: %.cpp
	$(CROSS)g++ -c -o $@ $(inc) $(def) $(CPPFLAGS) $<

dll = \
  ggml/src/ggml-alloc.c.o \
  ggml/src/ggml-backend-reg.cpp.o \
  ggml/src/ggml-backend.cpp.o \
  ggml/src/ggml-cpu/binary-ops.cpp.o \
  ggml/src/ggml-cpu/ggml-cpu-aarch64.cpp.o \
  ggml/src/ggml-cpu/ggml-cpu-quants.c.o \
  ggml/src/ggml-cpu/ggml-cpu-traits.cpp.o \
  ggml/src/ggml-cpu/ggml-cpu.c.o \
  ggml/src/ggml-cpu/ggml-cpu.cpp.o \
  ggml/src/ggml-cpu/llamafile/sgemm.cpp.o \
  ggml/src/ggml-cpu/ops.cpp.o \
  ggml/src/ggml-cpu/unary-ops.cpp.o \
  ggml/src/ggml-cpu/vec.cpp.o \
  ggml/src/ggml-opt.cpp.o \
  ggml/src/ggml-quants.c.o \
  ggml/src/ggml-threading.cpp.o \
  ggml/src/ggml.c.o \
  ggml/src/gguf.cpp.o \
  src/llama-adapter.cpp.o \
  src/llama-arch.cpp.o \
  src/llama-batch.cpp.o \
  src/llama-chat.cpp.o \
  src/llama-context.cpp.o \
  src/llama-grammar.cpp.o \
  src/llama-graph.cpp.o \
  src/llama-hparams.cpp.o \
  src/llama-impl.cpp.o \
  src/llama-io.cpp.o \
  src/llama-kv-cache.cpp.o \
  src/llama-mmap.cpp.o \
  src/llama-model-loader.cpp.o \
  src/llama-model-saver.cpp.o \
  src/llama-model.cpp.o \
  src/llama-quant.cpp.o \
  src/llama-sampling.cpp.o \
  src/llama-vocab.cpp.o \
  src/llama.cpp.o \
  src/unicode-data.cpp.o \
  src/unicode.cpp.o

exe = \
  common/arg.cpp.o \
  common/chat-parser.cpp.o \
  common/chat.cpp.o \
  common/common.cpp.o \
  common/console.cpp.o \
  common/json-partial.cpp.o \
  common/json-schema-to-grammar.cpp.o \
  common/log.cpp.o \
  common/ngram-cache.cpp.o \
  common/regex-partial.cpp.o \
  common/sampling.cpp.o \
  common/speculative.cpp.o \
  common/w64dk-build-info.cpp.o \
  tools/mtmd/clip.cpp.o \
  tools/mtmd/mtmd-audio.cpp.o \
  tools/mtmd/mtmd-helper.cpp.o \
  tools/mtmd/mtmd.cpp.o \
  tools/server/server.cpp.o

all: llama.dll llama-server.exe

llama-server.exe: $(exe) $(dll)
	$(CROSS)g++ $(LDFLAGS) -o $@ $(exe) $(dll) -lws2_32

llama.dll: $(dll) llama.def
	$(CROSS)g++ -shared $(LDFLAGS) -o $@ $(dll) llama.def

clean:
	rm -f $(dll) $(exe) llama.def llama.dll llama-server.exe \
	   tools/server/index.html.gz.hpp tools/server/loading.html.hpp \
	   common/w64dk-build-info.cpp

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

tools/server/index.html.gz.hpp: tools/server/public/index.html.gz
	cd tools/server/public/ && xxd -i index.html.gz >../index.html.gz.hpp
tools/server/loading.html.hpp: tools/server/public/loading.html
	cd tools/server/public/ && xxd -i loading.html >../loading.html.hpp
tools/server/server.cpp.o: \
  tools/server/server.cpp \
  tools/server/index.html.gz.hpp \
  tools/server/loading.html.hpp

llama.def:
	@cat >$@ <<EOF
	LIBRARY llama
	EXPORTS
	llama_adapter_lora_free
	llama_adapter_lora_init
	llama_add_bos_token
	llama_add_eos_token
	llama_apply_adapter_cvec
	llama_attach_threadpool
	llama_backend_free
	llama_backend_init
	llama_batch_free
	llama_batch_get_one
	llama_batch_init
	llama_chat_apply_template
	llama_chat_builtin_templates
	llama_clear_adapter_lora
	llama_context_default_params
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
	llama_get_kv_self
	llama_get_logits
	llama_get_logits_ith
	llama_get_model
	llama_get_state_size
	llama_init_from_model
	llama_kv_self_can_shift
	llama_kv_self_clear
	llama_kv_self_defrag
	llama_kv_self_n_tokens
	llama_kv_self_seq_add
	llama_kv_self_seq_cp
	llama_kv_self_seq_div
	llama_kv_self_seq_keep
	llama_kv_self_seq_pos_max
	llama_kv_self_seq_rm
	llama_kv_self_update
	llama_kv_self_used_cells
	llama_load_model_from_file
	llama_load_session_file
	llama_log_set
	llama_max_devices
	llama_model_chat_template
	llama_model_decoder_start_token
	llama_model_default_params
	llama_model_desc
	llama_model_free
	llama_model_get_vocab
	llama_model_has_decoder
	llama_model_has_encoder
	llama_model_is_recurrent
	llama_model_load_from_file
	llama_model_load_from_splits
	llama_model_meta_count
	llama_model_meta_key_by_index
	llama_model_meta_val_str
	llama_model_meta_val_str_by_index
	llama_model_n_ctx_train
	llama_model_n_embd
	llama_model_n_head
	llama_model_n_head_kv
	llama_model_n_layer
	llama_model_n_params
	llama_model_quantize
	llama_model_quantize_default_params
	llama_model_rope_freq_scale_train
	llama_model_rope_type
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
	llama_rm_adapter_lora
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
	llama_sampler_init
	llama_sampler_init_dist
	llama_sampler_init_dry
	llama_sampler_init_grammar
	llama_sampler_init_grammar_lazy
	llama_sampler_init_grammar_lazy_patterns
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
	llama_sampler_init_top_n_sigma
	llama_sampler_init_top_p
	llama_sampler_init_typical
	llama_sampler_init_xtc
	llama_sampler_name
	llama_sampler_reset
	llama_sampler_sample
	llama_save_session_file
	llama_set_abort_callback
	llama_set_adapter_lora
	llama_set_causal_attn
	llama_set_embeddings
	llama_set_n_threads
	llama_set_state_data
	llama_set_warmup
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
	llama_token_nl
	llama_token_pad
	llama_token_sep
	llama_token_to_piece
	llama_tokenize
	llama_vocab_bos
	llama_vocab_cls
	llama_vocab_eos
	llama_vocab_eot
	llama_vocab_fim_mid
	llama_vocab_fim_pad
	llama_vocab_fim_pre
	llama_vocab_fim_rep
	llama_vocab_fim_sep
	llama_vocab_fim_suf
	llama_vocab_get_add_bos
	llama_vocab_get_add_eos
	llama_vocab_get_attr
	llama_vocab_get_score
	llama_vocab_get_text
	llama_vocab_is_control
	llama_vocab_is_eog
	llama_vocab_n_tokens
	llama_vocab_nl
	llama_vocab_pad
	llama_vocab_sep
	llama_vocab_type
	EOF
