#include <intro.h>

#include ".//../test////basic.h"
#include "../test/../lib/intro.h"

struct Undefined1;
typedef struct Undefined2 Undefined2;
enum UndefEnum;

typedef struct Forward Forward;

struct TestPtr {
    Forward * p_forward;
    char **** indirect_4;
    struct TestPtr * recur;
};

struct Forward {
    u8 buffer [4];
    int i;
} inline_declaration;

typedef enum Skills {
    SKILL_PROGRAMMER  = 0x01,
    SKILL_WRITER      = 0x02,
    SKILL_ARTIST      = 0x04,
    SKILL_MUSICIAN    = 0x08,
    SKILL_ACROBAT     = 0x10,
    SKILL_BASEBALLBAT = 0x20,
} Skills;

typedef struct Nest {
    char * name;
    s32 id;
    struct {
        char * name;
        s32 id;
        struct {
            s32 age;
            enum {
                FRUIT_APPLE,
                FRUIT_BANANA,
                FRUIT_PEAR,
                FRUIT_PEACH,
                FRUIT_PLUM,
                FRUIT_ME,
                FRUIT_COUNT,
            } favorite_fruit;
        } son;
    } son;
    union {
        s32 id;
        float speed;
    } daughter;

    Skills skills;
} Nest;

typedef struct TestAttributes {
    u8 * buffer I(1, length buffer_size);
    s32 buffer_size;
    u32 v1 I(3);
    int h I(2, note "notes test, hello");
} TestAttributes;

enum {
    ANON_UNSEEN,
    ANON_INVISIBLE,
};

struct {
    int v1;

    struct {
        int v1;
    } nested;

    enum {
        GLOBAL_MAIN,
        GLOBAL_PAUSE,
        GLOBAL_LOADING,
    } state;

    bool defined; // make sure intro doesn't try to expand this
    _Bool test_boo;
} global_state;

typedef struct {
    struct Undefined1 * s_u1;
    Undefined2 * u2;
    struct Undefined2 * s_u2;

    enum UndefEnum * e_;

    //Undefined2 fail_0;
    //struct Undefined1 fail_1;
    //struct Undefined2 fail_2;
    //enum UndefEnum faile_enum;
} TestUndefined;

typedef struct {
    int v_int I(default 123);
    uint8_t v_u8 I(=1);
    int64_t v_s64 I(=-54321);
    float v_float I(= 3.14159);
    IntroType * type I(type);
    char * name I(= "Brian");

    uint8_t numbers [8] I(= {4, 5, 8, 9, 112, 9});
    char * words [5] I(= {"apple", "banana", "mango", "pineapple", "newline\ntest"});

    float * speeds I(length count_speeds, default {3.4, 5.6, 1.7, 8.2, 0.002});
    uint32_t count_speeds I(remove gui_show);

    Skills skills I(= SKILL_MUSICIAN | SKILL_PROGRAMMER);
} TestDefault;

typedef struct {
    int strange_array [sizeof(int) * 4];
    struct {
        char a I(= 3), b I(= 4);
        short u I(= 5);
    } anon_struct_array [4];
} Dumb;

typedef enum {
    SIZEOF_INT = sizeof(int),
    SIZEOF_SHORT = sizeof(short),
} EvilEnum;

// commented out because gcc doesn't enabled MS extensions by default
#if 0
typedef struct {
    char * name;
    Forward;
} MsExt;
#endif

#include "test.h.intro"
