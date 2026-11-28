#include "tgpt.h"
#include "heap.h"
#include "lib.h"
#include "pmm.h"
#include "string.h"

#define TGPT_HEADER_BYTES 40

#define TGPT_DTYPE_F16 1
#define TGPT_DTYPE_I8 2
#define TGPT_DTYPE_F32 0

static float *half_table = NULL;
static int half_table_ready = 0;

u64 tgpt_rdtsc(void) {
    u32 low = 0;
    u32 high = 0;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((u64)high << 32) | low;
}

void tgpt_profile_reset(struct tgpt_profile *profile) {
    memset(profile, 0, sizeof(*profile));
}

static float half_convert(u16 value) {
    u32 sign = (u32)(value & 0x8000u) << 16;
    u32 exponent = (u32)(value >> 10) & 0x1Fu;
    u32 mantissa = (u32)value & 0x3FFu;
    u32 bits = 0;
    if (exponent == 0) {
        if (mantissa != 0) {
            int shift = 0;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                shift++;
            }
            mantissa &= 0x3FFu;
            bits = sign | ((u32)(113 - shift) << 23) | (mantissa << 13);
        } else {
            bits = sign;
        }
    } else if (exponent == 31) {
        bits = sign | 0x7F800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
    }
    float result = 0.0f;
    memcpy(&result, &bits, 4);
    return result;
}

static int half_prepare(void) {
    if (half_table_ready) {
        return 0;
    }
    half_table = (float *)kmalloc(65536 * sizeof(float));
    if (half_table == NULL) {
        return -1;
    }
    for (u32 index = 0; index < 65536; index++) {
        half_table[index] = half_convert((u16)index);
    }
    half_table_ready = 1;
    return 0;
}

static float half_to_float(u16 value) {
    return half_table != NULL ? half_table[value] : half_convert(value);
}

static float tgpt_exp(float x) {
    if (x < -87.0f) {
        return 0.0f;
    }
    if (x > 88.0f) {
        x = 88.0f;
    }
    float scaled = x * 1.4426950408889634f;
    float rounded = scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f;
    int exponent = (int)rounded;
    float fraction = x - (float)exponent * 0.6931471805599453f;
    float polynomial = 1.0f + fraction * (1.0f + fraction * (0.5f + fraction * (0.16666666666666666f +
                       fraction * (0.041666666666666664f + fraction * (0.008333333333333333f +
                       fraction * 0.001388888888888889f)))));
    int biased = exponent + 127;
    if (biased < 1) {
        return 0.0f;
    }
    if (biased > 254) {
        biased = 254;
    }
    u32 bits = (u32)biased << 23;
    float scale = 0.0f;
    memcpy(&scale, &bits, 4);
    return polynomial * scale;
}

static float tgpt_sqrt(float x) {
    if (x <= 0.0f) {
        return 0.0f;
    }
    float result = x;
    for (int index = 0; index < 6; index++) {
        result = 0.5f * (result + x / result);
    }
    return result;
}

static float tgpt_tanh(float x) {
    if (x > 9.0f) {
        return 1.0f;
    }
    if (x < -9.0f) {
        return -1.0f;
    }
    float doubled = tgpt_exp(2.0f * x);
    return (doubled - 1.0f) / (doubled + 1.0f);
}

static void tgpt_gelu(float *values, int count) {
    for (int index = 0; index < count; index++) {
        float value = values[index];
        float cube = value * value * value;
        values[index] = 0.5f * value * (1.0f + tgpt_tanh(0.7978845608028654f * (value + 0.044715f * cube)));
    }
}

static void tgpt_layernorm(float *output, const float *input, const u16 *weight, const u16 *bias, int count) {
    float mean = 0.0f;
    for (int index = 0; index < count; index++) {
        mean += input[index];
    }
    mean /= (float)count;
    float variance = 0.0f;
    for (int index = 0; index < count; index++) {
        float delta = input[index] - mean;
        variance += delta * delta;
    }
    variance /= (float)count;
    float inverse = 1.0f / tgpt_sqrt(variance + 1e-5f);
    for (int index = 0; index < count; index++) {
        output[index] = (input[index] - mean) * inverse * half_to_float(weight[index]) +
                        half_to_float(bias[index]);
    }
}

static void tgpt_matvec(const float *input, const u16 *matrix, int rows, int columns, float *output) {
    for (int row = 0; row < rows; row++) {
        const u16 *weights = matrix + (u64)row * columns;
        float total = 0.0f;
        int column = 0;
        float a0 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
        float a3 = 0.0f;
        for (; column + 4 <= columns; column += 4) {
            a0 += input[column] * half_to_float(weights[column]);
            a1 += input[column + 1] * half_to_float(weights[column + 1]);
            a2 += input[column + 2] * half_to_float(weights[column + 2]);
            a3 += input[column + 3] * half_to_float(weights[column + 3]);
        }
        total = (a0 + a1) + (a2 + a3);
        for (; column < columns; column++) {
            total += input[column] * half_to_float(weights[column]);
        }
        output[row] = total;
    }
}

static float tgpt_row_absmax(const float *values, int count) {
    float peak = 0.0f;
    for (int index = 0; index < count; index++) {
        float value = values[index] < 0.0f ? -values[index] : values[index];
        if (value > peak) {
            peak = value;
        }
    }
    return peak;
}

static void tgpt_quantize_vector(const float *input, i8 *output, int count, float *scale) {
    float peak = tgpt_row_absmax(input, count);
    if (peak <= 0.0f) {
        *scale = 0.0f;
        for (int index = 0; index < count; index++) {
            output[index] = 0;
        }
        return;
    }
    float factor = 127.0f / peak;
    *scale = peak / 127.0f;
    for (int index = 0; index < count; index++) {
        float scaled = input[index] * factor;
        int rounded = (int)(scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f);
        if (rounded > 127) {
            rounded = 127;
        }
        if (rounded < -127) {
            rounded = -127;
        }
        output[index] = (i8)rounded;
    }
}

static void tgpt_matvec_nt(const float *input, const u16 *matrix, int rows, int columns, float *output) {
    for (int row = 0; row < rows; row++) {
        const u16 *weights = matrix + (u64)row * columns;
        float total = 0.0f;
        for (int column = 0; column < columns; column++) {
            total += input[column] * half_to_float(weights[column]);
        }
        output[row] = total;
    }
}

static void tgpt_matvec_q8(const i8 *vector, float vector_scale, const i8 *matrix, const float *scales,
                           int rows, int columns, float *output) {
    for (int row = 0; row < rows; row++) {
        const i8 *weights = matrix + (u64)row * columns;
        i32 total = 0;
        for (int column = 0; column < columns; column++) {
            total += (i32)weights[column] * (i32)vector[column];
        }
        output[row] = (float)total * scales[row] * vector_scale;
    }
}

static void tgpt_softmax(float *values, int count) {
    float maximum = values[0];
    for (int index = 1; index < count; index++) {
        if (values[index] > maximum) {
            maximum = values[index];
        }
    }
    float total = 0.0f;
    for (int index = 0; index < count; index++) {
        float value = tgpt_exp(values[index] - maximum);
        values[index] = value;
        total += value;
    }
    if (total <= 0.0f) {
        return;
    }
    float inverse = 1.0f / total;
    for (int index = 0; index < count; index++) {
        values[index] *= inverse;
    }
}

struct tgpt_section {
    const u8 *data;
    u64 bytes;
    u32 dtype;
    u64 rows;
    u64 columns;
};

static int tgpt_section_read(const u8 *blob, u64 size, u64 *cursor, const char *want,
                             struct tgpt_section *section) {
    u64 offset = *cursor;
    u32 name_length = 0;
    if (offset + 4 > size) {
        return 0;
    }
    memcpy(&name_length, blob + offset, 4);
    offset += 4;
    if (name_length == 0 || name_length > 96 || offset + name_length > size) {
        return 0;
    }
    const char *name = (const char *)(blob + offset);
    offset += name_length;
    u32 rank = 0;
    if (offset + 4 > size) {
        return 0;
    }
    memcpy(&rank, blob + offset, 4);
    offset += 4;
    if (rank == 0 || rank > 2) {
        return 0;
    }
    if (offset + (u64)rank * 8 + 12 > size) {
        return 0;
    }
    u64 dims[2] = {0, 0};
    for (u32 index = 0; index < rank; index++) {
        memcpy(&dims[index], blob + offset, 8);
        offset += 8;
    }
    u32 dtype = 0;
    memcpy(&dtype, blob + offset, 4);
    offset += 4;
    u64 payload = 0;
    memcpy(&payload, blob + offset, 8);
    offset += 8;
    if (payload > size || offset + payload > size) {
        return 0;
    }
    if (want != NULL && strlen(want) == name_length && memcmp(name, want, name_length) == 0) {
        section->data = blob + offset;
        section->bytes = payload;
        section->dtype = dtype;
        section->rows = dims[0];
        section->columns = rank == 2 ? dims[1] : 0;
        return 1;
    }
    *cursor = offset + payload;
    return -1;
}

static const u16 *tgpt_tensor(const u8 *blob, u64 size, const char *name, u64 expected) {
    u64 cursor = TGPT_HEADER_BYTES;
    struct tgpt_section section;
    for (;;) {
        int found = tgpt_section_read(blob, size, &cursor, name, &section);
        if (found > 0) {
            if (section.dtype != TGPT_DTYPE_F16 || section.bytes != expected * 2) {
                return NULL;
            }
            return (const u16 *)section.data;
        }
        if (found == 0) {
            return NULL;
        }
    }
}

static int tgpt_tensor_q8(const u8 *blob, u64 size, const char *name, u64 rows, u64 columns,
                          const i8 **codes, const float **scales) {
    char scale_name[96];
    ksnprintf(scale_name, sizeof(scale_name), "%s.q8scale", name);
    u64 cursor = TGPT_HEADER_BYTES;
    struct tgpt_section codes_section;
    int found = 0;
    for (;;) {
        int result = tgpt_section_read(blob, size, &cursor, name, &codes_section);
        if (result > 0) {
            found = 1;
            break;
        }
        if (result == 0) {
            return -1;
        }
    }
    if (found == 0 || codes_section.dtype != TGPT_DTYPE_I8 || codes_section.bytes != rows * columns) {
        return -1;
    }
    cursor = TGPT_HEADER_BYTES;
    struct tgpt_section scale_section;
    for (;;) {
        int result = tgpt_section_read(blob, size, &cursor, scale_name, &scale_section);
        if (result > 0) {
            break;
        }
        if (result == 0) {
            return -1;
        }
    }
    if (scale_section.dtype != TGPT_DTYPE_F32 || scale_section.bytes != rows * sizeof(float)) {
        return -1;
    }
    *codes = (const i8 *)codes_section.data;
    *scales = (const float *)scale_section.data;
    return 0;
}

static int tgpt_build_name(char *buffer, int capacity, int layer, const char *suffix) {
    return ksnprintf(buffer, (usize)capacity, "h.%d.%s", layer, suffix);
}

int tgpt_model_header(struct tgpt_model *model, const void *blob, u64 size) {
    const u8 *source = (const u8 *)blob;
    if (model == NULL || blob == NULL || size < TGPT_HEADER_BYTES) {
        return -1;
    }
    if (memcmp(source, "TGPTBIN1", 8) != 0) {
        return -1;
    }
    u32 header[8];
    memcpy(header, source + 8, 32);
    model->n_layer = (int)header[1];
    model->n_head = (int)header[2];
    model->n_embd = (int)header[3];
    model->n_ctx = (int)header[4];
    model->n_vocab = (int)header[5];
    model->n_inner = (int)header[6];
    if (model->n_layer < 1 || model->n_layer > TGPT_MAX_LAYERS || model->n_head < 1 || model->n_embd < 1) {
        return -1;
    }
    model->head_dim = model->n_embd / model->n_head;
    if (model->head_dim * model->n_head != model->n_embd) {
        return -1;
    }
    if (half_prepare() != 0) {
        return -1;
    }
    return 0;
}

int tgpt_model_attach(struct tgpt_model *model, const void *blob, u64 size) {
    const u8 *source = (const u8 *)blob;
    if (model == NULL || blob == NULL || size < TGPT_HEADER_BYTES) {
        return -1;
    }
    if (memcmp(source, "TGPTBIN1", 8) != 0) {
        return -1;
    }
    u32 header[8];
    memcpy(header, source + 8, 32);
    model->n_layer = (int)header[1];
    model->n_head = (int)header[2];
    model->n_embd = (int)header[3];
    model->n_ctx = (int)header[4];
    model->n_vocab = (int)header[5];
    model->n_inner = (int)header[6];
    if (model->n_layer < 1 || model->n_layer > TGPT_MAX_LAYERS || model->n_head < 1 || model->n_embd < 1) {
        return -1;
    }
    model->head_dim = model->n_embd / model->n_head;
    if (model->head_dim * model->n_head != model->n_embd) {
        return -1;
    }
    if (half_prepare() != 0) {
        return -1;
    }
    model->wte = tgpt_tensor(source, size, "wte.weight", (u64)model->n_vocab * model->n_embd);
    model->wpe = tgpt_tensor(source, size, "wpe.weight", (u64)model->n_ctx * model->n_embd);
    model->lnf_w = tgpt_tensor(source, size, "ln_f.weight", (u64)model->n_embd);
    model->lnf_b = tgpt_tensor(source, size, "ln_f.bias", (u64)model->n_embd);
    if (model->wte == NULL || model->wpe == NULL || model->lnf_w == NULL || model->lnf_b == NULL) {
        return -1;
    }
    char name[64];
    for (int layer = 0; layer < model->n_layer; layer++) {
        struct tgpt_layer *entry = &model->layers[layer];
        tgpt_build_name(name, (int)sizeof(name), layer, "ln_1.weight");
        entry->ln1_w = tgpt_tensor(source, size, name, (u64)model->n_embd);
        tgpt_build_name(name, (int)sizeof(name), layer, "ln_1.bias");
        entry->ln1_b = tgpt_tensor(source, size, name, (u64)model->n_embd);
        tgpt_build_name(name, (int)sizeof(name), layer, "attn.c_attn.weight");
        entry->qkv_w = tgpt_tensor(source, size, name, (u64)model->n_embd * 3 * model->n_embd);
        tgpt_build_name(name, (int)sizeof(name), layer, "attn.c_attn.bias");
        entry->qkv_b = tgpt_tensor(source, size, name, (u64)model->n_embd * 3);
        tgpt_build_name(name, (int)sizeof(name), layer, "attn.c_proj.weight");
        entry->proj_w = tgpt_tensor(source, size, name, (u64)model->n_embd * model->n_embd);
        tgpt_build_name(name, (int)sizeof(name), layer, "attn.c_proj.bias");
        entry->proj_b = tgpt_tensor(source, size, name, (u64)model->n_embd);
        tgpt_build_name(name, (int)sizeof(name), layer, "ln_2.weight");
        entry->ln2_w = tgpt_tensor(source, size, name, (u64)model->n_embd);
        tgpt_build_name(name, (int)sizeof(name), layer, "ln_2.bias");
        entry->ln2_b = tgpt_tensor(source, size, name, (u64)model->n_embd);
        tgpt_build_name(name, (int)sizeof(name), layer, "mlp.c_fc.weight");
        entry->fc_w = tgpt_tensor(source, size, name, (u64)model->n_inner * model->n_embd);
        tgpt_build_name(name, (int)sizeof(name), layer, "mlp.c_fc.bias");
        entry->fc_b = tgpt_tensor(source, size, name, (u64)model->n_inner);
        tgpt_build_name(name, (int)sizeof(name), layer, "mlp.c_proj.weight");
        entry->ff_w = tgpt_tensor(source, size, name, (u64)model->n_embd * model->n_inner);
        tgpt_build_name(name, (int)sizeof(name), layer, "mlp.c_proj.bias");
        entry->ff_b = tgpt_tensor(source, size, name, (u64)model->n_embd);
        if (entry->ln1_w == NULL || entry->ln1_b == NULL || entry->qkv_w == NULL || entry->qkv_b == NULL ||
            entry->proj_w == NULL || entry->proj_b == NULL || entry->ln2_w == NULL || entry->ln2_b == NULL ||
            entry->fc_w == NULL || entry->fc_b == NULL || entry->ff_w == NULL || entry->ff_b == NULL) {
            return -1;
        }
    }
    return 0;
}

int tgpt_state_open(struct tgpt_state *state, const struct tgpt_model *model) {
    int context = model->n_ctx;
    if (context > 128) {
        context = 128;
    }
    memset(state, 0, sizeof(*state));
    state->x = (float *)kmalloc((usize)model->n_embd * sizeof(float));
    state->h = (float *)kmalloc((usize)model->n_embd * sizeof(float));
    state->qkv = (float *)kmalloc((usize)model->n_embd * 3 * sizeof(float));
    state->attn = (float *)kmalloc((usize)model->n_embd * sizeof(float));
    state->ff = (float *)kmalloc((usize)model->n_inner * sizeof(float));
    state->logits = (float *)kmalloc((usize)model->n_vocab * sizeof(float));
    state->scores = (float *)kmalloc((usize)context * sizeof(float));
    state->quant_buffer = (i8 *)kmalloc((usize)model->n_inner * sizeof(i8));
    if (state->x == NULL || state->h == NULL || state->qkv == NULL || state->attn == NULL ||
        state->ff == NULL || state->logits == NULL || state->scores == NULL || state->quant_buffer == NULL) {
        tgpt_state_close(state);
        return -1;
    }
    tgpt_profile_reset(&state->profile);
    u64 elements = (u64)model->n_layer * (u64)context * (u64)model->n_embd;
    u64 pages = (elements * sizeof(float) + PAGE_SIZE - 1) / PAGE_SIZE;
    u64 key_block = pmm_alloc_contiguous((u32)pages);
    if (key_block == 0) {
        tgpt_state_close(state);
        return -1;
    }
    u64 value_block = pmm_alloc_contiguous((u32)pages);
    if (value_block == 0) {
        pmm_free_contiguous(key_block, (u32)pages);
        tgpt_state_close(state);
        return -1;
    }
    state->k_physical = key_block;
    state->v_physical = value_block;
    state->k_pages = (u32)pages;
    state->v_pages = (u32)pages;
    state->k_cache = (float *)(usize)key_block;
    state->v_cache = (float *)(usize)value_block;
    state->cache_ctx = context;
    return 0;
}

void tgpt_state_close(struct tgpt_state *state) {
    if (state->k_physical != 0) {
        pmm_free_contiguous(state->k_physical, state->k_pages);
    }
    if (state->v_physical != 0) {
        pmm_free_contiguous(state->v_physical, state->v_pages);
    }
    if (state->x) {
        kfree(state->x);
    }
    if (state->h) {
        kfree(state->h);
    }
    if (state->qkv) {
        kfree(state->qkv);
    }
    if (state->attn) {
        kfree(state->attn);
    }
    if (state->ff) {
        kfree(state->ff);
    }
    if (state->logits) {
        kfree(state->logits);
    }
    if (state->scores) {
        kfree(state->scores);
    }
    if (state->quant_buffer) {
        kfree(state->quant_buffer);
    }
    memset(state, 0, sizeof(*state));
}

static u64 tgpt_hash(const u8 *data, u32 length) {
    u64 hash = 1469598103934665603ULL;
    for (u32 index = 0; index < length; index++) {
        hash ^= (u64)data[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

int tgpt_vocab_attach(struct tgpt_vocab *vocab, const void *blob, u64 size) {
    const u8 *source = (const u8 *)blob;
    if (vocab == NULL || blob == NULL || size < 12) {
        return -1;
    }
    if (memcmp(source, "VOCB0001", 8) != 0) {
        return -1;
    }
    u32 count = 0;
    u32 payload_size = 0;
    memcpy(&count, source + 8, 4);
    memcpy(&payload_size, source + 12, 4);
    u64 table_bytes = (u64)count * 8;
    if (count == 0 || count > 200000 || 16 + table_bytes + payload_size > size) {
        return -1;
    }
    vocab->blob = source;
    vocab->blob_size = size;
    vocab->token_count = count;
    vocab->offsets = (const u32 *)(source + 16);
    vocab->lengths = (const u32 *)(source + 20);
    vocab->payload = source + 16 + table_bytes;
    u32 capacity = 1;
    while (capacity < count * 2) {
        capacity <<= 1;
    }
    vocab->hash_keys = (u64 *)kmalloc((usize)capacity * sizeof(u64));
    vocab->hash_ids = (u32 *)kmalloc((usize)capacity * sizeof(u32));
    if (vocab->hash_keys == NULL || vocab->hash_ids == NULL) {
        return -1;
    }
    memset(vocab->hash_keys, 0, (usize)capacity * sizeof(u64));
    for (u32 index = 0; index < capacity; index++) {
        vocab->hash_ids[index] = 0xFFFFFFFFu;
    }
    vocab->hash_capacity = capacity;
    for (u32 identifier = 0; identifier < count; identifier++) {
        u32 offset = vocab->offsets[identifier * 2];
        u32 length = vocab->offsets[identifier * 2 + 1];
        u64 hash = tgpt_hash(vocab->payload + offset, length);
        if (hash == 0) {
            hash = 1;
        }
        u32 mask = capacity - 1;
        u32 slot = (u32)(hash & mask);
        while (vocab->hash_ids[slot] != 0xFFFFFFFFu) {
            slot = (slot + 1) & mask;
        }
        vocab->hash_keys[slot] = hash;
        vocab->hash_ids[slot] = identifier;
    }
    return 0;
}

static int tgpt_vocab_find(const struct tgpt_vocab *vocab, const u8 *data, u32 length) {
    if (length == 0) {
        return -1;
    }
    u64 hash = tgpt_hash(data, length);
    if (hash == 0) {
        hash = 1;
    }
    u32 mask = vocab->hash_capacity - 1;
    u32 slot = (u32)(hash & mask);
    while (vocab->hash_ids[slot] != 0xFFFFFFFFu) {
        if (vocab->hash_keys[slot] == hash) {
            u32 identifier = vocab->hash_ids[slot];
            u32 offset = vocab->offsets[identifier * 2];
            u32 stored = vocab->offsets[identifier * 2 + 1];
            if (stored == length && memcmp(vocab->payload + offset, data, length) == 0) {
                return (int)identifier;
            }
        }
        slot = (slot + 1) & mask;
    }
    return -1;
}

int tgpt_vocab_encode(const struct tgpt_vocab *vocab, const char *text, int *ids, int capacity) {
    const u8 *data = (const u8 *)text;
    u32 length = (u32)strlen(text);
    u32 position = 0;
    int produced = 0;
    while (position < length) {
        u32 best_length = 0;
        int best_id = -1;
        u32 limit = length - position;
        if (limit > 128) {
            limit = 128;
        }
        for (u32 size = limit; size > 0; size--) {
            int identifier = tgpt_vocab_find(vocab, data + position, size);
            if (identifier >= 0) {
                best_length = size;
                best_id = identifier;
                break;
            }
        }
        if (best_id < 0) {
            return -1;
        }
        if (produced >= capacity) {
            return -1;
        }
        ids[produced++] = best_id;
        position += best_length;
    }
    return produced;
}

int tgpt_vocab_decode(const struct tgpt_vocab *vocab, int id, char *output, int capacity) {
    if (id < 0 || (u32)id >= vocab->token_count) {
        return -1;
    }
    u32 offset = vocab->offsets[id * 2];
    u32 length = vocab->offsets[id * 2 + 1];
    if ((int)length >= capacity) {
        return -1;
    }
    memcpy(output, vocab->payload + offset, length);
    output[length] = 0;
    return (int)length;
}

int tgpt_model_quantize(struct tgpt_model *model, const void *blob, u64 size) {
    const u8 *source = (const u8 *)blob;
    char name[96];
    if (model == NULL || blob == NULL || size < TGPT_HEADER_BYTES) {
        return -1;
    }
    if (memcmp(source, "TGPTBIN1", 8) != 0) {
        return -1;
    }
    model->wpe = tgpt_tensor(source, size, "wpe.weight", (u64)model->n_ctx * model->n_embd);
    model->lnf_w = tgpt_tensor(source, size, "ln_f.weight", (u64)model->n_embd);
    model->lnf_b = tgpt_tensor(source, size, "ln_f.bias", (u64)model->n_embd);
    if (model->wpe == NULL || model->lnf_w == NULL || model->lnf_b == NULL) {
        return -1;
    }
    model->wte = tgpt_tensor(source, size, "wte.weight", (u64)model->n_vocab * model->n_embd);
    if (model->wte == NULL) {
        return -1;
    }
    for (int layer = 0; layer < model->n_layer; layer++) {
        struct tgpt_layer *entry = &model->layers[layer];
        tgpt_build_name(name, (int)sizeof(name), layer, "ln_1.weight");
        entry->ln1_w = tgpt_tensor(source, size, name, (u64)model->n_embd);
        tgpt_build_name(name, (int)sizeof(name), layer, "ln_1.bias");
        entry->ln1_b = tgpt_tensor(source, size, name, (u64)model->n_embd);
        tgpt_build_name(name, (int)sizeof(name), layer, "ln_2.weight");
        entry->ln2_w = tgpt_tensor(source, size, name, (u64)model->n_embd);
        tgpt_build_name(name, (int)sizeof(name), layer, "ln_2.bias");
        entry->ln2_b = tgpt_tensor(source, size, name, (u64)model->n_embd);
        tgpt_build_name(name, (int)sizeof(name), layer, "attn.c_attn.bias");
        entry->qkv_b = tgpt_tensor(source, size, name, (u64)model->n_embd * 3);
        tgpt_build_name(name, (int)sizeof(name), layer, "attn.c_proj.bias");
        entry->proj_b = tgpt_tensor(source, size, name, (u64)model->n_embd);
        tgpt_build_name(name, (int)sizeof(name), layer, "mlp.c_fc.bias");
        entry->fc_b = tgpt_tensor(source, size, name, (u64)model->n_inner);
        tgpt_build_name(name, (int)sizeof(name), layer, "mlp.c_proj.bias");
        entry->ff_b = tgpt_tensor(source, size, name, (u64)model->n_embd);
        if (entry->ln1_w == NULL || entry->ln1_b == NULL || entry->ln2_w == NULL || entry->ln2_b == NULL ||
            entry->qkv_b == NULL || entry->proj_b == NULL || entry->fc_b == NULL || entry->ff_b == NULL) {
            return -1;
        }
        tgpt_build_name(name, (int)sizeof(name), layer, "attn.c_attn.weight");
        if (tgpt_tensor_q8(source, size, name, (u64)model->n_embd * 3, (u64)model->n_embd, &entry->qkv_q,
                           &entry->qkv_scale) != 0) {
            return -1;
        }
        tgpt_build_name(name, (int)sizeof(name), layer, "attn.c_proj.weight");
        if (tgpt_tensor_q8(source, size, name, (u64)model->n_embd, (u64)model->n_embd, &entry->proj_q,
                           &entry->proj_scale) != 0) {
            return -1;
        }
        tgpt_build_name(name, (int)sizeof(name), layer, "mlp.c_fc.weight");
        if (tgpt_tensor_q8(source, size, name, (u64)model->n_inner, (u64)model->n_embd, &entry->fc_q,
                           &entry->fc_scale) != 0) {
            return -1;
        }
        tgpt_build_name(name, (int)sizeof(name), layer, "mlp.c_proj.weight");
        if (tgpt_tensor_q8(source, size, name, (u64)model->n_embd, (u64)model->n_inner, &entry->ff_q,
                           &entry->ff_scale) != 0) {
            return -1;
        }
    }
    model->quantized = 1;
    return 0;
}
int tgpt_forward(struct tgpt_model *model, struct tgpt_state *state, int token, int position) {
    if (token < 0 || token >= model->n_vocab || position < 0 || position >= state->cache_ctx) {
        return -1;
    }
    struct tgpt_profile *profile = &state->profile;
    int embd = model->n_embd;
    int heads = model->n_head;
    int dim = model->head_dim;
    int context = state->cache_ctx;
    int quantized = model->quantized != 0;
    float vector_scale = 0.0f;
    u64 mark = tgpt_rdtsc();
    u64 tsc = mark;
    const u16 *position_row = model->wpe + (u64)position * embd;
    const u16 *token_row = model->wte + (u64)token * embd;
    for (int index = 0; index < embd; index++) {
        state->x[index] = half_to_float(token_row[index]) + half_to_float(position_row[index]);
    }
    profile->embed_cycles += tgpt_rdtsc() - tsc;
    for (int layer = 0; layer < model->n_layer; layer++) {
        const struct tgpt_layer *entry = &model->layers[layer];
        tsc = tgpt_rdtsc();
        tgpt_layernorm(state->h, state->x, entry->ln1_w, entry->ln1_b, embd);
        profile->ln1_cycles += tgpt_rdtsc() - tsc;
        tsc = tgpt_rdtsc();
        if (quantized) {
            tgpt_quantize_vector(state->h, state->quant_buffer, embd, &vector_scale);
            tgpt_matvec_q8(state->quant_buffer, vector_scale, entry->qkv_q, entry->qkv_scale, 3 * embd, embd,
                           state->qkv);
        } else {
            tgpt_matvec(state->h, entry->qkv_w, 3 * embd, embd, state->qkv);
        }
        profile->qkv_cycles += tgpt_rdtsc() - tsc;
        for (int index = 0; index < 3 * embd; index++) {
            state->qkv[index] += half_to_float(entry->qkv_b[index]);
        }
        const float *query = state->qkv;
        const float *key = state->qkv + embd;
        const float *value = state->qkv + 2 * embd;
        u64 base = (u64)layer * context * embd;
        float *key_slot = state->k_cache + base + (u64)position * embd;
        float *value_slot = state->v_cache + base + (u64)position * embd;
        memcpy(key_slot, key, (usize)embd * sizeof(float));
        memcpy(value_slot, value, (usize)embd * sizeof(float));
        tsc = tgpt_rdtsc();
        float scale = 1.0f / tgpt_sqrt((float)dim);
        for (int head = 0; head < heads; head++) {
            const float *query_head = query + head * dim;
            float *scores = state->scores;
            for (int step = 0; step <= position; step++) {
                const float *key_head = state->k_cache + base + (u64)step * embd + head * dim;
                float total = 0.0f;
                for (int index = 0; index < dim; index++) {
                    total += query_head[index] * key_head[index];
                }
                scores[step] = total * scale;
            }
            tgpt_softmax(scores, position + 1);
            float *output = state->attn + head * dim;
            for (int index = 0; index < dim; index++) {
                output[index] = 0.0f;
            }
            for (int step = 0; step <= position; step++) {
                float weight = scores[step];
                const float *value_head = state->v_cache + base + (u64)step * embd + head * dim;
                for (int index = 0; index < dim; index++) {
                    output[index] += weight * value_head[index];
                }
            }
        }
        profile->attn_cycles += tgpt_rdtsc() - tsc;
        tsc = tgpt_rdtsc();
        if (quantized) {
            tgpt_quantize_vector(state->attn, state->quant_buffer, embd, &vector_scale);
            tgpt_matvec_q8(state->quant_buffer, vector_scale, entry->proj_q, entry->proj_scale, embd, embd,
                           state->h);
        } else {
            tgpt_matvec(state->attn, entry->proj_w, embd, embd, state->h);
        }
        profile->proj_cycles += tgpt_rdtsc() - tsc;
        for (int index = 0; index < embd; index++) {
            state->x[index] += state->h[index] + half_to_float(entry->proj_b[index]);
        }
        tsc = tgpt_rdtsc();
        tgpt_layernorm(state->h, state->x, entry->ln2_w, entry->ln2_b, embd);
        if (quantized) {
            tgpt_quantize_vector(state->h, state->quant_buffer, embd, &vector_scale);
            tgpt_matvec_q8(state->quant_buffer, vector_scale, entry->fc_q, entry->fc_scale, model->n_inner, embd,
                           state->ff);
        } else {
            tgpt_matvec(state->h, entry->fc_w, model->n_inner, embd, state->ff);
        }
        for (int index = 0; index < model->n_inner; index++) {
            state->ff[index] += half_to_float(entry->fc_b[index]);
        }
        tgpt_gelu(state->ff, model->n_inner);
        if (quantized) {
            tgpt_quantize_vector(state->ff, state->quant_buffer, model->n_inner, &vector_scale);
            tgpt_matvec_q8(state->quant_buffer, vector_scale, entry->ff_q, entry->ff_scale, embd, model->n_inner,
                           state->h);
        } else {
            tgpt_matvec(state->ff, entry->ff_w, embd, model->n_inner, state->h);
        }
        profile->ffn_cycles += tgpt_rdtsc() - tsc;
        for (int index = 0; index < embd; index++) {
            state->x[index] += state->h[index] + half_to_float(entry->ff_b[index]);
        }
    }
    tsc = tgpt_rdtsc();
    tgpt_layernorm(state->h, state->x, model->lnf_w, model->lnf_b, embd);
    if (quantized) {
        tgpt_matvec_nt(state->h, model->wte, model->n_vocab, embd, state->logits);
    } else {
        tgpt_matvec(state->h, model->wte, model->n_vocab, embd, state->logits);
    }
    profile->logits_cycles += tgpt_rdtsc() - tsc;
    profile->forward_calls++;
    profile->total_cycles += tgpt_rdtsc() - mark;
    return 0;
}


int tgpt_greedy(struct tgpt_model *model, struct tgpt_state *state) {
    int best = 0;
    float maximum = state->logits[0];
    for (int index = 1; index < model->n_vocab; index++) {
        if (state->logits[index] > maximum) {
            maximum = state->logits[index];
            best = index;
        }
    }
    return best;
}

int tgpt_generate(struct tgpt_model *model, struct tgpt_state *state, const struct tgpt_vocab *vocab,
                  const char *prompt, int max_tokens, char *output, int capacity, int *produced) {
    int ids[TGPT_MAX_PROMPT];
    int prompt_tokens = tgpt_vocab_encode(vocab, prompt, ids, TGPT_MAX_PROMPT);
    if (prompt_tokens <= 0) {
        return -1;
    }
    if (prompt_tokens + max_tokens + 1 >= state->cache_ctx) {
        max_tokens = state->cache_ctx - prompt_tokens - 1;
    }
    if (max_tokens <= 0) {
        return -1;
    }
    int written = 0;
    for (int index = 0; index < prompt_tokens; index++) {
        if (tgpt_forward(model, state, ids[index], index) != 0) {
            return -1;
        }
    }
    int generated = 0;
    for (int step = 0; step < max_tokens; step++) {
        int next = tgpt_greedy(model, state);
        if (next == TGPT_EOT) {
            break;
        }
        char piece[160];
        int length = tgpt_vocab_decode(vocab, next, piece, (int)sizeof(piece));
        if (length < 0) {
            return -1;
        }
        if (written + length >= capacity) {
            break;
        }
        memcpy(output + written, piece, (usize)length);
        written += length;
        generated++;
        if (step + 1 >= max_tokens) {
            break;
        }
        if (tgpt_forward(model, state, next, prompt_tokens + generated - 1) != 0) {
            return -1;
        }
    }
    output[written] = 0;
    if (produced != NULL) {
        *produced = generated;
    }
    return 0;
}
