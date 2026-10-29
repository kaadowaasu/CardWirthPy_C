#include "replace_text_targets.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/background.hpp"
#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/coupon.hpp"
#include "cwxeditor_src/core/cwx/jpy.hpp"
#include "cwxeditor_src/core/cwx/motion.hpp"
#include "cwxeditor_src/core/cwx/msgutils.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include <algorithm>
#include <filesystem>
#include <unordered_set>
#include <tuple>
#include <stdexcept>

namespace cwxeditor::cwx::editor::gui::dwt::replacedialog::replace_text {
namespace {
struct Stopped {};
Strings nonempty(Strings values) {
    std::erase(values, String{});
    return values;
}
String joined(const Strings& values) {
    String value;
    for (const auto& part : values) { if (!value.empty()) value += '\n'; value += part; }
    return value;
}
String utf8(char32_t ch) {
    String s;
    if (ch < 0x80) s += char(ch);
    else if (ch < 0x800) { s += char(0xc0 | (ch >> 6)); s += char(0x80 | (ch & 63)); }
    else if (ch < 0x10000) { s += char(0xe0 | (ch >> 12)); s += char(0x80 | ((ch >> 6) & 63)); s += char(0x80 | (ch & 63)); }
    else { s += char(0xf0 | (ch >> 18)); s += char(0x80 | ((ch >> 12) & 63)); s += char(0x80 | ((ch >> 6) & 63)); s += char(0x80 | (ch & 63)); }
    return s;
}
char32_t firstChar(StringView s) {
    if (s.empty()) return 0;
    const auto c = static_cast<unsigned char>(s[0]);
    if (c < 0x80) return c;
    std::size_t n = c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
    if (s.size() < n) return 0;
    char32_t result = c & (n == 2 ? 31 : n == 3 ? 15 : 7);
    for (std::size_t i = 1; i < n; ++i) {
        const auto next = static_cast<unsigned char>(s[i]);
        if ((next & 0xc0) != 0x80) return 0;
        result = (result << 6) | (next & 63);
    }
    return result;
}

// 遅延処理する定義参照は、統合先からの逆検索ではなく、実
// 際の参照元を保持する。 参照元より先に定義を反映する。
template<class ID, class Value> auto definitionMigration(const std::shared_ptr<Value>& value) {
    struct State {
        std::vector<lib::Owner<usecounter::IDToUser_t<ID>>> users;
        ID oldId;
        bool captured{};
    };
    auto state = std::make_shared<State>();
    auto capture = [value, state] {
        if (state->captured) return;
        state->oldId = ID(value->path());
        if (const auto counter = value->useCounter()) {
            const auto current = counter->values(state->oldId);
            state->users.insert(state->users.end(), current.begin(), current.end());
            if (const auto sub = counter->sub(); sub && sub != counter) {
                const auto retained = sub->values(state->oldId);
                state->users.insert(state->users.end(), retained.begin(), retained.end());
            }
        }
        state->captured = true;
    };
    auto finish = [value, state]() -> undo::UndoArray {
        undo::UndoArray result;
        if (!state->captured) return result;
        const ID next(value->path());
        // 遅延処理の境界でもスナップショットを取得する。先のディレクトリ移
        // 動で、定義の旧パスが反映された場合があるため。 初期のスナップ
        // ショットは宣言の無効化で外へ移ったローカルの参照元を保持し、
        // 現在のものは正当な連続するディレクトリ・名前の変更を保持する。
        if (const auto counter = value->useCounter()) {
            const auto current = counter->values(state->oldId);
            state->users.insert(state->users.end(), current.begin(), current.end());
            if (const auto sub = counter->sub(); sub && sub != counter) {
                const auto retained = sub->values(state->oldId);
                state->users.insert(state->users.end(), retained.begin(), retained.end());
            }
        }
        std::unordered_set<const usecounter::IDToUser_t<ID>*> seen;
        for (const auto& user : state->users) {
            if (!user || !seen.insert(user.get()).second) continue;
            const auto old = referenceId<ID>(user);
            if (old == next || !replaceReference<ID>(user, next)) continue;
            result.push_back(std::make_shared<undo::TUndo<ID>>(old, next,
                [value, user](ID id) { (void)replaceReference<ID>(user, id); }));
        }
        return result;
    };
    return std::pair{std::move(capture), std::move(finish)};
}

template<class ID, class Value> void updateDeclaration(
    const std::shared_ptr<Value>& value, const ID& old) {
    if (const auto counter = value->useCounter(); counter && counter->global() != counter) {
        const ID next(value->path());
        if (old != next) { counter->deleteID(old); counter->createID(next); }
    }
}

class Visitor {
public:
    Visitor(SummaryPtr s, const ReplaceOptions& o, bool definitions, Emit e,
            const system::System* system)
        : summary(std::move(s)), options(o), definitionsSelected(definitions),
          emit(std::move(e)), sys(system ? *system : fallbackSystem) {}
    void push(TextTarget target) {
        if (!emit(std::move(target))) throw Stopped{};
    }
    TextTarget target(const CWXPathPtr& owner, String kind, String group = "fields") {
        TextTarget t;
        t.owner = owner;
        t.path = owner ? owner->cwxPath(true) : String{};
        if (owner && dynamic_cast<Summary*>(owner.get())) t.path = "summary";
        for (auto current = owner; current; current = current->cwxParent().lock()) {
            if (current.get() != summary->flagDirRoot().get()) continue;
            // 保持した全体ルートが、モデルの相対パスを返す場合がある。 D
            // 版と同様、検索の走査側で結果に明示的な変数ルートを指定する。
            if (path::cpcategory(t.path) != "variable")
                t.path = t.path.empty() ? "variable" : "variable/" + t.path;
            break;
        }
        t.filePath = kind == "file";
        t.kind = std::move(kind);
        // ポインターの同一性は、コマンド内のグループ化にだけ使い、保存する CWXPath に
        // は使わない。 コメント・ダイアログ・補助画面の別々のグループは、別行として維持する。
        t.resultGroup = std::to_string(reinterpret_cast<std::uintptr_t>(owner.get())) + ":" + group;
        return t;
    }
    template<class Read, class Write> void scalar(const CWXPathPtr& owner,
        String kind, Read read, Write write, String group = "fields", bool writable = true,
        bool variablePath = false) {
        auto t = target(owner, std::move(kind), std::move(group));
        t.read = std::move(read);
        t.replaceable = writable;
        t.variablePath = variablePath;
        t.write = [write = std::move(write)](String text) mutable { write(std::move(text)); return true; };
        push(std::move(t));
    }
    template<class Read, class Write> void array(const CWXPathPtr& owner,
        String kind, Read read, Write write, String group = "fields", bool writable = true, bool variablePath = false) {
        auto t = target(owner, std::move(kind), std::move(group));
        t.variablePath = variablePath;
        t.readParts = std::move(read);
        t.read = [read = t.readParts] { return joined(read()); };
        t.writeParts = [write = std::move(write)](const Strings& values) mutable { write(values); return true; };
        t.replaceable = writable;
        push(std::move(t));
    }
    template<class Images, class Write> void images(const CWXPathPtr& owner, Images original, Write write) {
        std::vector<std::size_t> selected;
        Strings paths;
        for (std::size_t i = 0; i < original.size(); ++i) {
            if (!original[i] || original[i]->type != types::CardImageType::File || utils::isBinImg(original[i]->path())) continue;
            selected.push_back(i); paths.push_back(utils::decodePath(original[i]->path()));
        }
        if (selected.empty()) return;
        array(owner, "file", [paths] { return paths; },
            [original, selected, write](const Strings& values) {
                auto next = original;
                for (std::size_t i = 0; i < selected.size() && i < values.size(); ++i)
                    next[selected[i]] = std::make_shared<card::CardImage>(values[i], original[selected[i]]->positionType());
                write(std::move(next));
            });
    }
    template<class Coupons, class Write> void coupons(const CWXPathPtr& owner, const Coupons& values, Write write) {
        // CouponsOwner は、共通の記述情報を通じて現
        // 在のスロットを公開する。 置換コマンドの元一覧は、従来
        // の値で返すネイティブ配列と同様に独立させ、 置換結果の
        // 未変更の Coupon は同じオブジェクトを保持する。
        lib::Array<lib::Owner<coupon::Coupon>> original;
        original.reserve(values.size());
        for (const auto& value : values) original.push_back(value);
        Strings names;
        for (const auto& c : original) names.push_back(c ? c->name() : String{});
        array(owner, "coupon", [names] { return names; },
            [original = std::move(original), write](const Strings& replacements) {
                lib::Array<lib::Owner<coupon::Coupon>> next;
                for (std::size_t i = 0; i < original.size() && i < replacements.size(); ++i) {
                    if (!original[i] || replacements[i].empty()) continue;
                    // 未変更の称号は同じオブジェクトを保持する。 名前が変わ
                    // ったものは独立した値にし、設定された数値は維持する。
                    next.push_back(replacements[i] == original[i]->name() ? original[i]
                        : std::make_shared<coupon::Coupon>(replacements[i], original[i]->value()));
                }
                write(std::move(next));
            });
    }
    template<class Value, class ID> void definition(const std::shared_ptr<Value>& value) {
        auto t = target(value, "variable-name");
        auto [capture, finish] = definitionMigration<ID>(value);
        t.read = [value] { return value->name(); };
        t.write = [value, capture](String name) {
            capture();
            const ID old(value->path());
            if (!value->name(flag::FlagDir::validName(std::move(name)))) return false;
            updateDeclaration(value, old);
            return true;
        };
        t.finishWrite = std::move(finish);
        push(std::move(t));
    }
    void directory(const flag::FlagDirPtr& value) {
        // ローカルルートに限定する意図的な保護を維持する。 全体のルート
        // には編集する名前がなく、D版のnullの親参照は再現しない。
        if (!value->parent()) return;
        auto t = target(value, "variable-name");
        using Action = std::function<void()>;
        std::vector<Action> captures;
        std::vector<std::function<undo::UndoArray()>> finishes;
        auto declarations = std::make_shared<std::vector<Action>>();
        const auto add = [&]<class ID>(const auto& items) {
            for (const auto& item : items) {
                auto [capture, finish] = definitionMigration<ID>(item);
                captures.push_back(std::move(capture)); finishes.push_back(std::move(finish));
                auto old = std::make_shared<ID>(item->path());
                declarations->push_back([item, old] { updateDeclaration(item, *old); *old = ID(item->path()); });
            }
        };
        add.template operator()<usecounter::FlagId>(value->allFlags());
        add.template operator()<usecounter::StepId>(value->allSteps());
        add.template operator()<usecounter::VariantId>(value->allVariants());
        t.read = [value] { return value->name(); };
        t.write = [value, captures, declarations](String name) {
            for (const auto& capture : captures) capture();
            if (!value->name(flag::FlagDir::validName(std::move(name)))) return false;
            for (const auto& declaration : *declarations) declaration();
            return true;
        };
        t.finishWrite = [finishes] { undo::UndoArray result; for (const auto& finish : finishes) {
            auto part = finish(); result.insert(result.end(), part.begin(), part.end());
        } return result; };
        push(std::move(t));
    }
    void embedded(const CWXPathPtr& owner, const std::shared_ptr<textholder::ISimpleTextHolder>& holder) {
        if (!holder) return;
        if (options.variableName && !options.message) {
            Strings refs = holder->flagsInText();
            const auto steps = holder->stepsInText(), variants = holder->variantsInText();
            refs.insert(refs.end(), steps.begin(), steps.end()); refs.insert(refs.end(), variants.begin(), variants.end());
            // D版はこれらの参照を意図的に報告するが、changeInText
            // には元のパスを渡す。 変更しないその動作を維持し、
            // 遅延した定義置換と競合する直接の改名を追加しない。
            if (!refs.empty()) array(owner, "variable-name", [refs] { return refs; }, [](const Strings&) {}, "fields", false, !definitionsSelected);
        }
        const auto rich = std::dynamic_pointer_cast<textholder::ITextHolder>(holder);
        if (!rich || !options.file) return;
        const auto fonts = rich->fontsInText();
        if (fonts.empty()) return;
        Strings letters;
        for (const auto& font : fonts) letters.push_back(utf8(msgutils::decodeFontPath(font)));
        const auto original = holder->text();
        array(owner, "embedded-font", [letters] { return letters; }, [holder, fonts, original, letters](const Strings& values) {
            // すべての置換を、同じ元の本文から組み立てる。 フォントの改名で
            // 記号が組み込みの名前記号となり、fontsInText から消
            // える場合があるため、 変更後の数値インデックスを再利用しない。
            const auto characters = [](StringView text) {
                Strings result;
                while (!text.empty()) {
                    const auto lead = static_cast<unsigned char>(text.front());
                    const auto size = std::min(text.size(), std::size_t(
                        lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4));
                    result.emplace_back(text.substr(0, size));
                    text.remove_prefix(size);
                }
                return result;
            };
            const auto before = characters(original);
            auto after = before;
            for (std::size_t i = 0; i < values.size() && i < fonts.size(); ++i) {
                // 対象外の記号はすべて元の表記を保つ。
                if (values[i].empty() || values[i] == letters[i]) continue;
                const auto extension = structs::pathToUtf8(structs::pathFromUtf8(fonts[i]).extension());
                const auto path = msgutils::encodeFontPath(firstChar(values[i]), extension);
                if (path.empty()) continue;
                const auto transformed = characters(msgutils::replTextUseFont(original, fonts[i], path));
                if (transformed.size() != before.size())
                    throw std::invalid_argument("Font substitution changed message character count");
                for (std::size_t j = 0; j < before.size(); ++j)
                    if (transformed[j] != before[j]) after[j] = transformed[j];
            }
            String body;
            for (const auto& character : after) body += character;
            holder->text(body);
        });
    }
    void visit(const CWXPathPtr& node) {
        if (!node || !seen.insert(node.get()).second) return;
        // D版の searchAll と同様、Motion 自体ではなく、非リンクの召喚獣
        // の内容を走査する。 Motion 自身の説明フィールドは、テキスト検索の対象外。
        if (auto motion = std::dynamic_pointer_cast<motion::Motion>(node)) {
            const auto beast = motion->beast();
            if (beast && !beast->linkId()) visit(beast);
            return;
        }
        if (auto comment = std::dynamic_pointer_cast<path::Commentable>(node))
            scalar(node, "comment", [comment] { return comment->comment(); },
                   [comment](String v) { comment->comment(std::move(v)); }, "comment");
        if (auto owner = std::dynamic_pointer_cast<event::EventTreeOwner>(node))
            scalar(node, "comment", [owner] { return owner->commentForEvents(); },
                   [owner](String v) { owner->commentForEvents(std::move(v)); }, "event-comment");
        if (auto value = std::dynamic_pointer_cast<card::Card>(node)) {
            const auto effect = std::dynamic_pointer_cast<card::EffectCard>(value);
            if (effect)
                scalar(node,"comment",[effect]{return effect->commentForEvents();},[effect](String v){effect->commentForEvents(std::move(v));},"event-comment");
            if (!effect || !effect->linkId()) {
                scalar(node,"card-name",[value]{return value->name();},[value](String v){value->name(std::move(v));});
                scalar(node,"card-description",[value]{return value->desc();},[value](String v){value->desc(std::move(v));});
                if (effect) {
                    scalar(node,"scenario",[effect]{return effect->scenario();},[effect](String v){effect->scenario(std::move(v));});
                    scalar(node,"author",[effect]{return effect->author();},[effect](String v){effect->author(std::move(v));});
                }
                images(node,value->paths(),[value](auto v){value->paths(v);});
                if (effect) {
                    array(node,"keycode",[effect]{return effect->keyCodes();},[effect](const Strings& v){effect->keyCodes(v);});
                    visit(effect->flagDirRoot());
                }
            }
            if (auto cast = std::dynamic_pointer_cast<card::CastCard>(value))
                coupons(node,cast->coupons(),[cast](auto v){cast->coupons(v);});
        }
        if (auto value=std::dynamic_pointer_cast<area::AbstractArea>(node))
            scalar(node,"area-name",[value]{return value->name();},[value](String v){value->name(std::move(v));});
        if (auto value=std::dynamic_pointer_cast<area::MenuCard>(node)) {
            scalar(node,"card-name",[value]{return value->name();},[value](String v){value->name(std::move(v));});
            scalar(node,"card-description",[value]{return value->desc();},[value](String v){value->desc(std::move(v));});
            scalar(node,"cardgroup",[value]{return value->cardGroup();},[value](String v){value->cardGroup(std::move(v));});
            scalar(node,"variable-name",[value]{return value->flag();},[value](String v){value->flag(std::move(v));},"fields",!definitionsSelected);
            images(node,value->paths(),[value](auto v){value->paths(v);});
        }
        if (auto value=std::dynamic_pointer_cast<flag::FlagDir>(node)) directory(value);
        if (auto value=std::dynamic_pointer_cast<flag::Flag>(node)) {
            definition<flag::Flag,usecounter::FlagId>(value);
            scalar(node,"variable-value",[value]{return value->on();},[value](String v){value->on(std::move(v));});
            scalar(node,"variable-value",[value]{return value->off();},[value](String v){value->off(std::move(v));});
        }
        if (auto value=std::dynamic_pointer_cast<flag::Step>(node)) {
            definition<flag::Step,usecounter::StepId>(value);
            for (std::uint32_t i=0;i<value->count();++i)
                scalar(node,"variable-value",[value,i]{return value->getValue(i);},[value,i](String v){value->setValue(i,std::move(v));});
        }
        if (auto value=std::dynamic_pointer_cast<flag::Variant>(node)) {
            definition<flag::Variant,usecounter::VariantId>(value);
            if (value->type()==types::VariantType::String)
                scalar(node,"variable-value",[value]{return value->strVal();},[value](String v){value->value(std::move(v));});
        }
        if (auto value=std::dynamic_pointer_cast<event::EventTree>(node)) {
            Strings keys; for(const auto& key:value->keyCodes())keys.push_back(sys.convFireKeyCode(key));
            const auto systemCopy=sys;
            array(node,"keycode",[keys]{return keys;},[value,systemCopy](const Strings& v){
                lib::Array<system::FKeyCode> keys;
                for(const auto& text:v){auto key=systemCopy.toFKeyCode(text);if(!key.keyCode.empty())keys.push_back(std::move(key));}
                value->keyCodes(std::move(keys));
            },"keycodes");
        }
        if (auto value=std::dynamic_pointer_cast<event::Content>(node)) {
            const auto parent=value->parent(); const auto& d=value->detail();
            if(parent&&(parent->detail().nextType==types::CNextType::Text||parent->detail().nextType==types::CNextType::Coupon))
                scalar(node,parent->detail().nextType==types::CNextType::Text?"event-text":"coupon",[value]{return value->name();},[value](String v){value->setName(std::move(v));});
#define FIELD(ARG, METHOD, KIND) if(d.use(types::CArg::ARG)) scalar(node,KIND,[value]{return value->METHOD();},[value](String v){value->METHOD(std::move(v));})
#define VARIABLE(ARG, METHOD) if(d.use(types::CArg::ARG)) scalar(node,"variable-name",[value]{return value->METHOD();},[value](String v){value->METHOD(std::move(v));},"fields",!definitionsSelected,!definitionsSelected)
            VARIABLE(Flag,flag); VARIABLE(Flag2,flag2); VARIABLE(Step,step); VARIABLE(Step2,step2); VARIABLE(Variant,variant);
            if(d.use(types::CArg::Start))scalar(node,"start",[value]{return value->start();},[](String){},"fields",false);
            if(value->type()==types::CType::Start)scalar(node,"start",[value]{return value->name();},[value](String v){value->setName(std::move(v));});
            FIELD(Coupon,coupon,"coupon"); FIELD(HoldingCoupon,holdingCoupon,"coupon");
            if(d.use(types::CArg::Coupons))coupons(node,value->coupons(),[value](auto v){value->coupons(v);});
            if(d.use(types::CArg::CouponNames))array(node,"coupon",[value]{return value->couponNames();},[value](const Strings& v){value->couponNames(lib::SharedSlice<String>(nonempty(v)));});
            FIELD(Gossip,gossip,"gossip"); FIELD(CompleteStamp,completeStamp,"end-scenario");
            FIELD(Text,text,"message");
            if(d.use(types::CArg::Text))embedded(node,std::dynamic_pointer_cast<textholder::ISimpleTextHolder>(value));
            for (const auto& dialog : value->dialogs()) visit(dialog);
            if(d.use(types::CArg::TalkerC))images(node,value->cardImages(),[value](auto v){value->cardImages(v);});
            FIELD(BgmPath,bgmPath,"file"); FIELD(InitialSoundPath,initialSoundPath,"file"); FIELD(SoundPath,soundPath,"file");
            FIELD(KeyCode,keyCode,"keycode");
            if(d.use(types::CArg::KeyCodes))array(node,"keycode",[value]{return value->keyCodes();},[value](const Strings& v){value->keyCodes(v);},"keycodes");
            FIELD(CellName,cellName,"cellname"); FIELD(CardGroup,cardGroup,"cardgroup"); FIELD(Expression,expression,"expression");
#undef FIELD
#undef VARIABLE
        }
        if(auto value=std::dynamic_pointer_cast<event::SDialog>(node)) {
            scalar(node,"message",[value]{return value->text();},[value](String v){value->text(v);});
            array(node,"coupon",[value]{return value->rCoupons();},[value](const Strings& v){value->rCoupons(nonempty(v));});
            embedded(node,std::dynamic_pointer_cast<textholder::ISimpleTextHolder>(value));
        }
        if(auto value=std::dynamic_pointer_cast<background::BgImage>(node)) {
            scalar(node,"variable-name",[value]{return value->flag();},[value](String v){value->flag(std::move(v));},"fields",!definitionsSelected);
            scalar(node,"cellname",[value]{return value->cellName();},[value](String v){value->cellName(std::move(v));});
            if(auto image=std::dynamic_pointer_cast<background::ImageCell>(value))
                scalar(node,"file",[image]{return utils::decodePath(image->path());},[image](String v){image->path(std::move(v));});
            if(auto text=std::dynamic_pointer_cast<background::TextCell>(value)) {
                scalar(node,"jptx",[text]{return text->text();},[text](String v){text->text(v);});
                embedded(node,std::dynamic_pointer_cast<textholder::ISimpleTextHolder>(text));
            }
        }
        for(const auto& child:node->cwxChilds()) {
            auto next=child.lock();
            if(auto effect=std::dynamic_pointer_cast<card::EffectCard>(next);effect&&effect->linkId()!=0)continue;
            visit(next);
        }
    }
    void run() {
        const auto root=CWXPathPtr(summary,static_cast<area::AreaOwner*>(summary.get()));
        images(root,summary->imagePaths(),[s=summary](auto v){s->imagePaths(v);});
        scalar(root,"summary",[s=summary]{return s->desc();},[s=summary](String v){s->desc(std::move(v));});
        scalar(root,"scenario",[s=summary]{return s->scenarioName();},[s=summary](String v){s->scenarioName(std::move(v));});
        scalar(root,"author",[s=summary]{return s->author();},[s=summary](String v){s->author(std::move(v));});
        array(root,"coupon",[s=summary]{return s->rCoupons();},[s=summary](const Strings& v){s->rCoupons(nonempty(v));});
        visit(summary->flagDirRoot());
        const auto each=[this](const auto& values){for(const auto& value:values) {
            if constexpr (std::is_base_of_v<area::AbstractArea,typename std::remove_reference_t<decltype(value)>::element_type>)
                visit(std::static_pointer_cast<area::AbstractArea>(value));
            else visit(value);
        }};
        each(summary->areas());each(summary->battles());each(summary->packages());
        each(summary->casts());each(summary->skills());each(summary->items());each(summary->beasts());each(summary->infos());
    }
private:
    SummaryPtr summary;
    const ReplaceOptions& options;
    bool definitionsSelected;
    Emit emit;
    system::System fallbackSystem;
    const system::System& sys;
    std::unordered_set<const CWXPath*> seen;
};
}

bool enumerate(const SummaryPtr& summary,const ReplaceOptions& options,bool definitionsSelected,
               const Emit& emit,const system::System* system) {
    if(!summary||!emit)return true;
    try { Visitor(summary,options,definitionsSelected,emit,system).run();return true; }
    catch(const Stopped&){return false;}
}

bool enumerateFiles(const SummaryPtr& summary,const ReplaceOptions& options,const Strings& extensions,
                    const props::CProps& props,const lib::Owner<filesync::FileSync>& sync,const Emit& emit) {
    if(!summary||!emit||(!options.jptx&&!options.textFile))return true;
    const auto root=structs::pathFromUtf8(summary->scenarioPath());
    std::error_code error;
    if(!std::filesystem::is_directory(root,error))return true;
    const auto publish=[&](const std::filesystem::path& file,String text,String kind,auto write) {
        TextTarget t;
        t.path=structs::pathToUtf8(file.lexically_relative(root));
        t.kind=std::move(kind);t.text=std::move(text);
        // 書き込み用に元のテンプレート・文字コードを、取り消し用に復号した変更前後のテ
        // キストを保持する。 履歴再適用時には読み直さず、リソース範囲にも依存しない。
        t.write=[write=std::move(write)](String text){try{write(text);}catch(const std::exception&){}return true;};
        t.externalOpenTarget=ExternalOpenTarget{ExternalOpenKind::File,summary->scenarioPath(),t.path,{},0};
        return emit(std::move(t));
    };
    for(std::filesystem::recursive_directory_iterator it(root),end;it!=end;++it) {
        if(!it->is_regular_file())continue;
        const auto file=it->path(); const auto extension=structs::pathToUtf8(file.extension());
        if(options.jptx&&utils::fncmp(extension,".jptx")==0) {
            try {
                bool sjis{};jpy::EffectBoosterError errors;
                const auto path=structs::pathToUtf8(file);
                const auto original=jpy::readJPYFile(path,props,errors,sjis);
                if(!publish(file,jpy::jptxText(original),"jptx",[path,original,sjis,sync](StringView text){jpy::writeJPYFile(path,jpy::jptxText(original,text),sjis,sync);}))return false;
            }catch(const std::exception&){}
        }
        if(options.textFile)for(auto wanted:extensions) {
            // D版と同様、設定された拡張子はドットも含めてそのまま比較する。
            if(lib::editor_window_host_options().all_mode && !wanted.empty()&&wanted.front()!='.')
                wanted.insert(wanted.begin(),'.');
            if(utils::fncmp(extension,wanted)!=0)continue;
            bool sjis{};const auto value=utils::readTextFile(file,sjis);
            if(!publish(file,value,"text-file",[file,sjis,sync](StringView text){utils::writeTextFile(file,text,sjis,sync);}))return false;
            break;
        }
    }
    return true;
}
} // namespace
