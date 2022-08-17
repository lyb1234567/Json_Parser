#ifdef _WINDOWS
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#include "leptjson.h"
#include <assert.h> /* assert() */
#include <errno.h>  /* errno, ERANGE */
#include <math.h>   /* HUGE_VAL */
#include <stdlib.h> /* NULL, malloc(), realloc(), free(), strtod() */
#include <string.h> /* memcpy() */
#ifndef LEPT_PARSE_STACK_INIT_SIZE
#define LEPT_PARSE_STACK_INIT_SIZE 256
#endif
//检查字符是否相同
#define STRING_ERROR(ret) \
    do                    \
    {                     \
        c->top = head;    \
        return ret;       \
    } while (0)
#define EXPECT(c, ch)             \
    do                            \
    {                             \
        assert(*c->json == (ch)); \
        c->json++;                \
    } while (0)
//检查字符是否为0~9
#define ISDIGIT(ch) ((ch) >= '0' && (ch) <= '9')
#define ISDIGIT1TO9(ch) ((ch) >= '1' && (ch) <= '9')
//进行一波入栈操作
#define PUTC(c, ch)                                         \
    do                                                      \
    {                                                       \
        *(char *)lept_context_push(c, sizeof(char)) = (ch); \
    } while (0)

typedef struct
{
    const char *json;
    char *stack;
    size_t size, top;
} lept_context;
#define STRING_ERROR(ret) \
    do                    \
    {                     \
        c->top = head;    \
        return ret;       \
    } while (0)
//入栈的逻辑如下：
/*
1.首先我们需要确保推入的字符大小本身大于0
2然后检查推入之后栈顶否超出栈本身的内存大小
3.如果超出，先检查栈的内存大小，如果为0，就先分配一个足够大的值，本次是256
4.否则就对栈内存进行扩展，每次扩展1.5倍大小 >>1是移位相当于除以2，所以c->size=c->size+0.5*c->size
5.重新设置好的内存分配给原来的栈
6.当内存大小足够大时，ret就指向栈顶，栈顶同时+1
*/
static void *lept_context_push(lept_context *c, size_t size)
{
    void *ret;
    assert(size > 0);
    if (c->top + size >= c->size)
    {
        if (c->size == 0)
            c->size = LEPT_PARSE_STACK_INIT_SIZE;
        while (c->top + size >= c->size)
            //当栈大小不够时，就会以原来内存的1.5倍进行扩展
            c->size += c->size >> 1; /* c->size * 1.5 */
        c->stack = (char *)realloc(c->stack, c->size);
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
static void *lept_context_pop(lept_context *c, size_t size)
{
    assert(c->top >= size);
    return c->stack + (c->top -= size);
}

//这个API用于判断传入字符串是否开头有空格，如果有，就先遍历过去知道没有空格。
static void lept_parse_whitespace(lept_context *c)
{
    const char *p = c->json;
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
static int lept_parse_literal(lept_context *c, lept_value *v, const char *literal, lept_type type)
{
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
static int lept_parse_number(lept_context *c, lept_value *v)
{
    const char *p = c->json;
    if (*p == '-')
        p++;
    if (*p == '0')
        p++;
    else
    {
        if (!ISDIGIT1TO9(*p))
            return LEPT_PARSE_INVALID_VALUE;
        for (p++; ISDIGIT(*p); p++)
            ;
    }
    if (*p == '.')
    {
        p++;
        if (!ISDIGIT(*p))
            return LEPT_PARSE_INVALID_VALUE;
        for (p++; ISDIGIT(*p); p++)
            ;
    }
    if (*p == 'e' || *p == 'E')
    {
        p++;
        if (*p == '+' || *p == '-')
            p++;
        if (!ISDIGIT(*p))
            return LEPT_PARSE_INVALID_VALUE;
        for (p++; ISDIGIT(*p); p++)
            ;
    }
    errno = 0;
    v->u.n = strtod(c->json, NULL);
    if (errno == ERANGE && (v->u.n == HUGE_VAL || v->u.n == -HUGE_VAL))
        return LEPT_PARSE_NUMBER_TOO_BIG;
    v->type = LEPT_NUMBER;
    c->json = p;
    return LEPT_PARSE_OK;
}
/*
值得需要注意的是，当我们使用静态函数时，我们需要再使用他们之前就声明好。例如下面的例子可以看到，我们
lept_parse_string中，要使用 lept_parse_hex4() 和 lept_encode_UTF-8(),两个函数，那么我们就首先需要在此之前声明，否则
就会发生"function not declared in this scope 的报错"。
A static function in C is a function that has a scope that is limited to its object file. This means that the static function is only visible in its object file.
A function can be declared as static function by placing the static keyword before the function name.
以下是例子：
#include <stdio.h>
static void staticFunc(void){
   printf("Inside the static function staticFunc() ");
}

int main()
{
   staticFunc();
   return 0;
}
*/
static const char *lept_parse_hex4(const char *p, unsigned *u)
{
    int i;
    *u = 0;
    for (i = 0; i < 4; i++)
    {
        char ch = *p++;
        *u <<= 4;
        if (ch >= '0' && ch <= '9')
            *u |= ch - '0';
        else if (ch >= 'A' && ch <= 'F')
            *u |= ch - ('A' - 10);
        else if (ch >= 'a' && ch <= 'f')
            *u |= ch - ('a' - 10);
        else
            return NULL;
    }
    return p;
}

static void lept_encode_utf8(lept_context *c, unsigned u)
{
    if (u <= 0x7F)
        PUTC(c, u & 0xFF);
    else if (u <= 0x7FF)
    {
        PUTC(c, 0xC0 | ((u >> 6) & 0xFF));
        PUTC(c, 0x80 | (u & 0x3F));
    }
    else if (u <= 0xFFFF)
    {
        PUTC(c, 0xE0 | ((u >> 12) & 0xFF));
        PUTC(c, 0x80 | ((u >> 6) & 0x3F));
        PUTC(c, 0x80 | (u & 0x3F));
    }
    else
    {
        assert(u <= 0x10FFFF);
        PUTC(c, 0xF0 | ((u >> 18) & 0xFF));
        PUTC(c, 0x80 | ((u >> 12) & 0x3F));
        PUTC(c, 0x80 | ((u >> 6) & 0x3F));
        PUTC(c, 0x80 | (u & 0x3F));
    }
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

//该函数用于读取输入字符串中前四位字符用于判断是否是十六进制数，0~FFFF
//同时将Unique Code中16进制数字转化成10进制数字比如00A2对应的就是162
// 002A
//同时该函数最大的特点就是可以将十六进制字符串转化为十进制数字
static int lept_parse_string(lept_context *c, lept_value *v)
{
    size_t head = c->top, len;
    unsigned u, u2;
    const char *p;
    EXPECT(c, '\"');
    p = c->json;
    for (;;)
    {
        char ch = *p++;
        switch (ch)
        {
        case '\"':
            len = c->top - head;
            lept_set_string(v, (const char *)lept_context_pop(c, len), len);
            c->json = p;
            return LEPT_PARSE_OK;
        case '\\':
            switch (*p++)
            {
            case '\"':
                PUTC(c, '\"');
                break;
            case '\\':
                PUTC(c, '\\');
                break;
            case '/':
                PUTC(c, '/');
                break;
            case 'b':
                PUTC(c, '\b');
                break;
            case 'f':
                PUTC(c, '\f');
                break;
            case 'n':
                PUTC(c, '\n');
                break;
            case 'r':
                PUTC(c, '\r');
                break;
            case 't':
                PUTC(c, '\t');
                break;
                /*
               而对于 JSON字符串中的 \uXXXX 是以 16 进制表示码点 U+0000 至 U+FFFF，我们需要：
               1.解析 4 位十六进制整数为码点，例如：00A2
               2.由于字符串是以 UTF-8 存储，我们要把这个码点编码成 UTF-8。
               4 位的 16 进制数字只能表示 0 至 0xFFFF，但之前我们说 UCS 的码点是从 0 至 0x10FFFF，那怎么能表示多出来的码点？
               其实，U+0000 至 U+FFFF 这组 Unicode 字符称为基本多文种平面（basic multilingual plane, BMP），
               还有另外 16 个平面。那么 BMP 以外的字符，JSON 会使用代理对（surrogate pair）表示 \uXXXX\uYYYY。在 BMP 中，保留了 2048 个代理码点。
               如果第一个码点是 U+D800 至 U+DBFF，我们便知道它的代码对的高代理项（high surrogate），之后应该伴随一个 U+DC00 至 U+DFFF 的低代理项（low surrogate）。
               然后，我们用下列公式把代理对 (H, L) 变换成真实的码点：
                       codepoint = 0x10000 + (H − 0xD800) × 0x400 + (L − 0xDC00)
                       H = 0xD834, L = 0xDD1E
                       codepoint = 0x10000 + (H − 0xD800) × 0x400 + (L − 0xDC00)
                       = 0x10000 + (0xD834 - 0xD800) × 0x400 + (0xDD1E − 0xDC00)
                       = 0x10000 + 0x34 × 0x400 + 0x11E
                       = 0x10000 + 0xD000 + 0x11E
                       = 0x1D11E
               这样就得出这转义序列的码点，然后我们再把它编码成 UTF-8。
               如果只有高代理项而欠缺低代理项，或是低代理项不在合法码点范围，我们都返回 LEPT_PARSE_INVALID_UNICODE_SURROGATE 错误。
               如果 \u 后不是 4 位十六进位数字，则返回LEPT_PARSE_INVALID_UNICODE_HEX 错误。
               */
            case 'u':
                if (!(p = lept_parse_hex4(p, &u)))
                    STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);
                //上述检查完了之后，我们就需要判断代理项，如00A2解析完之后就是162，就符合其中代理项的要求
                // Surrogates are characters in the Unicode range U+D800—U+DFFF
                //如果满足条件，我们就知道这是一个高代理项，那么随后应该伴随一个 U+DC00 至 U+DFFF 的低代理项（low surrogate)，当然也应该形如：\uxxxx
                //则同理我们应该继续检查 'u','\'，以及十六进制的范围用于满足低代理项的要求。
                //全部检查完了之后，我们就可以开始编译utf-8了。
                if (u >= 0xD800 && u <= 0xDBFF)
                { /* surrogate pair */
                    if (*p++ != '\\')
                        STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
                    if (*p++ != 'u')
                        STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
                    if (!(p = lept_parse_hex4(p, &u2)))
                        STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);
                    if (u2 < 0xDC00 || u2 > 0xDFFF)
                        STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
                    u = (((u - 0xD800) << 10) | (u2 - 0xDC00)) + 0x10000;
                }
                lept_encode_utf8(c, u);
                break;
            default:
                STRING_ERROR(LEPT_PARSE_INVALID_STRING_ESCAPE);
            }
            break;
        case '\0':
            STRING_ERROR(LEPT_PARSE_MISS_QUOTATION_MARK);
        default:
            if ((unsigned char)ch < 0x20)
                STRING_ERROR(LEPT_PARSE_INVALID_STRING_CHAR);
            PUTC(c, ch);
        }
    }
}
static int lept_parse_value(lept_context *c, lept_value *v)
{
    switch (*c->json)
    {
    case 't':
        return lept_parse_literal(c, v, "true", LEPT_TRUE);
    case 'f':
        return lept_parse_literal(c, v, "false", LEPT_FALSE);
    case 'n':
        return lept_parse_literal(c, v, "null", LEPT_NULL);
    default:
        return lept_parse_number(c, v);
    case '"':
        return lept_parse_string(c, v);
    case '\0':
        return LEPT_PARSE_EXPECT_VALUE;
    }
}

int lept_parse(lept_value *v, const char *json)
{
    lept_context c;
    int ret;
    assert(v != NULL);
    c.json = json;
    c.stack = NULL;
    c.size = c.top = 0;
    lept_init(v);
    lept_parse_whitespace(&c);
    if ((ret = lept_parse_value(&c, v)) == LEPT_PARSE_OK)
    {
        lept_parse_whitespace(&c);
        if (*c.json != '\0')
        {
            v->type = LEPT_NULL;
            ret = LEPT_PARSE_ROOT_NOT_SINGULAR;
        }
    }
    assert(c.top == 0);
    free(c.stack);
    return ret;
}

//这个API用于释放Json字符
void lept_free(lept_value *v)
{
    assert(v != NULL);
    if (v->type == LEPT_STRING)
        free(v->u.s.s);
    v->type = LEPT_NULL;
}

lept_type lept_get_type(const lept_value *v)
{
    assert(v != NULL);
    return v->type;
}

int lept_get_boolean(const lept_value *v)
{
    assert(v != NULL && (v->type == LEPT_TRUE || v->type == LEPT_FALSE));
    return v->type == LEPT_TRUE;
}
//若b,为1,那么类型为TRUE,否则为FALSE
void lept_set_boolean(lept_value *v, int b)
{
    lept_free(v);
    v->type = b ? LEPT_TRUE : LEPT_FALSE;
}
//在确保v的数据类型下，返回数字
double lept_get_number(const lept_value *v)
{
    assert(v != NULL && v->type == LEPT_NUMBER);
    return v->u.n;
}
//
void lept_set_number(lept_value *v, double n)
{
    lept_free(v);
    v->u.n = n;
    v->type = LEPT_NUMBER;
}

//返回json对象中存储的字符串
const char *lept_get_string(const lept_value *v)
{
    assert(v != NULL && v->type == LEPT_STRING);
    return v->u.s.s;
}

//读取字符串的长度并且返回字符串的长度
size_t lept_get_string_length(const lept_value *v)
{
    assert(v != NULL && v->type == LEPT_STRING);
    return v->u.s.len;
}

//用于设置一个v的字符串
//首先先判断对象本身是否为空，以及对象的字符串和长度是否为空
//首先释放v的内存，接着重新根据长度分配内存，然后将所需要设置的字符串复制到v的空字符串中
//同时记住一定要补上最后的'\0'！！！
void lept_set_string(lept_value *v, const char *s, size_t len)
{
    assert(v != NULL && (s != NULL || len == 0));
    lept_free(v);
    v->u.s.s = (char *)malloc(len + 1);
    memcpy(v->u.s.s, s, len);
    v->u.s.s[len] = '\0';
    v->u.s.len = len;
    v->type = LEPT_STRING;
}
