// Created by moisrex on 8/18/26.

#include "./common/tests_common_pch.hpp"

import dynamic_scoping;

namespace {

    /// Plain value type bound directly by a non-polymorphic scope.
    struct simple_value {
        int x = 0;
    };

    /// A dynamically-scoped type binding a `simple_value` directly (no model).
    constexpr struct [[nodiscard]] simple_scoped : fs8::global_binding<simple_value> {
        static constexpr fs8::global_binding<simple_value> self{};

        [[nodiscard]] int x() const noexcept {
            return self->x;
        }
    } simple_context;

    static_assert(fs8::dynamically_scoped<simple_scoped>);
    static_assert(!fs8::polymorphic_scoped<simple_scoped>);

    /// Polymorphic interface for the polymorphic-scope test.
    struct test_interface {
        test_interface() noexcept                                 = default;
        test_interface(test_interface const&) noexcept            = default;
        test_interface& operator=(test_interface const&) noexcept = default;
        test_interface(test_interface&&) noexcept                 = default;
        test_interface& operator=(test_interface&&) noexcept      = default;
        virtual ~test_interface() noexcept                        = default;

        [[nodiscard]] virtual int value() const noexcept = 0;
    };

    /// Model bridging any concrete type into `test_interface`.
    template <typename ConcreteT>
    struct test_interface_model final : test_interface {
        ConcreteT* obj;

        explicit test_interface_model(ConcreteT* inp_obj) noexcept : obj{inp_obj} {}

        [[nodiscard]] int value() const noexcept override {
            return obj->value();
        }
    };

    struct test_concrete {
        int val = 0;

        explicit test_concrete(int const v) noexcept : val{v} {}

        [[nodiscard]] int value() const noexcept {
            return val;
        }
    };

    /// A polymorphic dynamically-scoped type (has a model_type).
    constexpr struct [[nodiscard]] test_scoped : fs8::global_binding<test_interface> {
        template <typename ConcreteT>
        using model_type = test_interface_model<ConcreteT>;

        static constexpr fs8::global_binding<test_interface> self{};

        [[nodiscard]] int value() const noexcept {
            return self->value();
        }
    } test_context;

    static_assert(fs8::polymorphic_scoped<test_scoped>);

} // namespace

TEST(DynamicScopingTest, ExchangeReturnsPrevious) {
    using binding = fs8::global_binding<int, 1>;

    EXPECT_EQ(binding::exchange(nullptr), nullptr);
    int a = 1;
    EXPECT_EQ(binding::exchange(&a), nullptr);
    int b = 2;
    EXPECT_EQ(binding::exchange(&b), &a);
    EXPECT_EQ(binding::instance(), &b);
    EXPECT_EQ(binding::exchange(&a), &b);
    EXPECT_EQ(binding::exchange(nullptr), &a);
}

TEST(DynamicScopingTest, PrimaryScopeBindsAndRestores) {
    simple_value v{42};
    {
        fs8::dynamic_scope scope{simple_context, v};
        EXPECT_EQ(simple_context.x(), 42);

        // Mutations through the bound object are visible through the scope.
        v.x = 7;
        EXPECT_EQ(simple_context.x(), 7);
    }
    // After the scope ends the binding is restored to its previous value (nullptr).
    EXPECT_EQ(fs8::global_binding<simple_value>::instance(), nullptr);
}

TEST(DynamicScopingTest, NestedScopesAreLifo) {
    simple_value v1{1};
    simple_value v2{2};
    {
        fs8::dynamic_scope scope1{simple_context, v1};
        EXPECT_EQ(simple_context.x(), 1);
        {
            fs8::dynamic_scope scope2{simple_context, v2};
            EXPECT_EQ(simple_context.x(), 2);
        }
        EXPECT_EQ(simple_context.x(), 1);
    }
    EXPECT_EQ(fs8::global_binding<simple_value>::instance(), nullptr);
}

TEST(DynamicScopingTest, PolymorphicScopeDispatchesThroughModel) {
    test_concrete c1{10};
    test_concrete c2{20};
    {
        fs8::dynamic_scope scope{test_context, c1};
        EXPECT_EQ(test_context.value(), 10);

        c1.val = 11;
        EXPECT_EQ(test_context.value(), 11);

        {
            fs8::dynamic_scope inner{test_context, c2};
            EXPECT_EQ(test_context.value(), 20);
        }
        EXPECT_EQ(test_context.value(), 11);
    }
    EXPECT_EQ(fs8::global_binding<test_interface>::instance(), nullptr);
}
