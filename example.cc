#include "guard.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace guard;

template <typename G, typename = void>
struct CanSuspend : std::false_type {};

template <typename G>
struct CanSuspend<G, std::void_t<decltype(std::declval<G &>().suspend())>>
    : std::true_type {};

// ===========================================================================
// Rules
// ===========================================================================

struct ClampPercent {
    void operator()(int *value) const noexcept {
        *value = std::clamp(*value, 0, 100);
    }
};

struct ClampLongPercent {
    void operator()(long *value) const noexcept {
        *value = std::clamp(*value, 0L, 100L);
    }
};

struct ClampElements {
    void operator()(std::vector<int> *value) const noexcept {
        for (int &element : *value) element = std::clamp(element, 0, 10);
    }
};

struct TrimToFive {
    void operator()(std::string *value) const {
        value->resize(std::min<std::size_t>(value->size(), 5));
    }
};

struct MaxOf {
    int max;
    void operator()(int *value) const noexcept {
        if (*value > max) *value = max;
    }
};

struct MaxLongOf {
    long max;
    void operator()(long *value) const noexcept {
        if (*value > max) *value = max;
    }
};

struct AddBy {
    int amount;
    void operator()(int *value) const noexcept { *value += amount; }
};

struct MultiplyBy final {
    int factor;
    void operator()(int *value) const noexcept { *value *= factor; }
};

struct MoveOnlyCap {
    std::unique_ptr<int> cap;
    MoveOnlyCap() : cap(std::make_unique<int>(0)) {}
    MoveOnlyCap(MoveOnlyCap &&) = default;
    MoveOnlyCap &operator=(MoveOnlyCap &&) = default;
    MoveOnlyCap(const MoveOnlyCap &) = delete;
    MoveOnlyCap &operator=(const MoveOnlyCap &) = delete;
    void operator()(int *value) const noexcept {
        if (*value > *cap) *value = *cap;
    }
};

struct Observe {
    int *checks;
    void operator()(const int *) const noexcept { ++*checks; }
};

template <int N>
struct SetTo {
    void operator()(auto *value) const noexcept { *value = N; }
};

struct Counter {
    int calls = 0;
    int operator()(int add) {
        calls += add;
        return calls;
    }
    int operator()() const noexcept { return calls; }
    void fail() {
        calls = -1;
        throw std::runtime_error("counter failure");
    }
};

struct KeepCounter {
    void operator()(Counter *counter) const noexcept {
        counter->calls = std::clamp(counter->calls, 0, 1000);
    }
};

// Underlying types that would previously have been rejected as dangerous:

struct PtrLike {
    std::string text;
    std::string *operator->() noexcept { return &text; }
    const std::string *operator->() const noexcept { return &text; }
};

struct KeepPtrText {
    void operator()(PtrLike *value) const noexcept {
        value->text.resize(std::min<std::size_t>(value->text.size(), 3));
    }
};

int fallback = 7;

struct EnsureNonNull {
    void operator()(int **value) const noexcept {
        if (!*value) *value = &fallback;
    }
};

struct TouchInner {
    template <typename Inner>
    void operator()(Inner *inner) const { inner->touch(); }
};

template <std::size_t N>
struct RelayInner {
    template <typename Inner>
    void operator()(Inner *inner) const { inner->touch(); }
};

struct MyVector : public std::vector<int> {
    using std::vector<int>::vector;
    int &operator()(std::size_t index) { return this->at(index); }
};

// ===========================================================================
// Tests
// ===========================================================================

int main() {
    // --- Modes -------------------------------------------------------------
    static_assert(!impl::enforce_on_construction_v<EMode::Passive>);
    static_assert(impl::enforce_on_construction_v<EMode::Once>);
    static_assert(!impl::enforce_on_construction_v<EMode::Defer>);
    static_assert(impl::enforce_on_construction_v<EMode::Always>);
    static_assert(!impl::enforce_on_construction_v<EMode::SDefer>);
    static_assert(impl::enforce_on_construction_v<EMode::SAlways>);
    static_assert(!impl::enforce_on_mutation_v<EMode::Passive>);
    static_assert(!impl::enforce_on_mutation_v<EMode::Once>);
    static_assert(impl::enforce_on_mutation_v<EMode::Defer>);
    static_assert(impl::enforce_on_mutation_v<EMode::Always>);
    static_assert(impl::enforce_on_mutation_v<EMode::SDefer>);
    static_assert(impl::enforce_on_mutation_v<EMode::SAlways>);
    static_assert(!impl::suspendable_v<EMode::Passive>);
    static_assert(!impl::suspendable_v<EMode::Once>);
    static_assert(!impl::suspendable_v<EMode::Defer>);
    static_assert(!impl::suspendable_v<EMode::Always>);
    static_assert(impl::suspendable_v<EMode::SDefer>);
    static_assert(impl::suspendable_v<EMode::SAlways>);

    // --- Noexcept guarantees -----------------------------------------------
    static_assert(std::is_nothrow_default_constructible_v<Defer<int, ClampPercent>>);
    static_assert(std::is_nothrow_copy_constructible_v<Passive<int, ClampPercent>>);
    static_assert(std::is_nothrow_move_constructible_v<Always<int, ClampPercent>>);
    static_assert(noexcept(std::declval<Passive<int, ClampPercent> &>().touch()));
    static_assert(noexcept(std::declval<Always<int, ClampPercent> &>().touch()));
    static_assert(noexcept(std::declval<Always<int, ClampPercent> &>().commit()));
    static_assert(noexcept(std::declval<SAlways<int, ClampPercent> &>().suspend()));
    static_assert(CanSuspend<SAlways<int, ClampPercent>>::value);
    static_assert(!CanSuspend<Always<int, ClampPercent>>::value);
    static_assert(noexcept(std::declval<Always<int, ClampPercent> &>().bind(1)));
    static_assert(noexcept(std::declval<Always<int, ClampPercent> &>()
                               .bind(std::declval<int &>())));
    static_assert(noexcept(
        bind(1, std::declval<Always<int, ClampPercent> &>())));
    static_assert(!noexcept(Always<int, Deny<[](int) { return false; }>>(1)));
    static_assert(noexcept(std::declval<Transform<[](int value) noexcept {
                                      return value;
                                  }> &>()(std::declval<int *>())));

    Always<int, ClampPercent> percent(120);
    assert(percent == 100);              // enforced at construction

    Once<int, ClampPercent> once(120);
    assert(once == 100);                 // enforced once, at construction
    once += 50;
    assert(once == 150);                 // not enforced afterwards
    once.touch();                        // touch() is a no-op for Once
    assert(once == 150);

    Passive<int, ClampPercent> passive(120);
    assert(passive == 120);              // never enforced automatically
    passive.touch();                     // touch() is a no-op for Passive
    assert(passive == 120);
    passive = 300;
    assert(passive == 300);
    passive.commit();                    // force enforcement
    assert(passive == 100);

    Defer<int, ClampPercent> deferred(120);
    assert(deferred == 120);             // no enforcement in any constructor
    deferred = 300;                      // mutations enforce, like Always
    assert(deferred == 100);
    deferred.touch();                    // touch() enforces, like a mutation
    assert(deferred == 100);
    Defer<int, ClampPercent> deferred_copy(deferred);
    assert(deferred_copy == 100);        // copying does not re-enforce
    Defer<int, ClampPercent> deferred_move(std::move(deferred_copy));
    assert(deferred_move == 100);        // moving is already storage-only

    // --- Mutating operators ------------------------------------------------
    percent -= 130;
    assert(percent == 0);
    ++percent;
    assert(percent == 1);
    percent *= 50;
    assert(percent == 50);

    // --- Binary operators and comparisons -----------------------------------
    Always<long, MaxLongOf> cap(1000L, MaxLongOf{500});
    assert(cap == 500L);
    const long sum = percent + cap;      // operands unwrapped; result is raw
    assert(sum == 550L);
    assert(10 + percent == 60);
    assert((cap / 5) == 100L);
    assert(percent < cap);

    int enforcements = 0;
    Always<int, Observe> test(42, Observe{&enforcements});
    assert(enforcements == 1);
    (void)(test == 42);    // const path: no enforcement
    assert(enforcements == 1);

    Always<int, ClampPercent> streamed(0);
    std::istringstream input("250");
    input >> streamed;
    assert(streamed == 100);         // mutable path: enforced after mutation
    std::ostringstream output;
    output << streamed;
    assert(output.str() == "100");

    // --- get() --------------------------------------------------------------
    static_assert(std::is_same_v<decltype(percent.get()), int &>);
    static_assert(std::is_same_v<decltype(std::as_const(percent).get()),
                                 const int &>);
    percent.get() = 300;             // raw mutable access — no enforcement
    assert(percent.get() == 300);
    percent.touch();
    assert(percent == 100);

    // --- apply() ------------------------------------------------------------
    Always<std::vector<int>, ClampElements> applied(std::vector<int>{1, 2});
    const int grown = applied.apply([](std::vector<int> &v) {
        v.push_back(50);
        return static_cast<int>(v.size());
    });
    assert(grown == 3);                  // returns whatever the callable returns
    assert((applied == std::vector<int>{1, 2, 10}));   // enforced after apply

    const int tail = applied.apply([](std::vector<int> *v) { return v->back(); });
    assert(tail == 10);

    // --- operator[] ---------------------------------------------------------
    Always<std::vector<int>, ClampElements> numbers(std::vector<int>{2, 4});
    numbers[0] = 50;                     // raw T& — no enforcement
    assert(numbers[0] == 50);
    numbers.touch();
    assert((numbers == std::vector<int>{10, 4}));
    static_assert(std::is_same_v<decltype(numbers[0]), int &>);
    static_assert(std::is_same_v<decltype(std::as_const(numbers)[0]),
                                 const int &>);

    // --- operator-> and operator() ------------------------------------------
    Always<std::string, TrimToFive> text("abcdefgh");
    assert(text == "abcde");
    text->append("XYZ");                 // enforced after the member call
    assert(text == "abcde");

    Always<Counter, KeepCounter> counter(Counter{});
    assert(counter(40) == 40);           // enforced after the call
    assert(std::as_const(counter)() == 40);
    try {
        counter->fail();                 // exception: enforcement is skipped
        assert(false && "Expected counter exception");
    } catch (const std::runtime_error &e) {
        assert(std::string(e.what()) == "counter failure");
    }
    assert(std::as_const(counter)->calls == -1);   // left unchecked
    counter.touch();
    assert(std::as_const(counter)->calls == 0);

    Always<MyVector, ClampElements> my_vec{MyVector{1, 2, 3}};
    auto it = my_vec->begin();
    *it = 50;                            // raw mutable access — no enforcement
    assert(my_vec[0] == 50);
    my_vec[0] = 1;
    (*my_vec->begin()) = 50;             // enforced after the assignment
    assert(my_vec[0] == 10);
    my_vec[0] = 1;
    auto& my_element = my_vec(0);        // operator() returns a reference
    my_element = 50;                     // no enforcement
    assert(my_vec[0] == 50);
    my_vec[0] = 1;
    my_vec(0) = 50;                      // assigned after enforcement!
    assert(my_vec[0] == 50);

    // --- Rule state ---------------------------------------------------------
    Always<int, MaxOf> rule_a(100, MaxOf{50});
    Always<int, MaxOf> rule_b(200, MaxOf{90});
    rule_b = rule_a;                     // assignment transfers only the value
    rule_b = 80;
    assert(rule_b == 80);                // rules still MaxOf{90}
    rule_b.set_rule_state(rule_a.get_rule_state());
    rule_b = 80;
    assert(rule_b == 50);                // now MaxOf{50}

    rule_b.access_rule<0>().max = 30;   // mutate the rule in place
    rule_b = 50;
    assert(rule_b == 30);

    static_assert(std::is_same_v<
                  decltype(rule_b.access_rule<0>()), MaxOf &>);
    static_assert(std::is_same_v<
                  decltype(std::as_const(rule_b).access_rule<0>()),
                  const MaxOf &>);

    // --- Ordered, stateful, final and move-only rules ------------------------
    Always<int, SetTo<1>, SetTo<2>, SetTo<3>> ordered(0);
    assert(ordered == 3);                // rules run left to right

    Once<int, AddBy, MultiplyBy> stateful(3, AddBy{2}, MultiplyBy{4});
    assert(stateful == 20);              // (3 + 2) * 4

    Always<int, MoveOnlyCap> move_only(50, MoveOnlyCap{});
    move_only = 200;
    assert(move_only == 0);
    move_only.access_rule<0>().cap = std::make_unique<int>(30);
    move_only = 50;
    assert(move_only == 30);
    auto move_only_2 = std::move(move_only);
    assert(move_only_2 == 30);
    static_assert(!std::copy_constructible<Always<int, MoveOnlyCap>::RuleState>);

    // --- Copy/move and cross-wrapper conversion ------------------------------
    Always<int, ClampPercent> copied = percent;
    Always<int, ClampPercent> moved = std::move(copied);
    assert(moved == 100);

    int move_checks = 0;
    Always<int, Observe> move_source(5, Observe{&move_checks});
    assert(move_checks == 1);
    Always<int, Observe> move_destination = std::move(move_source);
    assert(move_checks == 1);             // moving never invokes a rule
    assert(move_destination == 5);

    Always<long, ClampLongPercent> converted = percent;
    assert(converted == 100L);

    using SharedGuard = Passive<int, AddBy>;
    using LeftBranch = Passive<SharedGuard, RelayInner<1>>;
    using RightBranch = Passive<SharedGuard, RelayInner<2>>;
    using LeftOuter = Passive<LeftBranch, RelayInner<3>>;

    SharedGuard shared(5, AddBy{23});
    LeftBranch left(std::move(shared));
    LeftOuter deeper(std::move(left));
    RightBranch aligned(std::move(deeper));
    assert(aligned.get().access_rule<0>().amount == 23);

    // --- Rule adapters -------------------------------------------------------
    Always<int, Transform<std::clamp<int>, 0, 100>> transformed(150);
    assert(transformed == 100);
    transformed = -5;
    assert(transformed == 0);

    try {
        Always<int, Deny<[](int value) { return value < 0; }>> positive(-1);
        assert(false && "Expected Deny exception");
    } catch (const std::invalid_argument &) {
    }

    // --- Suspension ---------------------------------------------------------
    SAlways<int, ClampPercent> suspended(50);
    {
        auto suspension = suspended.suspend();
        suspended = 150;
        assert(suspended == 150);        // not enforced while suspended
        {
            auto nested = suspended.suspend();
            suspended = -50;
        }
        assert(suspended == -50);        // inner scope does not re-enable checks
        suspended = 150;
    }
    assert(suspended == 100);            // enforced once at the outermost exit

    SAlways<int, ClampPercent> uncommitted(50);
    {
        auto suspension = uncommitted.suspend().nocommit();
        uncommitted = 150;
    }
    assert(uncommitted == 150);          // intentionally left unvalidated
    uncommitted.touch();
    assert(uncommitted == 100);

    SAlways<int, ClampPercent> unwound(50);
    try {
        auto suspension = unwound.suspend();
        unwound = 150;
        throw std::runtime_error("unrelated failure");
    } catch (const std::runtime_error &e) {
        assert(std::string(e.what()) == "unrelated failure");
    }
    assert(unwound == 150);              // no enforcement during unwinding
    unwound.touch();
    assert(unwound == 100);

    SDefer<int, ClampPercent> deferred_suspend(50);
    {
        auto suspension = deferred_suspend.suspend();   // SDefer suspends too
        deferred_suspend = 150;
        assert(deferred_suspend == 150); // not enforced while suspended
    }
    assert(deferred_suspend == 100);     // enforced at scope exit

    // --- Reference storage ---------------------------------------------------
    int aliased = 150;
    Always<int &, ClampPercent> alias(aliased);
    assert(aliased == 100);              // enforced through the alias
    aliased = 300;                       // external mutation bypasses the guard
    assert(alias == 300);
    alias.touch();
    assert(aliased == 100);

    const int observed = 42;
    int checks = 0;
    Always<const int &, Observe> view(observed, Observe{&checks});
    assert(checks == 1);
    const int doubled = view.apply([](const int &v) { return v * 2; });
    assert(doubled == 84);
    assert(checks == 1);                 // read-only callable: no enforcement
    view.touch();
    assert(checks == 2);

    // --- bind() and Trigger -----------------------------------------------
    Always<int, ClampPercent> parent(250);
    assert(parent == 100);

    // Lvalue: the child aliases the object and re-enforces the parent.
    auto child = parent.bind(parent.get());   // Defer<int&, Trigger<...>>
    child = 500;                              // mutates the parent's value
    assert(child == 100);                     // clamped by the parent's rules

    // Rvalue: the child owns its value; only the parent is re-enforced.
    parent.get() = 250;                       // secretly mutate the parent
    auto owned = parent.bind(999);            // Defer<int, Trigger<...>>
    assert(owned == 999);                     // child's own value is untouched
    assert(parent == 250);                    // bind() with Defer does not touch parent
    owned = -5;
    assert(owned == -5);
    assert(parent == 100);                    // parent is re-enforced

    // Const lvalue: a read-only child view bound to the parent.
    const int fixed = 42;
    parent.get() = 250;                       // secretly mutate the parent
    auto const_child = parent.bind<EMode::Passive>(fixed);
    assert(parent == 250);                    // child is passive, no enforcement
    const_child.commit();                     // commit() forces the child's rules
    assert(parent == 100);                    // parent is re-enforced

    // Trigger accessors: rebind through get_parent(), read through the const one.
    {
        Trigger<TMode::Touch, Always<int, ClampPercent>> trigger(&parent);
        trigger.set_parent(nullptr);                    // mutable pointer access
        assert(trigger.get_parent() == nullptr);
        trigger.set_parent(&parent);                    // rebind to the parent
        const auto &const_trigger = trigger;
        assert(const_trigger.get_parent() == &parent);     // const read access
    }

    // Use case: containers
    Always<std::vector<int>, ClampElements> container(std::vector<int>{1, 2});
    container[0] = 50;                       // raw mutable access — no enforcement
    assert(container[0] == 50);
    auto element_0 = container.bind(container[0]);   // Defer<int&, Trigger<...>>
    element_0 = 50;                          // triggers parent enforcement
    assert(container[0] == 10);              // clamped by the parent's rules

    container = std::vector<int>{1, 2};
    container.bind(container[0]) = 50;       // triggers the parent to re-enforce
    assert(container[0] == 10);              // clamped by the parent's rules

    // Multiple parents
    Defer<int, ClampPercent> parent_a(250);
    Defer<int, ClampPercent> parent_b(250);
    Defer<int, ClampPercent> parent_c(250);
    auto multi_child = bind(42, parent_a, parent_b, parent_c);
    assert(parent_a == 250 && parent_b == 250 && parent_c == 250);
    multi_child = 500;                       // triggers all three parents
    assert(multi_child == 500);              // child's own value is untouched
    assert(parent_a == 100 && parent_b == 100 && parent_c == 100);

    // Non-default binding
    Passive<int, ClampPercent> passive_parent(250);
    auto touching_child = passive_parent.bind<EMode::Passive>(42);
    touching_child.commit();                 // touches parent
    assert(passive_parent == 250);           // parent is passive, so not enforced
    auto committing_child = passive_parent.bind<EMode::Passive, TMode::Commit>(42);
    committing_child.commit();               // commits parent
    assert(passive_parent == 100);           // parent is re-enforced

    Defer<int, ClampPercent> parent_1(250);
    Passive<int, ClampPercent> parent_2(250);
    Passive<int, ClampPercent> parent_3(250);
    auto multi_child_2 = bind<EMode::Passive>(
        42, parent_1, bind_as<TMode::Commit>(parent_2), parent_3
    );
    multi_child_2.commit();               // commits parent_2, touches 1 and 3
    assert(parent_1 == 100 && parent_2 == 100 && parent_3 == 250);

    // Bind something other than Guard
    struct FakeGuard {
        int value;    
        void touch() { ++value; }
        void commit() { --value; }
    };
    FakeGuard fake_guard_1{0}, fake_guard_2{0};
    auto trigger_handle = bind(15, fake_guard_1, bind_as<TMode::Commit>(fake_guard_2));
    trigger_handle.touch();
    assert(fake_guard_1.value == 1 && fake_guard_2.value == -1);

    // dynamic binding
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

    // --- Underlying types with operator-> ------------------------------------
    Always<int, AddBy> inner(1, AddBy{2});
    Always<Always<int, AddBy>, TouchInner> nested(inner);
    assert(nested.get().access_rule<0>().amount == 2);
    nested.get().get() = 10;             // raw access bypasses the inner guard
    nested.touch();                      // TouchInner calls the inner touch()
    assert(nested.get().get() == 12);    // inner stateful rule was preserved

    // --- make_guard -----------------------------------------------------------
    int raw = 250;
    auto made_ref = make_guard<EMode::Defer>(raw, ClampPercent{});
    static_assert(std::is_same_v<decltype(made_ref), Defer<int &, ClampPercent>>);
    assert(&made_ref.get() == &raw);             // aliases, no copy
    raw = 300;
    made_ref.touch();
    assert(raw == 100);                          // clamped through the alias

    auto made_owned = make_guard<EMode::Defer>(int{250}, ClampPercent{});
    static_assert(std::is_same_v<decltype(made_owned), Defer<int, ClampPercent>>);
    made_owned.touch();
    assert(made_owned == 100);
    assert(raw == 100);                          // unrelated to raw

    ClampPercent rule{};
    auto made_lvalue_rule = make_guard<EMode::Defer>(raw, rule);  // lvalue rule is copied
    static_assert(std::is_same_v<decltype(made_lvalue_rule),
                                 Defer<int &, ClampPercent>>);

    Always<PtrLike, KeepPtrText> ptrlike(PtrLike{"abcdef"});
    assert(ptrlike->text == "abc");      // type with operator->

    Always<int *, EnsureNonNull> pointer(nullptr, EnsureNonNull{});
    assert(*pointer.get() == 7);         // raw pointer storage
    *pointer.get() = 50;
    pointer.touch();
    assert(*pointer.get() == 50);

    // --- Size guarantees -----------------------------------------------------
    static_assert(sizeof(Passive<int, ClampPercent>) == sizeof(int));
    static_assert(sizeof(Once<int, SetTo<1>, SetTo<2>, SetTo<3>>) == sizeof(int));
    static_assert(sizeof(Always<int, ClampPercent>) == sizeof(int));
    static_assert(sizeof(SAlways<int, ClampPercent>) ==
                  sizeof(int) + sizeof(std::uint32_t));
    static_assert(sizeof(Once<const int &, Deny<[](int) { return false; }>>) ==
                  sizeof(const int *));

    // --- Memory layout guarantees --------------------------------------------
    Always<int, ClampPercent> my_guard(0);
    static_assert(std::is_standard_layout_v<decltype(my_guard)>);
    assert(&my_guard.get() == reinterpret_cast<int *>(&my_guard));
}