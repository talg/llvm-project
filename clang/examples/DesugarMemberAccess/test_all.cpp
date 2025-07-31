// test_pure.cpp
// Pure test file with NO transformations - only original C++ code
#include <cstdio>
#include <vector>

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
    p.x = 10;
    p.y = 20;
    
    // Arrow member access
    pp->x = 30;
    pp->y = 40;
}

// Test 2: Method calls
void test_method_calls() {
    Point p;
    Point* pp = &p;
    
    // No-arg method
    int mag = p.magnitude();
    
    // Multi-arg method
    p.set(5, 10);
    pp->set(15, 20);
    
    // Method returning reference - chained calls
    p.offset(1, 2).offset(3, 4);
}

// Test 3: Nested access
void test_nested_access() {
    Line l;
    Line* lp = &l;
    
    // Two levels deep
    l.start.x = 0;
    l.end.y = 100;
    
    // Mixed arrow/dot
    lp->start.x = 5;
    
    // Three levels deep
    Shape s;
    s.edges[0].start.x = 10;
    
    // Nested method calls
    int len = l.start.magnitude();
}

// Test 4: Arrays and subscripts
void test_arrays() {
    Shape shapes[3];
    Shape* sp = &shapes[0];
    
    // Array element access
    shapes[0].center.x = 50;
    shapes[1].edges[2].end.y = 75;
    
    // Through pointer
    sp->edges[1].start.x = 25;
    
    // Method returning pointer
    Point* corner = shapes[0].getCorner(0);
    corner->x = 100;
}

// Test 5: Mixed attribute/non-attribute
void test_mixed_attributes() {
    SafeContainer sc;
    
    // Non-attributed member of attributed class
    sc.count = 5;
    
    // Non-attributed member's member
    sc.vec.x = 10;
    
    // Attributed member of attributed class
    sc.point.x = 20;
    
    // Through method
    RegularVector* v = sc.getVec();
    v->y = 30;  // Should NOT be transformed
}

// Test 6: Complex expressions
void test_complex_expressions() {
    Point p1, p2;
    Line l;
    
    // In arithmetic
    int sum = p1.x + p2.y * 2;
    
    // In conditionals
    if (p1.x > p2.x && l.start.y < l.end.y) {
        p1.x = p2.y;
    }
    
    // Ternary operator
    int max_x = (p1.x > p2.x) ? p1.x : p2.x;
    
    // Function arguments
    printf("Point: (%d, %d)\n", p1.x, p1.y);
}

// Test 7: Const correctness
void test_const_access() {
    const Point cp = {10, 20};
    const Line* clp = nullptr;
    
    // Const object access
    int x = cp.x;
    int mag = cp.magnitude();
    
    // Const pointer access
    if (clp) {
        int y = clp->start.y;
    }
    
    // Const method returning const ref
    Shape s;
    const Point& center = s.getCenter();
    int cx = center.x;
}

// Test 8: Templates
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
    ci.value = 42;
    int& ref = ci.get();
    
    // Template with attributed type
    cp.value.x = 10;
    Point& pref = cp.get();
    pref.y = 20;
}

// Test 9: Edge cases
void test_edge_cases() {
    Point p;
    Point* pp = &p;
    Point** ppp = &pp;
    
    // Double pointer dereference
    (*ppp)->x = 5;
    
    // Parenthesized expressions
    (p).x = 10;
    (pp)->y = 20;
    
    // Address-of member
    int* px = &p.x;
    Point* sp = &(pp->offset(1, 2));
    
    // Comma operator
    int last = (p.x = 5, p.y = 10, p.magnitude());
}

// Test 10: Stress test
Shape* createComplexShape() {
    static Shape s;
    s.center.x = 100;
    s.center.y = 100;
    
    for (int i = 0; i < 4; ++i) {
        s.edges[i].start.set(i * 10, i * 10);
        s.edges[i].end = s.edges[(i + 1) % 4].start;
    }
    
    return &s;
}

// Test 11: Return statements
int get_value_from_point(const Point& p) {
    return p.x;
}

Point get_center_from_shape(const Shape& s) {
    return s.center;
}

int complex_return(Line* lp) {
    return lp->start.x + lp->end.y;
}

// Test 12: Switch statements
void test_switch(Point& p) {
    switch (p.x) {
        case 0:
            p.y = 100;
            break;
        case 1:
            p.set(10, 20);
            break;
        default:
            p.x = p.y * 2;
            break;
    }
}

// Test 13: Loops with member access
void test_loops() {
    Point points[10];
    Line lines[5];
    
    // For loop
    for (int i = 0; i < 10; ++i) {
        points[i].x = i;
        points[i].y = i * i;
    }
    
    // While loop
    int j = 0;
    while (lines[j].start.x < 100) {
        lines[j].end.y = lines[j].start.x * 2;
        j++;
        if (j >= 5) break;
    }
    
    // Range-based for (if container has begin/end)
    for (auto& line : lines) {
        line.start.set(0, 0);
    }
}

// Test 14: Exception handling
void test_exceptions() {
    Point p;
    try {
        p.x = 42;
        if (p.magnitude() > 100) {
            throw p.y;
        }
    } catch (int val) {
        p.y = val;
    }
}

// Test 15: Lambda expressions
void test_lambdas() {
    Point p;
    Line l;
    
    // Simple lambda
    auto get_x = [&p]() { return p.x; };
    
    // Lambda with parameters
    auto set_point = [](Point& pt, int val) { 
        pt.x = val; 
        pt.y = val * 2; 
    };
    
    // Lambda with capture and nested access
    auto process_line = [&l]() {
        l.start.x = 0;
        l.end.y = l.start.magnitude();
    };
    
    int x_val = get_x();
    set_point(p, 10);
    process_line();
}

// Test 16: Assignment chains
void test_assignment_chains() {
    Point p1, p2, p3;
    
    // Simple assignment chain
    p1.x = p2.x = p3.x = 10;
    
    // Mixed member assignment
    p1.y = p2.x = 20;
}

// Test 17: Increment/decrement operators
void test_increment_operators() {
    Point p;
    p.x = 0;
    p.y = 0;
    
    // Pre-increment
    ++p.x;
    --p.y;
    
    // Post-increment
    p.x++;
    p.y--;
    
    // In expressions
    int a = ++p.x + p.y--;
}

// Test 18: Pointer arithmetic with members
void test_pointer_arithmetic() {
    Point points[5];
    Point* p = points;
    
    // Pointer arithmetic
    (p + 1)->x = 10;
    (p + 2)->y = 20;
    
    // Array-style with pointer
    p[3].x = 30;
    p[4].y = 40;
}

// Test 19: Nested function calls
void process_point(int x, int y) {
    printf("Processing: %d, %d\n", x, y);
}

void test_nested_calls() {
    Point p;
    Line l;
    
    // Member access in function arguments
    process_point(p.x, p.y);
    
    // Nested member access in function arguments
    process_point(l.start.x, l.end.y);
}

// Test 20: Static members (if supported)
struct [[clang::desugar_member_access("RLBOX_MEMBER_ACCESS")]] Counter {
    static int count;
    int id;
    
    Counter() : id(++count) {}
    int getId() const { return id; }
};

int Counter::count = 0;

void test_static_members() {
    Counter c1, c2;
    
    // Access instance members
    int id1 = c1.id;
    int id2 = c2.getId();
    
    // Access static member (may not transform)
    int total = Counter::count;
}

// Main function
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
    printf("Shape center: (%d, %d)\n", s->center.x, s->center.y);
    
    Point p = {5, 10};
    printf("Point value: %d\n", get_value_from_point(p));
    
    test_switch(p);
    test_loops();
    test_exceptions();
    test_lambdas();
    test_assignment_chains();
    test_increment_operators();
    test_pointer_arithmetic();
    test_nested_calls();
    test_static_members();
    
    return 0;
}
