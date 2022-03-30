#include "basic.h"

#include "../intro.h"

#include "test.h"
#include "test.h.intro"

static IntroType *
get_type_with_name(const char * name) {
    for (int i=0; i < LENGTH(__intro_types); i++) {
        IntroType * type = &__intro_types[i];
        if (type->name && strcmp(type->name, name) == 0) {
            return type;
        }
    }
    return NULL;
}

bool
intro_attribute_int(const IntroMember * m, int32_t attr_type, int32_t * o_int) {
    for (int i=0; i < m->count_attributes; i++) {
        const IntroAttributeData * attr = &m->attributes[i];
        if (attr->type == attr_type) {
            if (o_int) *o_int = attr->v.i;
            return true;
        }
    }
    return false;
}

bool
intro_attribute_member_index(const IntroMember * m, int32_t attr_type, int32_t * o_index) {
    for (int i=0; i < m->count_attributes; i++) {
        const IntroAttributeData * attr = &m->attributes[i];
        if (attr->type == attr_type) {
            if (o_index) *o_index = attr->v.i;
            return true;
        }
    }
    return false;
}

static const IntroType *
intro_base(const IntroType * type, int * o_depth) {
    int depth = 0;
    while (type->category == INTRO_ARRAY || type->category == INTRO_POINTER) {
        type = type->parent;
        depth++;
    }

    if (o_depth) *o_depth = depth;
    return type;
}

static bool
intro_is_complex(const IntroType * type) {
    return (type->category == INTRO_STRUCT
         || type->category == INTRO_UNION
         || type->category == INTRO_ENUM);
}

static bool
intro_is_basic(const IntroType * type) {
    return (type->category >= INTRO_U8 && type->category <= INTRO_F64);
}

static int
intro_size(const IntroType * type) {
    if (intro_is_basic(type)) {
        return (int) type->category & 0x00f;
    } else {
        return 0;
    }
}

int64_t
intro_int_value(const void * data, const IntroType * type) {
    int64_t result;
    switch(type->category) {
    case INTRO_U8:
        result = *(uint8_t *)data;
        break;
    case INTRO_U16:
        result = *(uint16_t *)data;
        break;
    case INTRO_U32:
        result = *(uint32_t *)data;
        break;
    case INTRO_U64:
        result = *(uint64_t *)data;
        break;

    case INTRO_S8:
        result = *(int8_t *)data;
        break;
    case INTRO_S16:
        result = *(int16_t *)data;
        break;
    case INTRO_S32:
        result = *(int32_t *)data;
        break;
    case INTRO_S64:
        result = *(int64_t *)data;
        break;

    default:
        result = 0;
        break;
    }

    return result;
}

void
intro_print_basic(const void * data, const IntroType * type) {
    switch(type->category) {
    case INTRO_U8:
    case INTRO_U16:
    case INTRO_U32:
    case INTRO_U64:
    case INTRO_S8:
    case INTRO_S16:
    case INTRO_S32:
    case INTRO_S64: {
        int64_t value = intro_int_value(data, type);
        printf("%li", value);
        break;
    }break;

    case INTRO_F32:
        printf("%f", *(float *)data);
        break;
    case INTRO_F64:
        printf("%f", *(double *)data);
        break;

    default:
        printf("NaN");
        break;
    }
}

void
intro_print_enum(int value, const IntroType * type) {
    const IntroEnum * i_enum = type->i_enum;
    if (i_enum->is_sequential) {
        if (value >= 0 && value < i_enum->count_members) {
            printf("%s", i_enum->members[value].name);
        } else {
            printf("%i", value);
        }
    } else if (i_enum->is_flags) {
        bool more_than_one = false;
        if (value) {
            for (int f=0; f < i_enum->count_members; f++) {
                if (value & i_enum->members[f].value) {
                    if (more_than_one) printf(" | ");
                    printf("%s", i_enum->members[f].name);
                    more_than_one = true;
                }
            }
        } else {
            printf("0");
        }
    } else {
        printf("%i", value);
    }
}

void
intro_print_basic_array(const void * data, const IntroType * type, int length) {
    int elem_size = intro_size(type);
    if (elem_size) {
        printf("{");
        for (int i=0; i < length; i++) {
            if (i > 0) printf(", ");
            intro_print_basic(data + elem_size * i, type);
        }
        printf("}");
    } else {
        printf("<concealed>");
    }
}

void
intro_print_type_name(const IntroType * type) {
    while (1) {
        if (type->category == INTRO_POINTER) {
            printf("*");
            type = type->parent;
        } else if (type->category == INTRO_ARRAY) {
            printf("[%u]", type->array_size);
            type = type->parent;
        } else if (type->name) {
            printf("%s", type->name);
            break;
        } else {
            printf("<anon>");
            break;
        }
    }
}

bool
intro_attribute_length(const void * struct_data, const IntroType * struct_type, const IntroMember * m, int64_t * o_length) {
    int32_t member_index;
    const void * m_data = struct_data + m->offset;
    if (intro_attribute_member_index(m, INTRO_ATTR_LENGTH, &member_index)) {
        const IntroMember * length_member = &struct_type->i_struct->members[member_index];
        const void * length_member_loc = struct_data + length_member->offset;
        *o_length = intro_int_value(length_member_loc, length_member->type);
        return true;
    } else {
        *o_length = 0;
        return false;
    }
}

typedef struct {
    int indent;
} IntroPrintOptions;

void
intro_print_struct(const void * data, const IntroType * type, const IntroPrintOptions * opt) {
    static const IntroPrintOptions opt_default = {0};
    static const char * tab = "    ";

    if (type->category != INTRO_STRUCT && type->category != INTRO_UNION) {
        return;
    }

    if (!opt) opt = &opt_default;

    printf("%s {\n", (type->category == INTRO_STRUCT)? "struct" : "union");

    for (int m_index = 0; m_index < type->i_struct->count_members; m_index++) {
        const IntroMember * m = &type->i_struct->members[m_index];
        const void * m_data = data + m->offset;
        for (int t=0; t < opt->indent + 1; t++) fputs(tab, stdout);
        printf("%s: ", m->name);
        intro_print_type_name(m->type);
        printf(" = ");
        if (intro_is_basic(m->type)) {
            intro_print_basic(m_data, m->type);
        } else {
            switch(m->type->category) {
            case INTRO_ARRAY: {
                int depth;
                const IntroType * base = intro_base(m->type, &depth);
                const int MAX_EXPOSED_LENGTH = 64;
                if (depth == 1 && intro_is_basic(base) && base->array_size <= MAX_EXPOSED_LENGTH) {
                    intro_print_basic_array(m_data, m->type, base->array_size);
                } else {
                    printf("<concealed>");
                }
            }break;

            case INTRO_POINTER: { // TODO
                void * ptr = *(void **)m_data;
                if (ptr) {
                    int depth;
                    const IntroType * base = intro_base(m->type, &depth);
                    if (depth == 1 && intro_is_basic(base)) {
                        int64_t length;
                        if (intro_attribute_length(data, type, m, &length)) {
                            intro_print_basic_array(ptr, base, length);
                        } else {
                            if (strcmp(base->name, "char") == 0) {
                                char * str = (char *)ptr;
                                const int max_string_length = 32;
                                if (strlen(str) <= max_string_length) {
                                    printf("\"%s\"", str);
                                } else {
                                    printf("\"%*.s...\"", max_string_length - 3, str);
                                }
                            } else {
                                intro_print_basic_array(ptr, base, 1);
                            }
                        }
                    } else {
                        printf("%p", ptr);
                    }
                } else {
                    printf("<null>");
                }
            }break;

            case INTRO_STRUCT: // FALLTHROUGH
            case INTRO_UNION: {
                IntroPrintOptions opt2 = *opt;
                opt2.indent++;
                intro_print_struct(m_data, m->type, &opt2);
            }break;

            case INTRO_ENUM: {
                intro_print_enum(*(int *)m_data, m->type);
            }break;

            default: {
                printf("<unknown>");
            }break;
            }
        }
        printf(";\n");
    }
    for (int t=0; t < opt->indent; t++) fputs(tab, stdout);
    printf("}");
}

int
main(int argc, char ** argv) {
    printf("==== TEST OUTPUT ====\n\n");

    TestAttributes obj = {0};
    obj.buffer_size = 8;
    obj.buffer = malloc(obj.buffer_size * sizeof(*obj.buffer));
    obj.v1 = 12345678;
    obj.h = -54321;
    for (int i=0; i < obj.buffer_size; i++) {
        obj.buffer[i] = i * i;
    }

    const IntroType * t_obj = get_type_with_name("TestAttributes");
    assert(t_obj && t_obj->category == INTRO_STRUCT);

    printf("obj = ");
    intro_print_struct(&obj, t_obj, NULL);
    printf("\n\n");

    /*=====================*/

    Nest nest = {
        .name = "Jon Garbuckle",
        .id = 32,
        .son = {
            .name = "Tim Garbuckle",
            .id = 33,
            .son = { .age = 5 },
        },
        .daughter = {
            .speed = 4.53,
        },
    };

    printf("nest = ");
    intro_print_struct(&nest, get_type_with_name("Nest"), NULL);
    printf("\n");

    return 0;
}
