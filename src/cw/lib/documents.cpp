#include "cw/lib/documents.hpp"
#include "cw/lib/web_upload_options.hpp"
#include "cw/lib/encoding.hpp"
#include <SDL3/SDL.h>
#if defined(SDL_PLATFORM_EMSCRIPTEN) || defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_ANDROID)
#include "app/explorer/platform_transfer.hpp"
#endif
#if defined(SDL_PLATFORM_IOS)
#include "cw/lib/documents_apple.hpp"
#endif
#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <thread>
#include <utility>

namespace cw::lib::documents {
struct Ticket::State {
    std::atomic<Phase> phase{Phase::Waiting};
};
void Ticket::cancel() noexcept {
    auto phase=state_->phase.load();
    while(phase!=Phase::Delivered && phase!=Phase::Cancelled &&
        !state_->phase.compare_exchange_weak(phase,Phase::Cancelled)) {}
}
bool Ticket::pending() const noexcept {
    const auto phase=state_->phase.load();
    return phase==Phase::Waiting || phase==Phase::Queued;
}
Result parse_selection(const char* const* paths,const std::string& error) {
    if(!paths) return {Status::Failed,{},error.empty()?"ファイル選択に失敗しました":error};
    if(!*paths) return {Status::Cancelled,{},{}};
    Result result{Status::Success,{},{}};
    for(const char* const* path=paths;*path;++path) {
        const std::string location=*path;
        if(location.empty()) return {Status::Failed,{},"ファイル選択が空の場所を返しました"};
        result.documents.push_back({location.find("://")==std::string::npos?
            LocationKind::NativePath:LocationKind::ProviderUri,location,{}});
    }
    return result;
}
namespace {
struct LocalReadLease {
    std::filesystem::path directory, file;
    ~LocalReadLease() {
        // 対象は自身が不可分に作成した、その一時ファイル・ディレクトリーだけと
        // する。 再帰削除、選択パスの追跡、ユーザーの元文書の削除は行わない。
        std::error_code ignored;
        if(!file.empty()) std::filesystem::remove(file,ignored);
        if(!directory.empty()) std::filesystem::remove(directory,ignored);
    }
};

#if defined(SDL_PLATFORM_EMSCRIPTEN) || defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_ANDROID)
struct ImportedFolderLease {
    std::vector<std::filesystem::path> paths;
    ~ImportedFolderLease() {
        try { cardwirth::explorer::release_upload_paths(paths); } catch (...) {}
    }
};

struct ExportedFolderLease {
    std::filesystem::path path;
    std::atomic_bool finalized{};
    ~ExportedFolderLease() {
        if(!finalized.exchange(true)) try {
            cardwirth::explorer::finalize_download_path_async(path,false,[](bool,std::string){});
        } catch (...) {}
    }
    void finish(bool success,std::function<void(bool,std::string)> done) {
        if(finalized.exchange(true)) {
            if(done) done(false,"書き出し先は既に確定されています");
            return;
        }
        try {
            cardwirth::explorer::finalize_download_path_async(path,success,std::move(done));
        } catch(...) {
            finalized.store(false);
            throw;
        }
    }
};

Status transfer_status(const cardwirth::explorer::TransferStatus status) {
    using Source=cardwirth::explorer::TransferStatus;
    switch(status) {
    case Source::Success:return Status::Success;
    case Source::Cancelled:return Status::Cancelled;
    case Source::Unsupported:return Status::Unsupported;
    case Source::Failed:return Status::Failed;
    }
    return Status::Failed;
}

void start_external_folder(const Request& request,Completion complete,Cancelled cancelled,
                           SDL_Window* parent) {
    using namespace cardwirth::explorer;
    const bool writing=request.folder_intent==FolderIntent::Write;
    auto callback=[complete=std::move(complete),cancelled=std::move(cancelled),writing]
        (NativePathSelection selection) mutable {
        if((cancelled&&cancelled()) || selection.cancelled) {
            if(writing) for(const auto& path:selection.paths)
                finalize_download_path_async(path,false,[](bool,std::string){});
            else release_upload_paths(selection.paths);
            complete({Status::Cancelled,{},{}});return;
        }
        if(selection.status!=TransferStatus::Success) {
            complete({transfer_status(selection.status),{},std::move(selection.error)});return;
        }
        if(selection.paths.size()!=1 || selection.paths.front().empty()) {
            if(writing) for(const auto& path:selection.paths)
                finalize_download_path_async(path,false,[](bool,std::string){});
            else release_upload_paths(selection.paths);
            complete({Status::Failed,{},"選択したフォルダの件数が正しくありません"});return;
        }
        Result result{Status::Success,{},{}};
        const auto path=selection.paths.front();
        const auto utf8=::cw::lib::encoding::path_to_utf8(path);
        if(writing) {
            auto lease=std::make_shared<ExportedFolderLease>();lease->path=path;
            Document document{LocationKind::NativePath,utf8,lease};
            document.finalize=[lease](bool success,std::function<void(bool,std::string)> done) {
                lease->finish(success,std::move(done));
            };
            result.documents.push_back(std::move(document));
        } else {
            auto lease=std::make_shared<ImportedFolderLease>();
            lease->paths=std::move(selection.paths);
            result.documents.push_back({LocationKind::NativePath,utf8,lease});
        }
        complete(std::move(result));
    };
    if(writing) (void)choose_download_folder_async(parent,std::move(callback));
    else (void)choose_upload_folders_async(parent,std::move(callback));
}
#endif

int hex_digit(const unsigned char value) noexcept {
    if(value>='0'&&value<='9') return value-'0';
    if(value>='a'&&value<='f') return value-'a'+10;
    if(value>='A'&&value<='F') return value-'A'+10;
    return -1;
}

std::string decoded_uri_tail(std::string value) {
    if(const auto end=value.find_first_of("?#");end!=std::string::npos) value.resize(end);
    std::string decoded;decoded.reserve(value.size());
    for(std::size_t i=0;i<value.size();++i) {
        if(value[i]=='%'&&i+2<value.size()) {
            const int hi=hex_digit(static_cast<unsigned char>(value[i+1]));
            const int lo=hex_digit(static_cast<unsigned char>(value[i+2]));
            if(hi>=0&&lo>=0) {decoded.push_back(static_cast<char>((hi<<4)|lo));i+=2;continue;}
        }
        decoded.push_back(value[i]);
    }
    const auto slash=decoded.find_last_of("/\\");
    return slash==std::string::npos?decoded:decoded.substr(slash+1);
}

std::string safe_extension(const Document& document,const std::vector<std::uint8_t>& bytes) {
    const auto starts=[&](std::initializer_list<unsigned char> signature) {
        return bytes.size()>=signature.size()&&std::equal(signature.begin(),signature.end(),bytes.begin());
    };
    // 内容の識別子を優先する。 プロバイダー URI は意味のない数値
    // ID で終わる場合が多いが、利用側は取得した形式を必要とする。
    if(starts({0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a})) return ".png";
    if(starts({0xff,0xd8,0xff})) return ".jpg";
    if(starts({'G','I','F','8','7','a'})||starts({'G','I','F','8','9','a'})) return ".gif";
    if(starts({'B','M'})) return ".bmp";
    if(starts({'I','I',0x2a,0x00})||starts({'M','M',0x00,0x2a})) return ".tiff";
    if(bytes.size()>=12&&starts({'R','I','F','F'})&&bytes[8]=='W'&&bytes[9]=='E'&&
       bytes[10]=='B'&&bytes[11]=='P') return ".webp";
    if(starts({'/','*',' ','X','P','M',' ','*','/'})) return ".xpm";
    if(starts({'P','K',0x03,0x04})||starts({'P','K',0x05,0x06})||starts({'P','K',0x07,0x08})) return ".zip";
    if(starts({'M','S','C','F'})) return ".cab";
    if(bytes.size()>=7&&bytes[2]=='-'&&bytes[3]=='l'&&bytes[4]=='h'&&bytes[6]=='-') return ".lzh";
    std::size_t text=0;
    if(bytes.size()>=3&&bytes[0]==0xef&&bytes[1]==0xbb&&bytes[2]==0xbf) text=3;
    while(text<bytes.size()&&(bytes[text]==' '||bytes[text]=='\t'||bytes[text]=='\r'||bytes[text]=='\n')) ++text;
    if(text<bytes.size()&&bytes[text]=='<') return ".xml";

    const auto tail=decoded_uri_tail(document.location);
    const auto dot=tail.find_last_of('.');
    if(dot!=std::string::npos&&dot+1<tail.size()&&tail.size()-dot<=13) {
        std::string extension=tail.substr(dot);
        if(std::all_of(extension.begin()+1,extension.end(),[](const unsigned char ch) {
            return std::isalnum(ch)||ch=='_'||ch=='-';
        })) {
            std::transform(extension.begin(),extension.end(),extension.begin(),[](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return extension;
        }
    }
    return ".bin";
}
std::vector<std::uint8_t> read_provider(const Document& document,std::size_t limit,const Cancelled& cancelled) {
    // SDL の Android 入出力は、ContentResolver のファイル記述子を通して content://
    // を開く。 ネイティブパスとして std::filesystem へ渡さない。
    auto* stream=SDL_IOFromFile(document.location.c_str(),"rb");
    if(!stream) throw std::runtime_error(SDL_GetError());
    struct Close {SDL_IOStream* stream;~Close(){if(stream) SDL_CloseIO(stream);}} close{stream};
    std::vector<std::uint8_t> bytes;
    std::array<std::uint8_t,65536> chunk;
    for(;;) {
        if(cancelled && cancelled()) return {};
        // 境界で一バイト多く読み、ちょうど上限サイズの EOF と区別する。
        const auto capacity=bytes.size()==limit?std::size_t{1}:(std::min)(chunk.size(),limit-bytes.size());
        const auto count=SDL_ReadIO(stream,chunk.data(),capacity);
        if(count>limit-bytes.size()) throw std::runtime_error("選択した文書が読み込み上限を超えています");
        bytes.insert(bytes.end(),chunk.begin(),chunk.begin()+count);
        const auto status=SDL_GetIOStatus(stream);
        if(status==SDL_IO_STATUS_ERROR) throw std::runtime_error(SDL_GetError());
        if(status==SDL_IO_STATUS_EOF) break;
        if(!count) throw std::runtime_error("選択した文書の読み込みが停止しました");
    }
    close.stream=nullptr;
    if(!SDL_CloseIO(stream)) throw std::runtime_error(SDL_GetError());
    return bytes;
}
std::shared_ptr<LocalReadLease> make_local_read_lease(const std::string& extension) {
#if defined(SDL_PLATFORM_ANDROID)
    const auto* internal=SDL_GetAndroidInternalStoragePath();
    if(!internal || !*internal) throw std::runtime_error("文書取得の一時領域がありません");
    const auto base=std::filesystem::path(internal);
#else
    const auto base=std::filesystem::temp_directory_path();
#endif
    std::random_device random;
    for(int attempt=0;attempt<16;++attempt) {
        const auto directory=base/("cw-document-"+std::to_string(random())+"-"+std::to_string(random()));
        if(!std::filesystem::create_directory(directory)) continue;
        auto lease=std::make_shared<LocalReadLease>();lease->directory=directory;
        std::filesystem::permissions(directory,std::filesystem::perms::owner_all);
        lease->file=directory/("document"+extension);
        return lease;
    }
    throw std::runtime_error("文書取得の一時領域を確保できません");
}
struct SdlRequest {
    Request request;
    Completion complete;
    Cancelled cancelled;
    std::vector<SDL_DialogFileFilter> filters;
};
void SDLCALL selected(void* userdata,const char* const* paths,int) {
    std::unique_ptr<std::shared_ptr<SdlRequest>> owner(static_cast<std::shared_ptr<SdlRequest>*>(userdata));
    try {(*owner)->complete(parse_selection(paths,paths?std::string{}:SDL_GetError()));}
    catch(...) { /* SDL のコールバック境界を越えて例外を出さない。 */ }
}
void SDLCALL show_picker(void* userdata) {
    std::unique_ptr<std::shared_ptr<SdlRequest>> owner(static_cast<std::shared_ptr<SdlRequest>*>(userdata));
    const auto state=*owner;
    if(state->cancelled()) return;
    try {
        const auto& request=state->request;
        SDL_Window* parent=request.parent_window_id?SDL_GetWindowFromID(request.parent_window_id):SDL_GetKeyboardFocus();
        if(request.parent_window_id && !parent) {
            state->complete({Status::Cancelled,{},{}});return;
        }
#if defined(SDL_PLATFORM_EMSCRIPTEN) || defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_ANDROID)
        if(request.operation==Operation::OpenFolder) {
            start_external_folder(request,state->complete,state->cancelled,parent);
            return;
        }
#endif
#if defined(SDL_PLATFORM_IOS)
        apple_adapter::start_import(request, state->complete, state->cancelled,
            [parent](auto options, auto callback) {
                cardwirth::explorer::apple::choose_import_files(parent, std::move(options), std::move(callback));
            }, cardwirth::explorer::apple::release_upload_paths);
        return;
#endif
#if defined(__EMSCRIPTEN__)
        if(request.operation==Operation::OpenFiles) {
            // ブラウザーのファイルは既存の HTML アップロード経路を使い、 SDL の
            // デスクトップ用ダイアログ処理である zenity・portal は使わない。
            const auto options = detail::web_upload_options(request);
            cardwirth::explorer::choose_upload_files_async(parent,
                [state](cardwirth::explorer::NativePathSelection selection) {
                    auto lease=std::make_shared<ImportedFolderLease>();
                    lease->paths=std::move(selection.paths);
                    if(state->cancelled()||selection.cancelled){
                        state->complete({Status::Cancelled,{},{}});return;
                    }
                    Result result{transfer_status(selection.status),{},std::move(selection.error)};
                    for(const auto& path:lease->paths)
                        result.documents.push_back({LocationKind::NativePath,::cw::lib::encoding::path_to_utf8(path),lease});
                    state->complete(std::move(result));
                },options);
            return;
        }
#endif
        const auto props=SDL_CreateProperties();
        if(!props) {state->complete({Status::Failed,{},SDL_GetError()});return;}
        struct Properties {SDL_PropertiesID id;~Properties(){SDL_DestroyProperties(id);}} properties{props};
        bool ok=SDL_SetPointerProperty(props,SDL_PROP_FILE_DIALOG_WINDOW_POINTER,parent) &&
            SDL_SetBooleanProperty(props,SDL_PROP_FILE_DIALOG_MANY_BOOLEAN,
                request.operation!=Operation::SaveFile && request.multiple);
        if(!request.title.empty()) ok=ok && SDL_SetStringProperty(props,SDL_PROP_FILE_DIALOG_TITLE_STRING,request.title.c_str());
        if(!request.initial_location.empty()) ok=ok && SDL_SetStringProperty(props,SDL_PROP_FILE_DIALOG_LOCATION_STRING,request.initial_location.c_str());
        for(const auto& filter:request.filters) state->filters.push_back({filter.label.c_str(),filter.extensions.c_str()});
        if(!state->filters.empty()) ok=ok &&
            SDL_SetPointerProperty(props,SDL_PROP_FILE_DIALOG_FILTERS_POINTER,state->filters.data()) &&
            SDL_SetNumberProperty(props,SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER,static_cast<Sint64>(state->filters.size()));
        if(!ok) {state->complete({Status::Failed,{},SDL_GetError()});return;}
        const auto type=request.operation==Operation::SaveFile?SDL_FILEDIALOG_SAVEFILE:
            request.operation==Operation::OpenFolder?SDL_FILEDIALOG_OPENFOLDER:SDL_FILEDIALOG_OPENFILE;
        // SDL が読み終えるまで、コールバックがフィルター文字列を所有する。
        auto* callback_owner=new std::shared_ptr<SdlRequest>(state);
        SDL_ShowFileDialogWithProperties(type,selected,callback_owner,props);
    } catch(const std::exception& e) {state->complete({Status::Failed,{},e.what()});}
      catch(...) {state->complete({Status::Failed,{},"ファイル選択を開始できませんでした"});}
}
void start_platform(const Request& request,Completion complete,Cancelled cancelled) {
#if defined(SDL_PLATFORM_TVOS)
    // これらのホストはブラウザー・プロバイダーからの取得を必要とし、仮のパス入力では代替
    // しない。 プラットフォームアダプターが、この明示的な対応可否の結果を置き換える。
    complete({Status::Unsupported,{},"この環境の文書取得サービスは未接続です"});
#else
    auto state=std::make_shared<SdlRequest>();
    state->request=request;state->complete=std::move(complete);state->cancelled=std::move(cancelled);
    auto* owner=new std::shared_ptr<SdlRequest>(state);
    if(SDL_IsMainThread()) show_picker(owner);
    else if(!SDL_RunOnMainThread(show_picker,owner,false)) {
        delete owner;state->complete({Status::Failed,{},SDL_GetError()});
    }
#endif
}
}
Result prepare_local_read(Result result,std::size_t max_bytes,const Cancelled& cancelled,DocumentReader reader) {
    if(result.status!=Status::Success || !max_bytes) return result;
    try {
        if(!reader) reader=read_provider;
        for(auto& document:result.documents) {
            if(cancelled && cancelled()) return {Status::Cancelled,{},{}};
            if(document.kind==LocationKind::NativePath) continue;
            if(document.location.empty()) throw std::runtime_error("選択した文書の場所が空です");
            const auto bytes=reader(document,max_bytes,cancelled);
            if(cancelled && cancelled()) return {Status::Cancelled,{},{}};
            if(bytes.size()>max_bytes) throw std::runtime_error("選択した文書が読み込み上限を超えています");
            const auto lease=make_local_read_lease(safe_extension(document,bytes));
            std::ofstream output(lease->file,std::ios::binary|std::ios::trunc);
            if(!output) throw std::runtime_error("文書の一時ファイルを作成できません");
            if(!bytes.empty()) output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
            output.close();
            if(!output) throw std::runtime_error("文書の一時ファイルを書き込めません");
            if(cancelled && cancelled()) return {Status::Cancelled,{},{}};
            document={LocationKind::NativePath,::cw::lib::encoding::path_to_utf8(lease->file),lease};
        }
        return result;
    } catch(const std::exception& error) {return {Status::Failed,{},error.what()};}
      catch(...) {return {Status::Failed,{},"選択した文書を取得できませんでした"};}
}
std::shared_ptr<Ticket> request(const Request& request,Dispatch dispatch,Completion completion,Backend backend) {
    auto state=std::make_shared<Ticket::State>();
    auto ticket=std::shared_ptr<Ticket>(new Ticket(state));
    if(!dispatch || !completion) {ticket->cancel();return ticket;}
    const auto deliver=[state,dispatch=std::move(dispatch),completion=std::move(completion)](Result result) {
        auto expected=Ticket::Phase::Waiting;
        if(!state->phase.compare_exchange_strong(expected,Ticket::Phase::Queued)) return;
        if(result.status==Status::Success && result.documents.empty())
            result={Status::Failed,{},"ファイル選択が取得結果を返しませんでした"};
        try {
            if(dispatch([state,completion,result=std::move(result)]() mutable {
                auto expected=Ticket::Phase::Queued;
                if(state->phase.compare_exchange_strong(expected,Ticket::Phase::Delivered)) completion(std::move(result));
            })) return;
        } catch(...) {}
        expected=Ticket::Phase::Queued;
        state->phase.compare_exchange_strong(expected,Ticket::Phase::Cancelled);
    };
    const Cancelled cancelled=[state]{return state->phase.load()==Ticket::Phase::Cancelled;};
    // 入出力を始める前に選択を受付済みとする。 最初のワーカーがまだ動いて
    // いても、重複コールバックで二回目のプロバイダー読み取りを開始しない。
    const auto selected_once=std::make_shared<std::atomic<bool>>(false);
    const Completion selected=[selected_once,deliver,cancelled,operation=request.operation,
        limit=request.transient_read_limit](Result result) {
        if(selected_once->exchange(true) || cancelled()) return;
        const bool local_read=operation==Operation::OpenFiles && limit && result.status==Status::Success &&
            std::any_of(result.documents.begin(),result.documents.end(),[](const Document& document) {
                return document.kind==LocationKind::ProviderUri;
            });
        if(!local_read) {deliver(std::move(result));return;}
        try {
            std::thread([result=std::move(result),limit,cancelled,deliver]() mutable {
                deliver(prepare_local_read(std::move(result),limit,cancelled));
            }).detach();
        } catch(const std::exception& error) {deliver({Status::Failed,{},error.what()});}
    };
    try {
        (backend?backend:Backend(start_platform))(request,selected,cancelled);
    } catch(const std::exception& e) {selected({Status::Failed,{},e.what()});}
      catch(...) {selected({Status::Failed,{},"ファイル選択サービスに失敗しました"});}
    return ticket;
}
std::shared_ptr<Ticket> write(const Document& target,std::vector<std::uint8_t> bytes,
    Dispatch dispatch,Completion completion,DocumentWriter writer) {
    if(!writer) writer=[](const Document& document,const std::vector<std::uint8_t>& data,const Cancelled& cancelled) -> Result {
        if(cancelled()) return {Status::Cancelled,{},{}};
        if(document.location.empty()) return {Status::Failed,{},"保存先が空です"};
        // SDL は Android の content:// を ContentResolver
        // から、ネイティブパスも同じ共通入出力 API から開く。 拡張子やパスは書き換えない。
        auto* stream=SDL_IOFromFile(document.location.c_str(),"wb");
        if(!stream) return {Status::Failed,{},SDL_GetError()};
        struct Close {SDL_IOStream* stream;~Close(){if(stream) SDL_CloseIO(stream);}} close{stream};
        std::string error;
        std::size_t offset=0;
        while(offset<data.size()) {
            const auto count=SDL_WriteIO(stream,data.data()+offset,(std::min)(std::size_t{65536},data.size()-offset));
            if(!count || SDL_GetIOStatus(stream)==SDL_IO_STATUS_ERROR) {error=SDL_GetError();break;}
            offset+=count;
        }
        bool ok=offset==data.size();
        if(ok && !SDL_FlushIO(stream)) {ok=false;error=SDL_GetError();}
        close.stream=nullptr;
        if(!SDL_CloseIO(stream)) {ok=false;error=SDL_GetError();}
        if(!ok) return {Status::Failed,{},"保存に失敗しました。保存先が不完全な場合があります。\n"+error};
        return {Status::Success,{document},{}};
    };
    Request save;save.operation=Operation::SaveFile;
    return request(save,std::move(dispatch),std::move(completion),
        [target,bytes=std::move(bytes),writer=std::move(writer)](const Request&,Completion done,Cancelled cancelled) mutable {
            std::thread([target,bytes=std::move(bytes),writer=std::move(writer),done,cancelled]() mutable {
                try {
                    if(cancelled()) {done({Status::Cancelled,{},{}});return;}
                    done(writer(target,bytes,cancelled));
                } catch(const std::exception& error) {done({Status::Failed,{},error.what()});}
                  catch(...) {done({Status::Failed,{},"文書の保存に失敗しました"});}
            }).detach();
        });
}
} // namespace cw::lib::documents
