#include <stdio.h>

/*
 * The very basic test() function. It returns a (int)(1).
 */
asm
(
 "test:\n\t"
 "pushq %rbp\n\t"
 "mov %rsp, %rbp\n\t"
 "mov $1,%eax\n\t"
 "leave\n\t"
 "ret"
 );

/*
 * add1(int a, int b) to add value for @a and @b.
 */
asm
(
 "add1:\n\t"
 "pushq %rbp\n\t"
 "mov %rsp, %rbp\n\t"
 "pushq %rax\n\t"
 "pushq %rbx\n\t"
 "mov 16(%rbp), %eax\n\t"
 "mov 20(%rbp), %ebx\n\t"
 "add %ebx, %eax\n\t"
 "popq %rbx\n\t"
 "popq %rax\n\t"
 "leave\n\t"
 "ret"
 );

/*
 * Extended ASM example to do add1().
 */
int add2(int a, int b)
{
    asm volatile
        ("add %1, %0\n\t"
         : "+r" (a) : "r" (b) : );
    return a;
}

#define CHAR(v, n) ((v >> (8 * n)) & 0xff)

void dump_chars(unsigned int v)
{
    printf(" (\"%c%c%c%c\")\n", CHAR(v, 0), CHAR(v, 1),
           CHAR(v, 2), CHAR(v, 3));
}

/*
 * Test "cpuid" instruction
 */
void test_cpuid(void)
{
    unsigned int a, b, c, d;
    asm volatile
        ("cpuid":
         "=a" (a), "=b" (b), "=c" (c), "=d" (d):
         "0" (0), "2" (0));
    printf("Dumping cpuid with eax=0, ecx=0:\n");
    printf("EAX: 0x%08x\n", a);
    printf("EBX: 0x%08x", b); dump_chars(b);
    printf("ECX: 0x%08x", c); dump_chars(c);
    printf("EDX: 0x%08x", d); dump_chars(d);
}

int main(void)
{
    int test(void);
    int add1(int a, int b);

    printf("TEST returns %d\n", test());
    printf("ADD1 is %d\n", add1(3, 4));
    printf("ADD2 is %d\n", add2(3, 4));
    test_cpuid();

    return 0;
}
