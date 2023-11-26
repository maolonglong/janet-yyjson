#include <janet.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "yyjson.h"

#define JSON_KEYWORD_KEY 0x10000
#define JSON_NULL_TO_NIL 0x20000

/*****************/
/* JSON Decoding */
/*****************/

static const char *decode_one(yyjson_val *root, Janet *out, int depth) {
    if ((depth & 0xFFFF) > JANET_RECURSION_GUARD) goto recurdepth;

    switch (yyjson_get_type(root)) {
        default:
            goto badtype;
        case YYJSON_TYPE_NULL:
            if (depth & JSON_NULL_TO_NIL)
                *out = janet_wrap_nil();
            else
                *out = janet_ckeywordv("null");
            break;
        case YYJSON_TYPE_BOOL:
            if (yyjson_get_bool(root))
                *out = janet_wrap_true();
            else
                *out = janet_wrap_false();
            break;
        case YYJSON_TYPE_NUM:
            *out = janet_wrap_number(yyjson_get_num(root));
            break;
        case YYJSON_TYPE_STR:
            *out = janet_cstringv(yyjson_get_str(root));
            break;
        case YYJSON_TYPE_ARR: {
            const char *err;
            JanetArray *array = janet_array(yyjson_arr_size(root));
            Janet       subval;
            size_t      idx, max;
            yyjson_val *val;
            yyjson_arr_foreach(root, idx, max, val) {
                err = decode_one(val, &subval, depth + 1);
                if (err) return err;
                janet_array_push(array, subval);
            }
            *out = janet_wrap_array(array);
            break;
        }
        case YYJSON_TYPE_OBJ: {
            const char *err;
            JanetTable *table = janet_table(yyjson_obj_size(root));
            Janet       subkey, subval;
            size_t      idx, max;
            yyjson_val *key, *val;
            yyjson_obj_foreach(root, idx, max, key, val) {
                if (!yyjson_is_str(key)) return "object key must be a str";
                if (depth & JSON_KEYWORD_KEY)
                    subkey = janet_ckeywordv(yyjson_get_str(key));
                else
                    subkey = janet_cstringv(yyjson_get_str(key));
                err = decode_one(val, &subval, depth + 1);
                if (err) return err;
                janet_table_put(table, subkey, subval);
            }
            *out = janet_wrap_table(table);
            break;
        }
    }

    return NULL;

badtype:
    return "type not supported";
recurdepth:
    return "recursed too deeply";
}

static Janet json_decode(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 3);
    Janet           ret = janet_wrap_nil();
    yyjson_read_err err;
    char           *p;
    if (janet_checktype(argv[0], JANET_BUFFER)) {
        JanetBuffer *buffer = janet_unwrap_buffer(argv[0]);
        /* Ensure 0 padded */
        janet_buffer_push_u8(buffer, 0);
        buffer->count--;
        p = (char *)buffer->data;
    } else {
        JanetByteView bytes = janet_getbytes(argv, 0);
        p                   = (char *)bytes.bytes;
    }
    yyjson_doc *doc = yyjson_read_opts(p, strlen(p), 0, NULL, &err);
    if (!doc) {
        janet_panicf("decode error at position %d: %s", err.pos, err.msg);
    }
    int flags = 0;
    if (argc > 1 && janet_truthy(argv[1])) flags |= JSON_KEYWORD_KEY;
    if (argc > 2 && janet_truthy(argv[2])) flags |= JSON_NULL_TO_NIL;
    const char *err_msg = decode_one(yyjson_doc_get_root(doc), &ret, flags);
    yyjson_doc_free(doc);
    if (err_msg) janet_panicf("decode error: %s", err_msg);
    return ret;
}

/*****************/
/* JSON Encoding */
/*****************/

static const char *encode_one(yyjson_mut_doc *doc, yyjson_mut_val **root, Janet x, int depth) {
    if ((depth & 0xFFFF) > JANET_RECURSION_GUARD) goto recurdepth;
    switch (janet_type(x)) {
        default:
            goto badtype;
        case JANET_NIL:
            *root = yyjson_mut_null(doc);
            break;
        case JANET_BOOLEAN:
            *root = yyjson_mut_bool(doc, janet_unwrap_boolean(x));
            break;
        case JANET_NUMBER: {
            double number = janet_unwrap_number(x);
            if (round(number) == number)
                *root = yyjson_mut_int(doc, (int64_t)number);
            else
                *root = yyjson_mut_real(doc, number);
            break;
        }
        case JANET_STRING:
        case JANET_SYMBOL:
        case JANET_KEYWORD:
        case JANET_BUFFER: {
            const uint8_t *bytes;
            int32_t        len;
            if (janet_keyeq(x, "null")) {
                *root = yyjson_mut_null(doc);
                break;
            }
            janet_bytes_view(x, &bytes, &len);
            *root = yyjson_mut_str(doc, (const char *)bytes);
            break;
        }
        case JANET_TUPLE:
        case JANET_ARRAY: {
            const char  *err;
            const Janet *items;
            int32_t      len;
            janet_indexed_view(x, &items, &len);
            yyjson_mut_val *array = yyjson_mut_arr(doc);
            yyjson_mut_val *subval;
            for (int32_t i = 0; i < len; i++) {
                if ((err = encode_one(doc, &subval, items[i], depth + 1))) return err;
                yyjson_mut_arr_append(array, subval);
            }
            *root = array;
            break;
        }
        case JANET_TABLE:
        case JANET_STRUCT: {
            const char    *err;
            const JanetKV *kvs;
            int32_t        count, capacity;
            janet_dictionary_view(x, &kvs, &count, &capacity);
            yyjson_mut_val *obj = yyjson_mut_obj(doc);
            yyjson_mut_val *subkey, *subval;
            for (int32_t i = 0; i < capacity; i++) {
                if (janet_checktype(kvs[i].key, JANET_NIL)) continue;
                if (!janet_checktypes(kvs[i].key, JANET_TFLAG_BYTES))
                    return "object key must be a byte sequence";
                if ((err = encode_one(doc, &subkey, kvs[i].key, depth + 1))) return err;
                if ((err = encode_one(doc, &subval, kvs[i].value, depth + 1))) return err;
                yyjson_mut_obj_add(obj, subkey, subval);
            }
            *root = obj;
            break;
        }
    }
    return NULL;

badtype:
    return "type not supported";
invalidutf8:
    return "string contains invalid utf-8";
recurdepth:
    return "recursed too deeply";
}

static Janet json_encode(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 3);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root;

    const char *err = encode_one(doc, &root, argv[0], 0);
    if (err) {
        yyjson_mut_doc_free(doc);
        janet_panicf("encode error: %s", err);
    }

    yyjson_mut_doc_set_root(doc, root);
    yyjson_write_flag flg = 0;
    if (argc > 1 && janet_truthy(argv[1])) flg |= YYJSON_WRITE_PRETTY_TWO_SPACES;

    size_t       dflt_len;
    char        *json = yyjson_mut_write(doc, flg, &dflt_len);
    JanetBuffer *buf  = janet_optbuffer(argv, argc, 2, dflt_len);
    janet_buffer_push_cstring(buf, json);

    yyjson_mut_doc_free(doc);
    return janet_wrap_buffer(buf);
}

/****************/
/* Module Entry */
/****************/

static const JanetReg cfuns[] = {
    {"encode", json_encode,
     "(yyjson/encode x &opt pretty buf)\n\n"
     "Encodes a janet value in JSON (utf-8). If pretty is truthy, writes JSON with a pretty "
     "format uing a 2-space indent. If buf is provided, the JSON is append to "
     "buf instead of a new buffer. Returns the modifed buffer."},
    {"decode", json_decode,
     "(yyjson/decode json-source &opt keywords nils)\n\n"
     "Returns a janet object after parsing JSON. If keywords is truthy, string "
     "keys will be converted to keywords. If nils is truthy, null will become nil instead "
     "of the keyword :null."},
    {NULL, NULL, NULL}};

JANET_MODULE_ENTRY(JanetTable *env) { janet_cfuns(env, "yyjson", cfuns); }
