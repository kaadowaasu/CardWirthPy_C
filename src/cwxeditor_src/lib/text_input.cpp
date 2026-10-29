#include "text_input.hpp"

#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

namespace cwxeditor::lib {
namespace {

[[nodiscard]] TextSelection clamp_selection(TextSelection value,
                                             std::size_t text_size) noexcept {
    value.start = std::min(value.start, text_size);
    value.length = std::min(value.length, text_size - value.start);
    return value;
}

[[nodiscard]] TextComposition clamp_composition(TextComposition value) {
    value.selection = clamp_selection(value.selection, value.text.size());
    value.cursor = std::min(value.cursor, value.text.size());
    return value;
}

[[nodiscard]] TextInputCapabilities backend_capabilities(
    const std::shared_ptr<TextInputBackend>& backend) noexcept {
    return backend ? backend->capabilities() : TextInputCapabilities::headless();
}

}  // namespace

struct TextInputService::Impl {
    struct CallbackRecord {
        TextInputCallbackId id{};
        TextInputCallbacks callbacks;
        std::shared_ptr<std::atomic_bool> connected;
    };

    mutable std::mutex mutex;
    std::shared_ptr<TextInputBackend> backend;
    TextInputState state;
    TextInputCallbacks default_callbacks;
    std::vector<CallbackRecord> callbacks;
    TextInputCallbackId next_callback_id{1U};

    [[nodiscard]] std::vector<TextInputCallbacks> callback_snapshot() const {
        std::vector<TextInputCallbacks> snapshot;
        std::scoped_lock lock(mutex);
        snapshot.reserve(callbacks.size() + 1U);
        snapshot.push_back(default_callbacks);
        for (const auto& record : callbacks) {
            if (record.connected && record.connected->load(std::memory_order_acquire))
                snapshot.push_back(record.callbacks);
        }
        return snapshot;
    }

    template <typename Invoker>
    void invoke(Invoker&& invoker) const {
        const auto snapshot = callback_snapshot();
        for (const auto& handlers : snapshot) {
            try {
                invoker(handlers);
            } catch (...) {
                // エディタのリスナーの例外で、プラットフォームのイベントループから
                // 抜けない。 失敗したリスナーは、他のリスナーから分離して扱う。
            }
        }
    }

    void notify_state_changed() const {
        TextInputState copy;
        {
            std::scoped_lock lock(mutex);
            copy = state;
        }
        invoke([&copy](const TextInputCallbacks& callbacks) {
            if (callbacks.on_state_changed) callbacks.on_state_changed(copy);
        });
    }

    void notify_composition_start() const {
        invoke([](const TextInputCallbacks& callbacks) {
            if (callbacks.on_composition_start) callbacks.on_composition_start();
        });
    }

    void notify_composition_update(const TextComposition& composition) const {
        const TextComposition copy = composition;
        invoke([&copy](const TextInputCallbacks& callbacks) {
            if (callbacks.on_composition_update) callbacks.on_composition_update(copy);
            if (callbacks.on_text_input) {
                TextInputEvent event{copy.text, true, false};
                callbacks.on_text_input(event);
            }
        });
    }

    void notify_commit(std::string text) const {
        // 全リスナー用に一つの不変の複製を保持する。 コールバックに渡す
        // std::string_view は、各呼び出し中は有効。
        invoke([&text](const TextInputCallbacks& callbacks) {
            if (callbacks.on_commit) callbacks.on_commit(text);
            if (callbacks.on_text_input) {
                TextInputEvent event{text, false, false};
                callbacks.on_text_input(event);
            }
        });
    }

    void notify_composition_cancel() const {
        invoke([](const TextInputCallbacks& callbacks) {
            if (callbacks.on_composition_cancel) callbacks.on_composition_cancel();
        });
    }

    void notify_selection_changed(TextSelection selection) const {
        invoke([selection](const TextInputCallbacks& callbacks) {
            if (callbacks.on_selection_changed) callbacks.on_selection_changed(selection);
        });
    }

    void notify_soft_keyboard(bool visible) const {
        invoke([visible](const TextInputCallbacks& callbacks) {
            if (callbacks.on_soft_keyboard) callbacks.on_soft_keyboard(visible);
        });
    }

    void handle_event(const TextInputEventPacket& event) {
        switch (event.kind) {
        case TextInputEventKind::CompositionStart:
            composition_start();
            break;
        case TextInputEventKind::CompositionUpdate:
            composition_update(TextComposition{event.text, event.selection, event.cursor});
            break;
        case TextInputEventKind::Commit:
            commit(event.text);
            break;
        case TextInputEventKind::CompositionCancel:
            composition_cancel();
            break;
        case TextInputEventKind::SelectionChanged:
            selection_changed(event.selection);
            break;
        case TextInputEventKind::SoftKeyboardChanged:
            soft_keyboard_changed(event.visible);
            break;
        }
    }

    void composition_start() {
        {
            std::scoped_lock lock(mutex);
            state.composition = TextComposition{};
        }
        notify_composition_start();
        notify_state_changed();
    }

    void composition_update(TextComposition composition) {
        composition = clamp_composition(std::move(composition));
        {
            std::scoped_lock lock(mutex);
            state.composition = composition;
        }
        notify_composition_update(composition);
        notify_state_changed();
    }

    void commit(std::string text) {
        {
            std::scoped_lock lock(mutex);
            state.composition.reset();
        }
        notify_commit(std::move(text));
        notify_state_changed();
    }

    void composition_cancel() {
        bool had_composition{};
        {
            std::scoped_lock lock(mutex);
            had_composition = state.composition.has_value();
            state.composition.reset();
        }
        // ネイティブIMEは、空の開始の後にもキャンセルを送る場合がある。 どちら
        // でもイベントを通知し、重複しても安全に寿命の推移を観測できるようにする。
        (void)had_composition;
        notify_composition_cancel();
        notify_state_changed();
    }

    void selection_changed(TextSelection selection) {
        {
            std::scoped_lock lock(mutex);
            selection = clamp_selection(selection, state.text.size());
            if (state.selection == selection) return;
            state.selection = selection;
        }
        notify_selection_changed(selection);
        notify_state_changed();
    }

    void soft_keyboard_changed(bool visible) {
        {
            std::scoped_lock lock(mutex);
            if (state.soft_keyboard_visible == visible) return;
            state.soft_keyboard_visible = visible;
        }
        notify_soft_keyboard(visible);
        notify_state_changed();
    }
};

TextInputSubscription::TextInputSubscription(
    std::function<void()> disconnect,
    TextInputCallbackId id,
    std::shared_ptr<std::atomic_bool> connected)
    : disconnect_(std::move(disconnect)), id_(id), connected_(std::move(connected)) {}

TextInputSubscription::~TextInputSubscription() { disconnect(); }

TextInputSubscription::TextInputSubscription(TextInputSubscription&& other) noexcept
    : disconnect_(std::move(other.disconnect_)), id_(other.id_),
      connected_(std::move(other.connected_)) {
    other.id_ = 0U;
}

TextInputSubscription& TextInputSubscription::operator=(TextInputSubscription&& other) noexcept {
    if (this == &other) return *this;
    disconnect();
    disconnect_ = std::move(other.disconnect_);
    id_ = other.id_;
    connected_ = std::move(other.connected_);
    other.id_ = 0U;
    return *this;
}

void TextInputSubscription::disconnect() noexcept {
    if (!disconnect_ || !connected_) return;
    bool expected = true;
    if (connected_->compare_exchange_strong(expected, false,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
        try {
            disconnect_();
        } catch (...) {
        }
    }
    disconnect_ = {};
    id_ = 0U;
}

bool TextInputSubscription::connected() const noexcept {
    return connected_ && connected_->load(std::memory_order_acquire);
}

TextInputService::TextInputService(std::shared_ptr<TextInputBackend> backend)
    : impl_(std::make_shared<Impl>()) {
    set_backend(std::move(backend));
}

TextInputService::~TextInputService() {
    if (!impl_) return;
    std::shared_ptr<TextInputBackend> backend;
    {
        std::scoped_lock lock(impl_->mutex);
        backend = std::move(impl_->backend);
        impl_->state.active = false;
    }
    if (backend) {
        try {
            backend->set_event_sink({});
            backend->end();
        } catch (...) {
        }
    }
}

TextInputService::TextInputService(TextInputService&& other) noexcept
    : impl_(std::move(other.impl_)) {}

TextInputService& TextInputService::operator=(TextInputService&& other) noexcept {
    if (this == &other) return *this;

    if (impl_) {
        std::shared_ptr<TextInputBackend> backend;
        {
            std::scoped_lock lock(impl_->mutex);
            backend = std::move(impl_->backend);
            impl_->state.active = false;
        }
        if (backend) {
            try {
                backend->set_event_sink({});
                backend->end();
            } catch (...) {
            }
        }
    }
    impl_ = std::move(other.impl_);
    return *this;
}

void TextInputService::set_backend(std::shared_ptr<TextInputBackend> backend) {
    if (!impl_) impl_ = std::make_shared<Impl>();

    std::shared_ptr<TextInputBackend> old;
    bool was_active{};
    TextInputRequest request;
    TextInputState current;
    {
        std::scoped_lock lock(impl_->mutex);
        old = std::move(impl_->backend);
        impl_->backend = std::move(backend);
        was_active = impl_->state.active;
        current = impl_->state;
        request.text = current.text;
        request.selection = current.selection;
    }

    if (old) {
        try {
            old->set_event_sink({});
            if (was_active) old->end();
        } catch (...) {
        }
    }

    std::shared_ptr<TextInputBackend> next;
    {
        std::scoped_lock lock(impl_->mutex);
        next = impl_->backend;
    }
    if (!next) return;

    next->set_event_sink(event_sink());
    if (!was_active) return;

    std::string ignored;
    if (!next->begin(request, ignored)) {
        std::scoped_lock lock(impl_->mutex);
        impl_->state.active = false;
        return;
    }
    if (current.text.empty() && current.selection.empty()) return;
    (void)next->set_surrounding_text(current.text, current.selection, ignored);
}

std::shared_ptr<TextInputBackend> TextInputService::backend() const {
    if (!impl_) return {};
    std::scoped_lock lock(impl_->mutex);
    return impl_->backend;
}

TextInputCapabilities TextInputService::capabilities() const noexcept {
    if (!impl_) return TextInputCapabilities::headless();
    std::shared_ptr<TextInputBackend> current;
    {
        std::scoped_lock lock(impl_->mutex);
        current = impl_->backend;
    }
    return backend_capabilities(current);
}

bool TextInputService::supports(TextInputCapability value) const noexcept {
    return capabilities().supports(value);
}

bool TextInputService::begin(TextInputRequest request, std::string& error) {
    if (!impl_) impl_ = std::make_shared<Impl>();
    request.selection = clamp_selection(request.selection, request.text.size());

    std::shared_ptr<TextInputBackend> current;
    {
        std::scoped_lock lock(impl_->mutex);
        current = impl_->backend;
    }

    bool accepted = true;
    if (current) accepted = current->begin(request, error);
    else error.clear();
    if (!accepted) return false;

    {
        std::scoped_lock lock(impl_->mutex);
        impl_->state.text = std::move(request.text);
        impl_->state.selection = request.selection;
        impl_->state.composition.reset();
        impl_->state.active = true;
        impl_->state.soft_keyboard_visible = false;
    }
    impl_->notify_state_changed();
    return true;
}

bool TextInputService::begin(TextInputRequest request) {
    std::string ignored;
    return begin(std::move(request), ignored);
}

void TextInputService::end() noexcept {
    if (!impl_) return;
    std::shared_ptr<TextInputBackend> current;
    {
        std::scoped_lock lock(impl_->mutex);
        current = impl_->backend;
        impl_->state.active = false;
        impl_->state.composition.reset();
        impl_->state.soft_keyboard_visible = false;
    }
    if (current) {
        try {
            current->end();
        } catch (...) {
        }
    }
    impl_->notify_state_changed();
}

bool TextInputService::active() const noexcept {
    if (!impl_) return false;
    std::scoped_lock lock(impl_->mutex);
    return impl_->state.active;
}

bool TextInputService::set_surrounding_text(std::string text,
                                            TextSelection selection,
                                            std::string& error) {
    if (!impl_) impl_ = std::make_shared<Impl>();
    selection = clamp_selection(selection, text.size());

    std::shared_ptr<TextInputBackend> current;
    {
        std::scoped_lock lock(impl_->mutex);
        current = impl_->backend;
    }
    bool accepted = true;
    if (current) accepted = current->set_surrounding_text(text, selection, error);
    else error.clear();

    {
        std::scoped_lock lock(impl_->mutex);
        impl_->state.text = std::move(text);
        impl_->state.selection = selection;
    }
    impl_->notify_selection_changed(selection);
    impl_->notify_state_changed();
    return accepted;
}

void TextInputService::set_surrounding_text(std::string text, TextSelection selection) {
    std::string ignored;
    (void)set_surrounding_text(std::move(text), selection, ignored);
}

void TextInputService::set_text(std::string text, TextSelection selection) {
    set_surrounding_text(std::move(text), selection);
}

TextInputState TextInputService::state() const {
    if (!impl_) return {};
    std::scoped_lock lock(impl_->mutex);
    return impl_->state;
}

void TextInputService::composition_start() {
    if (!impl_) return;
    impl_->composition_start();
}

void TextInputService::composition_update(TextComposition composition) {
    if (!impl_) return;
    impl_->composition_update(std::move(composition));
}

void TextInputService::commit(std::string text) {
    if (!impl_) return;
    impl_->commit(std::move(text));
}

void TextInputService::composition_cancel() {
    if (!impl_) return;
    impl_->composition_cancel();
}

void TextInputService::selection_changed(TextSelection selection) {
    if (!impl_) return;
    impl_->selection_changed(selection);
}

void TextInputService::soft_keyboard_changed(bool visible) {
    if (!impl_) return;
    impl_->soft_keyboard_changed(visible);
}

void TextInputService::handle_event(const TextInputEventPacket& event) {
    if (!impl_) return;
    impl_->handle_event(event);
}

void TextInputService::handle_text_input(const TextInputEvent& event) {
    if (event.composing) {
        composition_update(TextComposition{event.text, {}, 0U});
    } else {
        commit(event.text);
    }
}

TextInputEventSink TextInputService::event_sink() const {
    if (!impl_) return {};
    const std::weak_ptr<Impl> weak = impl_;
    return [weak](const TextInputEventPacket& event) {
        if (const auto impl = weak.lock()) impl->handle_event(event);
    };
}

bool TextInputService::set_selection(TextSelection selection, std::string& error) {
    if (!impl_) impl_ = std::make_shared<Impl>();
    std::shared_ptr<TextInputBackend> current;
    std::size_t text_size{};
    {
        std::scoped_lock lock(impl_->mutex);
        current = impl_->backend;
        text_size = impl_->state.text.size();
    }
    selection = clamp_selection(selection, text_size);

    bool accepted = true;
    if (current) accepted = current->set_selection(selection, error);
    else error.clear();
    impl_->selection_changed(selection);
    return accepted;
}

void TextInputService::set_selection(TextSelection selection) {
    std::string ignored;
    (void)set_selection(selection, ignored);
}

bool TextInputService::request_soft_keyboard(bool visible, std::string& error) {
    if (!impl_) impl_ = std::make_shared<Impl>();
    std::shared_ptr<TextInputBackend> current;
    {
        std::scoped_lock lock(impl_->mutex);
        current = impl_->backend;
    }

    bool accepted = true;
    if (current) accepted = current->request_soft_keyboard(visible, error);
    else error.clear();
    // デスクトップのバックエンドがキーボードを表示できなく
    // ても、要求された状態は保持する。 利用側は戻り値で対
    // 応可否を確認しつつ、自前の入力表示を一貫して描ける。
    impl_->soft_keyboard_changed(visible);
    return accepted;
}

bool TextInputService::request_soft_keyboard(bool visible) {
    std::string ignored;
    return request_soft_keyboard(visible, ignored);
}

bool TextInputService::show_soft_keyboard(std::string& error) {
    return request_soft_keyboard(true, error);
}

bool TextInputService::hide_soft_keyboard(std::string& error) {
    return request_soft_keyboard(false, error);
}

bool TextInputService::show_soft_keyboard() { return request_soft_keyboard(true); }

bool TextInputService::hide_soft_keyboard() { return request_soft_keyboard(false); }

TextInputSubscription TextInputService::subscribe(TextInputCallbacks callbacks) {
    if (!impl_) impl_ = std::make_shared<Impl>();
    auto connected = std::make_shared<std::atomic_bool>(true);
    TextInputCallbackId id{};
    {
        std::scoped_lock lock(impl_->mutex);
        id = impl_->next_callback_id++;
        if (id == 0U) id = impl_->next_callback_id++;
        impl_->callbacks.push_back(Impl::CallbackRecord{id, std::move(callbacks), connected});
    }

    const std::weak_ptr<Impl> weak = impl_;
    auto disconnect = [weak, id] {
        if (const auto impl = weak.lock()) {
            std::scoped_lock lock(impl->mutex);
            std::erase_if(impl->callbacks, [id](const Impl::CallbackRecord& record) {
                return record.id == id;
            });
        }
    };
    return TextInputSubscription{std::move(disconnect), id, std::move(connected)};
}

void TextInputService::unsubscribe(TextInputCallbackId id) noexcept {
    if (!impl_ || id == 0U) return;
    std::scoped_lock lock(impl_->mutex);
    std::erase_if(impl_->callbacks, [id](const Impl::CallbackRecord& record) {
        if (record.id != id) return false;
        if (record.connected) record.connected->store(false, std::memory_order_release);
        return true;
    });
}

void TextInputService::set_callbacks(TextInputCallbacks callbacks) {
    if (!impl_) impl_ = std::make_shared<Impl>();
    std::scoped_lock lock(impl_->mutex);
    impl_->default_callbacks = std::move(callbacks);
}

void TextInputService::clear_callbacks() noexcept {
    if (!impl_) return;
    std::scoped_lock lock(impl_->mutex);
    impl_->default_callbacks = {};
}

}  // namespace cwxeditor::lib
