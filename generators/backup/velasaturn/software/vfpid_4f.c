#include <stdio.h>
#include <stdlib.h>
#ifdef __linux__
#include <time.h>   /* Linux userspace: rdcycle (csrr cycle) is illegal in U-mode; use CLOCK_MONOTONIC */
#endif

typedef struct {
    float setpoint;
    float currpoint;
    float Kp, Ki, Kd;
    float I_prev;
    float error_prev;
    float output;
} Actuator;

int main() {
    const int N = 4;
    Actuator actuator[4] = {
        {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
        {11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f, 17.0f, 18.0f},
        {21.0f, 22.0f, 23.0f, 24.0f, 25.0f, 26.0f, 27.0f, 28.0f},
        {31.0f, 32.0f, 33.0f, 34.0f, 35.0f, 36.0f, 37.0f, 38.0f}
    };
    float dt = 0.1f;
#ifdef __linux__
    struct timespec __t0, __t1;
    clock_gettime(CLOCK_MONOTONIC, &__t0);
#else
    int start, end;
    __asm__ volatile ("csrr %0, cycle" : "=r"(start) :: "memory"); // cycle(user) — mcycle은 유저모드 트랩
#endif
    __asm__ volatile (
        "vsetivli t0, 8, e32, m1, ta, ma\n\t"
        "flw fa0, (%1)\n\t"
        "mv t0, %0\n\t"
        "vle32.v v1, (%0)\n\t"
        "addi t0, t0, 32\n\t"
        "vle32.v v2, (t0)\n\t"
        "addi t0, t0, 32\n\t"
        "vle32.v v3, (t0)\n\t"
        "addi t0, t0, 32\n\t"
        "vle32.v v4, (t0)\n\t"
        
        // funct6 + vm(1) + vs2(1) + rs1(10) + funct3(101) + vd(xxxxx) + op(1010111)
        // vd = v4 (bits[11:7] = 00100)
        ".word 0b00000110000101010101001001010111 \n\t"
        
        "vse32.v v1, (%0)\n\t"
        "mv t1, %0\n\t"
        "addi t1, t1, 32\n\t"
        "vse32.v v2, (t1)\n\t"
        "addi t1, t1, 32\n\t"
        "vse32.v v3, (t1)\n\t"
        "addi t1, t1, 32\n\t"
        "vse32.v v4, (t1)\n\t"
        :
        : "r"(actuator), "r"(&dt)
        : "v1", "v2", "v3", "v4", "fa0", "t0", "t1", "memory"
    );
#ifdef __linux__
    clock_gettime(CLOCK_MONOTONIC, &__t1);
    long __ns = (__t1.tv_sec - __t0.tv_sec) * 1000000000L + (__t1.tv_nsec - __t0.tv_nsec);
#else
    __asm__ volatile ("csrr %0, cycle" : "=r"(end) :: "memory"); // cycle(user)
    int a = end - start;
#endif

    // 결과 출력 (각 벡터 = 한 actuator의 8개 e32 원소: sp,cp,Kp,Ki,Kd,I_prev,err_prev,out)
#ifdef __linux__
    printf("vfpid_4f elapsed: %ld ns\n", __ns);
#else
    printf("vfpid_4f cycles: %d\n", a);
#endif
    float *vp = (float *)actuator;
    for (int v = 0; v < N; v++) {
        printf("v%d:", v + 1);
        for (int e = 0; e < 8; e++) {
            printf(" %f", vp[v * 8 + e]);
        }
        printf("\n");
    }
    return 0;
}
