# 一些Json解析器的设计思路
**Key points**
- [Stack](#stack)
  - lept_context_push()
  - lept_context_pop()
  - lept_set_string()



## Stack
当我们在解析字符串的时候，我们需要将解析字符串结果暂时存在一个栈中，然后再利用 ```lept_set_string()```设置值。但是在完成解析这个字符串之前，所面临的字符串大小是无法预知的，所以我们需要一个动态数组：数组空间不足时，就能自动扩展。
如果每次解析字符串时，都重新建一个动态数组，那么是比较耗时的。我们可以重用这个动态数组，每次解析 JSON 时就只需要创建一个。而且我们将会发现，无论是解析字符串、数组或对象，我们也只需要以先进后出的方式访问这个动态数组。换句话说，我们需要一个动态的堆栈（stack）数据结构。
我们把一个动态堆栈的数据放进 ```lept_context``` 里：
```C++
typedef struct {
    const char* json;
    char* stack;
    size_t size, top;
}lept_context;
```
可以看到，所设置的堆栈数据包含所需要暂时缓存的字符串，堆栈以及栈顶和栈底。该动态栈的实现逻辑如下：
首先检查当前栈的大小是否为0，如果为0，那么就首先将栈的大小设置为一个定值。接着，每推入一个字符，栈顶的大小就会+1,如果栈顶大小超过了目前栈的大小，那么，栈就会进行扩容。这边参照 [[1]](https://github.com/facebook/folly/blob/main/folly/docs/FBVector.md).在内存分配的过程中，我们希望能够重新用到之前分配的 **内存**，否则就浪费了。假如我们每次空间不足，空间就扩大两倍，那么就永远不可能用到之前的分配的内存。比如一开始需要分配10，地址为0，扩大两倍，需要分配20， 地址为10，接着扩大两倍，分配40，地址30依旧不够，根据公式推算$$2^{n}>2^{n-1}$$
所以，增长因子大于2不是一个好的选择。那么我们就需要选择小于2的增长因子，本次项目选择了1.5
```C++
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
```
该函数会将解析的字符串中每一个字符一个个压入栈中，并返回最终的其初始数据。这里是一小段测试代码：
```C++
 const char *json = "Incoming string";
    lept_context c;
    c.json = json;
    c.stack = NULL;
    c.size = c.top = 0;
    char *str;
    size_t len = sizeof(char);
    while (*c.json)
    {
        *(char *)lept_context_push(&c, sizeof(char)) = (*c.json++);
        printf("Stack:%s \n", c.stack);
    }
```
结果如下：
```
Stack:I 
Stack:In 
Stack:Inc 
Stack:Inco 
Stack:Incom 
Stack:Incomi 
Stack:Incomin 
Stack:Incoming 
Stack:Incoming  
Stack:Incoming s 
Stack:Incoming st 
Stack:Incoming str 
Stack:Incoming stri 
Stack:Incoming strin 
Stack:Incoming string 
```
