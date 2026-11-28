#ifndef Z_OS_TGPT_H
#define Z_OS_TGPT_H

#include "types.h"

#define TGPT_MAX_LAYERS 8
#define TGPT_EOT 50256
#define TGPT_MAX_PROMPT 256
#define TGPT_MAX_OUTPUT 4096

struct tgpt_layer {
    const u16 *ln1_w;
    const u16 *ln1_b;
    const u16 *qkv_w;
    const u16 *qkv_b;
    const u16 *proj_w;
    const u16 *proj_b;
    const u16 *ln2_w;
    const u16 *ln2_b;
    const u16 *fc_w;
    const u16 *fc_b;
    const u16 *ff_w;
    const u16 *ff_b;
};

struct tgpt_model {
    int n_layer;
    int n_head;
    int n_embd;
    int n_ctx;
    int n_vocab;
    int n_inner;
    int head_dim;
    const u16 *wte;
    const u16 *wpe;
    const u16 *lnf_w;
    const u16 *lnf_b;
    struct tgpt_layer layers[TGPT_MAX_LAYERS];
};

struct tgpt_state {
    float *x;
    float *h;
    float *qkv;
    float *attn;
    float *ff;
    float *logits;
    float *scores;
    float *k_cache;
    float *v_cache;
    u64 k_physical;
    u64 v_physical;
    u32 k_pages;
    u32 v_pages;
    int cache_ctx;
};

struct tgpt_vocab {
    const u8 *blob;
    u64 blob_size;
    u32 token_count;
    const u8 *payload;
    const u32 *offsets;
    const u32 *lengths;
    u64 *hash_keys;
    u32 *hash_ids;
    u32 hash_capacity;
};

int tgpt_model_attach(struct tgpt_model *model, const void *blob, u64 size);
int tgpt_state_open(struct tgpt_state *state, const struct tgpt_model *model);
void tgpt_state_close(struct tgpt_state *state);
int tgpt_vocab_attach(struct tgpt_vocab *vocab, const void *blob, u64 size);
int tgpt_vocab_encode(const struct tgpt_vocab *vocab, const char *text, int *ids, int capacity);
int tgpt_vocab_decode(const struct tgpt_vocab *vocab, int id, char *output, int capacity);
int tgpt_forward(struct tgpt_model *model, struct tgpt_state *state, int token, int position);
int tgpt_greedy(struct tgpt_model *model, struct tgpt_state *state);
int tgpt_generate(struct tgpt_model *model, struct tgpt_state *state, const struct tgpt_vocab *vocab,
                  const char *prompt, int max_tokens, char *output, int capacity, int *produced);

#endif
