# `guard.h` — Automatic Rule-Enforcing Wrappers

A header-only, C++20 online automatic rule-enforcement system.

```cpp
#include "guard.h"
using namespace guard;

Always<int, ClampPercent> percent(120);   // → 100 immediately
percent -= 130;                           // → 0
++percent;                                // → 1
```

Rules are plain callables taking a pointer to the underlying value and
returning `void`; they may repair the value in place or throw to reject it.

**Important:** This library can be used to implement variables with invariants.
However, it is **not** dedicated to that purpose. It is a general-purpose wrapper that
can enforce arbitrary rules and transformations on many different types, including 
primitives, containers, strings, and user-defined types.

**Warning:** Due to C++ practicalities, no wrapper can reliably know whether the 
underlying object has been mutated through other means. Thus, automatic enforcement 
is guaranteed only for mutations whose completion is observable through a `Guard` 
operation. Also, although we try not to introduce **extra** safety problems, this 
library does **not** attempt to fix existing safety issues in standard C++, including 
dangling references, iterator invalidation, etc. Use with care.

---

## Requirements

- C++20 with standard library
- No external dependencies

---

## Core type

The implementation lives in `namespace guard::impl`; the public entry points
are six aliases:

| Alias | Meaning |
| --- | --- |
| `guard::Passive<U, E...>` | Never enforces automatically; only on `commit()` |
| `guard::Once<U, E...>` | Enforces once when the wrapper is constructed |
| `guard::Defer<U, E...>` | Enforces after every mutation, never in any constructor |
| `guard::Always<U, E...>` | Enforces on construction **and** after every mutation |
| `guard::SDefer<U, E...>` | Like `Defer`, with suspension enabled |
| `guard::SAlways<U, E...>` | Like `Always`, with suspension enabled |

The raw template entry is `guard::Guard<guard::impl::EConfig M, U, E...>`.
- `M` is the enforcement mode.
- `U` is the underlying type.
- `E...` is an ordered rule pack; rules run **left to right**. 

```cpp
using Ordered = guard::Guard<guard::EMode::Always, 
                             int, SetTo<1>, SetTo<2>, SetTo<3>>;
Ordered value{0};     // final value: 3
```

### Enforcement configs

Three variable templates in `guard::impl` capture the policy:
`enforce_on_construction_v<M>` (true for `Once`, `Always`, `SAlways`),
`enforce_on_mutation_v<M>` (true for `Defer`, `Always`, `SDefer`, `SAlways`),
and `suspendable_v<M>` (true only for `SDefer` and `SAlways`); the last one
decides whether a wrapper carries a suspension counter.

The built-in configuration constants are `EMode::Passive`, `EMode::Once`,
`EMode::Defer`, `EMode::Always`, `EMode::SDefer`, and `EMode::SAlways`.
Custom modes are built with
`EMode::mode<EnforceOnConstruction, EnforceOnMutation, SuspensionStateSize = 0>`:
the suspension size must be 0 or the byte size of some unsigned integer type,
and only enforce-on-mutate modes may enable it.

Note that the constants and the `mode` template are under the `guard::EMode` struct, 
but the enforcement config itself is of a separate type `guard::impl::EConfig`. This
is due to a C++ limitation that a class cannot contain static constexper members of
its own type. 

```cpp
// enforce at construction only
constexpr auto CheckOnce = EMode::mode<true, false>;
// mutate-only, 8-byte suspension counter
constexpr auto Suspendable = EMode::mode<false, true, 8>;

using Custom = guard::Guard<Suspendable, int, ClampPercent>;
```


### Storage modes

The template parameter `U` selects how the value is held:

| `U` | Semantics |
| --- | --- |
| `T` | The wrapper **owns** a `T`. Copy/move of the wrapper copy/move the value. |
| `T&` | The wrapper **aliases** an existing object; rules receive `T*`. |
| `const T&` | The wrapper is a read-only **view**; rules receive `const T*`. |

Reference-backed wrappers never copy or move the referent.

`U` may also be another `Guard`, a raw pointer, or any type with an
`operator->`. Nesting, pointer storage, and arrow access are the programmer's
responsibility, as with any raw access.

**Aliasing:** A `T&` wrapper only sees mutations made *through the
wrapper*. Direct mutation of the aliased object bypasses enforcement until
you call `touch()`.

**Lifetime:** A reference-backed wrapper must not outlive the object it
aliases. Do not bind one to a temporary.

### Construction and conversion

Normal construction is straightforward:

1. If the argument is exactly the destination `Guard` type, the ordinary
   copy/move constructor transfers both the value and the rule state.
2. Otherwise the destination's rules are default-constructed when they are
   not supplied explicitly, the value is built through `U` (owned wrappers
   move/copy it, reference wrappers rebind to the referent), and the rules
   are then enforced according to the mode: `Once`, `Always`, and `SAlways`
   run them once at construction; `Passive`, `Defer`, and `SDefer` do not.

Exact copy/move construction is deliberately only a storage operation: it
copy/moves the value and rule state but does **not** invoke any rule. This
is important for types whose moved state is valid but not suitable for
immediate enforcement or container types that might copy and move elements
during reallocation. Use `touch()`/`commit()` to enforce afterward.

When nested guards are involved, construction is resolved by guard depth:
a non-guard has depth 0, and `Guard<M, U, E...>` has depth `1 + depth(U)`.

1. If the types differ, the deeper side is peeled first. A deeper source is
   unwrapped through `get()`; a deeper destination begins constructing its
   `U` from the source.
2. At equal depth, the destination is peeled first. The process repeats
   until it reaches either an exact shared guard type or the underlying
   non-guard value.

```cpp
Always<long, LongRule> converted = Always<int, IntRule>{5};
// Both have depth 1: peel the destination, then unwrap the source to int.

using Shared = Passive<int, AddBy>;
using Left = Passive<Shared, LeftRule>;
using Right = Passive<Shared, RightRule>;

Right destination = Left{Shared{5, AddBy{23}}};
// Equal-depth branches align at Shared, preserving AddBy{23} and its value.
```

Converting from an rvalue guard moves at the aligned layer; an lvalue copies
there. A reference-backed destination rebinds to the source's referent, so it
must not outlive that referent.

### Factory helper (`make_guard()`)

`make_guard<M>(value, rules...)` is the deduction counterpart of the aliases:
`U` and the rule types are deduced from the arguments. An lvalue value
produces a **reference** guard that aliases the object, while an rvalue
produces an owning guard:

```cpp
int raw = 250;
auto aliasing = make_guard<EMode::Defer>(raw, ClampPercent{});   // Defer<int&, ClampPercent>
raw = 300;
aliasing.touch();                                                // clamps raw through the alias
assert(raw == 100);

auto owning = make_guard<EMode::Defer>(int{250}, ClampPercent{});  // Defer<int, ClampPercent>
owning.touch();
assert(owning == 100);
```

At least one rule is required. `make_guard` is `noexcept` exactly when the
wrapped construction is.

---

## Rules

A rule is any callable `R` such that `std::invoke(rule, U*)` is valid and
returns `void`. It may be a plain functor:

```cpp
struct ClampPercent {
    void operator()(int* value) const noexcept {
        *value = std::clamp(*value, 0, 100);
    }
};
```

Rules may be stateful or `final`; they are stored with an empty-base
optimization when possible. A rule that throws aborts the enclosing operation
(see [Exceptions](#exceptions)).

Rules are **always** owned by the wrapper. If a reference to a callable is
necessary, wrap it in a `std::reference_wrapper`.

### Rule state

The stored rule instances are exposed through the nested type
`Guard::RuleState` (a class that privately wraps the rule storage). Because
rules may be stateful, the rule state is an independent, explicitly managed
part of the wrapper:

- **Assignment transfers only the value.** `a = b` copies/moves `b`'s value
  into `a` but leaves `a`'s rule instances untouched; enforcement afterward
  runs with `a`'s own rules. The philosophy is that a rule transfer must be
  explicit, since rules may be stateful and the user may not want to copy them.
- **Exact copy/move construction transfers rules** — a newly built wrapper
  receives its rules from the source. Conversion between different nested
  guard types aligns their depths recursively; if it reaches an exact shared
  guard type, that whole layer is copied/moved, including its rule state.
  Otherwise only the final underlying value transfers.

Transfer rule state explicitly with `get_rule_state()` / `set_rule_state()`.
`get_rule_state()` returns a **reference** to the stored rules — mutable for a
non-const guard, `const` otherwise — and never copies. Copy only when you
explicitly want a snapshot:

```cpp
struct MaxOf {
    int max;
    void operator()(int *value) const noexcept { if (*value > max) *value = max; }
};

using State = Always<int, MaxOf>::RuleState;

Always<int, MaxOf> a(100, MaxOf{50});   // → 50
Always<int, MaxOf> b(200, MaxOf{90});   // → 90

b = a;                       // value copied, rule stays MaxOf{90}
b = 80;                      // → 80 (proves the rule was not copied)
b.set_rule_state(a.get_rule_state());  // now MaxOf{50}
b = 80;                      // → 50

State &live = b.get_rule_state();        // reference to b's stored rules — no copy
State snapshot = b.get_rule_state();     // explicit copy, if you want one
```

`set_rule_state` copies/moves into the wrapper and does not re-enforce the current
value; call `touch()`/`commit()` if the new rules should apply immediately.

Rule access is available through the `RuleState` handle returned by
`get_rule_state()`, whose `access<I>()` returns a mutable or `const` reference
depending on the handle's constness:

```cpp
b.get_rule_state().access<0>().max = 42;   // mutate the stored rule in place
const auto &cb = b;
cb.get_rule_state().access<0>().max;       // const read

b.get_rule_state().access<1>();            // access another rule
```

Rules run left-to-right, so `access<0>()` is the first rule, `access<1>()` the
second, and so on. Out-of-range indices are a compile-time error.

A common pattern is to access an underlying rule state through the guard directly.
For this purpose, `Guard` provides a forwarding `access_rule<I>()`:
```cpp
guard.access_rule<0>().max = 42;   // mutate the stored rule in place
const auto &cguard = guard;
cguard.access_rule<0>().max;       // const read
```
  
### Rule adapters

Instead of writing functors by hand, adapters wrap a pointer-to-member or a
compile-time callable:

| Adapter | Behavior |
| --- | --- |
| `Apply<Fn, Args...>` | Calls `Fn` for its side effect; the result is discarded |
| `Transform<Fn, Args...>` | Calls `Fn` and assigns its result back to the value |
| `Allow<Fn, Args...>` | Throws unless `Fn(...) == true` |
| `Deny<Fn, Args...>` | Throws if `Fn(...) == true` |
| `Require<Res, Fn, Args...>` | Throws unless `Fn(...) == Res` |
| `Forbid<Res, Fn, Args...>` | Throws if `Fn(...) == Res` |

`Fn` may be a free function, a member-function pointer, or a stateless lambda.
Adapters dispatch between `Fn(T&, Args...)` (reference form) and
`Fn(T*, Args...)` (pointer form). Invalid combinations fail to satisfy the
adapter's `operator()` constraints rather than producing body-level errors:

```cpp
static_assert(!std::invocable<Allow<[](int*) { return EmptyStruct{}; }>&, int*>);
static_assert(std::invocable<Apply<[](int* v) { ++*v; }>&, int*>);
static_assert(!std::invocable<Transform<[](int) {}>&, int*>);
```

For read-only views (`U = const T&`), rules must accept `const T*`, which the
compiler enforces at the call site.

If it is necessary to have type-erased rules (e.g. for container compatibility), 
consider using STL wrappers like `std::function<void(T*)>`.

---

## Accessing and mutating

**Main Principle:** `Guard` can only enforce when it knows a mutation has occurred. 
Any operation made on the exposed underlying value directly bypasses automatic enforcement. 
Revalidate with `touch()` (`Always`/`Defer`/`SAlways`/`SDefer`) or `commit()` (every mode).

### Reads

```cpp
value.get();                   // U& on a non-const guard, const U& on a const one
std::as_const(value).get();    // always const U&
static_cast<const U&>(value);  // always const U&
```

Mutating through the returned `U&` bypasses enforcement; revalidate with
`touch()` (`Always`/`Defer`/`SAlways`/`SDefer`) or `commit()` (every mode).

### Manual enforcement

`commit()` forces enforcement explicitly in every mode. If an
`SAlways`/`SDefer` wrapper is suspended, `commit()` enforces immediately
and clears the dirty bit.

`touch()` revalidates like a mutation: it enforces for every mutation mode
and is a no-op for `Passive` and `Once`. If an `SAlways`/`SDefer` wrapper
is suspended, `touch()` does **not** enforce immediately but marks the
wrapper as dirty so that enforcement will run when the outermost
suspension scope exits.

### Apply (`apply()`)

*Not to be confused with the rule adapter `Apply`.*

`apply(fn)` invokes `fn` on the wrapped value — either `U&` or `U*` — and
returns exactly what `fn` returns. In `Always`/`Defer`/`SAlways`/`SDefer` mode
it enforces afterward when `fn` was handed a mutable reference/pointer (i.e. it
could have mutated):

```cpp
Always<std::vector<int>, ClampElements<0, 10>> vec{{1, 2}};
int n = vec.apply([](std::vector<int> &v) {
    v.push_back(50);     // bypasses the check
    v[1] = 30;           // bypasses the check
    assert(v[1] == 30);
    return static_cast<int>(v.size());
});
assert(n == 3);
assert(vec == std::vector<int>{1, 10, 10});   // enforced after apply
```

Read-only callables (taking `const T&` / `const T*`) do not trigger
enforcement, and `Passive`/`Once` wrappers never enforce here.

### Assignment and compound operators

```cpp
value = 50;
value += 5;   value -= 5;   value *= 2;   value /= 2;
value %= 3;   value &= 1;   value |= 2;   value ^= 3;
value <<= 1;  value >>= 1;
++value;  --value;
```

`Always`, `Defer`, `SAlways`, and `SDefer` enforce after each of these; 
`Passive` and `Once` do not.

Assigning from another guard (`a = b`) copies **only the value**; the
destination keeps its own rule state (see [Rule state](#rule-state)).

### Methods (`operator->`)

```cpp
Always<std::string, ForceLowercase> str{"hello"};
str->append(" WORLD");   // → "hello world"
```

Non-const method calls go through an RAII proxy so `Always`, `Defer`,
`SAlways`, and `SDefer` enforce **after** the full member-expression completes. 
The proxy checks only if the call did not throw; otherwise it lets the original 
exception propagate and leaves the value unchecked.

However, if the call returns a mutable reference/pointer to the internals, the 
proxy cannot magically enforce afterward:

```cpp
Always<std::vector<int>, ClampElements<0, 10>> my_vec{std::vector<int>{1, 2, 3}};
(*my_vec->begin()) = 50;             // enforced after the assignment
assert(my_vec[0] == 10);
my_vec[0] = 1;
auto it = my_vec->begin();           // enforce here since begin() is non-const
*it = 50;                            // but Guard cannot see this mutation
assert(my_vec[0] == 50);
```

### Call operator (`operator()`)

If `U` is callable, `value(args...)` forwards the call. The mutable overload
enforces afterward for `Always`, `Defer`, `SAlways`, and `SDefer` if the
`operator()` of the underlying is non-const, unless the call throws. As with 
`operator->`, a call that returns a mutable reference or pointer to the internals 
may later bypass checks; revalidate with `touch()` when needed.

Do note that unlike `operator->` which guards all mutations in the full 
member-expression, `operator()` only guards the call itself. Any mutation of the
returned reference/pointer is not observed by the wrapper, even if the mutation
happens in the same expression.

### Indexing (`operator[]`)

`operator[]` forwards directly to the underlying value: a non-const guard
returns `T&`, a const guard returns `const T&` — no proxy, no check, no
overhead. Mutating through the returned reference bypasses enforcement;
revalidate with `touch()`.

If possible, prefer wrapping the inner type rather than the container itself, 
so that the wrapper can enforce on each element individually:
```cpp
// inconvenient and wasteful
Always<std::vector<int>, ClampElements<0, 10>> vec{{1, 2, 3}};
vec[1] = -10;   // bypasses the check
vec.touch();    // revalidates the whole vector even if only one element changed
for (auto& x : vec);   // compile error: vec has no iterator

// better: wrap the element type
std::vector<Always<int, Transform<std::clamp<int>, 0, 10>>> vec2{{1, 2, 3}};
vec[1] = -10;   // automatically clamped to 0
for (auto& x : vec2);   // works, vec2 has an iterator

// if we do need to wrap the container, bind(), apply(), and operator-> may help
Always<std::vector<int>, KeepSorted> vec3{{1, 2, 3}};
vec3.bind(vec3[1]) = 10;                             // triggers re-sort
vec3.apply([](std::vector<int>& v) { v[2] = 0; });   // triggers re-sort
vec3->at(0) = 20;                                    // triggers re-sort
```
We make the choice not to automatically wrap the element type because wrapping a
return value is error-prone and may undermine clear reference and iterator semantics. 
Instead, the user can opt in the enforcement by using `bind()` or `apply()`.

### Binary operators

`+ - * / % & | ^ << >> == != < <= > >=` are forwarded as free functions,
unwrapping any `Guard` operand:

```cpp
Guard<long, E1> a;
Guard<int, E2> b;
long sum = a + b;   // unwrapped long (NOT guarded)
long x = 10 + a;
```

Results are **not** wrapped. Each guard operand is read through the const path
when the operation allows it; if the operation only compiles with mutable
access (e.g. `std::cin >> value`), the mutable path is used and that guard is
enforced right afterwards.

`&&`/`||` are intentionally **not** overloaded to preserve short-circuiting.

### Streams

Extraction goes through the generic binary operators: the const path is not
viable for `std::cin >> value` (nothing can extract into a `const U&`), so the
mutable path is selected and the wrapper is enforced right after the
extraction. A failed extraction may leave a partially written value behind
(then subject to the rules). Insertion uses the read-only path and never
enforces.

```cpp
std::cin >> value;   // extracted in place, then enforced
std::cout << value;  // read-only
```

---

## Binding

Binding is a way of creating a child guard that automatically triggers enforcement
on a parent guard as part of its own enforcement. This is useful for creating
external update handles or guarded views to the parent's internal value.

**Note:** Binding is syntactic sugar for constructing an ordinary Guard whose 
rule triggers the parent. No separate binding/wrapper mechanism is used here.

### Static Binding

`guard.bind(value)` creates a child guard bound to this one. The child's only
rule is an internal `Trigger` functor that, whenever the child enforces,
revalidates the parent: by default through `touch()`. The template
`bind<EConfig Me, TMode Mt>(value)` selects the child's enforcement mode
(`Defer` by default) and the trigger mode — `TMode::Touch` (default) or
`TMode::Commit`, which calls `commit()` on the parent and therefore forces
its rules even in non-mutating modes. The child stores `T&` (or `const T&`)
when `value` is an lvalue reference, and owns a `T` when it is an rvalue.
The binding operation itself does **not** touch the parent in `Defer` and
`Passive` modes, but it does in `Always`, `Once`, and `SAlways` modes.

```cpp
Always<std::vector<int>, ClampElements<0, 10>> container(std::vector<int>{1, 2});
container[0] = 50;                        // raw mutable access — no enforcement
assert(container[0] == 50);
auto element_0 = container.bind(container[0]);   // Defer<int&, Trigger<...>>
element_0 = 50;                           // triggers parent enforcement
assert(container[0] == 10);               // clamped by the parent's rules

Always<int, ClampPercent> parent(250);
assert(parent == 100);
parent.get() = 250;                       // secretly mutate the parent
auto owned = parent.bind(999);            // Defer<int&, Trigger<...>>
assert(owned == 999);                     // child's own value is untouched
assert(parent == 250);                    // bind() with Defer does not touch parent
owned = -5;                               // parent is re-enforced
assert(owned == -5);
assert(parent == 100);
```

To create a child that triggers multiple parents, use the free function
`bind(value, parent1, parent2, ...)`. The child revalidates every parent from
left to right on mutation. Wrap an individual parent in
`bind_as<TMode::Commit>(parent)` to commit that parent instead of touching it.
Again, the default binding mode is `Defer`, and the binding operation itself
does **not** touch any parent in `Defer` and `Passive` modes. Use
`bind<EConfig N>(value, parent1, parent2, ...)` to select another mode.

```cpp
// Multiple parents
Defer<int, ClampPercent> parent_a(250);
Defer<int, ClampPercent> parent_b(250);
Defer<int, ClampPercent> parent_c(250);
auto child = bind(42, parent_a, parent_b, parent_c);
assert(parent_a == 250 && parent_b == 250 && parent_c == 250);
child = 500;                             // triggers all three parents
assert(child == 500);                    // child's own value is untouched
assert(parent_a == 100 && parent_b == 100 && parent_c == 100);
```

`TMode::Commit` forces a parent's rules even when the parent is `Passive`:

```cpp
Passive<int, ClampPercent> passive_parent(250);
auto committing = bind(42, bind_as<TMode::Commit>(passive_parent));
committing.touch();                    // child revalidates → commits the parent
assert(passive_parent == 100);
```

The trigger only revalidates the parent (`touch()` or `commit()`); it never
changes the child's value. A `const T&` child is a read-only view bound to 
the parent.

A trigger can be rebound to a different parent of the **same** type via 
`Trigger::set_parent()`. To obtain the pointer to the parent, use `Trigger::get_parent()`.

```cpp
Defer<int, ClampPercent> parent_a(250);
auto child = parent_a.bind(42);
Defer<int, ClampPercent> parent_b(std::move(parent_a));  // child dangling!
child.access_rule<0>().set_parent(&parent_b);   // rebind to the new parent
child.access_rule<0>().set_parent(nullptr);     // unbind the parent
```

We also provide a factory helper `make_trigger` to create a standalone trigger object, 
so that it can be freely composed with other rules. The pattern is similar to `bind()`:
```cpp
Defer<int, ClampPercent> parent_a(250);
auto trigger_1 = make_trigger(parent_a);   // default trigger mode is Touch
auto trigger_2 = make_trigger(bind_as<TMode::Commit>(parent_a));  // trigger mode is Commit
```

A static trigger is pointer-sized. Static binding forms a dependency graph in the type system. Since a child guard's type
signature contains the entire parent type, the dependency graph is always acyclic.

### Dynamic Binding

**Warning:** This is an advanced feature involving type-erasure to deliberately bypass
the strict type system. Use with care.

Dynamic binding allows a child guard to rebind to a different parent or switch to
a different trigger mode at runtime. The type for a dynamically bound trigger is
`DTrigger` (without template parameters). To create a dynamically bound child, 
use `parent.dbind<EConfig Me = Defer>(TMode Mt, value)` (where `Mt` can be omitted 
and is `Touch` by default) or the free function 
`dbind<EConfig Me = Defer>(Mt, value, parent1, parent2, ...)`. When using the free 
function, the default trigger mode is also `Touch`; wrap a parent in 
`dbind_as(TMode Mt, parent)` to select a different trigger mode for that parent.   

```cpp
Passive<int, ClampPercent> parent_i(250);
Defer<long, ClampLongPercent> parent_ii(250);
Defer<std::string, TrimToFive> parent_iii("abcdefgh");
auto dchild = parent_i.dbind(TMode::Touch, 42);
dchild.touch();                                   // touches parent_i, do nothing
assert(parent_i == 250 && parent_ii == 250);
dchild.access_rule<0>().set_mode(TMode::Commit);  // rebind to commit mode
dchild.touch();                                   // commits parent_i
assert(parent_i == 100 && parent_ii == 250);
dchild.access_rule<0>().set_parent(&parent_ii);   // rebind to parent_ii
dchild.touch();                                   // re-enforces parent_ii
assert(parent_i == 100 && parent_ii == 100);
auto dchild_2 = parent_iii.dbind(10);      // default mode is Touch
dchild_2.touch();                                 // touches parent_iii
assert(parent_iii == "abcde");

Passive<int, ClampPercent> parent_iv(250);
Passive<long, ClampLongPercent> parent_v(250);
Defer<std::string, TrimToFive> parent_vi("abcdefgh");
auto dchild_3 = dbind<EMode::Passive>(
    42, 
    dbind_as(TMode::Commit, parent_iv),
    parent_v,
    dbind_as(TMode::Commit, parent_vi)
);
dchild_3.commit();    // commits parent_iv and parent_vi, touches parent_v
assert(parent_iv == 100 && parent_v == 250 && parent_vi == "abcde");
dchild_3.access_rule<2>().set_mode(TMode::Touch); 
// trigger for parent_vi is now in Touch mode
```
We also provide a factory helper `make_dtrigger` to create a standalone trigger 
object, so that it can be freely composed with other rules. The pattern is similar to 
`dbind()`:
```cpp
Defer<int, ClampPercent> parent_a(250);
auto trigger_1 = make_dtrigger(parent_a);   // default trigger mode is Touch
auto trigger_2 = make_dtrigger(dbind_as(TMode::Commit, parent_a));
// trigger mode is Commit
```

`DTrigger` contains **three** pointers and one `TMode` enum, so it is
larger than a statically bound trigger as a cost of flexibility. Plus, it cannot
check in compile time whether the parent type implements the required `touch()` or
`commit()` methods. If a  parent does not have the required method, the trigger will
throw a `std::runtime_error` at runtime. If rebinding to a different parent type is
not the intended use case, prefer static binding for better performance and type safety.

---

## Suspension

This feature is exclusive to `SAlways` and `SDefer` guards.

`suspend()` returns a `SuspendGuard` object, which is an RAII handle that 
suspends enforcement for the lifetime of the object. During suspension, the
wrapper maintains a suspension counter and a dirty flag, so that enforcements
only happens on leaving the outermost suspension scope with an observed mutation.

```cpp
SAlways<int, ClampPercent> value(50);
{
    auto s = value.suspend();
    value = 150;               // not clamped here
    assert(value == 150);
    { auto nested = value.suspend(); value = -50; }   // still suspended
    assert(value == -50);
}                              // one clamp runs → 0
```

The suspension handle has a `nocommit()` method that keeps the value unvalidated at 
scope exit (an intentional, potentially incoherent state).

```cpp
SAlways<int, ClampPercent> value(50);
{
    auto s = value.suspend();
    s.nocommit();              // keep the value unvalidated at scope exit
    value = -50;
}                              // no clamp runs → -50
assert(value == -50);

value = 50;
{
    auto s = value.suspend().nocommit();    // this also works
    value = -50;
}
assert(value == -50);
```

---

## Exceptions

- Rule exceptions propagate to the caller of the triggering operation.
- Destructors that would run a rule never throw:
`ArrowProxy` / `CallProxy` / `SuspendGuard` skip enforcement during stack unwinding.
- `noexcept` specifications are conditional and exact: constructors, `bind()`,
  `touch()`, `commit()`, and the enforcement helpers are `noexcept` exactly
  when the underlying value operations and every rule are. `Transform` also
  accounts for the assignment back into the value.
- After an incoherent state, call `commit()` — or `touch()` on
  `Always`/`Defer`/`SAlways`/`SDefer` — to revalidate or repair.

---

## Size and performance

- Rules that are empty and non-`final` use empty-base optimization; owned
  `Passive`/`Once`/`Defer`/`Always` wrappers add **zero** bytes:
  ```cpp
  static_assert(sizeof(Passive<int, ClampPercent>) == sizeof(int));
  static_assert(sizeof(Once<int, SetTo<1>, SetTo<2>, SetTo<3>>) == sizeof(int));
  ```
- `SAlways` and `SDefer` add one `uint32_t` for suspension state:
  ```cpp
  static_assert(sizeof(SAlways<int, ClampPercent>) == sizeof(int) + sizeof(uint32_t));
  ```
- Reference-backed wrappers are pointer-sized if all rules are empty and non-`final`:
  ```cpp
  static_assert(sizeof(Once<const MyVector&, NonEmpty<MyVector>>) ==
                sizeof(const MyVector*));
  ```
- `Guard` is standard-layout if `U` and all rules are standard-layout, 
  and the underlying value is the first member:
  ```cpp
  Always<int, ClampPercent> my_guard(0);
  static_assert(std::is_standard_layout_v<decltype(my_guard)>);
  assert(&my_guard.get() == reinterpret_cast<int *>(&my_guard));
  ```
- No virtual calls, no allocations, no type-erasure.

---

## Restrictions

- At least one rule is required.
- Every rule must accept `U*` (or `const T*` for const-reference views) and
  return `void`.
- Rule enforcement is **not** atomic — if a rule throws, the wrapper may be left in 
  an incoherent state. A wise choice is to treat any state after an exception as 
  potentially incoherent and call `touch()` or `commit()` to revalidate or repair.
