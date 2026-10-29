#pragma once
// 共通 Presentation の文字編集。OS
// ごとの別 UI・別編集規則は追加しない。
#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cw::dialog::textedit {
inline std::size_t boundary(std::string_view text,std::size_t pos) {
    pos=std::min(pos,text.size());
    while(pos && pos<text.size() && (static_cast<unsigned char>(text[pos])&0xc0)==0x80) --pos;
    return pos;
}
inline std::size_t previous(std::string_view text,std::size_t pos) {
    return pos?boundary(text,pos-1):0;
}
inline std::size_t next(std::string_view text,std::size_t pos) {
    pos=std::min(pos,text.size());
    if(pos<text.size()) ++pos;
    while(pos<text.size() && (static_cast<unsigned char>(text[pos])&0xc0)==0x80) ++pos;
    return pos;
}
inline std::size_t count(std::string_view text) {
    return std::count_if(text.begin(),text.end(),[](unsigned char c){return (c&0xc0)!=0x80;});
}
inline std::string clean(std::string_view text,bool multiline) {
    std::string result;
    for(std::size_t i=0;i<text.size();) {
        const auto c=static_cast<unsigned char>(text[i]);
        if(c<128) {
            ++i;
            if(c=='\r') {if(i<text.size() && text[i]=='\n') ++i;if(multiline) result+='\n';}
            else if(c=='\n') {if(multiline) result+='\n';}
            else if(c>=32 && c!=127) result+=static_cast<char>(c);
            continue;
        }
        const int length=c>=0xc2 && c<=0xdf?2:c>=0xe0 && c<=0xef?3:c>=0xf0 && c<=0xf4?4:0;
        bool valid=length && i+length<=text.size();
        for(int j=1;valid && j<length;++j) valid=(static_cast<unsigned char>(text[i+j])&0xc0)==0x80;
        if(valid) {
            const auto second=static_cast<unsigned char>(text[i+1]);
            valid=!(c==0xe0 && second<0xa0) && !(c==0xed && second>=0xa0) &&
                  !(c==0xf0 && second<0x90) && !(c==0xf4 && second>=0x90);
        }
        if(valid) {result.append(text.substr(i,length));i+=length;} else ++i;
    }
    return result;
}
inline void replace(std::string& text,std::size_t& caret,std::size_t& anchor,
    std::string_view input,std::size_t limit,bool multiline) {
    caret=boundary(text,caret);anchor=boundary(text,anchor);
    auto first=std::min(caret,anchor);const auto last=std::max(caret,anchor);
    const bool backspace=input=="\b";
    if(backspace && first==last) first=previous(text,first);
    auto inserted=backspace?std::string{}:clean(input,multiline);
    if(limit) {
        const auto retained=count(std::string_view(text).substr(0,first))+count(std::string_view(text).substr(last));
        const auto available=retained<limit?limit-retained:0;
        std::size_t end=0;
        for(std::size_t n=0;n<available && end<inserted.size();++n) end=next(inserted,end);
        inserted.resize(end);
    }
    text.replace(first,last-first,inserted);caret=anchor=first+inserted.size();
}
struct Line {std::size_t first{},last{};};
inline int horizontal_offset(int current,int caret,int viewport,int content) {
    viewport=std::max(1,viewport);
    return std::clamp(std::max(std::min(current,caret),caret-viewport+1),0,std::max(0,content-viewport+1));
}
inline std::vector<Line> layout(std::string_view text,int width,const std::function<int(std::string_view)>& measure) {
    std::vector<Line> result;std::size_t first=0,pos=0;
    while(pos<text.size()) {
        if(text[pos]=='\n') {result.push_back({first,pos});first=++pos;continue;}
        const auto end=next(text,pos);
        if(pos>first && width>0 && measure(text.substr(first,end-first))>width) {
            result.push_back({first,pos});first=pos;
        }
        pos=end;
    }
    result.push_back({first,pos});return result;
}
// SDL の文字単位の未確定選択に対応する UTF-8 のバイト範囲。start
// が負ならカーソル指定なしとして末尾を使い、確定済みテキストには触れない。
inline std::pair<std::size_t,std::size_t> composition_range(std::string_view text,int start,int length) {
    const auto chars=count(text);
    const auto first=start<0?chars:std::min(chars,static_cast<std::size_t>(start));
    const auto selected=std::min(chars-first,static_cast<std::size_t>(std::max(0,length)));
    std::size_t begin=0,end=0;
    for(std::size_t i=0;i<first;++i) begin=next(text,begin);
    end=begin;
    for(std::size_t i=0;i<selected;++i) end=next(text,end);
    return {begin,end};
}
struct Preedit {
    std::string text;
    int start{-1}, length{};
    void clear() { text.clear(); start=-1; length=0; }
    void set(std::string_view value,int cursor=-1,int selected=0) {
        text=clean(value,false);
        const auto range=composition_range(text,cursor,selected);
        start=static_cast<int>(count(std::string_view(text).substr(0,range.first)));
        length=static_cast<int>(count(std::string_view(text).substr(range.first,range.second-range.first)));
    }
};
struct DisplayText {
    std::string text;
    std::size_t caret{}, first{}, last{}, preedit_first{}, preedit_last{};
    bool composing{};
};
// 未確定入力は表示上だけ選択部分を置き換え、TEXTINPUT が届くまで確定済み
// のテキストや選択を変更しない。下の位置情報はすべて UTF-8 のバイト単位。
inline DisplayText display_text(std::string_view committed,std::size_t caret,
                                std::size_t anchor,const Preedit& preedit) {
    caret=boundary(committed,caret);anchor=boundary(committed,anchor);
    const auto first=std::min(caret,anchor),last=std::max(caret,anchor);
    DisplayText result{std::string(committed),caret,first,last};
    if(preedit.text.empty()) return result;
    const auto range=composition_range(preedit.text,preedit.start,preedit.length);
    result.text.replace(first,last-first,preedit.text);
    result.preedit_first=first;result.preedit_last=first+preedit.text.size();
    result.first=first+range.first;result.last=first+range.second;
    result.caret=result.first;result.composing=true;
    return result;
}
struct Buffer {
    struct Snapshot {std::string text;std::size_t caret{},anchor{};};
    std::string text,composition;
    int composition_start{-1},composition_length{};
    void set_composition(std::string_view value,int start=-1,int length=0) {
        composition=clean(value,false);
        const auto [first,last]=composition_range(composition,start,length);
        composition_start=static_cast<int>(count(std::string_view(composition).substr(0,first)));
        composition_length=static_cast<int>(count(std::string_view(composition).substr(first,last-first)));
    }
    std::size_t caret{},anchor{};
    int scroll{},horizontal{};
    bool reveal{true};
    std::vector<Snapshot> undo,redo;
    void reset(std::string value,bool select_all=false,bool at_end=true) {
        text=std::move(value);caret=at_end?text.size():0;anchor=select_all?0:caret;
        composition.clear();undo.clear();redo.clear();scroll=horizontal=0;reveal=true;
    }
    void insert(std::string_view value,std::size_t limit,bool multiline) {
        Snapshot old{text,caret,anchor};
        replace(text,caret,anchor,value,limit,multiline);composition.clear();reveal=true;
        if(old.text!=text) {if(undo.size()==64) undo.erase(undo.begin());undo.push_back(std::move(old));redo.clear();}
    }
    void restore(bool forwards) {
        auto& from=forwards?redo:undo;auto& to=forwards?undo:redo;
        if(from.empty()) return;
        to.push_back({text,caret,anchor});auto value=std::move(from.back());from.pop_back();
        text=std::move(value.text);caret=value.caret;anchor=value.anchor;composition.clear();reveal=true;
    }
    void move(std::size_t pos,bool selecting) {
        caret=boundary(text,pos);if(!selecting) anchor=caret;reveal=true;
    }
};
} // namespace cw::dialog::textedit
