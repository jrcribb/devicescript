#include "devs_internal.h"
#include <stdlib.h>

#define LOG_TAG "inspect"
#include "devs_logging.h"

typedef struct {
    devs_ctx_t *ctx;
    unsigned off;
    unsigned size;
    unsigned ulen;
    char *dst;
    uint8_t overflow;
} inspect_t;

static void add_str(inspect_t *state, const char *s) {
    if (state->overflow)
        return;

    unsigned space = state->size - state->off;
    unsigned sz = strlen(s);

    if (sz > space) {
        state->overflow = 1;

        // the char at ep is the last one to be included
        int ep = (int)space - 1;
        if (ep >= 0 && s[ep] & 0x80) {
            while (ep >= 0) {
                if (!devs_utf8_is_cont(s[ep])) {
                    ep--;
                    break;
                }
                ep--;
            }
        }
        sz = ep + 1;
    }

    for (unsigned i = 0; i < sz; ++i) {
        if (!devs_utf8_is_cont(s[i]))
            state->ulen++;
        if (state->dst)
            state->dst[state->off + i] = s[i];
    }
    state->off += sz;
}

static void add_ch(inspect_t *state, char c) {
    char s[2] = {c, 0};
    add_str(state, s);
}

static void inspect_obj(inspect_t *state, value_t v);

static bool is_id(const char *p, unsigned sz) {
    for (unsigned i = 0; i < sz; ++i) {
        char c = p[i];
        if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || (i > 0 && '0' <= c && c <= '9') ||
            c == '_')
            continue;
        return false;
    }
    return true;
}

static void inspect_field(devs_ctx_t *ctx, void *state_, value_t k, value_t v) {
    inspect_t *state = state_;

    if (state->overflow)
        return;

    int id = 0;
    if (devs_is_string(ctx, k)) {
        unsigned size;
        const char *d = devs_string_get_utf8(ctx, k, &size);
        if (is_id(d, size)) {
            id = 1;
            add_str(state, d);
        }
    }
    if (!id)
        inspect_obj(state, k);
    add_ch(state, ':');
    inspect_obj(state, v);
    add_ch(state, ',');
}

static bool is_complex(int type_of) {
    switch (type_of) {
    case DEVS_OBJECT_TYPE_MAP:
    case DEVS_OBJECT_TYPE_ARRAY:
        return true;
    }
    return false;
}

static void inspect_obj(inspect_t *state, value_t v) {
    devs_ctx_t *ctx = state->ctx;

    // LOG_VAL("str", v);
    LOGV("off=%d", state->off);

    if (state->overflow)
        return;

    if (devs_value_is_pinned(ctx, v)) {
        add_str(state, "[Circular]");
        return;
    }

    unsigned type_of = devs_value_typeof(ctx, v);

    unsigned sz;
    const char *data = NULL;

    if (type_of == DEVS_OBJECT_TYPE_STRING) {
        data = devs_string_get_utf8(ctx, v, &sz);
        char *p = devs_json_escape(data, sz);
        add_str(state, p);
        jd_free(p);
        return;
    }

    if (!is_complex(type_of) || devs_handle_type(v) == DEVS_HANDLE_TYPE_ROLE_MEMBER) {
        v = devs_value_to_string(ctx, v);
        data = devs_string_get_utf8(ctx, v, &sz);
        add_str(state, data);
        return;
    }

    devs_value_pin(ctx, v);

    if (devs_is_array(ctx, v)) {
        devs_array_t *arr = devs_value_to_gc_obj(ctx, v);
        add_ch(state, '[');
        for (unsigned i = 0; i < arr->length; ++i) {
            inspect_obj(state, arr->data[i]);
            if (state->overflow)
                break;
            if ((int)i != arr->length - 1)
                add_ch(state, ',');
        }
        add_ch(state, ']');
    } else {
        devs_maplike_t *map = devs_object_get_attached_enum(ctx, v);
        add_ch(state, '{');
        if (map != NULL) {
            devs_maplike_iter(ctx, map, state, inspect_field);
            // final comma eating interacts badly with ulen and length limitations
        }
        add_ch(state, '}');
    }

    devs_value_unpin(ctx, v);
}

static int devs_inspect_to(devs_ctx_t *ctx, value_t v, char *dst, unsigned size, unsigned *ulen) {
    if (size < 10)
        return -1;

    inspect_t state = {
        .ctx = ctx,
        .dst = dst,
        .off = 0,
        .size = size - 3, // space for final '...'
        .ulen = 0,
    };
    inspect_obj(&state, v);
    JD_ASSERT(state.off + 3 <= size);

    if (state.overflow)
        state.ulen += 3;
    *ulen = state.ulen;

    if (!dst)
        return state.overflow ? state.off + 3 : state.off;

    if (state.overflow) {
        dst[state.off++] = '.';
        dst[state.off++] = '.';
        dst[state.off++] = '.';
    }
    return state.off;
}

value_t devs_inspect(devs_ctx_t *ctx, value_t v, unsigned size) {
    if (size == 0)
        size = 100;
    unsigned type_of = devs_value_typeof(ctx, v);
    if (!is_complex(type_of))
        return devs_value_to_string(ctx, v);

    unsigned ulen;
    int sz = devs_inspect_to(ctx, v, NULL, size, &ulen);
    if (sz == -1)
        return devs_undefined;

    value_t r;
    char *d = devs_string_prep(ctx, &r, sz, ulen);
    if (d) {
        sz = devs_inspect_to(ctx, v, d, size, &ulen);
        devs_string_finish(ctx, &r, sz, ulen);
    }
    return r;
}