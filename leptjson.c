#ifdef _WINDOWS
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#include "leptjson.h"
#include <assert.h>  /* assert() */
#include <errno.h>   /* errno, ERANGE */
#include <math.h>    /* HUGE_VAL */
#include <stdlib.h>  /* NULL, malloc(), realloc(), free(), strtod() */
#include <string.h>  /* memcpy() */

#ifndef LEPT_PARSE_STACK_INIT_SIZE
#define LEPT_PARSE_STACK_INIT_SIZE 256
#endif
//检查字符是否相同
#define EXPECT(c, ch)       do { assert(*c->json == (ch)); c->json++; } while(0)
//检查字符是否为0~9
#define ISDIGIT(ch)         ((ch) >= '0' && (ch) <= '9')
#define ISDIGIT1TO9(ch)     ((ch) >= '1' && (ch) <= '9')
//进行一波入栈操作
#define PUTC(c, ch)         do { *(char*)lept_context_push(c, sizeof(char)) = (ch); } while(0)

typedef struct {
    const char* json;
    char* stack;
    size_t size, top;
}lept_context;

//入栈的逻辑如下：
/*
1.首先我们需要确保推入的字符大小本身大于0
2然后检查推入之后栈顶否超出栈本身的内存大小
3.如果超出，先检查栈的内存大小，如果为0，就先分配一个足够大的值，本次是256
4.否则就对栈内存进行扩展，每次扩展1.5倍大小 >>1是移位相当于除以2，所以c->size=c->size+0.5*c->size
5.重新设置好的内存分配给原来的栈
6.当内存大小足够大时，ret就指向栈顶，栈顶同时+1
*/
static void* lept_context_push(lept_context* c, size_t size) {
    void* ret;
    assert(size > 0);
    if (c->top + size >= c->size) {
        if (c->size == 0)
            c->size = LEPT_PARSE_STACK_INIT_SIZE;
        while (c->top + size >= c->size)
            //当栈大小不够时，就会以原来内存的1.5倍进行扩展
            c->size += c->size >> 1;  /* c->size * 1.5 */
        c->stack = (char*)realloc(c->stack, c->size);
    }
    ret = c->stack + c->top;
    c->top += size;
    return ret;
}

//出栈的逻辑如下：
/*
1.首先确保此时栈顶时大于出栈的字节数的
2.接着原栈指向此时的栈顶（c->top-size）
*/
static void* lept_context_pop(lept_context* c, size_t size) {
    assert(c->top >= size);
    return c->stack + (c->top -= size);
}

//这个API用于判断传入字符串是否开头有空格，如果有，就先遍历过去知道没有空格。
static void lept_parse_whitespace(lept_context* c) {
    const char* p = c->json;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    c->json = p;
}
//用于解析 true,false,null
/*
1.首先判断，传入文本首字母开头是否为我们要的几个 'n' 't' 'f'
2.接着我们开始遍历字符串，此时可以注意到，为什么用 size_i，这代表字节索引单元
3.如果c->json中的字符串有一个不符合我们的要求，那就返回错误码
4.解析成功后，c-json就遍历完成
5.返回正确码
*/
static int lept_parse_literal(lept_context* c, lept_value* v, const char* literal, lept_type type) {
    size_t i;
    EXPECT(c, literal[0]);
    for (i = 0; literal[i + 1]; i++)
        if (c->json[i] != literal[i + 1])
            return LEPT_PARSE_INVALID_VALUE;
    c->json += i;
    v->type = type;
    return LEPT_PARSE_OK;
}
//对于解析数字逻辑分为以下几步：
/*
1.首先我们先需要判断首字符，我们首先得判断 '-'和'0' 两个字符，比方说'-1.xxx'或者'0.xxx'
2.如果时以上两个字符，那么就直接去到下一位就好了，否则就需要判断是否是 1~9中的任意在字符，如果不是，那么就代表不合法
3.如果是1~9中一个字符，那么就需要判断后续中是否是0~9中的字符，比如说190或291，所以就直接遍历
4.接着需要判断是否有小数点，如果有，我们就需要判断，从小数点开始后续是否有不合法的字符
5.如果有，就返回错误码，如果没有就开始，遍历直至遍历完成或者说遇到不合法的字符
6.接着需要判断指数如 100E+10这种也是合法的，所上述数字遍历完之后，需要看看末尾是否有上述符号
7. 遇到了上述符号，接着需要判断是否有'+''-',否则就直接判断是否为数字字符，如果不是就返回错误码
8.如果是，就遍历直至结束
9.首先设置错误码 erron=0,这个变量会因为函数出错，而此时我们需要检查转换的数字是否超出了范围，就需要ERANGE 宏
"As mentioned above, the C library macro ERANGE represents a range error, which occurs if an input argument is outside the range, 
over which the mathematical function is defined and errno is set to ERANGE."
10.如果输入数字过大，那么就会返回对应的错误码
11.
*/
static int lept_parse_number(lept_context* c, lept_value* v) {
    const char* p = c->json;
    if (*p == '-') p++;
    if (*p == '0') p++;
    else {
        if (!ISDIGIT1TO9(*p)) return LEPT_PARSE_INVALID_VALUE;
        for (p++; ISDIGIT(*p); p++);
    }
    if (*p == '.') {
        p++;
        if (!ISDIGIT(*p)) return LEPT_PARSE_INVALID_VALUE;
        for (p++; ISDIGIT(*p); p++);
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        if (!ISDIGIT(*p)) return LEPT_PARSE_INVALID_VALUE;
        for (p++; ISDIGIT(*p); p++);
    }
    errno = 0;
    v->u.n = strtod(c->json, NULL);
    if (errno == ERANGE && (v->u.n == HUGE_VAL || v->u.n == -HUGE_VAL))
        return LEPT_PARSE_NUMBER_TOO_BIG;
    v->type = LEPT_NUMBER;
    c->json = p;
    return LEPT_PARSE_OK;
}

//在解析字符串的过程中，首先要判断输入的文本是否是Json合法的字符串
//根据Json对于字符串的定义，字符串首字符一定是“开头，所以先用EXPECT判断第一个字符
//如果通过了，就对该输入字符串进行解析。
//解析过程中一共有三种情况，第一种就是假如直接遇到的是'\"'，那就意味着该字符串解析完成，因为一个合法的Json字符串应该遵从"XXX"的形式，出栈
//如果遇到'\\'，那就说明遇到了转义序列:
/* %x22 /          ; "    quotation mark  U+0022
       %x5C /          ; \    reverse solidus U+005C
       %x2F /          ; /    solidus         U+002F
       %x62 /          ; b    backspace       U+0008
       %x66 /          ; f    form feed       U+000C
       %x6E /          ; n    line feed       U+000A
       %x72 /          ; r    carriage return U+000D
       %x74 /          ; t    tab             U+0009
       %x75 4HEXDIG )  ; uXXXX                U+XXXX*/
//如果遇'\0'就代表，字符串结束，但是整个字符串解析还没有结束，比如说's \0 d'，打印出来的字符串只有s而没有d，
//如果上述条件都没有满足，那么就是其他任意字符，但对于任意字符，ASCII表上对于空白字符前的字符都定义为不合法
//在确认字符合法之后，那么就可以把字符推入栈中
static int lept_parse_string(lept_context* c, lept_value* v) {
    size_t head = c->top, len;
    const char* p;
    EXPECT(c, '\"');
    p = c->json;
    for (;;) {
        char ch = *p++;
        switch (ch) {
        case '\"':
            len = c->top - head;
            lept_set_string(v, (const char*)lept_context_pop(c, len), len);
            c->json = p;
            return LEPT_PARSE_OK;
        case '\\':
            switch (*p++) {
            case '\"': PUTC(c, '\"'); break;
            case '\\': PUTC(c, '\\'); break;
            case '/':  PUTC(c, '/'); break;
            case 'b':  PUTC(c, '\b'); break;
            case 'f':  PUTC(c, '\f'); break;
            case 'n':  PUTC(c, '\n'); break;
            case 'r':  PUTC(c, '\r'); break;
            case 't':  PUTC(c, '\t'); break;
            default:
                c->top = head;
                return LEPT_PARSE_INVALID_STRING_ESCAPE;
            }
            break;
        case '\0':
            c->top = head;
            return LEPT_PARSE_MISS_QUOTATION_MARK;
        default:
            //0x20是空白字符，如果此时的字符是空白字符之前的字符，那么对应在ascii 表上就不是合法的字符。详情可参照找ascii表
            if ((unsigned char)ch < 0x20) {
                c->top = head;
                return LEPT_PARSE_INVALID_STRING_CHAR;
            }
            PUTC(c, ch);
        }
    }
}

static int lept_parse_value(lept_context* c, lept_value* v) {
    switch (*c->json) {
    case 't':  return lept_parse_literal(c, v, "true", LEPT_TRUE);
    case 'f':  return lept_parse_literal(c, v, "false", LEPT_FALSE);
    case 'n':  return lept_parse_literal(c, v, "null", LEPT_NULL);
    default:   return lept_parse_number(c, v);
    case '"':  return lept_parse_string(c, v);
    case '\0': return LEPT_PARSE_EXPECT_VALUE;
    }
}

int lept_parse(lept_value* v, const char* json) {
    lept_context c;
    int ret;
    assert(v != NULL);
    c.json = json;
    c.stack = NULL;
    c.size = c.top = 0;
    //每次解析前，先初始化对象 赋一个初始NULL给数据类型
    lept_init(v);
    lept_parse_whitespace(&c);
    if ((ret = lept_parse_value(&c, v)) == LEPT_PARSE_OK) {
        lept_parse_whitespace(&c);
        if (*c.json != '\0') {
            v->type = LEPT_NULL;
            ret = LEPT_PARSE_ROOT_NOT_SINGULAR;
        }
    }
    assert(c.top == 0);
    free(c.stack);
    return ret;
}

//这个API用于释放Json字符
void lept_free(lept_value* v) {
    assert(v != NULL);
    if (v->type == LEPT_STRING)
        free(v->u.s.s);
    v->type = LEPT_NULL;
}

lept_type lept_get_type(const lept_value* v) {
    assert(v != NULL);
    return v->type;
}

int lept_get_boolean(const lept_value* v) {
    assert(v != NULL && (v->type == LEPT_TRUE || v->type == LEPT_FALSE));
    return v->type == LEPT_TRUE;
}
//若b,为1,那么类型为TRUE,否则为FALSE
void lept_set_boolean(lept_value* v, int b) {
    lept_free(v);
    v->type = b ? LEPT_TRUE : LEPT_FALSE;
}
//在确保v的数据类型下，返回数字
double lept_get_number(const lept_value* v) {
    assert(v != NULL && v->type == LEPT_NUMBER);
    return v->u.n;
}
//
void lept_set_number(lept_value* v, double n) {
    lept_free(v);
    v->u.n = n;
    v->type = LEPT_NUMBER;
}

const char* lept_get_string(const lept_value* v) {
    assert(v != NULL && v->type == LEPT_STRING);
    return v->u.s.s;
}

size_t lept_get_string_length(const lept_value* v) {
    assert(v != NULL && v->type == LEPT_STRING);
    return v->u.s.len;
}

//用于设置一个v的字符串
//首先先判断对象本身是否为空，以及对象的字符串和长度是否为空
//首先释放v的内存，接着重新根据长度分配内存，然后将所需要设置的字符串复制到v的空字符串中
//同时记住一定要补上最后的'\0'！！！
void lept_set_string(lept_value* v, const char* s, size_t len) {
    assert(v != NULL && (s != NULL || len == 0));
    lept_free(v);
    v->u.s.s = (char*)malloc(len + 1);
    memcpy(v->u.s.s, s, len);
    v->u.s.s[len] = '\0';
    v->u.s.len = len;
    v->type = LEPT_STRING;
}

