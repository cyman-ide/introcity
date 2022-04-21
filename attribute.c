static struct attribute_map_s {
    char * key;
    int32_t type;
    int32_t value_type;
} * attribute_map = NULL;

static char ** note_set = NULL;

void
create_initial_attributes() {
    const struct attribute_map_s initial [] = {
        {"id",      INTRO_ATTR_ID,      INTRO_V_INT}, // NOTE: maybe this should be part of IntroMember since it is common?
        {"default", INTRO_ATTR_DEFAULT, INTRO_V_VALUE},
        {"length",  INTRO_ATTR_LENGTH,  INTRO_V_MEMBER},
        {"type",    INTRO_ATTR_TYPE,    INTRO_V_FLAG},
        {"note",    INTRO_ATTR_NOTE,    INTRO_V_STRING},
        {"alias",   INTRO_ATTR_ALIAS,   INTRO_V_STRING},
    };
    // NOTE: might need to do this later:
    //sh_new_arena(attribute_map);
    for (int i=0; i < LENGTH(initial); i++) {
        shputs(attribute_map, initial[i]);
    }
}

int
parse_attribute_register(ParseContext * ctx, char * s, int type, Token * type_tk) {
    const struct { char * key; int value_type; } value_type_lookup [] = {
        {"flag",      INTRO_V_FLAG},
        {"int",       INTRO_V_INT},
        {"float",     INTRO_V_FLOAT},
        {"value",     INTRO_V_VALUE},
        {"member",    INTRO_V_MEMBER},
        {"string",    INTRO_V_STRING},
    };

    Token tk0 = next_token(&s), tk1;
    if (tk0.type != TK_L_PARENTHESIS) {
        parse_error(ctx, &tk0, "Expected '('.");
        return 1;
    }

    tk0 = next_token(&s);
    if (tk0.type != TK_IDENTIFIER) {
        parse_error(ctx, &tk0, "Expected identifier.");
        return 1;
    }

    int value_type = INTRO_V_FLAG;
    char * name = NULL;
    Token * name_ref = NULL;

    tk1 = next_token(&s);
    if (tk1.type == TK_IDENTIFIER) {
        bool matched = false;
        for (int i=0; i < LENGTH(value_type_lookup); i++) {
            if (tk_equal(&tk0, value_type_lookup[i].key)) {
                value_type = value_type_lookup[i].value_type;
                matched = true;
                break;
            }
        }
        if (!matched) {
            parse_error(ctx, &tk0, "Unknown attribute value type.");
            return 1;
        }
        name = copy_and_terminate(tk1.start, tk1.length);
        name_ref = &tk1;
    } else if (tk1.type == TK_R_PARENTHESIS) {
        name = copy_and_terminate(tk0.start, tk0.length);
        name_ref = &tk0;
    } else {
        parse_error(ctx, &tk1, "Expected identifier or ')'.");
        return 1;
    }

    int map_index = shgeti(attribute_map, name);
    if (map_index >= 0) {
        parse_error(ctx, name_ref, "Attribute name is reserved.");
        return 1;
    }

    for (int i=0; i < shlen(attribute_map); i++) {
        if (attribute_map[i].type == type) {
            char * msg = NULL;
            strputf(&msg, "Attribute type (%i) is reserved by attribute '%s'.", type, attribute_map[i].key);
            strputnull(msg);
            parse_error(ctx, type_tk, msg);
            arrfree(msg);
            return 2;
        }
    }

    struct attribute_map_s entry;
    entry.key = name;
    entry.type = type;
    entry.value_type = value_type;
    shputs(attribute_map, entry);

    return 0;
}

bool
check_id_valid(const IntroStruct * i_struct, int id) {
    for (int member_index = 0; member_index < i_struct->count_members; member_index++) {
        const IntroMember * member = &i_struct->members[member_index];
        for (int attr_index = 0; attr_index < member->count_attributes; attr_index++) {
            const IntroAttributeData * attr = &member->attributes[attr_index];
            if (attr->type == INTRO_ATTR_ID && attr->v.i == id) {
                return false;
            }
        }
    }
    return true;
}

static char *
parse_escaped_string(Token * str_tk, size_t * o_length) {
    char * result = NULL;
    char * src = str_tk->start + 1;
    while (src < str_tk->start + str_tk->length - 1) {
        if (*src == '\\') {
            src++;
            char c;
            if (*src >= '0' && *src <= '9') {
                char * end;
                long num = strtol(src, &end, 8);
                if (end - src > 3) return NULL;
                src = end - 1;
                c = (char)num;
            } else {
                switch(*src) {
                case 'n':  c = '\n'; break;
                case 't':  c = '\t'; break;
                case '\\': c = '\\'; break;
                case '\'': c = '\''; break;
                case '\"': c = '\"'; break;
                case 'b':  c = '\b'; break;
                case 'v':  c = '\v'; break;
                case 'r':  c = '\r'; break;
                case 'f':  c = '\f'; break;
                case '?':  c = '?' ; break;
                case 'x': {
                    char * end;
                    src++;
                    long num = strtol(src, &end, 16);
                    if (end - src != 2) return NULL;
                    src = end - 1;
                    c = (char)num;
                }break;
                default: {
                    return NULL;
                }break;
                }
            }
            arrput(result, c);
        } else {
            arrput(result, *src);
        }
        src++;
    }
    strputnull(result);

    char * ret = malloc(arrlen(result));
    memcpy(ret, result, arrlen(result));
    *o_length = arrlen(result);
    arrfree(result);
    return ret;
}

// NOTE: value storing is fairly similar to some of what the city implementation does,
// maybe some code can be resued between those two systems
ptrdiff_t
store_value(ParseContext * ctx, const void * value, size_t value_size) {
    void * storage = arraddnptr(ctx->value_buffer, value_size);
    memcpy(storage, value, value_size); // NOTE: this is only correct for LE
    return (storage - (void *)ctx->value_buffer);
}

ptrdiff_t
store_ptr(ParseContext * ctx, void * data, size_t size) {
    static const uint8_t nothing [sizeof(size_t)] = {0};
    ptrdiff_t offset = store_value(ctx, &nothing, sizeof(size_t));
    PtrStore ptr_store = {0};
    ptr_store.value_offset = offset;
    ptr_store.data = data;
    ptr_store.data_size = size;
    arrput(ctx->ptr_stores, ptr_store);
    return offset;
}

ptrdiff_t parse_array_value(ParseContext * ctx, const IntroType * type, char ** o_s, uint32_t * o_count);

ptrdiff_t
parse_value(ParseContext * ctx, IntroType * type, char ** o_s, uint32_t * o_count) {
    if (type->category >= INTRO_U8 && type->category <= INTRO_S64) {
        long result = strtol(*o_s, o_s, 0);
        int size = type->category & 0x0f;
        return store_value(ctx, &result, size);
    } else if (type->category == INTRO_F32) {
        float result = strtof(*o_s, o_s);
        return store_value(ctx, &result, 4);
    } else if (type->category == INTRO_F64) {
        double result = strtod(*o_s, o_s);
        return store_value(ctx, &result, 8);
    } else if (type->category == INTRO_POINTER) {
        Token tk = next_token(o_s);
        if (tk.type == TK_STRING) {
            if (type->parent->category == INTRO_S8 && 0==strcmp(type->parent->name, "char")) {
                size_t length;
                char * str = parse_escaped_string(&tk, &length);
                ptrdiff_t result = store_ptr(ctx, str, length);
                return result;
            }
        } else {
            *o_s = tk.start;
            ptrdiff_t array_value_offset = parse_array_value(ctx, type, o_s, o_count);
            ptrdiff_t pointer_offset = store_value(ctx, &array_value_offset, sizeof(size_t));
            return pointer_offset;
        }
    } else if (type->category == INTRO_ARRAY) {
        ptrdiff_t result = parse_array_value(ctx, type, o_s, NULL);
        return result;
    } else if (type->category == INTRO_ENUM) {
        int result = 0;
        Token tk = next_token(o_s);
        while (1) {
            if (is_digit(*tk.start)) {
                *o_s = tk.start;
                result = strtol(*o_s, o_s, 0);
            } else if (tk.type == TK_IDENTIFIER) {
                const IntroEnum * i_enum = type->i_enum;
                for (int i=0; i < i_enum->count_members; i++) {
                    IntroEnumValue v = i_enum->members[i];
                    if (tk_equal(&tk, v.name)) {
                        result |= v.value;
                        break;
                    }
                }
            } else {
                parse_error(ctx, &tk, "Invalid symbol.");
                return 1;
            }
            tk = next_token(o_s);
            if (tk.type == TK_BAR) {
                tk = next_token(o_s);
            } else {
                *o_s = tk.start;
                break;
            }
        }
        return store_value(ctx, &result, sizeof(result));
    }
    return -1;
}

ptrdiff_t
parse_array_value(ParseContext * ctx, const IntroType * type, char ** o_s, uint32_t * o_count) {
    Token tk = next_token(o_s);

    ptrdiff_t result = arrlen(ctx->value_buffer);
    size_t array_element_size = intro_size(type->parent);
    uint32_t count = 0;

    if (tk.type == TK_L_BRACE) {
        while (1) {
            tk = next_token(o_s);
            if (tk.type == TK_COMMA) {
                parse_error(ctx, &tk, "Invalid symbol.");
                return -1;
            } else if (tk.type == TK_R_BRACE) {
                break;
            }
            *o_s = tk.start;
            parse_value(ctx, type->parent, o_s, NULL);
            count++;

            tk = next_token(o_s);
            if (tk.type == TK_COMMA) {
            } else if (tk.type == TK_R_BRACE) {
                break;
            } else {
                parse_error(ctx, &tk, "Invalid symbol.");
                return -1;
            }
        }
        if (type->category == INTRO_ARRAY) {
            int left = array_element_size * (type->array_size - count);
            if (count < type->array_size) {
                memset(arraddnptr(ctx->value_buffer, left), 0, left);
            }
        }
        if (o_count) *o_count = count;
    } else {
        parse_error(ctx, &tk, "Expected '{'.");
        return -1;
    }
    return result;
}

void
store_differed_ptrs(ParseContext * ctx) {
    for (int i=0; i < arrlen(ctx->ptr_stores); i++) {
        PtrStore ptr_store = ctx->ptr_stores[i];
        store_value(ctx, &ptr_store.data_size, 4);
        size_t offset = store_value(ctx, ptr_store.data, ptr_store.data_size);
        size_t * o_offset = (size_t *)(ctx->value_buffer + ptr_store.value_offset);
        *o_offset = offset;
        free(ptr_store.data);
    }
    arrsetlen(ctx->ptr_stores, 0);
}

int
handle_value_attribute(ParseContext * ctx, char ** o_s, IntroStruct * i_struct, int member_index, IntroAttributeData * data, Token * p_tk) {
    IntroType * type = i_struct->members[member_index].type;
    assert(arrlen(ctx->ptr_stores) == 0);
    uint32_t length_value = 0;
    ptrdiff_t value_offset = parse_value(ctx, type, o_s, &length_value);
    if (value_offset < 0) {
        parse_error(ctx, p_tk, "Error parsing value attribute.");
        return 1;
    }
    if (length_value) {
        DifferedDefault def = {
            .member_index = member_index,
            .attribute_type = data->type,
            .value = length_value,
        };
        arrput(ctx->differed_length_defaults, def);
    }
    store_differed_ptrs(ctx);
    data->v.i = value_offset;
    return 0;
}

int
parse_attribute(ParseContext * ctx, char ** o_s, IntroStruct * i_struct, int member_index, IntroAttributeData * o_result) {
    IntroAttributeData data = {0};
    Token tk = next_token(o_s);
    if (tk.type == TK_IDENTIFIER) {
        char * terminated_name = copy_and_terminate(tk.start, tk.length);
        int map_index = shgeti(attribute_map, terminated_name);
        free(terminated_name);
        if (map_index < 0) {
            parse_error(ctx, &tk, "No such attribute.");
            return 1;
        }

        data.type = attribute_map[map_index].type;
        data.value_type = attribute_map[map_index].value_type; // NOTE: you could just lookup the attribute's value type

        switch(data.value_type) {
        case INTRO_V_FLAG: {
            if (data.type == INTRO_ATTR_TYPE) {
                IntroType * type = i_struct->members[member_index].type;
                if (!(type->category == INTRO_POINTER && strcmp(type->parent->name, "IntroType") == 0)) {
                    parse_error(ctx, &tk, "Member must be of type 'IntroType *' to have type attribute.");
                    return 1;
                }
            }
            data.v.i = 0;
        } break;

        case INTRO_V_INT: {
            tk = next_token(o_s);
            long result = strtol(tk.start, o_s, 0);
            if (*o_s == tk.start) {
                parse_error(ctx, &tk, "Invalid integer.");
                return 1;
            }
            data.v.i = (int32_t)result;
            if (data.type == INTRO_ATTR_ID && !check_id_valid(i_struct, data.v.i)) {
                parse_error(ctx, &tk, "This ID is reserved.");
                return 1;
            }
        } break;

        case INTRO_V_FLOAT: {
            tk = next_token(o_s);
            float result = strtof(tk.start, o_s);
            if (*o_s == tk.start) {
                parse_error(ctx, &tk, "Invalid floating point number.");
                return 1;
            }
            data.v.f = result;
        } break;

        case INTRO_V_STRING: {
            tk = next_token(o_s);
            char * result = NULL;
            if (data.type == INTRO_ATTR_ALIAS && tk.type == TK_IDENTIFIER) {
                result = copy_and_terminate(tk.start, tk.length);
            } else {
                if (tk.type != TK_STRING) {
                    parse_error(ctx, &tk, "Expected string.");
                    return 1;
                }
                result = copy_and_terminate(tk.start+1, tk.length-2);
            }
            int32_t index = arrlen(note_set);
            arrput(note_set, result);
            data.v.i = index;
        } break;

        // TODO: error check: member used as length can't have a value if the member it is the length of has a value
        // TODO: error check: if multiple members use the same member for length, values can't have different lengths
        // NOTE: the previous 2 todo's do not apply if the values are for different attributes
        case INTRO_V_VALUE: {
            if (handle_value_attribute(ctx, o_s, i_struct, member_index, &data, &tk)) return 1;
        } break;

        case INTRO_V_MEMBER: {
            tk = next_token(o_s);
            if (tk.type != TK_IDENTIFIER || is_digit(tk.start[0])) {
                parse_error(ctx, &tk, "Expected member name.");
                return 1;
            }
            bool success = false;
            for (int mi=0; mi < i_struct->count_members; mi++) {
                if (tk_equal(&tk, i_struct->members[mi].name)) {
                    if (data.type == INTRO_ATTR_LENGTH) {
                        IntroType * type = i_struct->members[mi].type;
                        uint32_t category_no_size = type->category & 0xf0;
                        if (category_no_size != INTRO_SIGNED && category_no_size != INTRO_UNSIGNED) {
                            parse_error(ctx, &tk, "Length defining member must be of an integer type.");
                            return 1;
                        }
                    }
                    data.v.i = mi;
                    success = true;
                    break;
                }
            }
            if (!success) {
                parse_error(ctx, &tk, "No such member.");
                return 1;
            }
        } break;
        }
    }

    if (o_result) *o_result = data;
    return 0;
}

int
parse_attributes(ParseContext * ctx, char * s, IntroStruct * i_struct, int member_index, IntroAttributeData ** o_result, uint32_t * o_count_attributes) {
    IntroAttributeData * attributes = NULL;

    Token tk = next_token(&s);
    if (tk.type != TK_L_PARENTHESIS) {
        parse_error(ctx, &tk, "Expected '('.");
        return 1;
    }
    while (1) {
        tk = next_token(&s);
        IntroAttributeData data;
        if (tk.type == TK_IDENTIFIER) {
            if (is_digit(tk.start[0])) {
                data.type = INTRO_ATTR_ID;
                data.value_type = INTRO_V_INT;
                // @copy from above
                long result = strtol(tk.start, &s, 0);
                if (s == tk.start) {
                    parse_error(ctx, &tk, "Invalid integer.");
                    return 1;
                }
                data.v.i = (int32_t)result;
                if (!check_id_valid(i_struct, data.v.i)) {
                    parse_error(ctx, &tk, "This ID is reserved.");
                    return 1;
                }
            } else {
                s = tk.start;
                int error = parse_attribute(ctx, &s, i_struct, member_index, &data);
                if (error) return 1;
            }
            arrput(attributes, data);
        } else if (tk.type == TK_EQUAL) {
            data.type = INTRO_ATTR_DEFAULT;
            data.value_type = INTRO_V_VALUE;
            if (handle_value_attribute(ctx, &s, i_struct, member_index, &data, &tk)) return 1;

            arrput(attributes, data);
        } else if (tk.type == TK_R_PARENTHESIS) {
            break;
        } else {
            parse_error(ctx, &tk, "Invalid symbol.");
            return 1;
        }

        tk = next_token(&s);
        if (tk.type == TK_COMMA) {
        } else if (tk.type == TK_R_PARENTHESIS) {
            break;
        } else {
            parse_error(ctx, &tk, "Expected ',' or ')'.");
            return 1;
        }
    }

    if (o_result && o_count_attributes) {
        *o_count_attributes = arrlenu(attributes);
        *o_result = attributes;
    } else {
        arrfree(attributes);
    }
    return 0;
}

void
handle_differed_defaults(ParseContext * ctx, IntroStruct * i_struct) {
    for (int i=0; i < arrlen(ctx->differed_length_defaults); i++) {
        DifferedDefault def = ctx->differed_length_defaults[i];
        IntroMember * array_member = &i_struct->members[def.member_index];
        int32_t length_member_index;
        if (intro_attribute_int(array_member, INTRO_ATTR_LENGTH, &length_member_index)) {
            IntroMember * member = &i_struct->members[length_member_index];
            ptrdiff_t value_offset = store_value(ctx, &def.value, intro_size(member->type));
            IntroAttributeData data = {
                .type = def.attribute_type,
                .value_type = INTRO_V_VALUE,
                .v = {value_offset},
            };

            IntroAttributeData * new_attributes = NULL;
            for (int a=0; a < member->count_attributes; a++) {
                arrput(new_attributes, member->attributes[a]);
            }
            arrput(new_attributes, data);
            arrfree(member->attributes);
            member->attributes = new_attributes;
            member->count_attributes = arrlen(new_attributes);
        }
    }
    arrsetlen(ctx->differed_length_defaults, 0);
}
