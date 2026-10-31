// Tests of the crash guard (common/guard.h): real faults on this thread, caught and survived.
//
// What is checked:
//   * a fault inside guarded() returns true with the exception code and address, and the program goes on;
//   * the caller's registers are as before the call - even when the faulting code scrambled every callee-saved
//     register and mxcsr first (scrambleAndCrash below);
//   * guards nest, a fault lands in the innermost one, and a C++ exception still passes through;
//   * exceptions that are not faults, and faults outside a guarded call, are left to others.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "third_party/doctest.h"

#include <xmmintrin.h>

#include <stdexcept>

#include "../common/guard.h"

// Puts garbage into every callee-saved register, xmm6..xmm15 and mxcsr (round toward zero, all exceptions
// masked), then writes to address 0. Has no unwind info - the guard does not need any.
extern "C" void scrambleAndCrash();
asm(R"(
    .text
    .globl scrambleAndCrash
scrambleAndCrash:
    movabsq $0x1111111111111111, %rbx
    movabsq $0x2222222222222222, %rbp
    movabsq $0x3333333333333333, %rsi
    movabsq $0x4444444444444444, %rdi
    movabsq $0x5555555555555555, %r12
    movabsq $0x6666666666666666, %r13
    movabsq $0x7777777777777777, %r14
    movabsq $0x8888888888888888, %r15
    movq %rbx, %xmm6
    movq %rbx, %xmm7
    movq %rbx, %xmm8
    movq %rbx, %xmm9
    movq %rbx, %xmm10
    movq %rbx, %xmm11
    movq %rbx, %xmm12
    movq %rbx, %xmm13
    movq %rbx, %xmm14
    movq %rbx, %xmm15
    pushq $0x7f80
    ldmxcsr (%rsp)
    popq %rax
    xorl %eax, %eax
    movl $1, (%rax)
    ret
)");

namespace {

// A plain null write, from C++. Not inlined, so the fault address is inside it; the pointer itself is volatile,
// so the compiler cannot see it is null and turn the write into a trap instruction.
__attribute__((noinline)) void writeNull() {
    int* volatile p = nullptr;
    *p = 1;
}

// A fake exception for guardHandler: code, flags, and a context to rewrite.
struct FakeException {
    EXCEPTION_RECORD record{};
    CONTEXT context{};
    EXCEPTION_POINTERS info{&record, &context};
    explicit FakeException(DWORD code, DWORD flags = 0) {
        record.ExceptionCode = code;
        record.ExceptionFlags = flags;
    }
};

}  // namespace

TEST_CASE("A call that returns normally - not faulted, the call ran") {
    int ran = 0;
    dfd::Fault fault;
    CHECK_FALSE(dfd::guarded([&] { ++ran; }, &fault));
    CHECK(ran == 1);
    CHECK(fault.code == 0);
}

TEST_CASE("A named callable (an lvalue) works the same as a temporary one") {
    int ran = 0;
    auto f = [&] { ++ran; };
    const auto crash = [] { writeNull(); };
    CHECK_FALSE(dfd::guarded(f));
    CHECK(dfd::guarded(crash));
    CHECK(ran == 1);
}

TEST_CASE("A null write is caught: faulted, with the code and the faulting instruction") {
    dfd::Fault fault;
    CHECK(dfd::guarded([] { writeNull(); }, &fault));
    CHECK(fault.code == EXCEPTION_ACCESS_VIOLATION);
    CHECK(fault.address >= reinterpret_cast<uintptr_t>(&writeNull));
    CHECK(fault.address < reinterpret_cast<uintptr_t>(&writeNull) + 64);
}

TEST_CASE("Integer division by zero is caught") {
    // both volatile: with a constant numerator of 1 the compiler divides without a div instruction
    volatile int zero = 0, thousand = 1000;
    volatile int out = 0;
    dfd::Fault fault;
    CHECK(dfd::guarded([&] { out = thousand / zero; }, &fault));
    CHECK(fault.code == EXCEPTION_INT_DIVIDE_BY_ZERO);
}

TEST_CASE("After a fault the caller's registers and mxcsr are as before") {
    // Many values live across the call: the compiler keeps them in callee-saved registers.
    volatile uint64_t seed = 12345;
    uint64_t a = seed * 3, b = seed * 5, c = seed * 7, d = seed * 11, e = seed * 13, f = seed * 17, g = seed * 19;
    double x = static_cast<double>(seed) / 3, y = static_cast<double>(seed) / 7, z = static_cast<double>(seed) / 9;
    unsigned before = _mm_getcsr();

    dfd::Fault fault;
    CHECK(dfd::guarded([] { scrambleAndCrash(); }, &fault));

    CHECK(fault.code == EXCEPTION_ACCESS_VIOLATION);
    CHECK(_mm_getcsr() == before);
    CHECK(a == 12345 * 3);
    CHECK(b == 12345 * 5);
    CHECK(c == 12345 * 7);
    CHECK(d == 12345 * 11);
    CHECK(e == 12345 * 13);
    CHECK(f == 12345 * 17);
    CHECK(g == 12345 * 19);
    CHECK(x == doctest::Approx(12345.0 / 3));
    CHECK(y == doctest::Approx(12345.0 / 7));
    CHECK(z == doctest::Approx(12345.0 / 9));
}

TEST_CASE("Many faults in a row - each one is caught") {
    int caught = 0;
    for (int i = 0; i < 100; ++i) caught += dfd::guarded([] { writeNull(); });
    CHECK(caught == 100);
}

TEST_CASE("Guards nest: the fault lands in the innermost, the outer call goes on") {
    bool inner = false;
    int afterInner = 0;
    bool outer = dfd::guarded([&] {
        inner = dfd::guarded([] { writeNull(); });
        ++afterInner;
    });
    CHECK(inner);
    CHECK(afterInner == 1);
    CHECK_FALSE(outer);
}

TEST_CASE("A fault after an inner guard returned lands in the outer one") {
    bool inner = true;
    int afterFault = 0;
    bool outer = dfd::guarded([&] {
        inner = dfd::guarded([] {});
        writeNull();
        ++afterFault;
    });
    CHECK_FALSE(inner);
    CHECK(outer);
    CHECK(afterFault == 0);
}

TEST_CASE("A C++ exception passes through the guard, and the guard is popped") {
    bool caught = false;
    try {
        dfd::guarded([] { throw std::runtime_error("boom"); });
    } catch (const std::runtime_error&) {
        caught = true;
    }
    CHECK(caught);
    // No guarded call is in progress now: a fault would be left to others.
    FakeException av(EXCEPTION_ACCESS_VIOLATION);
    CHECK(dfd::guardHandler(&av.info) == EXCEPTION_CONTINUE_SEARCH);
}

TEST_CASE("Outside a guarded call the handler leaves every exception alone") {
    FakeException av(EXCEPTION_ACCESS_VIOLATION);
    CHECK(dfd::guardHandler(&av.info) == EXCEPTION_CONTINUE_SEARCH);
    CHECK(av.context.Rip == 0);  // the context was not touched
}

TEST_CASE("Inside a guarded call: only faults are taken, other exceptions are passed on") {
    LONG breakpoint = 0, stackOverflow = 0, cxx = 0, noncontinuable = 0, access = 0;
    dfd::guarded([&] {
        FakeException bp(EXCEPTION_BREAKPOINT), so(EXCEPTION_STACK_OVERFLOW), gcc(0x20474343),
            hard(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE);
        breakpoint = dfd::guardHandler(&bp.info);
        stackOverflow = dfd::guardHandler(&so.info);
        cxx = dfd::guardHandler(&gcc.info);
        noncontinuable = dfd::guardHandler(&hard.info);
        // a real fault: it is taken, and the call lands
        FakeException av(EXCEPTION_ACCESS_VIOLATION);
        access = dfd::guardHandler(&av.info);
    });
    CHECK(breakpoint == EXCEPTION_CONTINUE_SEARCH);
    CHECK(stackOverflow == EXCEPTION_CONTINUE_SEARCH);
    CHECK(cxx == EXCEPTION_CONTINUE_SEARCH);
    CHECK(noncontinuable == EXCEPTION_CONTINUE_SEARCH);
    CHECK(access == EXCEPTION_CONTINUE_EXECUTION);
}
