// test_all.cpp
// Comprehensive test cases for the DesugarMemberAccess plugin
#include <cstdio>
#include <vector>

// Mock macro for testing (in real use, this would do sandboxing)
#define RLBOX_MEMBER_ACCESS(access_type, obj, member, ...) \
    ((access_type == 1) ? /* arrow access */ \
     (obj)->member(__VA_ARGS__) : /* simplified for testing */ \
     (obj).member(__VA_ARGS__))

// Test classes with desugar attribute
struct [[clang::desugar_member_access("RLBOX_MEMBER_ACCESS")]] Point {
    int x;
    int y;
    
    int magnitude() const { return x * x + y * y; }
    void set(int nx, int ny) { x = nx; y = ny; }
    Point& offset(int dx, int dy) { x += dx; y += dy; return *this; }
};

struct [[clang::desugar_member_access("RLBOX_MEMBER_ACCESS")]] Line {
    Point start;
    Point end;
    
    int length() const { 
        int dx = end.x - start.x;
        int dy = end.y - start.y;
        return dx * dx + dy * dy;
    }
};

struct [[clang::desugar_member_access("RLBOX_MEMBER_ACCESS")]] Shape {
    Line edges[4];
    Point center;
    const char* name;
    
    Point* getCorner(int i) { return &edges[i].start; }
    const Point& getCenter() const { return center; }
};

// Mixed: some with attribute, some without
struct RegularVector {
    int x, y, z;
};

struct [[clang::desugar_member_access("RLBOX_MEMBER_ACCESS")]] SafeContainer {
    RegularVector vec;  // No attribute
    Point point;        // Has attribute
    int count;
    
    RegularVector* getVec() { return &vec; }
};

// Test 1: Basic member access
void test_basic_access() {
    Point p;
    Point* pp = &p;
    
    // Direct member access
    p.x = 10;                    // -> RLBOX_MEMBER_ACCESS(2, p, x) = 10
    p.y = 20;                    // -> RLBOX_MEMBER_ACCESS(2, p, y) = 20
    
    // Arrow member access
    pp->x = 30;                  // -> RLBOX_MEMBER_ACCESS(1, pp, x) = 30
    pp->y = 40;                  // -> RLBOX_MEMBER_ACCESS(1, pp, y) = 40
}

// Test 2: Method calls
void test_method_calls() {
    Point p;
    Point* pp = &p;
    
    // No-arg method
    int mag = p.magnitude();     // -> int mag = RLBOX_MEMBER_ACCESS(2, p, magnitude)
    
    // Multi-arg method
    p.set(5, 10);               // -> RLBOX_MEMBER_ACCESS(2, p, set, 5, 10)
    pp->set(15, 20);            // -> RLBOX_MEMBER_ACCESS(1, pp, set, 15, 20)
    
    // Method returning reference
    p.offset(1, 2).offset(3, 4); // -> RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(2, p, offset, 1, 2), offset, 3, 4)
}

// Test 3: Nested access
void test_nested_access() {
    Line l;
    Line* lp = &l;
    
    // Two levels
    l.start.x = 0;               // -> RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(2, l, start), x) = 0
    l.end.y = 100;               // -> RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(2, l, end), y) = 100
    
    // Mixed arrow/dot
    lp->start.x = 5;             // -> RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(1, lp, start), x) = 5
    
    // Three levels
    Shape s;
    s.edges[0].start.x = 10;     // -> RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(2, s.edges[0], start), x) = 10
    
    // Nested method calls
    int len = l.start.magnitude(); // -> int len = RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(2, l, start), magnitude)
}

// Test 4: Arrays and subscripts
void test_arrays() {
    Shape shapes[3];
    Shape* sp = &shapes[0];
    
    // Array element access
    shapes[0].center.x = 50;     // -> RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(2, shapes[0], center), x) = 50
    shapes[1].edges[2].end.y = 75; // -> Complex nested with array subscripts
    
    // Through pointer
    sp->edges[1].start.x = 25;  // -> RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(1, sp, edges)[1], start), x) = 25
    
    // Method returning pointer
    Point* corner = shapes[0].getCorner(0); // -> Point* corner = RLBOX_MEMBER_ACCESS(2, shapes[0], getCorner, 0)
    corner->x = 100;             // -> RLBOX_MEMBER_ACCESS(1, corner, x) = 100
}

// Test 5: Mixed attribute/non-attribute
void test_mixed_attributes() {
    SafeContainer sc;
    
    // Non-attributed member of attributed class
    sc.count = 5;                // -> RLBOX_MEMBER_ACCESS(2, sc, count) = 5
    
    // Non-attributed member's member
    sc.vec.x = 10;               // -> RLBOX_MEMBER_ACCESS(2, sc, vec).x = 10 (vec access transformed, x not)
    
    // Attributed member of attributed class
    sc.point.x = 20;             // -> RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(2, sc, point), x) = 20
    
    // Through method
    RegularVector* v = sc.getVec(); // -> RegularVector* v = RLBOX_MEMBER_ACCESS(2, sc, getVec)
    v->y = 30;                   // -> v->y = 30 (no transformation - RegularVector has no attribute)
}

// Test 6: Complex expressions
void test_complex_expressions() {
    Point p1, p2;
    Line l;
    
    // In arithmetic
    int sum = p1.x + p2.y * 2;   // -> int sum = RLBOX_MEMBER_ACCESS(2, p1, x) + RLBOX_MEMBER_ACCESS(2, p2, y) * 2
    
    // In conditionals
    if (p1.x > p2.x && l.start.y < l.end.y) {
        // -> if (RLBOX_MEMBER_ACCESS(2, p1, x) > RLBOX_MEMBER_ACCESS(2, p2, x) && 
        //        RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(2, l, start), y) < RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(2, l, end), y))
        p1.x = p2.y;             // -> RLBOX_MEMBER_ACCESS(2, p1, x) = RLBOX_MEMBER_ACCESS(2, p2, y)
    }
    
    // Ternary operator
    int max_x = (p1.x > p2.x) ? p1.x : p2.x;
    // -> int max_x = (RLBOX_MEMBER_ACCESS(2, p1, x) > RLBOX_MEMBER_ACCESS(2, p2, x)) ? 
    //                 RLBOX_MEMBER_ACCESS(2, p1, x) : RLBOX_MEMBER_ACCESS(2, p2, x)
    
    // Function arguments
    printf("Point: (%d, %d)\n", p1.x, p1.y);
    // -> printf("Point: (%d, %d)\n", RLBOX_MEMBER_ACCESS(2, p1, x), RLBOX_MEMBER_ACCESS(2, p1, y))
}

// Test 7: Const correctness
void test_const_access() {
    const Point cp = {10, 20};
    const Line* clp = nullptr;
    
    // Const object access
    int x = cp.x;                // -> int x = RLBOX_MEMBER_ACCESS(2, cp, x)
    int mag = cp.magnitude();    // -> int mag = RLBOX_MEMBER_ACCESS(2, cp, magnitude)
    
    // Const pointer access
    if (clp) {
        int y = clp->start.y;    // -> int y = RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(1, clp, start), y)
    }
    
    // Const method returning const ref
    Shape s;
    const Point& center = s.getCenter(); // -> const Point& center = RLBOX_MEMBER_ACCESS(2, s, getCenter)
    int cx = center.x;           // -> int cx = RLBOX_MEMBER_ACCESS(2, center, x)
}

// Test 8: Templates (plugin should handle instantiations)
template<typename T>
struct [[clang::desugar_member_access("RLBOX_MEMBER_ACCESS")]] Container {
    T value;
    T* ptr;
    
    T& get() { return value; }
    const T& get() const { return value; }
};

void test_templates() {
    Container<int> ci;
    Container<Point> cp;
    
    // Template with built-in type
    ci.value = 42;               // -> RLBOX_MEMBER_ACCESS(2, ci, value) = 42
    int& ref = ci.get();         // -> int& ref = RLBOX_MEMBER_ACCESS(2, ci, get)
    
    // Template with attributed type
    cp.value.x = 10;             // -> RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(2, cp, value), x) = 10
    Point& pref = cp.get();      // -> Point& pref = RLBOX_MEMBER_ACCESS(2, cp, get)
    pref.y = 20;                 // -> RLBOX_MEMBER_ACCESS(2, pref, y) = 20
}

// Test 9: Edge cases
void test_edge_cases() {
    Point p;
    Point* pp = &p;
    Point** ppp = &pp;
    
    // Double pointer dereference
    (*ppp)->x = 5;               // -> RLBOX_MEMBER_ACCESS(1, (*ppp), x) = 5
    
    // Parenthesized expressions
    (p).x = 10;                  // -> RLBOX_MEMBER_ACCESS(2, (p), x) = 10
    (pp)->y = 20;                // -> RLBOX_MEMBER_ACCESS(1, (pp), y) = 20
    
    // Address-of member
    int* px = &p.x;              // -> int* px = &RLBOX_MEMBER_ACCESS(2, p, x)
    Point* sp = &(pp->offset(1, 2)); // -> Point* sp = &(RLBOX_MEMBER_ACCESS(1, pp, offset, 1, 2))
    
    // Comma operator
    int last = (p.x = 5, p.y = 10, p.magnitude());
    // -> int last = (RLBOX_MEMBER_ACCESS(2, p, x) = 5, RLBOX_MEMBER_ACCESS(2, p, y) = 10, RLBOX_MEMBER_ACCESS(2, p, magnitude))
}

// Test 10: Stress test with everything combined
Shape* createComplexShape() {
    static Shape s;
    s.center.x = 100;            // -> RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(2, s, center), x) = 100
    s.center.y = 100;            // -> RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(2, s, center), y) = 100
    
    for (int i = 0; i < 4; ++i) {
        s.edges[i].start.set(i * 10, i * 10);
        // -> RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(2, s.edges[i], start), set, i * 10, i * 10)
        
        s.edges[i].end = s.edges[(i + 1) % 4].start;
        // -> RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(2, s.edges[i], end) = RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(2, s.edges[(i + 1) % 4], start)
    }
    
    return &s;
}

int main() {
    test_basic_access();
    test_method_calls();
    test_nested_access();
    test_arrays();
    test_mixed_attributes();
    test_complex_expressions();
    test_const_access();
    test_templates();
    test_edge_cases();
    
    Shape* s = createComplexShape();
    printf("Shape center: (%d, %d)\n", 
           s->center.x,          // -> RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(1, s, center), x)
           s->center.y);         // -> RLBOX_MEMBER_ACCESS(2, RLBOX_MEMBER_ACCESS(1, s, center), y)
    
    return 0;
}
