#include "intro.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#define STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_NOFLOAT
#include "stb_sprintf.h"

#define DEPRECATED _Static_assert(0, "use of deprecated function")
#define sprintf(...) DEPRECATED
#define snprintf(...) DEPRECATED
#define vsprintf(...) DEPRECATED
#define vsnprintf(...) DEPRECATED

#define LENGTH(a) (sizeof(a)/sizeof(*(a)))
#define strput(a,v) memcpy(arraddnptr(a, strlen(v)), v, strlen(v))
#define strputn(a,v,n) memcpy(arraddnptr(a, n), v, n)
#define strputnull(a) arrput(a,0)
// index of last put or get
#define shtemp(t) stbds_temp((t)-1)
#define hmtemp(t) stbds_temp((t)-1)

__attribute__ ((format (printf, 2, 3)))
void
strputf(char ** p_str, const char * format, ...) {
    va_list args_original;
    va_start(args_original, format);

    while (1) {
        va_list args;
        va_copy(args, args_original);

        char * loc = *p_str + arrlen(*p_str);
        size_t n = arrcap(*p_str) - arrlen(*p_str);
        size_t pn = stbsp_vsnprintf(loc, n, format, args);
        if (pn <= n) {
            int offset = *(loc+pn-1) == '\0' ? -1 : 0;
            arrsetlen(*p_str, arrlen(*p_str) + pn + offset);
            break;
        } else {
            size_t p_cap = arrcap(*p_str);
            arrsetcap(*p_str, p_cap ? (p_cap << 1) : 64);
        }

        va_end(args);
    }

    va_end(args_original);
}

#include "lexer.c"
#include "pre.c"

struct name_set_s {
    char * key;
} * name_set = NULL;

struct type_set_s {
    IntroType key;
    IntroType * value;
} * type_set = NULL;

static const char * IntroCategory_strings [INTRO_CATEGORY_COUNT] = {
    "INTRO_UNKNOWN",

    "INTRO_FLOATING",
    "INTRO_SIGNED",
    "INTRO_UNSIGNED",

    "INTRO_STRUCT",
    "INTRO_ENUM",
};

typedef struct KnownType {
    char * key;
    int value;
    IntroCategory category;
    uint16_t indirection_level;
    uint32_t * indirection;
    union {
        IntroStruct * i_struct;
        IntroEnum * i_enum;
        int * forward_list;
    };
} KnownType;

static const KnownType type_list [] = {
    {"uint8_t", 1, INTRO_UNSIGNED}, {"uint16_t", 2, INTRO_UNSIGNED}, {"uint32_t", 4, INTRO_UNSIGNED}, {"uint64_t", 8, INTRO_UNSIGNED},
    {"int8_t", 1, INTRO_SIGNED}, {"int16_t", 2, INTRO_SIGNED}, {"int32_t", 4, INTRO_SIGNED}, {"int64_t", 8, INTRO_SIGNED},
    {"size_t", sizeof(size_t), INTRO_UNSIGNED}, {"ptrdiff_t", sizeof(ptrdiff_t), INTRO_SIGNED},

    {"bool", sizeof(bool), INTRO_UNSIGNED}, {"char", 1, INTRO_SIGNED}, {"unsigned char", 1, INTRO_UNSIGNED},
    {"short", sizeof(short), INTRO_SIGNED}, {"int", sizeof(int), INTRO_SIGNED}, {"long", sizeof(long), INTRO_SIGNED},
    {"unsigned short", sizeof(short), INTRO_UNSIGNED}, {"unsigned int", sizeof(int), INTRO_UNSIGNED}, {"unsigned long", sizeof(long), INTRO_UNSIGNED},
    {"float", 4, INTRO_FLOATING}, {"double", 8, INTRO_FLOATING},
};

KnownType * known_types = NULL;

IntroStruct ** structs = NULL;
IntroEnum   ** enums = NULL;

static uint32_t ZERO = 0;

struct nested_info_s {
    void * key;       // pointer of type that is nested
    int struct_index; // index of struct it is nested into
    int member_index; // index of member
    char * parent_member_name;
    char * grand_papi_name;
} * nested_info = NULL;

typedef struct Delcaration {
    IntroType type;
    Token type_tk;
    bool is_anonymous;
    bool is_nested;
    bool success;
} Declaration;

typedef struct Declaration2 {
    char * name;
    uint32_t * indirection;
    uint16_t indirection_level;
    bool success;
} Declaration2;

static char *
cache_name(char * name) {
    long index = shgeti(name_set, name);
    if (index >= 0) {
        return name_set[index].key;
    } else {
        return shputs(name_set, (struct name_set_s){name});
    }
}

static int
get_line(char * begin, char * pos, char ** o_start_of_line, char ** o_filename) {
    FileLoc * loc = NULL;
    for (int i = arrlen(file_location_lookup) - 1; i >= 0; i--) {
        if (pos - begin >= file_location_lookup[i].offset) {
            loc = &file_location_lookup[i];
            break;
        }
    }
    if (loc == NULL) return -1;
    char * s = begin + loc->offset;
    char * last_line = s;
    int line_num = loc->line;
    while (s < pos) {
        if (*s++ == '\n') {
            last_line = s;
            line_num++;
        }
    }
    if (o_start_of_line) *o_start_of_line = last_line;
    if (o_filename) *o_filename = loc->filename;
    return line_num;
}

#define BOLD_RED "\e[1;31m"
#define WHITE "\e[0;37m"
static void
parse_error_internal(char * buffer, Token * tk, char * message) {
    char * start_of_line;
    char * filename;
    int line_num = get_line(buffer, tk->start, &start_of_line, &filename);
    char * s = NULL;
    if (line_num < 0) {
        strputf(&s, "Error (?:?): %s\n\n", message ? message : "Failed to parse.");
        return;
    }
    char * end_of_line = strchr(tk->start + tk->length, '\n') + 1;
    strputf(&s, "Error (%s:%i): %s\n\n", filename, line_num, message ? message : "Failed to parse.");
    strputf(&s, "%.*s", (int)(tk->start - start_of_line), start_of_line);
    strputf(&s, BOLD_RED "%.*s" WHITE, tk->length, tk->start);
    strputf(&s, "%.*s", (int)(end_of_line - (tk->start + tk->length)), tk->start + tk->length);
    for (int i=0; i < (tk->start - start_of_line); i++) arrput(s, ' ');
    for (int i=0; i < (tk->length); i++) arrput(s, '~');
    arrput(s, '\n');
    strputnull(s);
    fputs(s, stderr);
}
#define parse_error(tk,message) parse_error_internal(buffer, tk, message)

Declaration2 parse_declaration(char * buffer, char ** o_s);
Declaration parse_type(char * buffer, char ** o_s);

IntroType
combine_type_and_declaration(IntroType * t, Declaration2 * in) {
    IntroType type = *t;
    if (type.indirection_level > 0) {
        uint32_t * new_indirection = NULL, * n;
        n = arraddnptr(new_indirection, in->indirection_level);
        for (int i=0; i < in->indirection_level; i++) {
            n[i] = in->indirection[i];
        }
        n = arraddnptr(new_indirection, type.indirection_level);
        for (int i=0; i < type.indirection_level; i++) {
            n[i] = type.indirection[i];
        }
        if (type.indirection != &ZERO) arrfree(type.indirection);
        type.indirection_level += in->indirection_level;
        type.indirection = new_indirection;
    } else {
        type.indirection_level = in->indirection_level;
        type.indirection = in->indirection;
    }
    return type;
}

int
parse_struct(char * buffer, char ** o_s, bool is_union) {
    IntroStruct struct_ = {0};
    struct_.is_union = is_union;

    Token tk = next_token(o_s);
    if (tk.type == TK_IDENTIFIER) {
        char * temp = copy_and_terminate(tk.start, tk.length);
        struct_.name = cache_name(temp);
        free(temp);
        tk = next_token(o_s);
    }

    if (!(tk.type == TK_BRACE && tk.is_open)) {
        if (tk.type == TK_IDENTIFIER || tk.type == TK_STAR) return 2;
        parse_error(&tk, "Expected open brace here.");
        return 1;
    }

    IntroMember * members = NULL;
    while (1) {
        Declaration decl = parse_type(buffer, o_s);
        if (!decl.success) {
            if (decl.type_tk.type == TK_BRACE && !decl.type_tk.is_open) {
                break;
            } else {
                if (decl.type_tk.length > 0) {
                    parse_error(&decl.type_tk, "Cannot parse symbol in type.");
                }
                return 1;
            }
        }

        Token tk = next_token(o_s);
        if (tk.type == TK_SEMICOLON) {
            if (decl.type.category == INTRO_STRUCT && decl.type.indirection_level == 0) {
                IntroStruct * s = decl.type.i_struct;
                for (int i=0; i < s->count_members; i++) {
                    arrput(members, s->members[i]);
                }
                if (decl.is_nested) {
                    (void)arrpop(structs);
                }
                continue;
            } else {
                parse_error(&tk, "Struct member has no name or type is unknown.");
                return 1;
            }
        } else {
            *o_s = tk.start;
            while (1) {
                IntroMember member = {0};

                Declaration2 in = parse_declaration(buffer, o_s);
                if (!in.success) return 1;
                IntroType type = combine_type_and_declaration(&decl.type, &in);

                if (type.category == INTRO_UNKNOWN && type.indirection_level == 0) {
                    parse_error(&decl.type_tk, "Unknown type.");
                    return 1;
                }

                member.name = in.name;

                if (hmgeti(type_set, type) >= 0) {
                    member.type = type_set[hmtemp(type_set)].value;
                } else {
                    IntroType * stored = malloc(sizeof(IntroType));
                    memcpy(stored, &type, sizeof(IntroType));
                    hmput(type_set, type, stored);
                    member.type = stored;
                }

                if (decl.is_nested) {
                    struct nested_info_s info = {0};
                    info.key = type.i_struct;
                    info.struct_index = arrlen(structs);
                    info.member_index = arrlen(members);
                    hmputs(nested_info, info);
                }
                arrput(members, member);

                tk = next_token(o_s);
                if (tk.type == TK_SEMICOLON) {
                    break;
                } else if (tk.type == TK_COMMA) {
                } else {
                    parse_error(&tk, "Cannot parse symbol in member declaration. Expected ';' or ','.");
                    return 1;
                }
            }
        }
    }

    struct_.count_members = arrlen(members);

    IntroStruct * result = malloc(sizeof(IntroStruct) + sizeof(IntroMember) * arrlen(members));
    memcpy(result, &struct_, sizeof(IntroStruct));
    memcpy(result->members, members, sizeof(IntroMember) * arrlen(members));
    arrfree(members);

    if (struct_.name != NULL) {
        char * struct_type_name = NULL;
        if (is_union) {
            strput(struct_type_name, "union ");
        } else {
            strput(struct_type_name, "struct ");
        }
        strput(struct_type_name, result->name);
        strputnull(struct_type_name);

        KnownType struct_type;
        struct_type.key = struct_type_name;
        struct_type.value = 0;
        struct_type.category = INTRO_STRUCT;
        struct_type.i_struct = result;
        
        KnownType * prev = shgetp_null(known_types, struct_type_name);
        if (prev != NULL && prev->category == INTRO_UNKNOWN) {
            for (int i=0; i < arrlen(prev->forward_list); i++) {
                KnownType * ft = &known_types[prev->forward_list[i]];
                ft->category = INTRO_STRUCT;
                ft->i_struct = result;
            }
            arrfree(prev->forward_list);
        }
        shputs(known_types, struct_type);

        arrfree(struct_type_name);
    }

    arrput(structs, result);

    return 0;
}

int
parse_enum(char * buffer, char ** o_s) {
    IntroEnum enum_ = {0};

    Token tk = next_token(o_s);
    if (tk.type == TK_IDENTIFIER) {
        char * temp = copy_and_terminate(tk.start, tk.length);
        enum_.name = cache_name(temp);
        free(temp);
        tk = next_token(o_s);
    }

    if (!(tk.type == TK_BRACE && tk.is_open)) {
        if (tk.type == TK_IDENTIFIER || tk.type == TK_STAR) return 2;
        parse_error(&tk, "Expected open brace here.");
        return 1;
    }

    enum_.is_flags = true;
    enum_.is_sequential = true;
    IntroEnumValue * members = NULL;
    int next_int = 0;
    int mask = 0;
    while (1) {
        IntroEnumValue v = {0};
        Token name = next_token(o_s);
        if (name.type == TK_BRACE && !name.is_open) {
            break;
        }
        if (name.type != TK_IDENTIFIER) {
            parse_error(&name, "Expected identifier.");
            return 1;
        }

        char * new_name = copy_and_terminate(name.start, name.length);
        if (shgeti(name_set, new_name) >= 0) {
            parse_error(&name, "Cannot define enumeration with reserved name.");
            return 1;
        }
        v.name = cache_name(new_name);

        tk = next_token(o_s);
        bool set = false;
        bool is_last = false;
        if (tk.type == TK_COMMA) {
            v.value = next_int++;
        } else if (tk.type == TK_EQUAL) {
            long num = strtol(*o_s, o_s, 0);
            if (num == 0 && errno != 0) {
                parse_error(&tk, "Unable to parse enumeration value.");
                return 1;
            }
            v.value = (int)num;
            if (v.value != next_int) {
                enum_.is_sequential = false;
            }
            next_int = v.value + 1;
            set = true;
        } else if (tk.type == TK_BRACE && !tk.is_open) {
            v.value = next_int;
            is_last = true;
        } else {
            parse_error(&tk, "Unexpected symbol.");
            return 1;
        }

        if (mask & v.value) enum_.is_flags = false;
        mask |= v.value;

        arrput(members, v);

        if (is_last) break;

        if (set) {
            tk = next_token(o_s);
            if (tk.type == TK_COMMA) {
            } else if (tk.type == TK_BRACE && !tk.is_open) {
                break;
            } else {
                parse_error(&tk, "Unexpected symbol.");
                return 1;
            }
        }
    };

    enum_.count_members = arrlen(members);

    IntroEnum * result = malloc(sizeof(IntroEnum) + sizeof(*members) * arrlen(members));
    memcpy(result, &enum_, sizeof(IntroEnum));
    memcpy(result->members, members, sizeof(*members) * arrlen(members));
    arrfree(members);

    if (enum_.name != NULL) {
        char * enum_type_name = NULL;
        strput(enum_type_name, "enum ");
        strput(enum_type_name, enum_.name);
        strputnull(enum_type_name);

        KnownType enum_type;
        enum_type.key = enum_type_name;
        enum_type.value = 0;
        enum_type.category = INTRO_ENUM;
        enum_type.i_enum = result;

        KnownType * prev = shgetp_null(known_types, enum_type_name);
        if (prev != NULL && prev->category == INTRO_UNKNOWN) {
            for (int i=0; i < arrlen(prev->forward_list); i++) {
                KnownType * ft = &known_types[prev->forward_list[i]];
                ft->category = INTRO_ENUM;
                ft->i_enum = result;
            }
            arrfree(prev->forward_list);
        }
        shputs(known_types, enum_type);

        arrfree(enum_type_name);
    }

    arrput(enums, result);

    return 0;
}

bool
is_ignored(Token * tk) {
    return tk_equal(tk, "const") || tk_equal(tk, "static");
}

Declaration
parse_type(char * buffer, char ** o_s) {
    Declaration result = {0};

    IntroType type = {0};
    char * type_name = NULL;

    Token first;
    do {
        first = next_token(o_s);
    } while (first.type == TK_IDENTIFIER && is_ignored(&first));
    if (first.type != TK_IDENTIFIER) {
        result.type_tk = first;
        return result;
    }

    result.type_tk.start = first.start;
    result.type_tk.type = TK_IDENTIFIER;

    strputn(type_name, first.start, first.length);

    bool is_struct = tk_equal(&first, "struct");
    bool is_union  = tk_equal(&first, "union");
    bool is_enum   = tk_equal(&first, "enum");
    if (is_struct || is_union || is_enum) {
        char * after_keyword = *o_s;
        int error;
        if (is_struct) {
            error = parse_struct(buffer, o_s, false);
        } else if (is_union) {
            error = parse_struct(buffer, o_s, true);
        } else {
            error = parse_enum(buffer, o_s);
        }
        if (error == 2) {
            *o_s = after_keyword;
            Token tk = next_token(o_s);
            strput(type_name, " ");
            strputn(type_name, tk.start, tk.length);
        } else if (error != 0) {
            return result;
        } else {
            char * name;
            result.is_nested = true;
            if (is_struct || is_union) {
                name = arrlast(structs)->name;
                type.category = INTRO_STRUCT;
                type.i_struct = arrlast(structs);
            } else {
                name = arrlast(enums)->name;
                type.category = INTRO_ENUM;
                type.i_enum   = arrlast(enums);
            }
            if (name == NULL) {
                result.is_anonymous = true;
            } else {
                strput(type_name, " ");
                strput(type_name, name);
            }
        }
    } else {
        Token tk, ltk = next_token(o_s), lltk = first;
        if (ltk.type == TK_IDENTIFIER) {
            while ((tk = next_token(o_s)).type == TK_IDENTIFIER) {
                strput(type_name, " ");
                strputn(type_name, ltk.start, ltk.length);
                lltk = ltk;
                ltk = tk;
            }
        }
        result.type_tk.length = lltk.start - first.start + lltk.length;
        *o_s = ltk.start;
    }

    strputnull(type_name);
    type.name = cache_name(type_name);
    arrfree(type_name);

    if (type.category == INTRO_UNKNOWN) {
        KnownType * kt = shgetp_null(known_types, type.name);
        if (kt != NULL) {
            type.size = type.indirection_level > 0 ? sizeof(void *) : kt->value;
            type.category = kt->category;
            type.indirection_level = kt->indirection_level;
            type.indirection = kt->indirection;
            if (kt->i_struct) type.i_struct = kt->i_struct; // also covers i_enum
        }
    }

    result.type = type;
    result.success = true;
    return result;
}

// NOTE: when variable length "array"s (just pointers with a length paramenter using annotations) 
// are implemented it might be better to be consistent and make arrays their own type.
// i am still figuring out how to structure the data in a way that makes sense...
Declaration2
parse_declaration(char * buffer, char ** o_s) {
    Declaration2 result = {0};

    uint32_t * indirection = NULL;
    uint32_t * temp = NULL;
    char * paren;

    Token tk;
    char * end = *o_s;
    do {
        paren = NULL;

        int pointer_level = 0;
        while ((tk = next_token(o_s)).type == TK_STAR) {
            pointer_level += 1;
        }

        if (tk.type == TK_PARENTHESIS && tk.is_open) {
            int depth = 1;
            paren = tk.start + 1;
            while (depth > 0) {
                tk = next_token(o_s);
                if (tk.type == TK_PARENTHESIS) {
                    if (tk.is_open) depth += 1;
                    else depth -= 1;
                }
            }
            tk = next_token(o_s);
        }

        if (tk.type == TK_IDENTIFIER) {
            char * temp = copy_and_terminate(tk.start, tk.length);
            result.name = cache_name(temp);
            free(temp);
            tk = next_token(o_s);
        }

        arrsetlen(temp, 0);
        while (tk.type == TK_BRACKET && tk.is_open) {
            tk = next_token(o_s);
            if (tk.type == TK_IDENTIFIER) {
                long num = strtol(tk.start, NULL, 0);
                if (num <= 0) {
                    parse_error(&tk, "Invalid array size.");
                    return result;
                }
                arrput(temp, (uint32_t)num);
                tk = next_token(o_s);
                if (!(tk.type == TK_BRACKET && !tk.is_open)) {
                    parse_error(&tk, "Invalid symbol. Expected closing bracket ']'.");
                    return result;
                }
            } else if (tk.type == TK_BRACKET && !tk.is_open) {
                arrput(temp, INTRO_ZERO_LENGTH);
            } else {
                parse_error(&tk, "Invalid symbol. Expected array size or closing bracket ']'.");
                return result;
            }
            tk = next_token(o_s);
        }

        if (tk.start > end) end = tk.start;

        for (int i=0; i < pointer_level; i++) {
            arrput(indirection, INTRO_POINTER);
        }
        for (int i = arrlen(temp) - 1; i >= 0; i--) {
            arrput(indirection, temp[i]);
        }
    } while ((*o_s = paren) != NULL);

    // reverse array
    for (int i=0; i < arrlen(indirection) / 2; i++) {
        int latter_index = arrlen(indirection) - i - 1;
        uint32_t t = indirection[i];
        indirection[i] = indirection[latter_index];
        indirection[latter_index] = t;
    }

    if (arrlen(indirection) == 1 && indirection[0] == 0) {
        arrfree(indirection);
        result.indirection_level = 1;
        result.indirection = &ZERO;
    } else {
        result.indirection_level = arrlen(indirection);
        result.indirection = indirection;
    }

    arrfree(temp);
    *o_s = end;
    result.success = true;
    return result;
}

int
parse_typedef(char * buffer, char ** o_s) {
    Declaration decl = parse_type(buffer, o_s);
    if (!decl.success) {
        if (decl.type_tk.length > 0) {
            parse_error(&decl.type_tk, "Cannot parse symbol in type.");
        }
        return 1;
    }
    Declaration2 in = parse_declaration(buffer, o_s);
    if (!in.success) return 1;
    decl.type = combine_type_and_declaration(&decl.type, &in);
    char * new_type_name = in.name;

    Token semicolon;
    if ((semicolon = next_token(o_s)).type != TK_SEMICOLON) {
        parse_error(&semicolon, "Cannot parse symbol in typedef. Expected ';'");
        return 1;
    }

    if (decl.is_anonymous) {
        KnownType nt = {0};
        nt.key = new_type_name;
        nt.category = decl.type.category;
        nt.indirection_level = decl.type.indirection_level;
        nt.indirection = decl.type.indirection;
        if (decl.type.category == INTRO_STRUCT) {
            nt.i_struct = arrlast(structs);
            nt.i_struct->name = nt.key;
        } else if (decl.type.category == INTRO_ENUM) {
            nt.i_enum = arrlast(enums);
            nt.i_enum->name = nt.key;
        } else {
            assert(false /* what did you do */);
        }
        shputs(known_types, nt);
    } else {
        char * type_name = decl.type.name;
        KnownType * kt = shgetp_null(known_types, type_name);
        if (kt != NULL) {
            KnownType nt = *kt;
            nt.key = new_type_name;
            nt.indirection_level = decl.type.indirection_level;
            nt.indirection = decl.type.indirection;
            shputs(known_types, nt);
            if (kt->category == INTRO_UNKNOWN) {
                arrput(kt->forward_list, shtemp(known_types));
            }
        } else {
            KnownType nt = {0};
            nt.key = new_type_name;
            nt.category = INTRO_UNKNOWN;
            nt.indirection_level = decl.type.indirection_level;
            nt.indirection = decl.type.indirection;
            shputs(known_types, nt);

            KnownType ut = {0};
            ut.key = type_name;
            ut.category = INTRO_UNKNOWN;
            ut.forward_list = NULL;
            arrput(ut.forward_list, shtemp(known_types));
            shputs(known_types, ut);
        }
    }

    return 0;
}

char *
get_parent_member_name(IntroStruct * parent, int parent_index, char ** o_grand_papi_name) {
    struct nested_info_s * nest = hmgetp_null(nested_info, parent);
    if (nest) {
        IntroStruct * grand_parent = structs[nest->struct_index];
        int grand_parent_index = nest->member_index;

        char * result = get_parent_member_name(grand_parent, grand_parent_index, o_grand_papi_name);
        arrput(result, '.');
        strput(result, parent->members[parent_index].name);
        return result;
    } else {
        char * result = NULL;
        strput(result, parent->members[parent_index].name);
        char * grand_papi_name = NULL;
        if (shgeti(known_types, parent->name) < 0) {
            if (parent->is_union) {
                strput(grand_papi_name, "union ");
            } else {
                strput(grand_papi_name, "struct ");
            }
        }
        strput(grand_papi_name, parent->name);
        strputnull(grand_papi_name);
        *o_grand_papi_name = grand_papi_name;
        return result;
    }
}

int
main(int argc, char ** argv) {
    char * output_filename;
    char * buffer = run_preprocessor(argc, argv, &output_filename);
    char * s = buffer;

    sh_new_arena(known_types);
    sh_new_arena(name_set);

    for (int i=0; i < LENGTH(type_list); i++) {
        shputs(known_types, type_list[i]);
    }

    Token key;
    while ((key = next_token(&s)).type != TK_END) {
        if (key.type == TK_IDENTIFIER) {
            int error = 0;
            if (tk_equal(&key, "struct")) {
                error = parse_struct(buffer, &s, false);
            } else if (tk_equal(&key, "union")) {
                error = parse_struct(buffer, &s, true);
            } else if (tk_equal(&key, "enum")) {
                error = parse_enum(buffer, &s);
            } else if (tk_equal(&key, "typedef")) {
                error = parse_typedef(buffer, &s);
            }
            if (error) return error;
        }
    }

    for (int i=0; i < hmlen(type_set); i++) {
        IntroType * t = type_set[i].value;
        if (t->category == INTRO_UNKNOWN) {
            KnownType * kt = shgetp_null(known_types, t->name);
            if (kt == NULL || kt->category == INTRO_UNKNOWN) {
                fputs("Error: Type is never defined: ", stderr);
                fputs(t->name, stderr);
                fputs(".\n", stderr);
                return 1;
            }
            (void)hmdel(type_set, *t);
            t->category = kt->category;
            if (t->category == INTRO_STRUCT) {
                t->i_struct = kt->i_struct;
            } else if (t->category == INTRO_ENUM) {
                t->i_enum = kt->i_enum;
            }
            hmput(type_set, *t, t);
        }
    }

    char num_buf [64];
    char * str = NULL;

    int anon_index = 0;
    for (int i=0; i < arrlen(structs); i++) {
        IntroStruct * s = structs[i];
        if (!s->name) {
            int len = stbsp_sprintf(num_buf, "Anon_%i", anon_index++);
            s->name = copy_and_terminate(num_buf, len);
        }
        struct nested_info_s * nest = hmgetp_null(nested_info, s);
        if (nest) {
            IntroStruct * parent = structs[nest->struct_index];
            int parent_index = nest->member_index;
            nest->parent_member_name = get_parent_member_name(parent, parent_index, &nest->grand_papi_name);
            strputnull(nest->parent_member_name);
        }
    }
    for (int i=0; i < arrlen(enums); i++) {
        IntroEnum * e = enums[i];
        if (!e->name) {
            int len = stbsp_sprintf(num_buf, "Anon_%i", anon_index++);
            e->name = copy_and_terminate(num_buf, len);
        }
        // copied from above (TODO)
        struct nested_info_s * nest = hmgetp_null(nested_info, e);
        if (nest) {
            IntroStruct * parent = structs[nest->struct_index];
            int parent_index = nest->member_index;
            nest->parent_member_name = get_parent_member_name(parent, parent_index, &nest->grand_papi_name);
            strputnull(nest->parent_member_name);
        }
    }

    strputf(&str, "\nstatic uint32_t intro_ZERO = 0;\n");
    strputf(&str, "\nstruct {\n");
    strputf(&str, "\tIntroType types [%i];\n", (int)hmlen(type_set));
    for (int enum_index = 0; enum_index < arrlen(enums); enum_index++) {
        strputf(&str, "\tIntroEnum * %s;\n", enums[enum_index]->name);
    }
    for (int struct_index = 0; struct_index < arrlen(structs); struct_index++) {
        strputf(&str, "\tIntroStruct * %s;\n", structs[struct_index]->name);
    }
    strputf(&str, "} intro_data;\n\n");

    strputf(&str, "void\n" "intro_init() {\n");

    strputf(&str, "\t// CREATE ENUM INTROSPECTION DATA\n");
    strputf(&str, "\n\tIntroEnumValue * v = NULL;\n");
    for (int enum_index = 0; enum_index < arrlen(enums); enum_index++) {
        IntroEnum * e = enums[enum_index];

        strputf(&str, "\n\t// %s\n\n", e->name);

        strputf(&str, "\tintro_data.%s = malloc(sizeof(IntroEnum) + %i * sizeof(IntroEnumValue));\n",
                e->name, e->count_members);

        strputf(&str, "\tintro_data.%s->name = \"%s\";\n",
                e->name, e->name);

        strputf(&str, "\tintro_data.%s->is_flags = %u;\n",
                e->name, e->is_flags);

        strputf(&str, "\tintro_data.%s->is_sequential = %u;\n",
                e->name, e->is_sequential);

        strputf(&str, "\tintro_data.%s->count_members = %u;\n",
                e->name, e->count_members);

        strputf(&str, "\tv = intro_data.%s->members;\n", e->name);

        for (int i=0; i < e->count_members; i++) {
            IntroEnumValue * v = &e->members[i];
            strputf(&str, "\n\tv[%i].name = \"%s\";\n", i, v->name);
            strputf(&str, "\tv[%i].value = %i;\n", i, v->value);
        }
    }

    strputf(&str, "\n\t// CREATE STRUCT INTROSPECTION DATA\n");
    strputf(&str, "\n\tIntroMember * m = NULL;\n");
    char * struct_name = NULL;
    for (int struct_index=0; struct_index < arrlen(structs); struct_index++) {
        IntroStruct * s = structs[struct_index];

        strputf(&str, "\n\t// %s\n\n", s->name);

        strputf(&str, "\tintro_data.%s = malloc(sizeof(IntroStruct) + %i * sizeof(IntroMember));\n",
                s->name, s->count_members);

        strputf(&str, "\tintro_data.%s->name = \"%s\";\n",
                s->name, s->name);

        strputf(&str, "\tintro_data.%s->is_union = %u;\n",
                s->name, s->is_union);

        strputf(&str, "\tintro_data.%s->count_members = %u;\n",
                s->name, s->count_members);

        strputf(&str, "\tm = intro_data.%s->members;\n", s->name);

        struct nested_info_s * nest = hmgetp_null(nested_info, s);
        if (!nest) {
            arrsetlen(struct_name, 0);
            if (shgeti(known_types, s->name) < 0) {
                strputf(&struct_name, s->is_union ? "union " : "struct ");
            }
            strputf(&struct_name, s->name);
            strputnull(struct_name);
        }

        for (int i=0; i < s->count_members; i++) {
            char m_buf [64];
            stbsp_sprintf(m_buf, "\tm[%i].", i);
            IntroMember * m = &s->members[i];

            strputf(&str, "\n%sname = ", m_buf);
            if (m->name) {
                strputf(&str, "\"%s\"", m->name);
            } else {
                arrput(str, '0');
            }
            strputf(&str, ";\n");

            strputf(&str, "%stype = &intro_data.types[%i];\n",
                    m_buf, (int)hmgeti(type_set, *m->type));

            if (!nest) {
                strputf(&str, "%soffset = offsetof(%s, %s);\n",
                        m_buf, struct_name, m->name);
            } else {
                strputf(&str, "%soffset = offsetof(%s, %s.%s) - offsetof(%s, %s);\n",
                        m_buf, nest->grand_papi_name, nest->parent_member_name, m->name,
                        nest->grand_papi_name, nest->parent_member_name);
            }
        }
    }
    arrfree(struct_name);

    strputf(&str, "\n\t// CREATE TYPES\n\n");
    strputf(&str, "\tIntroType * t = intro_data.types;\n\n");
    for (int type_index = 0; type_index < hmlen(type_set); type_index++) {
        char t_buf [64];
        stbsp_sprintf(t_buf, "\tt[%i].", type_index);

        IntroType * t = type_set[type_index].value;

        strputf(&str, "%sname = \"%s\";\n", t_buf, t->name);

        strputf(&str, "%ssize = ", t_buf);
        if (t->size) {
            strputf(&str, "%u", t->size);
        } else {
            struct nested_info_s * nest = NULL;
            if (t->category == INTRO_STRUCT || t->category == INTRO_ENUM) {
                nest = hmgetp_null(nested_info, t->i_struct);
            }
            strputf(&str, "sizeof(");
            if (!nest) {
                strputf(&str, t->name);
            } else {
                strputf(&str, "((%s*)0)->%s", nest->grand_papi_name, nest->parent_member_name);
            }
            arrput(str, ')');
        }
        strput(str, ";\n");

        strputf(&str, "%scategory = %s;\n", t_buf, IntroCategory_strings[t->category]);

        strputf(&str, "%sindirection_level = %u;\n", t_buf, t->indirection_level);
        if (t->indirection_level > 0) {
            if (t->indirection == &ZERO) {
                strputf(&str, "%sindirection = &intro_ZERO;\n", t_buf);
            } else {
                strputf(&str, "%sindirection = malloc(%u * 4);\n", t_buf, t->indirection_level);
                for (int i=0; i < t->indirection_level; i++) {
                    strputf(&str, "%sindirection[%i] = %u;\n", t_buf, i, t->indirection[i]);
                }
            }
        } else {
            strputf(&str, "%sindirection = 0;\n", t_buf);
        }

        if (t->category == INTRO_STRUCT) {
            strputf(&str, "%si_struct = intro_data.%s;\n",
                    t_buf, t->i_struct->name);
        } else if (t->category == INTRO_ENUM) {
            strputf(&str, "%si_enum = intro_data.%s;\n",
                    t_buf, t->i_enum->name);
        }

        arrput(str, '\n');
    }
    strputf(&str, "}\n\n");

    strputf(&str, "void\n");
    strputf(&str, "intro_uninit() {\n");
    for (int struct_index = 0; struct_index < arrlen(structs); struct_index++) {
        strputf(&str, "\tfree(intro_data.%s);\n", structs[struct_index]->name);
    }
    for (int enum_index = 0; enum_index < arrlen(enums); enum_index++) {
        strputf(&str, "\tfree(intro_data.%s);\n", enums[enum_index]->name);
    }
    strputf(&str, "\tfor (int i=0; i < %i; i++) {\n", (int)hmlen(type_set));
    strputf(&str, "\t\tIntroType * t = &intro_data.types[i];\n");
    strputf(&str, "\t\tif (t->indirection && t->indirection != &intro_ZERO) free(t->indirection);\n");
    strputf(&str, "\t}\n");
    strputf(&str, "}\n");

    FILE * save_file = fopen(output_filename, "w");
    fwrite(str, arrlen(str), 1, save_file);
    fclose(save_file);
}

// TODO LAND

/*
Refactoring
    The type system is kinda sloppy
    There is a lot of duplicate code
    Not big on the indirection system

Function pointers?

Bit fields?

Ignore functions
    should this be done in the preprocessor?

Custom format?

Serialization

User data (with macros)
    versions INTRO_V(value)
    id for serialization INTRO_ID(id)
    custom type data INTRO_DATA(value)
    union switch INTRO_SWITCH(member, value)
    default value INTRO_DEFAULT(value)
    array/string length -- INTRO_ARRLEN(member)

Transformative program arguments
    create typedefs for structs and enums
    create initializers
*/
