#ifndef GUARD_H
#define GUARD_H

#include <cassert>
#include <concepts>
#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace guard {

namespace integral_util {

template <std::size_t N, typename... Candidates>
struct unsigned_of_size_impl;

template <std::size_t N, typename T, typename... Rest>
struct unsigned_of_size_impl<N, T, Rest...>
    : std::conditional_t<
          sizeof(T) == N,
          std::type_identity<T>,
          unsigned_of_size_impl<N, Rest...>
      > {};

template <std::size_t N>
struct unsigned_of_size_impl<N> {};

template <std::size_t N>
using unsigned_of_size_t = typename unsigned_of_size_impl<
    N,
    unsigned char,
    unsigned short,
    unsigned int,
    unsigned long,
    unsigned long long
>::type;

template <std::size_t N, typename = void>
struct has_unsigned_of_size : std::false_type {};

template <std::size_t N>
struct has_unsigned_of_size<
    N, std::void_t<unsigned_of_size_t<N>>
> : std::true_type {};

template <std::size_t N>
inline constexpr bool has_unsigned_of_size_v = has_unsigned_of_size<N>::value;

} // namespace guard::integral_util

namespace impl {

struct EConfig {
    const bool enforce_on_construction;
    const bool enforce_on_mutation;
    const std::size_t suspension_state_size;

    constexpr bool operator==(const EConfig &other) const noexcept = default; 
};

template <EConfig M, typename U, typename... E>
class Guard;

template <typename G>
class SuspendGuard;

template <typename T>
struct SuspensionState {
    using value_type = T;
    value_type value = 0;
};

template <typename>
struct NoSuspensionState {};

template <std::size_t I, typename Rule,
          bool = std::is_empty_v<Rule> && !std::is_final_v<Rule>>
class RuleSlot;

template <std::size_t I, typename Rule>
class RuleSlot<I, Rule, true> : private Rule {
public:
    RuleSlot() = default;
    explicit RuleSlot(Rule rule) noexcept(
        std::is_nothrow_move_constructible_v<Rule>)
        : Rule(std::move(rule)) {}

    Rule &get() noexcept { return *this; }
    const Rule &get() const noexcept { return *this; }
};

template <std::size_t I, typename Rule>
class RuleSlot<I, Rule, false> {
public:
    RuleSlot() = default;
    explicit RuleSlot(Rule rule) noexcept(
        std::is_nothrow_move_constructible_v<Rule>)
        : rule_(std::move(rule)) {}

    Rule &get() noexcept { return rule_; }
    const Rule &get() const noexcept { return rule_; }

private:
    Rule rule_;
};

template <typename Indices, typename... Rules>
class RuleStateStorage;

template <std::size_t... I, typename... Rules>
class RuleStateStorage<std::index_sequence<I...>, Rules...>
    : private RuleSlot<I, Rules>... {
    template <std::size_t J>
    using RuleAt = std::tuple_element_t<J, std::tuple<Rules...>>;

public:
    RuleStateStorage() = default;

    explicit RuleStateStorage(Rules... rules) noexcept(
        (std::is_nothrow_move_constructible_v<Rules> && ...))
        : RuleSlot<I, Rules>(std::move(rules))... {}

    template <typename U>
    void enforce(U *value) {
        (std::invoke(static_cast<RuleSlot<I, Rules> &>(*this).get(), value), ...);
    }

    template <std::size_t J>
    RuleAt<J> &access() & noexcept {
        static_assert(J < sizeof...(Rules), "rule index out of range");
        return static_cast<RuleSlot<J, RuleAt<J>> &>(*this).get();
    }

    template <std::size_t J>
    const RuleAt<J> &access() const & noexcept {
        static_assert(J < sizeof...(Rules), "rule index out of range");
        return static_cast<const RuleSlot<J, RuleAt<J>> &>(*this).get();
    }
};

template <EConfig M>
inline constexpr bool enforce_on_construction_v = M.enforce_on_construction;

template <EConfig M>
inline constexpr bool enforce_on_mutation_v = M.enforce_on_mutation;

template <EConfig M>
inline constexpr bool suspendable_v = M.suspension_state_size > 0;

template <EConfig M, bool Suspendable = suspendable_v<M>>
struct SuspensionStorage;

template <EConfig M>
struct SuspensionStorage<M, true> {
    using type = SuspensionState<
        integral_util::unsigned_of_size_t<M.suspension_state_size>>;
};

template <EConfig M>
struct SuspensionStorage<M, false> {
    using type = NoSuspensionState<void>;
};

template <EConfig M>
using suspension_storage_t = typename SuspensionStorage<M>::type;

template <typename T>
struct is_guard : std::false_type {};

template <EConfig M, typename U, typename... E>
struct is_guard<Guard<M, U, E...>> : std::true_type {};

template <typename T>
inline constexpr bool is_guard_v = is_guard<std::remove_cvref_t<T>>::value;

template <typename T>
struct guard_depth : std::integral_constant<std::size_t, 0> {};

template <EConfig M, typename U, typename... E>
struct guard_depth<Guard<M, U, E...>>
    : std::integral_constant<
          std::size_t, 1 + guard_depth<std::remove_cvref_t<U>>::value> {};

template <typename T>
inline constexpr std::size_t guard_depth_v =
    guard_depth<std::remove_cvref_t<T>>::value;

template <typename T>
constexpr decltype(auto) unwrap(T &&value) noexcept {
    if constexpr (is_guard_v<T>) {
        return std::forward<T>(value).get();
    } else {
        return std::forward<T>(value);
    }
}

template <typename Destination, typename T>
constexpr decltype(auto) constructor_arg(T &&value) noexcept {
    if constexpr (is_guard_v<T> &&
                  guard_depth_v<T> > guard_depth_v<Destination>) {
        return constructor_arg<Destination>(
            std::forward<T>(value).get());
    } else {
        return std::forward<T>(value);
    }
}

template <typename T>
constexpr decltype(auto) unwrap_const(T &&value) noexcept {
    if constexpr (is_guard_v<T>) {
        if constexpr (std::is_lvalue_reference_v<T>) {
            return std::as_const(value).get();
        } else {
            return std::forward<T>(value).get();
        }
    } else {
        return std::forward<T>(value);
    }
}

template <typename Operand>
constexpr void enforce_binary_operand(Operand &&operand) {
    if constexpr (is_guard_v<Operand> &&
                  !std::is_const_v<std::remove_reference_t<Operand>>) {
        std::forward<Operand>(operand).enforce_after_change();
    }
}

template <typename Stored, typename T>
constexpr decltype(auto) move_stored(T &value) noexcept {
    if constexpr (std::is_reference_v<Stored>) {
        return (value);
    } else {
        return std::move(value);
    }
}

template <auto Fn, typename T, auto... Args>
concept ReferenceInvocable = requires(T &value) {
    std::invoke(Fn, value, Args...);
};

template <auto Fn, typename T, auto... Args>
concept PointerInvocable = requires(T *value) {
    std::invoke(Fn, value, Args...);
};

template <auto Fn, typename T, auto... Args>
concept TransformReferenceCompatible = requires(T &value) {
    requires std::is_assignable_v<
        T &, decltype(std::invoke(Fn, value, Args...))>;
};

template <auto Fn, typename T, auto... Args>
concept TransformPointerCompatible = requires(T *value) {
    requires std::is_assignable_v<
        T &, decltype(std::invoke(Fn, value, Args...))>;
};

template <auto Fn, typename T, auto Res, auto... Args>
concept CheckReferenceCompatible = requires(T &value) {
    { std::invoke(Fn, value, Args...) == Res } -> std::convertible_to<bool>;
};

template <auto Fn, typename T, auto Res, auto... Args>
concept CheckPointerCompatible = requires(T *value) {
    { std::invoke(Fn, value, Args...) == Res } -> std::convertible_to<bool>;
};

template <typename Rule, typename Pointer>
concept RuleFor = requires(Rule &rule, Pointer value) {
    { std::invoke(rule, value) } -> std::same_as<void>;
};

template <typename... E>
class RuleState
    : private RuleStateStorage<std::index_sequence_for<E...>, E...> {
    using Storage =
        RuleStateStorage<std::index_sequence_for<E...>, E...>;

    template <EConfig, typename, typename...>
    friend class Guard;

public:
    RuleState() = default;

    explicit RuleState(E... rules) noexcept(
        (std::is_nothrow_move_constructible_v<E> && ...))
        : Storage(std::move(rules)...) {}

    using Storage::access;

private:
    using Storage::enforce;
};

enum class TMode { Touch, Commit };

template <TMode M, typename G>
class Trigger {
public:
    Trigger() = default;
    explicit Trigger(G *parent) noexcept : parent_ptr_(parent) {}

    template <typename T> requires (M == TMode::Touch)
    void operator()(T *) const
        noexcept(noexcept(std::declval<G &>().touch())) {
        if (parent_ptr_ != nullptr) {
            (*parent_ptr_).touch();
        }
    }

    template <typename T> requires (M == TMode::Commit)
    void operator()(T *) const
        noexcept(noexcept(std::declval<G &>().commit())) {
        if (parent_ptr_ != nullptr) {
            (*parent_ptr_).commit();
        }
    }

    G *get_parent() const noexcept { return parent_ptr_; }
    void set_parent(G *new_parent) noexcept { parent_ptr_ = new_parent; }
    
private:
    G *parent_ptr_ = nullptr;
};

template <TMode M, typename G>
struct TriggerSpec {
    G *parent{nullptr};

    explicit TriggerSpec(G &parent) noexcept
        : parent(std::addressof(parent)) {}
};

template <typename T>
struct is_trigger_spec : std::false_type {};

template <TMode M, typename G>
struct is_trigger_spec<TriggerSpec<M, G>> : std::true_type {};

template <typename T>
inline constexpr bool is_trigger_spec_v =
    is_trigger_spec<std::remove_cvref_t<T>>::value;

template <typename G>
Trigger<TMode::Touch, G> make_trigger(G &parent) noexcept {
    return Trigger<TMode::Touch, G>{std::addressof(parent)};
}

template <TMode M, typename G>
Trigger<M, G> make_trigger(const TriggerSpec<M, G> &spec) noexcept {
    return Trigger<M, G>{spec.parent};
}

template <typename X>
using BindingTrigger = decltype(make_trigger(std::declval<X>()));

template <typename P>
concept BindingArgument = std::is_lvalue_reference_v<P> || impl::is_trigger_spec_v<P>;

class DTrigger {
public:
    DTrigger() = default;

    template <typename G>
    DTrigger(TMode mode, G *parent) noexcept
        : mode_(mode) {
        set_parent(parent);
    }

    template <typename T>
    void operator()(T *) const {
        primary_func_(parent_ptr_);
    }

    void *get_parent() const noexcept { return parent_ptr_; }

    template <typename G>
    void set_parent(G *new_parent) noexcept {
        parent_ptr_ = new_parent;
        if (new_parent == nullptr) {
            primary_func_ = &noop;
            secondary_func_ = &noop;
            return;
        }
        if constexpr (requires { new_parent->touch(); }) {
            primary_func_ = &touch<G>;
        } else {
            primary_func_ = &bad_parent;
        }
        if constexpr (requires { new_parent->commit(); }) {
            secondary_func_ = &commit<G>;
        } else {
            secondary_func_ = &bad_parent;
        }
        if (mode_ == TMode::Commit) {
            swap_func();
        }
    }

    TMode get_mode() const noexcept { return mode_; }

    void set_mode(TMode new_mode) noexcept {
        if (new_mode != mode_) {
            mode_ = new_mode;
            swap_func();
        }
    }

private:
    static void noop(void *) noexcept {}

    static void bad_parent(void *) {
        throw std::runtime_error(
            "DTrigger: parent does not support the requested operation"
        );
    }
    
    template <typename G>
    static void touch(void *parent) 
        noexcept(noexcept(std::declval<G &>().touch()))
    {
        static_cast<G *>(parent)->touch();
    }

    template <typename G>
    static void commit(void *parent) 
        noexcept(noexcept(std::declval<G &>().commit()))
    {
        static_cast<G *>(parent)->commit();
    }

    void swap_func() noexcept {
        std::swap(primary_func_, secondary_func_);
    }

    void *parent_ptr_{nullptr};
    void (*primary_func_)(void *){&noop};
    void (*secondary_func_)(void *){&noop};
    TMode mode_{TMode::Touch};
};

template <typename G>
struct DTriggerSpec {
    G *parent{nullptr};
    TMode mode{TMode::Touch};

    DTriggerSpec(TMode mode, G &parent) noexcept
        : parent(std::addressof(parent)), mode(mode) {}
};

template <typename T>
struct is_dtrigger_spec : std::false_type {};

template <typename G>
struct is_dtrigger_spec<DTriggerSpec<G>> : std::true_type {};

template <typename T>
inline constexpr bool is_dtrigger_spec_v =
    is_dtrigger_spec<std::remove_cvref_t<T>>::value;

template <typename G>
DTrigger make_dtrigger(G &parent) noexcept {
    return DTrigger{TMode::Touch, std::addressof(parent)};
}

template <typename G>
DTrigger make_dtrigger(const DTriggerSpec<G> &spec) noexcept {
    return DTrigger{spec.mode, spec.parent};
}
template <typename>
using BindingDTrigger = DTrigger;

template <typename G>
concept DBindingArgument = 
    std::is_lvalue_reference_v<G> || is_dtrigger_spec_v<G>;

template <EConfig M, typename U, typename... E>
class Guard : private RuleState<E...> {
    using ReferredType = std::remove_reference_t<U>;
    using RulePointer = ReferredType *;
    using ConstPointer = std::add_pointer_t<std::add_const_t<ReferredType>>;
    using ConstRef = std::add_lvalue_reference_t<std::add_const_t<ReferredType>>;

    static_assert(sizeof...(E) > 0, "Guard requires at least one rule");
    static_assert((RuleFor<E, RulePointer> && ...),
                  "Every rule must accept the underlying pointer and return void");

    template <EConfig, typename, typename...>
    friend class Guard;

    template <typename>
    friend class SuspendGuard;

    template <TMode Mt, typename G>
    friend class Trigger;

    template <typename Operand>
    friend constexpr void enforce_binary_operand(Operand &&);

    class ArrowProxy {
    public:
        explicit ArrowProxy(Guard &owner) noexcept : owner_(owner) {}
        ArrowProxy(const ArrowProxy &) = delete;
        ArrowProxy &operator=(const ArrowProxy &) = delete;
        ArrowProxy(ArrowProxy &&) = delete;
        ArrowProxy &operator=(ArrowProxy &&) = delete;

        ~ArrowProxy() noexcept(noexcept(std::declval<Guard &>().enforce_after_change())) {
            if (std::uncaught_exceptions() == 0) {
                owner_.enforce_after_change();
            }
        }

        RulePointer operator->() noexcept { return &owner_.value_; }

    private:
        Guard &owner_;
    };

    class CallProxy {
    public:
        explicit CallProxy(Guard &owner) noexcept : owner_(owner) {}

        CallProxy(const CallProxy &) = delete;
        CallProxy &operator=(const CallProxy &) = delete;
        CallProxy(CallProxy &&) = delete;
        CallProxy &operator=(CallProxy &&) = delete;

        ~CallProxy() noexcept(noexcept(std::declval<Guard &>().enforce_after_change())) {
            if (std::uncaught_exceptions() == 0) {
                owner_.enforce_after_change();
            }
        }

    private:
        Guard &owner_;
    };

public:
    using RuleState = ::guard::impl::RuleState<E...>;
    using value_type = U;
    using rule_types = std::tuple<E...>;
    static constexpr EConfig mode = M;

    Guard() noexcept(
        std::is_nothrow_default_constructible_v<U> &&
        std::is_nothrow_default_constructible_v<RuleState> &&
        (!enforce_on_construction_v<M> ||
         (std::is_nothrow_invocable_v<E &, RulePointer> && ...)))
        requires std::default_initializable<U> &&
                 (std::default_initializable<E> && ...)
        : RuleState{}, value_{} {
        enforce_constructor();
    }

    template <typename T>
        requires(!std::same_as<std::remove_cvref_t<T>, Guard> &&
                 (!is_guard_v<T> ||
                  guard_depth_v<T> <= guard_depth_v<Guard>) &&
                 std::constructible_from<
                     U, decltype(constructor_arg<U>(
                            std::declval<T &&>()))> &&
                 (std::default_initializable<E> && ...))
    explicit(!std::convertible_to<
             decltype(constructor_arg<U>(std::declval<T &&>())), U>)
        Guard(T &&value) noexcept(
            std::is_nothrow_constructible_v<
                U, decltype(constructor_arg<U>(
                       std::declval<T &&>()))> &&
            std::is_nothrow_default_constructible_v<RuleState> &&
            (!enforce_on_construction_v<M> ||
             (std::is_nothrow_invocable_v<E &, RulePointer> && ...)))
        : RuleState{},
          value_(constructor_arg<U>(std::forward<T>(value))) {
        enforce_constructor();
    }

    template <typename T>
        requires(!std::same_as<std::remove_cvref_t<T>, Guard> &&
                 is_guard_v<T> &&
                 guard_depth_v<T> > guard_depth_v<Guard> &&
                 std::constructible_from<
                     Guard, decltype(unwrap(std::declval<T &&>()))>)
    Guard(T &&value)
        : Guard(unwrap(std::forward<T>(value))) {}

    template <typename T>
        requires(!std::same_as<std::remove_cvref_t<T>, Guard> &&
                 std::constructible_from<
                     U, decltype(constructor_arg<U>(
                            std::declval<T &&>()))> &&
                 (std::move_constructible<E> && ...))
    Guard(T &&value, E... rules) noexcept(
        std::is_nothrow_constructible_v<
            U, decltype(constructor_arg<U>(
                   std::declval<T &&>()))> &&
        (std::is_nothrow_move_constructible_v<E> && ...) &&
        (!enforce_on_construction_v<M> ||
         (std::is_nothrow_invocable_v<E &, RulePointer> && ...)))
        : RuleState(std::move(rules)...),
          value_(constructor_arg<U>(std::forward<T>(value))) {
        enforce_constructor();
    }

    Guard(const Guard &other) noexcept(
        (std::is_reference_v<U> || std::is_nothrow_copy_constructible_v<U>) &&
        std::is_nothrow_copy_constructible_v<RuleState>)
        requires(std::is_reference_v<U> || std::copy_constructible<U>) &&
                 std::copy_constructible<RuleState>
        : RuleState(static_cast<const RuleState &>(other)), value_(other.value_) {}

    Guard(Guard &&other) noexcept(
        (std::is_reference_v<U> || std::is_nothrow_move_constructible_v<U>) &&
        std::is_nothrow_move_constructible_v<RuleState>)
        requires(std::is_reference_v<U> || std::move_constructible<U>) &&
                 std::move_constructible<RuleState>
                : RuleState(std::move(static_cast<RuleState &>(other))),
                    value_(move_stored<U>(other.value_)) {}

    Guard &operator=(const Guard &other)
        requires std::assignable_from<U &, const U &>
    {
        if (this != &other) {
            value_ = other.value_;
            enforce_after_change();
        }
        return *this;
    }

    Guard &operator=(Guard &&other) noexcept(
        std::is_nothrow_move_assignable_v<U> &&
        (std::is_nothrow_invocable_r_v<void, E &, RulePointer> && ...))
        requires std::assignable_from<U &, U>
    {
        if (this != &other) {
            value_ = std::move(other.value_);
            enforce_after_change();
        }
        return *this;
    }

    template <typename T>
        requires(!std::same_as<std::remove_cvref_t<T>, Guard> &&
                 requires(U &left, T &&right) {
                     left = unwrap(std::forward<T>(right));
                 })
    Guard &operator=(T &&other) {
        value_ = unwrap(std::forward<T>(other));
        enforce_after_change();
        return *this;
    }

    RuleState &get_rule_state() & noexcept {
        return static_cast<RuleState &>(*this);
    }

    const RuleState &get_rule_state() const & noexcept {
        return static_cast<const RuleState &>(*this);
    }

    RuleState&& get_rule_state() && noexcept {
        return std::move(static_cast<RuleState&>(*this));
    }

    const RuleState&& get_rule_state() const && noexcept {
        return std::move(static_cast<const RuleState&>(*this));
    }

    void set_rule_state(const RuleState &state) noexcept(
        std::is_nothrow_assignable_v<RuleState &, const RuleState &>)
        requires std::assignable_from<RuleState &, const RuleState &>
    {
        static_cast<RuleState &>(*this) = state;
    }

    void set_rule_state(RuleState &&state) noexcept(
        std::is_nothrow_assignable_v<RuleState &, RuleState>)
        requires std::assignable_from<RuleState &, RuleState>
    {
        static_cast<RuleState &>(*this) = std::move(state);
    }

    U &get() & noexcept { return value_; }
    const U &get() const & noexcept { return value_; }
    U &&get() && noexcept { return std::move(value_); }

    template <std::size_t J>
    decltype(auto) access_rule() & noexcept {
        return this->get_rule_state().template access<J>();
    }

    template <std::size_t J>
    decltype(auto) access_rule() const & noexcept {
        return this->get_rule_state().template access<J>();
    }

    template <typename Fn>
        requires std::invocable<Fn &, U &> || std::invocable<Fn &, RulePointer>
    decltype(auto) apply(Fn &&fn) {
        if constexpr (std::invocable<Fn &, U &>) {
            if constexpr (std::is_void_v<std::invoke_result_t<Fn &, U &>>) {
                std::invoke(fn, value_);
                if constexpr (!std::invocable<Fn &, ConstRef>) {
                    enforce_after_change();
                }
                return;
            } else {
                decltype(auto) result = std::invoke(fn, value_);
                if constexpr (!std::invocable<Fn &, ConstRef>) {
                    enforce_after_change();
                }
                return result;
            }
        } else {
            if constexpr (std::is_void_v<std::invoke_result_t<Fn &, RulePointer>>) {
                std::invoke(fn, &value_);
                if constexpr (!std::invocable<Fn &, ConstPointer>) {
                    enforce_after_change();
                }
                return;
            } else {
                decltype(auto) result = std::invoke(fn, &value_);
                if constexpr (!std::invocable<Fn &, ConstPointer>) {
                    enforce_after_change();
                }
                return result;
            }
        }
    }

    operator const U &() const & noexcept { return value_; }

    ArrowProxy operator->()
        requires std::is_class_v<ReferredType> || std::is_union_v<ReferredType>
    {
        return ArrowProxy(*this);
    }

    ConstPointer operator->() const noexcept
        requires std::is_class_v<ReferredType> || std::is_union_v<ReferredType>
    {
        return &value_;
    }

    template <typename... Args>
        requires std::invocable<U &, Args...>
    decltype(auto) operator()(Args &&...args)
        noexcept(std::is_nothrow_invocable_v<U &, Args...> &&
                 noexcept(std::declval<Guard &>().enforce_after_change()))
    {
        CallProxy check(*this);
        return std::invoke(value_, std::forward<Args>(args)...);
    }

    template <typename... Args>
        requires std::invocable<const U &, Args...>
    decltype(auto) operator()(Args &&...args) const
        noexcept(std::is_nothrow_invocable_v<const U &, Args...>)
    {
        return std::invoke(value_, std::forward<Args>(args)...);
    }

    template <typename Index>
        requires requires(U &value, Index &&index) {
            value[std::forward<Index>(index)];
        }
    decltype(auto) operator[](Index &&index) {
        return value_[std::forward<Index>(index)];
    }

    template <typename Index>
        requires requires(const U &value, Index &&index) {
            value[std::forward<Index>(index)];
        }
    decltype(auto) operator[](Index &&index) const {
        return value_[std::forward<Index>(index)];
    }

#define GUARD_COMPOUND_OPERATOR(op)                                          \
    template <typename T>                                                    \
        requires requires(U &left, T &&right) {                              \
            left op unwrap(std::forward<T>(right));                          \
        }                                                                    \
    Guard &operator op(T &&right) {                                          \
        value_ op unwrap(std::forward<T>(right));                            \
        enforce_after_change();                                              \
        return *this;                                                        \
    }

    GUARD_COMPOUND_OPERATOR(+=)
    GUARD_COMPOUND_OPERATOR(-=)
    GUARD_COMPOUND_OPERATOR(*=)
    GUARD_COMPOUND_OPERATOR(/=)
    GUARD_COMPOUND_OPERATOR(%=)
    GUARD_COMPOUND_OPERATOR(&=)
    GUARD_COMPOUND_OPERATOR(|=)
    GUARD_COMPOUND_OPERATOR(^=)
    GUARD_COMPOUND_OPERATOR(<<=)
    GUARD_COMPOUND_OPERATOR(>>=)

#undef GUARD_COMPOUND_OPERATOR

    Guard &operator++()
        requires requires(U &value) { ++value; }
    {
        ++value_;
        enforce_after_change();
        return *this;
    }

    ReferredType operator++(int)
        requires std::copy_constructible<ReferredType> && requires(U &value) { value++; }
    {
        ReferredType previous = value_++;
        enforce_after_change();
        return previous;
    }

    Guard &operator--()
        requires requires(U &value) { --value; }
    {
        --value_;
        enforce_after_change();
        return *this;
    }

    ReferredType operator--(int)
        requires std::copy_constructible<ReferredType> && requires(U &value) { value--; }
    {
        ReferredType previous = value_--;
        enforce_after_change();
        return previous;
    }

    void touch() noexcept(
        (!enforce_on_mutation_v<M>) ||
        (std::is_nothrow_invocable_v<E &, RulePointer> && ...)) {
        enforce_after_change();
    }

    void commit() noexcept(
        (std::is_nothrow_invocable_v<E &, RulePointer> && ...)) {
        enforce_forced();
        if constexpr (suspendable_v<M>) {
            suspension_.value &= ~static_cast<decltype(suspension_.value)>(1);
        }
    }

    [[nodiscard]] auto suspend() noexcept
        requires(suspendable_v<M>)
    {
        return SuspendGuard(*this);
    }

    template <
        EConfig Me = EConfig{false, true, 0}, 
        TMode Mt = TMode::Touch, 
        typename T
    >
    [[nodiscard]] auto bind(T &&value) noexcept(
        std::is_nothrow_constructible_v<
            Guard<Me, T, Trigger<Mt, Guard>>,
            decltype(std::forward<T>(value)), Trigger<Mt, Guard>>)
    {
        return Guard<Me, T, Trigger<Mt, Guard>>{
            std::forward<T>(value), Trigger<Mt, Guard>{this}
        };
    }

    template <
        EConfig Me = EConfig{false, true, 0},
        typename T
    >
    [[nodiscard]] auto dbind(T &&value) noexcept(
        std::is_nothrow_constructible_v<
            Guard<Me, T, DTrigger>,
            decltype(std::forward<T>(value)), DTrigger>)
    {
        return Guard<Me, T, DTrigger>{
            std::forward<T>(value), DTrigger{TMode::Touch, this}
        };
    }

    template <
        EConfig Me = EConfig{false, true, 0},
        typename T
    >
    [[nodiscard]] auto dbind(TMode tmode, T &&value) noexcept(
        std::is_nothrow_constructible_v<
            Guard<Me, T, DTrigger>,
            decltype(std::forward<T>(value)), DTrigger>)
    {
        return Guard<Me, T, DTrigger>{
            std::forward<T>(value), DTrigger{tmode, this}
        };
    }

private:
    void enforce_forced() noexcept(
        (std::is_nothrow_invocable_v<E &, RulePointer> && ...)) {
        RuleState::enforce(&value_);
    }

    void enforce_constructor() noexcept(
        (!enforce_on_construction_v<M>) ||
        (std::is_nothrow_invocable_v<E &, RulePointer> && ...)) {
        if constexpr (enforce_on_construction_v<M>) {
            enforce_forced();
        }
    }

    void enforce_after_change() noexcept(
        (!enforce_on_mutation_v<M>) ||
        (std::is_nothrow_invocable_v<E &, RulePointer> && ...)) {
        if constexpr (suspendable_v<M>) {
            if (suspension_.value == 0) {
                enforce_forced();
            } else {
                suspension_.value |= 1;
            }
        } else if constexpr (enforce_on_mutation_v<M>) {
            enforce_forced();
        }
    }

    void suspend_checks() noexcept
        requires(suspendable_v<M>)
    {
        assert(suspension_.value <=
               std::numeric_limits<decltype(suspension_.value)>::max() - 2);
        suspension_.value += 2;
    }

    [[nodiscard]] bool resume_checks() noexcept
        requires(suspendable_v<M>)
    {
        assert(suspension_.value >= 2);
        const bool check_pending = (suspension_.value & 1) != 0;
        suspension_.value -= 2;
        if (suspension_.value == 1 && check_pending) {
            suspension_.value = 0;
            return true;
        }
        return false;
    }

    U value_;
    [[no_unique_address]] suspension_storage_t<M> suspension_{};
};

template <EConfig M, typename U, typename... E>
    requires(suspendable_v<M>)
class SuspendGuard<Guard<M, U, E...>> {
public:
    using guard_type = Guard<M, U, E...>;

    explicit SuspendGuard(guard_type &guard) noexcept : guard_(&guard) {
        guard_->suspend_checks();
    }

    SuspendGuard(const SuspendGuard &) = delete;
    SuspendGuard &operator=(const SuspendGuard &) = delete;
    SuspendGuard(SuspendGuard &&other) noexcept
        : guard_(std::exchange(other.guard_, nullptr)),
          enforce_on_destruction_(other.enforce_on_destruction_) {}
    SuspendGuard &operator=(SuspendGuard &&) = delete;

    ~SuspendGuard() noexcept(noexcept(std::declval<guard_type &>().enforce_after_change())) {
        if (guard_ != nullptr) {
            const bool check_pending = guard_->resume_checks();
            if (check_pending && enforce_on_destruction_ &&
                std::uncaught_exceptions() == 0) {
                guard_->enforce_after_change();
            }
        }
    }

    SuspendGuard &nocommit() & noexcept {
        enforce_on_destruction_ = false;
        return *this;
    }

    [[nodiscard]] SuspendGuard &&nocommit() && noexcept {
        nocommit();
        return std::move(*this);
    }

private:
    guard_type *guard_;
    bool enforce_on_destruction_ = true;
};

template <EConfig M, typename U, typename... E>
    requires(suspendable_v<M>)
SuspendGuard(Guard<M, U, E...> &)
    -> SuspendGuard<Guard<M, U, E...>>;

#define GUARD_BINARY_OPERATOR(op)                                                 \
    template <typename Left, typename Right>                                      \
        requires((is_guard_v<Left> || is_guard_v<Right>) &&                       \
                 (requires(Left &&left, Right &&right) {                          \
                      unwrap_const(std::forward<Left>(left)) op                   \
                          unwrap_const(std::forward<Right>(right));               \
                  } ||                                                            \
                  requires(Left &&left, Right &&right) {                          \
                      unwrap_const(std::forward<Left>(left)) op                   \
                          unwrap(std::forward<Right>(right));                     \
                  } ||                                                            \
                  requires(Left &&left, Right &&right) {                          \
                      unwrap(std::forward<Left>(left)) op                         \
                          unwrap_const(std::forward<Right>(right));               \
                  } ||                                                            \
                  requires(Left &&left, Right &&right) {                          \
                      unwrap(std::forward<Left>(left)) op                         \
                          unwrap(std::forward<Right>(right));                     \
                  }))                                                             \
    constexpr decltype(auto) operator op(Left &&left, Right &&right) {            \
        if constexpr (requires {                                                  \
                          unwrap_const(std::forward<Left>(left)) op               \
                              unwrap_const(std::forward<Right>(right));           \
                      }) {                                                        \
            return unwrap_const(std::forward<Left>(left)) op                      \
                   unwrap_const(std::forward<Right>(right));                      \
        } else if constexpr (requires {                                           \
                                 unwrap_const(std::forward<Left>(left)) op        \
                                     unwrap(std::forward<Right>(right));          \
                             }) {                                                 \
            decltype(auto) result = unwrap_const(std::forward<Left>(left)) op     \
                                    unwrap(std::forward<Right>(right));           \
            enforce_binary_operand(std::forward<Right>(right));                   \
            return result;                                                        \
        } else if constexpr (requires {                                           \
                                 unwrap(std::forward<Left>(left)) op              \
                                     unwrap_const(std::forward<Right>(right));    \
                             }) {                                                 \
            decltype(auto) result = unwrap(std::forward<Left>(left)) op           \
                                    unwrap_const(std::forward<Right>(right));     \
            enforce_binary_operand(std::forward<Left>(left));                     \
            return result;                                                        \
        } else {                                                                  \
            decltype(auto) result = unwrap(std::forward<Left>(left)) op           \
                                    unwrap(std::forward<Right>(right));           \
            enforce_binary_operand(std::forward<Left>(left));                     \
            enforce_binary_operand(std::forward<Right>(right));                   \
            return result;                                                        \
        }                                                                         \
    }

GUARD_BINARY_OPERATOR(+)
GUARD_BINARY_OPERATOR(-)
GUARD_BINARY_OPERATOR(*)
GUARD_BINARY_OPERATOR(/)
GUARD_BINARY_OPERATOR(%)
GUARD_BINARY_OPERATOR(&)
GUARD_BINARY_OPERATOR(|)
GUARD_BINARY_OPERATOR(^)
GUARD_BINARY_OPERATOR(<<)
GUARD_BINARY_OPERATOR(>>)
GUARD_BINARY_OPERATOR(==)
GUARD_BINARY_OPERATOR(!=)
GUARD_BINARY_OPERATOR(<)
GUARD_BINARY_OPERATOR(<=)
GUARD_BINARY_OPERATOR(>)
GUARD_BINARY_OPERATOR(>=)

#undef GUARD_BINARY_OPERATOR

template <auto Fn, auto... Args>
class Apply {
private:
    template <typename T>
    static constexpr bool is_nothrow() {
        if constexpr (ReferenceInvocable<Fn, T, Args...>) {
            return noexcept(std::invoke(Fn, std::declval<T &>(), Args...));
        } else if constexpr (PointerInvocable<Fn, T, Args...>) {
            return noexcept(std::invoke(Fn, std::declval<T *>(), Args...));
        } else {
            return false;
        }
    }

public:
    template <typename T>
        requires(!std::is_pointer_v<std::remove_cvref_t<T>> &&
                 (PointerInvocable<Fn, T, Args...> ||
                  ReferenceInvocable<Fn, T, Args...>))
    void operator()(T *value) const noexcept(is_nothrow<T>()) {
        if constexpr (ReferenceInvocable<Fn, T, Args...>) {
            (void)std::invoke(Fn, *value, Args...);
        } else {
            (void)std::invoke(Fn, value, Args...);
        }
    }
};


template <auto Fn, auto... Args>
class Transform {
private:
    template <typename T>
    static constexpr bool is_nothrow() {
        if constexpr (TransformReferenceCompatible<Fn, T, Args...>) {
            return noexcept(std::invoke(Fn, std::declval<T &>(), Args...)) &&
                   std::is_nothrow_assignable_v<
                       T &, decltype(std::invoke(Fn, std::declval<T &>(),
                                                 Args...))>;
        } else if constexpr (TransformPointerCompatible<Fn, T, Args...>) {
            return noexcept(std::invoke(Fn, std::declval<T *>(), Args...)) &&
                   std::is_nothrow_assignable_v<
                       T &, decltype(std::invoke(Fn, std::declval<T *>(),
                                                 Args...))>;
        } else {
            return false;
        }
    }

public:
    template <typename T>
        requires(!std::is_pointer_v<std::remove_cvref_t<T>> &&
                 (TransformReferenceCompatible<Fn, T, Args...> ||
                  (!ReferenceInvocable<Fn, T, Args...> &&
                   TransformPointerCompatible<Fn, T, Args...>)))
    constexpr void operator()(T *value) const noexcept(is_nothrow<T>()) {
        if constexpr (TransformReferenceCompatible<Fn, T, Args...>) {
            *value = std::invoke(Fn, *value, Args...);
        } else {
            *value = std::invoke(Fn, value, Args...);
        }
    }
};

template <bool Expected, auto Res, auto Fn, auto... Args>
class Check {
public:
    template <typename T>
        requires(!std::is_pointer_v<std::remove_cvref_t<T>> &&
                 (CheckReferenceCompatible<Fn, T, Res, Args...> ||
                  (!ReferenceInvocable<Fn, T, Args...> &&
                   CheckPointerCompatible<Fn, T, Res, Args...>)))
    constexpr void operator()(T *value) const {
        if constexpr (CheckReferenceCompatible<Fn, T, Res, Args...>) {
            if (Expected != (std::invoke(Fn, *value, Args...) == Res)) {
                throw std::invalid_argument("Check failed: result does not match requirements");
            }
        } else {
            if (Expected != (std::invoke(Fn, value, Args...) == Res)) {
                throw std::invalid_argument("Check failed: result does not match requirements");
            }
        }
    }
};

} // namespace guard::impl

template <impl::TMode M, typename G>
[[nodiscard]] impl::TriggerSpec<M, G> bind_as(G &parent) noexcept {
    return impl::TriggerSpec<M, G>{parent};
}

template <
    impl::EConfig M = impl::EConfig{false, true, 0},
    typename T,
    typename... Parents
> requires (impl::BindingArgument<Parents&&> && ...)
[[nodiscard]] auto bind(T&& value, Parents&&... parents)
    noexcept(
        std::is_nothrow_constructible_v<
            impl::Guard<M, T, impl::BindingTrigger<Parents>...>,
            decltype(std::forward<T>(value)),
            impl::BindingTrigger<Parents>...
        >
    )
{
    return impl::Guard<M, T, impl::BindingTrigger<Parents>...>{
        std::forward<T>(value),
        impl::make_trigger(std::forward<Parents>(parents))...
    };
}

template<typename G>
[[nodiscard]] impl::DTriggerSpec<G> dbind_as(
    impl::TMode mode, G &parent) noexcept {
    return impl::DTriggerSpec<G>{mode, parent};
}

template <
    impl::EConfig M = impl::EConfig{false, true, 0},
    typename T,
    typename... Parents
> requires (impl::DBindingArgument<Parents&&> && ...)
[[nodiscard]] auto dbind(T&& value, Parents&&... parents)
    noexcept(
        std::is_nothrow_constructible_v<
            impl::Guard<M, T, impl::BindingDTrigger<Parents>...>,
            decltype(std::forward<T>(value)),
            impl::BindingDTrigger<Parents>...
        >
    )
{
    return impl::Guard<M, T, impl::BindingDTrigger<Parents>...>{
        std::forward<T>(value),
        impl::make_dtrigger(std::forward<Parents>(parents))...
    };
}

template <impl::EConfig M, typename U, typename... E>
[[nodiscard]] auto make_guard(U &&value, E &&...rules)
    noexcept(std::is_nothrow_constructible_v<
        impl::Guard<M, U, std::remove_cvref_t<E>...>,
        decltype(std::forward<U>(value)),
        decltype(std::forward<E>(rules))...>)
{
    return impl::Guard<M, U, std::remove_cvref_t<E>...>{
        std::forward<U>(value), std::forward<E>(rules)...
    };
}

struct EMode {
    template <bool EnforceOnConstruction,
              bool EnforceOnMutation,
              std::size_t SuspensionStateSize = 0>
        requires (SuspensionStateSize == 0 || (EnforceOnMutation &&
                  integral_util::has_unsigned_of_size_v<SuspensionStateSize>))
    static constexpr impl::EConfig mode{
        EnforceOnConstruction, EnforceOnMutation, SuspensionStateSize
    };

    constexpr static auto Passive = mode<false, false>;
    constexpr static auto Once    = mode<true, false>;
    constexpr static auto Always  = mode<true, true>;
    constexpr static auto Defer   = mode<false, true>;
    constexpr static auto SDefer  = mode<false, true, 4>;
    constexpr static auto SAlways = mode<true, true, 4>;
};

using impl::TMode;

using impl::Guard;

template <typename U, typename... E>
using Passive = impl::Guard<EMode::Passive, U, E...>;

template <typename U, typename... E>
using Once = impl::Guard<EMode::Once, U, E...>;

template <typename U, typename... E>
using Always = impl::Guard<EMode::Always, U, E...>;

template <typename U, typename... E>
using Defer = impl::Guard<EMode::Defer, U, E...>;

template <typename U, typename... E>
using SDefer = impl::Guard<EMode::SDefer, U, E...>;

template <typename U, typename... E>
using SAlways = impl::Guard<EMode::SAlways, U, E...>;

template <auto Fn, auto... Args>
using Apply = impl::Apply<Fn, Args...>;

template <auto Fn, auto... Args>
using Transform = impl::Transform<Fn, Args...>;

template <auto Fn, auto... Args>
using Allow = impl::Check<true, true, Fn, Args...>;

template <auto Fn, auto... Args>
using Deny = impl::Check<false, true, Fn, Args...>;

template <auto Res, auto Fn, auto... Args>
using Require = impl::Check<true, Res, Fn, Args...>;

template <auto Res, auto Fn, auto... Args>
using Forbid = impl::Check<false, Res, Fn, Args...>;

using impl::Trigger;

using impl::DTrigger;

using impl::make_trigger;

using impl::make_dtrigger;

} // namespace guard

#endif // GUARD_H