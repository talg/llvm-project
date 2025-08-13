// RUN: %check_clang_tidy %s misc-lfi-taint %t -- -- -std=c99

#define TAINTED(sandbox) __attribute__((annotate("tainted:" #sandbox)))

// CHECK-MESSAGES: :[[@LINE+1]]:{{.*}} found tainted variable 'x' with annotation 'tainted:jpeg'
TAINTED(jpeg) int x;

// CHECK-MESSAGES: :[[@LINE+1]]:{{.*}} found tainted variable 'y' with annotation 'tainted:png'
TAINTED(png) int y;

// Should not trigger
int normal_var;
