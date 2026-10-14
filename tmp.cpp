#include <cstdio>
#include <sys/ucontext.h>
#include <ucontext.h>
#include <unistd.h>

ucontext_t main_ctx, core_ctx;

void core_func(void) {
    printf("coroutine running\n");
    swapcontext(&core_ctx, &main_ctx);
    printf("coroutine resumed\n");
}

int main() {
    char stack[8192];
    getcontext(&core_ctx);
    core_ctx.uc_stack.ss_sp = stack;
    core_ctx.uc_stack.ss_size = sizeof(stack);
    core_ctx.uc_link = &main_ctx;
    makecontext(&core_ctx, core_func, 0);

    printf("before switch\n");
    swapcontext(&main_ctx, &core_ctx);
    printf("back in main\n");

    swapcontext(&main_ctx, &core_ctx);
    printf("done\n");

    return 0;
}
