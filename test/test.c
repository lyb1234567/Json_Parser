#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#define LEPT_PARSE_STACK_INIT_SIZE 256
#define PUTC(c, ch)                                         \
    do                                                      \
    {                                                       \
        *(char *)lept_context_push(c, sizeof(char)) = (ch); \
    }
typedef struct
{
    const char *json;
    char *stack;
    size_t size, top;
} lept_context;
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
static void *lept_context_pop(lept_context *c, size_t size)
{
    assert(c->top >= size);
    return c->stack + (c->top -= size);
}
int main()
{

    char *json = "Incoming string";
    char *a = "sadasd";
    *(void *ret) = *a++;
    ret = json + 15;
    printf("json:%s", json);
    // lept_context c;
    // c.json = json;
    // c.stack = NULL;
    // c.size = c.top = 0;
    // char *str;
    // size_t len = sizeof(char);
    // while (*c.json)
    // {
    //     *(char *)lept_context_push(&c, sizeof(char)) = (*c.json++);
    //     printf("Stack:%s \n", c.stack);
    // }
    // while (*c.stack)
    // {
    //     str = (char *)lept_context_pop(&c, 1);
    //     printf("Str:%s \n", str);
    //     len = len + 1;
    // }
    return 0;
}