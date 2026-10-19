// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/script_engine.hpp"

#include "logging/log.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <quickjs/quickjs.h>
#include <vector>

namespace rct3::scripting
{

    namespace
    {
        // Limits: memory for a script's values, and how long one tick may run before it is stopped.
        constexpr std::size_t kMemoryLimit = 64u << 20;
        constexpr std::chrono::milliseconds kTickLimit { 2000 };

        std::string to_text(JSContext* ctx, JSValueConst value)
        {
            const char* text = JS_ToCString(ctx, value);
            std::string out = text != nullptr ? text : "";
            JS_FreeCString(ctx, text);
            return out;
        }

        /// The value's JSON; null for undefined, functions and the like.
        Json to_json(JSContext* ctx, JSValueConst value)
        {
            const JSValue text = JS_JSONStringify(ctx, value, JS_UNDEFINED, JS_UNDEFINED);
            if (JS_IsException(text))
            {
                JS_FreeValue(ctx, JS_GetException(ctx));
                return nullptr;
            }
            Json out = JS_IsUndefined(text) ? Json(nullptr) : Json::parse(to_text(ctx, text), nullptr, false);
            JS_FreeValue(ctx, text);
            return out.is_discarded() ? Json(nullptr) : out;
        }

        JSValue from_json(JSContext* ctx, const Json& json)
        {
            const std::string text = json.dump();
            return JS_ParseJSON(ctx, text.c_str(), text.size(), "<native>");
        }

        /// The pending exception as text, with its stack where it has one.
        std::string exception_text(JSContext* ctx)
        {
            const JSValue exception = JS_GetException(ctx);
            std::string out = to_text(ctx, exception);
            if (JS_IsError(exception))
            {
                const JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
                if (!JS_IsUndefined(stack))
                    out += "\n" + to_text(ctx, stack);
                JS_FreeValue(ctx, stack);
            }
            JS_FreeValue(ctx, exception);
            return out;
        }
    } // namespace

    struct ScriptEngine::Impl
    {
        struct Wait
        {
            int frames = 0;
            JSValue resolve = JS_UNDEFINED;
            JSValue reject = JS_UNDEFINED;
        };

        JSRuntime* runtime = nullptr;
        JSContext* context = nullptr;
        std::vector<Native> natives;
        std::vector<Wait> waits;
        JSValue main = JS_UNDEFINED; ///< The module's promise
        ScriptState state = ScriptState::Finished;
        std::string error;
        std::chrono::steady_clock::time_point deadline;

        static Impl& of(JSContext* ctx)
        {
            return *static_cast<Impl*>(JS_GetContextOpaque(ctx));
        }

        static JSValue call_native(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int, JSValueConst* data)
        {
            Impl& self = of(ctx);
            std::int32_t index = 0;
            JS_ToInt32(ctx, &index, data[0]);
            Json arguments = Json::array();
            for (int i = 0; i < argc; ++i)
                arguments.push_back(to_json(ctx, argv[i]));
            try
            {
                const Json result = self.natives.at(static_cast<std::size_t>(index))(arguments);
                return result.is_null() ? JS_UNDEFINED : from_json(ctx, result);
            }
            catch (const std::exception& e)
            {
                return JS_ThrowInternalError(ctx, "%s", e.what());
            }
        }

        static JSValue frames(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
        {
            double count = 1;
            if (argc > 0 && JS_ToFloat64(ctx, &count, argv[0]) != 0)
                return JS_EXCEPTION;
            std::array<JSValue, 2> resolving { };
            const JSValue promise = JS_NewPromiseCapability(ctx, resolving.data());
            if (JS_IsException(promise))
                return promise;
            of(ctx).waits.push_back({ std::max(0, static_cast<int>(std::lround(count))), resolving[0], resolving[1] });
            return promise;
        }

        static JSValue log(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
        {
            std::string line;
            for (int i = 0; i < argc; ++i)
                line += (i > 0 ? " " : "") + to_text(ctx, argv[i]);
            logging::logger("script").info("{}", line);
            return JS_UNDEFINED;
        }

        static int interrupt(JSRuntime*, void* opaque)
        {
            return std::chrono::steady_clock::now() > static_cast<Impl*>(opaque)->deadline ? 1 : 0;
        }

        void fail(std::string text)
        {
            if (state != ScriptState::Failed)
                error = std::move(text);
            state = ScriptState::Failed;
        }

        /// Runs what the script has to do now, then looks at whether it is done.
        void settle()
        {
            for (;;)
            {
                JSContext* job_context = nullptr;
                const int ran = JS_ExecutePendingJob(runtime, &job_context);
                if (ran == 0)
                    break;
                if (ran < 0)
                {
                    fail(exception_text(job_context));
                    return;
                }
            }
            if (state != ScriptState::Running)
                return;
            switch (JS_PromiseState(context, main))
            {
                case JS_PROMISE_FULFILLED:
                case JS_PROMISE_NOT_A_PROMISE:
                    state = ScriptState::Finished;
                    break;
                case JS_PROMISE_REJECTED:
                {
                    JSValue reason = JS_PromiseResult(context, main);
                    JS_Throw(context, reason);
                    fail(exception_text(context));
                    break;
                }
                case JS_PROMISE_PENDING:
                    break;
            }
        }

        void set(std::string_view name, JSValue value)
        {
            JSValue owner = JS_GetGlobalObject(context);
            for (std::size_t dot = name.find('.'); dot != std::string_view::npos; dot = name.find('.'))
            {
                const std::string part(name.substr(0, dot));
                JSValue next = JS_GetPropertyStr(context, owner, part.c_str());
                if (JS_IsUndefined(next))
                {
                    next = JS_NewObject(context);
                    JS_SetPropertyStr(context, owner, part.c_str(), JS_DupValue(context, next));
                }
                JS_FreeValue(context, owner);
                owner = next;
                name.remove_prefix(dot + 1);
            }
            JS_SetPropertyStr(context, owner, std::string(name).c_str(), value);
            JS_FreeValue(context, owner);
        }
    };

    ScriptEngine::ScriptEngine()
        : impl_(std::make_unique<Impl>())
    {
        impl_->runtime = JS_NewRuntime();
        JS_SetMemoryLimit(impl_->runtime, kMemoryLimit);
        JS_SetInterruptHandler(impl_->runtime, &Impl::interrupt, impl_.get());
        impl_->context = JS_NewContext(impl_->runtime);
        JS_SetContextOpaque(impl_->context, impl_.get());
        impl_->set("frames", JS_NewCFunction(impl_->context, &Impl::frames, "frames", 1));
        impl_->set("log", JS_NewCFunction(impl_->context, &Impl::log, "log", 1));
    }

    ScriptEngine::~ScriptEngine()
    {
        for (const Impl::Wait& w : impl_->waits)
            JS_FreeValues(impl_->context, w.resolve, w.reject);
        JS_FreeValue(impl_->context, impl_->main);
        JS_FreeContext(impl_->context);
        JS_FreeRuntime(impl_->runtime);
    }

    void ScriptEngine::define(std::string_view name, Native fn)
    {
        JSValue index = JS_NewInt32(impl_->context, static_cast<std::int32_t>(impl_->natives.size()));
        impl_->natives.push_back(std::move(fn));
        const std::string last(name.substr(name.rfind('.') == std::string_view::npos ? 0 : name.rfind('.') + 1));
        impl_->set(name, JS_NewCFunctionData2(impl_->context, &Impl::call_native, last.c_str(), 0, 0, 1, &index));
    }

    bool ScriptEngine::evaluate(std::string_view source, std::string_view file)
    {
        Impl& self = *impl_;
        self.deadline = std::chrono::steady_clock::now() + kTickLimit;
        const std::string text(source);
        const std::string name(file);
        const JSValue result = JS_Eval(self.context, text.c_str(), text.size(), name.c_str(), JS_EVAL_TYPE_GLOBAL);
        const bool ok = !JS_IsException(result);
        if (!ok)
            self.fail(exception_text(self.context));
        JS_FreeValue(self.context, result);
        return ok;
    }

    void ScriptEngine::run(std::string_view source, std::string_view file)
    {
        Impl& self = *impl_;
        self.state = ScriptState::Running;
        self.deadline = std::chrono::steady_clock::now() + kTickLimit;
        // Evaluated from a terminated copy, as the engine reads one past the end.
        const std::string text(source);
        const std::string name(file);
        self.main = JS_Eval(self.context, text.c_str(), text.size(), name.c_str(), JS_EVAL_TYPE_MODULE);
        if (JS_IsException(self.main))
        {
            self.fail(exception_text(self.context));
            return;
        }
        self.settle();
    }

    ScriptState ScriptEngine::tick()
    {
        Impl& self = *impl_;
        if (self.state != ScriptState::Running)
            return self.state;
        self.deadline = std::chrono::steady_clock::now() + kTickLimit;
        std::vector<Impl::Wait> due;
        std::erase_if(self.waits, [&](Impl::Wait& w) {
            if (--w.frames > 0)
                return false;
            due.push_back(w);
            return true;
        });
        for (const Impl::Wait& w : due)
        {
            const JSValue result = JS_Call(self.context, w.resolve, JS_UNDEFINED, 0, nullptr);
            if (JS_IsException(result))
                self.fail(exception_text(self.context));
            JS_FreeValues(self.context, result, w.resolve, w.reject);
        }
        self.settle();
        return self.state;
    }

    ScriptState ScriptEngine::state() const
    {
        return impl_->state;
    }

    const std::string& ScriptEngine::error() const
    {
        return impl_->error;
    }

} // namespace rct3::scripting
