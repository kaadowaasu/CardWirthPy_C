#include "cw/lib/exception_origin.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <new>

#if defined(__EMSCRIPTEN__)
// Emscripten の libc・サニタイザーのスタック取得処理。emscripten_internal.h に対応する。
extern "C" std::uintptr_t emscripten_stack_snapshot();
extern "C" std::uint32_t emscripten_stack_unwind_buffer(std::uintptr_t,std::uintptr_t*,std::uint32_t);
extern "C" const char* emscripten_pc_get_function(std::uintptr_t);
#elif defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#include <unwind.h>
#include <cxxabi.h>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#endif

namespace {
using String=std::string;

template<class T> struct RestoreOperationValue {
    T& value;
    T previous;
    RestoreOperationValue(T& target,T replacement):value(target),previous(target){value=replacement;}
    ~RestoreOperationValue(){value=previous;}
};
struct ExceptionOriginTrace {
    std::uintptr_t object{};
    std::size_t count{};
    void* frames[48]{};
    std::shared_ptr<const String> symbols;
};
class ExceptionOriginRegistry {
    // exception_ptr は元の catch より長く残り、特に FileSync
    // ではワーカースレッドをまたぐ場合がある。 発生元はスレッドローカルの
    // catch の連鎖ではなく、例外の同一性で保持する。 診断情報は直近
    // 256 個の送出オブジェクトに制限する。除外や確保なしの取得で古い発生
    // 元が不明になっても、 後のログ出力・再送出時のスタックで置き換えない。
    std::array<ExceptionOriginTrace,256> entries_{};
    std::size_t next_{};
    std::atomic_flag locked_=ATOMIC_FLAG_INIT;
    struct Guard {
        std::atomic_flag& flag;
        explicit Guard(std::atomic_flag& value) noexcept:flag(value) {
            while(flag.test_and_set(std::memory_order_acquire)) {}
        }
        ~Guard(){flag.clear(std::memory_order_release);}
    };
public:
    void remember(std::uintptr_t object,void* const* frames,std::size_t count,
                  std::shared_ptr<const String> symbols={}) noexcept {
        const Guard guard(locked_);
        auto found=std::find_if(entries_.begin(),entries_.end(),
            [object](const auto& entry){return entry.object==object;});
        if(found==entries_.end()) {
            found=entries_.begin()+next_;
            next_=(next_+1)%entries_.size();
        }
        found->object=object;
        found->count=std::min(count,std::size(found->frames));
        std::copy_n(frames,found->count,found->frames);
        found->symbols=std::move(symbols);
    }
    ExceptionOriginTrace find(std::uintptr_t object) noexcept {
        if(!object)return {};
        const Guard guard(locked_);
        const auto found=std::find_if(entries_.begin(),entries_.end(),
            [object](const auto& entry){return entry.object==object;});
        return found==entries_.end()?ExceptionOriginTrace{}:*found;
    }
    void forget(std::uintptr_t object) noexcept {
        const Guard guard(locked_);
        for(auto& entry:entries_)if(entry.object==object)entry={};
    }
};
ExceptionOriginRegistry& exception_origins() noexcept
{
    // 例外送出の監視は、他のグローバル変数の構築前や破棄後にも動
    // く場合がある。 この上限付き診断キャッシュは初回使用時に確
    // 保なしで静的領域へ構築し、プロセスの生存期間中保持する。
    alignas(ExceptionOriginRegistry) static std::byte storage[sizeof(ExceptionOriginRegistry)]{};
    static auto* registry=::new(static_cast<void*>(storage)) ExceptionOriginRegistry;
    return *registry;
}
thread_local ExceptionOriginTrace transferred_exception_origin;

// C++ に標準の送出元フックはないため、ABI 固有の監視を下
// の診断境界に限定する。 例外の処理自体は必ずそのまま続行する。
#if defined(_WIN32) && defined(_MSC_VER)
extern "C" void** __cdecl __current_exception();
extern "C" __declspec(noreturn) void __stdcall _CxxThrowException(void*,_ThrowInfo*) noexcept(false);
thread_local bool capturing_exception_origin=false;
PEXCEPTION_RECORD current_native_exception_record() noexcept
{
    return static_cast<PEXCEPTION_RECORD>(*__current_exception());
}
bool has_original_cpp_throw_frame(void* const* frames,std::size_t count) noexcept
{
#if defined(_WIN64)
    auto target=reinterpret_cast<DWORD64>(&_CxxThrowException);
    // MSVC の増分リンク・インポートの中継コードには巻き戻し情報がない。 コンパイラーの
    // E9 と RIP 相対 IAT ジャンプだけをたどり、実際の実行時処理へ到達する。
    for(int hop=0;hop<4;++hop){
        const auto code=reinterpret_cast<const unsigned char*>(target);
        if(code[0]==0xE9){std::int32_t displacement{};std::memcpy(&displacement,code+1,4);target+=5+displacement;}
        else if(code[0]==0xFF && code[1]==0x25){std::int32_t displacement{};std::memcpy(&displacement,code+2,4);target=*reinterpret_cast<const DWORD64*>(target+6+displacement);}
        else break;
    }
    for(std::size_t index=0;index<count;++index){
        DWORD64 image{};
        const auto entry=RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(frames[index]),&image,nullptr);
        if(entry && target>=image+entry->BeginAddress && target<image+entry->EndAddress)return true;
    }
#endif
    // 不明な ABI やコピー再送出の発生元は取得不能とする。 元の失
    // 敗位置を exception_ptr の再送出位置で代用しない。
    return false;
}
LONG CALLBACK capture_exception_origin(PEXCEPTION_POINTERS pointers) noexcept
{
    const auto record=pointers->ExceptionRecord;
    if(record->ExceptionCode!=0xE06D7363 ||
       (record->NumberParameters!=3 && record->NumberParameters!=4) ||
       record->ExceptionInformation[0]!=0x19930520 ||
       !record->ExceptionInformation[1] || capturing_exception_origin)
        return EXCEPTION_CONTINUE_SEARCH;
    const auto active=current_native_exception_record();
    // 引数なしの throw と exception_ptr の再送出では、最初の送出時のフレームを維持する。
    if(active && active->NumberParameters>=2 &&
       active->ExceptionInformation[1]==record->ExceptionInformation[1])
        return EXCEPTION_CONTINUE_SEARCH;
    capturing_exception_origin=true;
    void* frames[48]{};
    const auto frame_count=CaptureStackBackTrace(1,48,frames,nullptr);
    if(has_original_cpp_throw_frame(frames,frame_count))
        exception_origins().remember(record->ExceptionInformation[1],frames,frame_count);
    else if(transferred_exception_origin.count)
        exception_origins().remember(record->ExceptionInformation[1],
            transferred_exception_origin.frames,transferred_exception_origin.count,
            transferred_exception_origin.symbols);
    else
        exception_origins().forget(record->ExceptionInformation[1]);
    capturing_exception_origin=false;
    return EXCEPTION_CONTINUE_SEARCH;
}
struct ExceptionOriginObserver {
    PVOID handle=AddVectoredExceptionHandler(1,capture_exception_origin);
    ~ExceptionOriginObserver(){if(handle)RemoveVectoredExceptionHandler(handle);}
} exception_origin_observer;
#elif defined(__EMSCRIPTEN__) || defined(__unix__) || defined(__APPLE__)
void capture_allocated_exception_origin(void* object) noexcept
{
    ExceptionOriginTrace trace;
#if defined(__EMSCRIPTEN__)
    std::uintptr_t pcs[48]{};
    const auto pc=emscripten_stack_snapshot();
    trace.count=emscripten_stack_unwind_buffer(pc,pcs,48);
    for(std::size_t i=0;i<trace.count;++i)
        trace.frames[i]=reinterpret_cast<void*>(pcs[i]);
#else
    _Unwind_Backtrace([](_Unwind_Context* context,void* state) {
        auto& trace=*static_cast<ExceptionOriginTrace*>(state);
        if(trace.count==std::size(trace.frames))return _URC_END_OF_STACK;
        const auto pc=_Unwind_GetIP(context);
        if(pc)trace.frames[trace.count++]=reinterpret_cast<void*>(pc);
        return _URC_NO_REASON;
    },&trace);
#endif
    exception_origins().remember(reinterpret_cast<std::uintptr_t>(object),trace.frames,trace.count);
}
#endif

std::uintptr_t active_exception_object() noexcept
{
    std::uintptr_t object{};
#if defined(_WIN32) && defined(_MSC_VER)
    const auto record=current_native_exception_record();
    if(record&&record->NumberParameters>=2)object=record->ExceptionInformation[1];
#elif (defined(__EMSCRIPTEN__) || defined(__unix__) || defined(__APPLE__)) && \
      (defined(_LIBCPP_VERSION) || defined(__GLIBCXX__))
    // Itanium の libstdc++/libc++ の exception_ptr は、主例
    // 外オブジェクトへのポインターを持つ。 これは ABI の基本処理であり、別のログ方針ではない。
    const auto exception=std::current_exception();
    if constexpr(sizeof(exception)==sizeof(object))
        std::memcpy(&object,&exception,sizeof(object));
#endif
    return object;
}

ExceptionOriginTrace active_exception_origin() noexcept
{
    return exception_origins().find(active_exception_object());
}

String format_exception_frames(void* const* frames, std::size_t count)
{
#ifdef _WIN32
    static std::mutex symbols_mutex;
    std::scoped_lock lock(symbols_mutex);
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);
    std::vector<unsigned char> storage(sizeof(SYMBOL_INFO) + MAX_SYM_NAME);
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage.data());
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    std::ostringstream output;
    for (USHORT index = 0; index < count; ++index) {
        const auto address = reinterpret_cast<DWORD64>(frames[index]);
        DWORD64 displacement = 0;
        output << '#' << index << " 0x" << std::hex << address << std::dec;
        if (SymFromAddr(process, address, &displacement, symbol))
            output << ' ' << symbol->Name << "+0x" << std::hex << displacement << std::dec;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD line_displacement = 0;
        if (SymGetLineFromAddr64(process, address, &line_displacement, &line))
            output << " (" << line.FileName << ':' << line.LineNumber << ')';
        output << "\r\n";
    }
    return output.str();
#elif defined(__EMSCRIPTEN__)
    std::ostringstream output;
    for(std::size_t index=0;index<count;++index) {
        const auto pc=reinterpret_cast<std::uintptr_t>(frames[index]);
        output<<'#'<<index<<" 0x"<<std::hex<<pc<<std::dec;
        if(const auto name=emscripten_pc_get_function(pc))output<<' '<<name;
        output<<'\n';
    }
    return output.str();
#elif defined(__unix__) || defined(__APPLE__)
    std::ostringstream output;
    for(std::size_t index=0;index<count;++index) {
        output<<'#'<<index<<" 0x"<<std::hex<<reinterpret_cast<std::uintptr_t>(frames[index])<<std::dec;
        Dl_info info{};
        if(dladdr(frames[index],&info)&&info.dli_sname) {
            int status{};
            char* name=abi::__cxa_demangle(info.dli_sname,nullptr,nullptr,&status);
            output<<' '<<(name?name:info.dli_sname);
            std::free(name);
        }
        output<<'\n';
    }
    return output.str();
#else
    return "Stack trace is unavailable on this platform.";
#endif
}

String format_exception_origin(const ExceptionOriginTrace& trace)
{
    if(trace.symbols)return *trace.symbols;
    if(trace.count)return format_exception_frames(trace.frames,trace.count);
    return cw::thread::kExceptionOriginUnavailable;
}

String exception_stacktrace_for(const std::exception& exception)
{
    const auto object=active_exception_object();
    const auto* thrown_object=dynamic_cast<const void*>(&exception);
    // 呼び出し側が現在処理中の最派生オブジェクトそのものを渡した場
    // 合だけ、追跡情報を関連付ける。 無関係・生成しただけの例外や
    // 、入れ子の catch から参照された外側の例外は拒否する。
    if(!object || reinterpret_cast<std::uintptr_t>(thrown_object)!=object)
        return cw::thread::kExceptionOriginUnavailable;
    return format_exception_origin(exception_origins().find(object));
}

[[maybe_unused]] const bool exception_origin_provider_registered =
    cw::thread::install_exception_origin_provider(&exception_stacktrace_for);


} // namespace

// 例外で呼び出し元が巻き戻る前に取得する。静的リンクの Web/Android は
// リンカーのラッパーを使い、 動的なネイティブの Itanium 実行環境は、公開
// された確保処理のシンボル差し替えを使う。 どちらも共通の識別レジストリーへ渡す。
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
extern "C" void* __real___cxa_allocate_exception(std::size_t) noexcept;
extern "C" void* __wrap___cxa_allocate_exception(std::size_t size) noexcept
{
    void* object=__real___cxa_allocate_exception(size);
    capture_allocated_exception_origin(object);
    return object;
}
#elif defined(__unix__) || defined(__APPLE__)
extern "C" __attribute__((visibility("default")))
void* __cxa_allocate_exception(std::size_t size) noexcept
{
    using Allocate=void*(*)(std::size_t) noexcept;
    static const auto allocate=reinterpret_cast<Allocate>(dlsym(RTLD_NEXT,"__cxa_allocate_exception"));
    if(!allocate)std::abort();
    void* object=allocate(size);
    capture_allocated_exception_origin(object);
    return object;
}
#endif

namespace cw::lib {
ExceptionPtr capture_exception() noexcept
{
    const auto trace=active_exception_origin();
    ExceptionPtr exception(std::current_exception());
    exception.count_=trace.count;
    std::copy_n(trace.frames,trace.count,exception.frames_.begin());
    exception.symbols_=trace.symbols;
#if defined(__EMSCRIPTEN__)
    // Emscripten は送出元ワーカーの JavaScript キ
    // ャッシュからプログラム位置を解決する。 別のワーカーへ例外を渡す前
    // に名前を確定し、数値の位置だけを別キャッシュで解決しようとしない。
    if(trace.count && !exception.symbols_) {
        try {exception.symbols_=std::make_shared<const String>(format_exception_frames(trace.frames,trace.count));}
        catch (...) {} // 診断処理で元の例外を置き換えない。
    }
#endif
    return exception;
}

[[noreturn]] void rethrow_exception(const ExceptionPtr& exception)
{
    ExceptionOriginTrace trace;
    trace.count=exception.count_;
    std::copy_n(exception.frames_.begin(),trace.count,trace.frames);
    trace.symbols=exception.symbols_;
#if (defined(__EMSCRIPTEN__) || defined(__unix__) || defined(__APPLE__)) && \
    (defined(_LIBCPP_VERSION) || defined(__GLIBCXX__))
    // この ABI は exception_ptr のコピー間でも主オブジェクトの同一性を維持する。
    std::uintptr_t object{};
    if constexpr(sizeof(exception.exception_)==sizeof(object))
        std::memcpy(&object,&exception.exception_,sizeof(object));
    if(object && trace.count)
        exception_origins().remember(object,trace.frames,trace.count,trace.symbols);
#endif
    const RestoreOperationValue guard(transferred_exception_origin,trace);
    std::rethrow_exception(exception.exception_);
}
}

namespace cw::thread {
std::string current_exception_stacktrace()
{
    return format_exception_origin(active_exception_origin());
}

} // namespace cw::thread
