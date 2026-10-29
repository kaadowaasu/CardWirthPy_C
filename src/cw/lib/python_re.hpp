#pragma once
/*
CPython 3.8.10 redistribution license (including CNRI/Secret Labs SRE):
A. HISTORY OF THE SOFTWARE
==========================

Python was created in the early 1990s by Guido van Rossum at Stichting
Mathematisch Centrum (CWI, see http://www.cwi.nl) in the Netherlands
as a successor of a language called ABC.  Guido remains Python's
principal author, although it includes many contributions from others.

In 1995, Guido continued his work on Python at the Corporation for
National Research Initiatives (CNRI, see http://www.cnri.reston.va.us)
in Reston, Virginia where he released several versions of the
software.

In May 2000, Guido and the Python core development team moved to
BeOpen.com to form the BeOpen PythonLabs team.  In October of the same
year, the PythonLabs team moved to Digital Creations, which became
Zope Corporation.  In 2001, the Python Software Foundation (PSF, see
https://www.python.org/psf/) was formed, a non-profit organization
created specifically to own Python-related Intellectual Property.
Zope Corporation was a sponsoring member of the PSF.

All Python releases are Open Source (see http://www.opensource.org for
the Open Source Definition).  Historically, most, but not all, Python
releases have also been GPL-compatible; the table below summarizes
the various releases.

    Release         Derived     Year        Owner       GPL-
                    from                                compatible? (1)

    0.9.0 thru 1.2              1991-1995   CWI         yes
    1.3 thru 1.5.2  1.2         1995-1999   CNRI        yes
    1.6             1.5.2       2000        CNRI        no
    2.0             1.6         2000        BeOpen.com  no
    1.6.1           1.6         2001        CNRI        yes (2)
    2.1             2.0+1.6.1   2001        PSF         no
    2.0.1           2.0+1.6.1   2001        PSF         yes
    2.1.1           2.1+2.0.1   2001        PSF         yes
    2.1.2           2.1.1       2002        PSF         yes
    2.1.3           2.1.2       2002        PSF         yes
    2.2 and above   2.1.1       2001-now    PSF         yes

Footnotes:

(1) GPL-compatible doesn't mean that we're distributing Python under
    the GPL.  All Python licenses, unlike the GPL, let you distribute
    a modified version without making your changes open source.  The
    GPL-compatible licenses make it possible to combine Python with
    other software that is released under the GPL; the others don't.

(2) According to Richard Stallman, 1.6.1 is not GPL-compatible,
    because its license has a choice of law clause.  According to
    CNRI, however, Stallman's lawyer has told CNRI's lawyer that 1.6.1
    is "not incompatible" with the GPL.

Thanks to the many outside volunteers who have worked under Guido's
direction to make these releases possible.


B. TERMS AND CONDITIONS FOR ACCESSING OR OTHERWISE USING PYTHON
===============================================================

Python software and documentation are licensed under the
Python Software Foundation License Version 2.

Starting with Python 3.8.6, examples, recipes, and other code in
the documentation are dual licensed under the PSF License Version 2
and the Zero-Clause BSD license.

Some software incorporated into Python is under different licenses.
The licenses are listed with code falling under that license.


PYTHON SOFTWARE FOUNDATION LICENSE VERSION 2
--------------------------------------------

1. This LICENSE AGREEMENT is between the Python Software Foundation
("PSF"), and the Individual or Organization ("Licensee") accessing and
otherwise using this software ("Python") in source or binary form and
its associated documentation.

2. Subject to the terms and conditions of this License Agreement, PSF hereby
grants Licensee a nonexclusive, royalty-free, world-wide license to reproduce,
analyze, test, perform and/or display publicly, prepare derivative works,
distribute, and otherwise use Python alone or in any derivative version,
provided, however, that PSF's License Agreement and PSF's notice of copyright,
i.e., "Copyright (c) 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010,
2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019, 2020, 2021 Python Software Foundation;
All Rights Reserved" are retained in Python alone or in any derivative version
prepared by Licensee.

3. In the event Licensee prepares a derivative work that is based on
or incorporates Python or any part thereof, and wants to make
the derivative work available to others as provided herein, then
Licensee hereby agrees to include in any such work a brief summary of
the changes made to Python.

4. PSF is making Python available to Licensee on an "AS IS"
basis.  PSF MAKES NO REPRESENTATIONS OR WARRANTIES, EXPRESS OR
IMPLIED.  BY WAY OF EXAMPLE, BUT NOT LIMITATION, PSF MAKES NO AND
DISCLAIMS ANY REPRESENTATION OR WARRANTY OF MERCHANTABILITY OR FITNESS
FOR ANY PARTICULAR PURPOSE OR THAT THE USE OF PYTHON WILL NOT
INFRINGE ANY THIRD PARTY RIGHTS.

5. PSF SHALL NOT BE LIABLE TO LICENSEE OR ANY OTHER USERS OF PYTHON
FOR ANY INCIDENTAL, SPECIAL, OR CONSEQUENTIAL DAMAGES OR LOSS AS
A RESULT OF MODIFYING, DISTRIBUTING, OR OTHERWISE USING PYTHON,
OR ANY DERIVATIVE THEREOF, EVEN IF ADVISED OF THE POSSIBILITY THEREOF.

6. This License Agreement will automatically terminate upon a material
breach of its terms and conditions.

7. Nothing in this License Agreement shall be deemed to create any
relationship of agency, partnership, or joint venture between PSF and
Licensee.  This License Agreement does not grant permission to use PSF
trademarks or trade name in a trademark sense to endorse or promote
products or services of Licensee, or any third party.

8. By copying, installing or otherwise using Python, Licensee
agrees to be bound by the terms and conditions of this License
Agreement.


BEOPEN.COM LICENSE AGREEMENT FOR PYTHON 2.0
-------------------------------------------

BEOPEN PYTHON OPEN SOURCE LICENSE AGREEMENT VERSION 1

1. This LICENSE AGREEMENT is between BeOpen.com ("BeOpen"), having an
office at 160 Saratoga Avenue, Santa Clara, CA 95051, and the
Individual or Organization ("Licensee") accessing and otherwise using
this software in source or binary form and its associated
documentation ("the Software").

2. Subject to the terms and conditions of this BeOpen Python License
Agreement, BeOpen hereby grants Licensee a non-exclusive,
royalty-free, world-wide license to reproduce, analyze, test, perform
and/or display publicly, prepare derivative works, distribute, and
otherwise use the Software alone or in any derivative version,
provided, however, that the BeOpen Python License is retained in the
Software, alone or in any derivative version prepared by Licensee.

3. BeOpen is making the Software available to Licensee on an "AS IS"
basis.  BEOPEN MAKES NO REPRESENTATIONS OR WARRANTIES, EXPRESS OR
IMPLIED.  BY WAY OF EXAMPLE, BUT NOT LIMITATION, BEOPEN MAKES NO AND
DISCLAIMS ANY REPRESENTATION OR WARRANTY OF MERCHANTABILITY OR FITNESS
FOR ANY PARTICULAR PURPOSE OR THAT THE USE OF THE SOFTWARE WILL NOT
INFRINGE ANY THIRD PARTY RIGHTS.

4. BEOPEN SHALL NOT BE LIABLE TO LICENSEE OR ANY OTHER USERS OF THE
SOFTWARE FOR ANY INCIDENTAL, SPECIAL, OR CONSEQUENTIAL DAMAGES OR LOSS
AS A RESULT OF USING, MODIFYING OR DISTRIBUTING THE SOFTWARE, OR ANY
DERIVATIVE THEREOF, EVEN IF ADVISED OF THE POSSIBILITY THEREOF.

5. This License Agreement will automatically terminate upon a material
breach of its terms and conditions.

6. This License Agreement shall be governed by and interpreted in all
respects by the law of the State of California, excluding conflict of
law provisions.  Nothing in this License Agreement shall be deemed to
create any relationship of agency, partnership, or joint venture
between BeOpen and Licensee.  This License Agreement does not grant
permission to use BeOpen trademarks or trade names in a trademark
sense to endorse or promote products or services of Licensee, or any
third party.  As an exception, the "BeOpen Python" logos available at
http://www.pythonlabs.com/logos.html may be used according to the
permissions granted on that web page.

7. By copying, installing or otherwise using the software, Licensee
agrees to be bound by the terms and conditions of this License
Agreement.


CNRI LICENSE AGREEMENT FOR PYTHON 1.6.1
---------------------------------------

1. This LICENSE AGREEMENT is between the Corporation for National
Research Initiatives, having an office at 1895 Preston White Drive,
Reston, VA 20191 ("CNRI"), and the Individual or Organization
("Licensee") accessing and otherwise using Python 1.6.1 software in
source or binary form and its associated documentation.

2. Subject to the terms and conditions of this License Agreement, CNRI
hereby grants Licensee a nonexclusive, royalty-free, world-wide
license to reproduce, analyze, test, perform and/or display publicly,
prepare derivative works, distribute, and otherwise use Python 1.6.1
alone or in any derivative version, provided, however, that CNRI's
License Agreement and CNRI's notice of copyright, i.e., "Copyright (c)
1995-2001 Corporation for National Research Initiatives; All Rights
Reserved" are retained in Python 1.6.1 alone or in any derivative
version prepared by Licensee.  Alternately, in lieu of CNRI's License
Agreement, Licensee may substitute the following text (omitting the
quotes): "Python 1.6.1 is made available subject to the terms and
conditions in CNRI's License Agreement.  This Agreement together with
Python 1.6.1 may be located on the Internet using the following
unique, persistent identifier (known as a handle): 1895.22/1013.  This
Agreement may also be obtained from a proxy server on the Internet
using the following URL: http://hdl.handle.net/1895.22/1013".

3. In the event Licensee prepares a derivative work that is based on
or incorporates Python 1.6.1 or any part thereof, and wants to make
the derivative work available to others as provided herein, then
Licensee hereby agrees to include in any such work a brief summary of
the changes made to Python 1.6.1.

4. CNRI is making Python 1.6.1 available to Licensee on an "AS IS"
basis.  CNRI MAKES NO REPRESENTATIONS OR WARRANTIES, EXPRESS OR
IMPLIED.  BY WAY OF EXAMPLE, BUT NOT LIMITATION, CNRI MAKES NO AND
DISCLAIMS ANY REPRESENTATION OR WARRANTY OF MERCHANTABILITY OR FITNESS
FOR ANY PARTICULAR PURPOSE OR THAT THE USE OF PYTHON 1.6.1 WILL NOT
INFRINGE ANY THIRD PARTY RIGHTS.

5. CNRI SHALL NOT BE LIABLE TO LICENSEE OR ANY OTHER USERS OF PYTHON
1.6.1 FOR ANY INCIDENTAL, SPECIAL, OR CONSEQUENTIAL DAMAGES OR LOSS AS
A RESULT OF MODIFYING, DISTRIBUTING, OR OTHERWISE USING PYTHON 1.6.1,
OR ANY DERIVATIVE THEREOF, EVEN IF ADVISED OF THE POSSIBILITY THEREOF.

6. This License Agreement will automatically terminate upon a material
breach of its terms and conditions.

7. This License Agreement shall be governed by the federal
intellectual property law of the United States, including without
limitation the federal copyright law, and, to the extent such
U.S. federal law does not apply, by the law of the Commonwealth of
Virginia, excluding Virginia's conflict of law provisions.
Notwithstanding the foregoing, with regard to derivative works based
on Python 1.6.1 that incorporate non-separable material that was
previously distributed under the GNU General Public License (GPL), the
law of the Commonwealth of Virginia shall govern this License
Agreement only as to issues arising under or with respect to
Paragraphs 4, 5, and 7 of this License Agreement.  Nothing in this
License Agreement shall be deemed to create any relationship of
agency, partnership, or joint venture between CNRI and Licensee.  This
License Agreement does not grant permission to use CNRI trademarks or
trade name in a trademark sense to endorse or promote products or
services of Licensee, or any third party.

8. By clicking on the "ACCEPT" button where indicated, or by copying,
installing or otherwise using Python 1.6.1, Licensee agrees to be
bound by the terms and conditions of this License Agreement.

        ACCEPT


CWI LICENSE AGREEMENT FOR PYTHON 0.9.0 THROUGH 1.2
--------------------------------------------------

Copyright (c) 1991 - 1995, Stichting Mathematisch Centrum Amsterdam,
The Netherlands.  All rights reserved.

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of Stichting Mathematisch
Centrum or CWI not be used in advertising or publicity pertaining to
distribution of the software without specific, written prior
permission.

STICHTING MATHEMATISCH CENTRUM DISCLAIMS ALL WARRANTIES WITH REGARD TO
THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS, IN NO EVENT SHALL STICHTING MATHEMATISCH CENTRUM BE LIABLE
FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

ZERO-CLAUSE BSD LICENSE FOR CODE IN THE PYTHON DOCUMENTATION
----------------------------------------------------------------------

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.

*/
// util.py の Python 3.8 の re に対応する、文字列パター
// ンのコンパイル・前方一致の共通処理。構文は CPython 3.8.10 の
// sre_parse.py/sre_compile.py と sre_lib.h
// に従い、Unicode 分類・単純な大小文字変換は UCD 12.1 に固
// 定する。PSF/Secret Labs の実際の SRE バイトスタック VM
// を使い、ネイティブのメモリー所有と固定の UCD 処理に接続する。AST
// は Python のビットマップ・接頭辞最適化なしで同等の基本文字集合を出力
// する。cw.util の意図的に限定した RegexParser とは別。
// Python のエラー分類は CompileError で返し、有効な UTF-8
// String を受け入れるが、単独サロゲートの文字列は表せない。
#include "cw/lib/regex_grapheme.hpp"
#include "cw/lib/python_integer.hpp"
#include "cw/lib/python_re_unicode_data.hpp"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>
#include <limits>
#include <memory>
#include <map>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace cw::lib::python_re {
// ネイティブ例外で失敗と分類の境界を保つ。Python の警告オ
// ブジェクトや処理系依存の診断文・位置を Python 例外と
// しては公開しない。position はスカラー値の診断情報。
class CompileError : public std::runtime_error {
public:
    String category;
    std::size_t position;
    CompileError(String message, std::size_t pos, String kind = "error")
        : std::runtime_error(std::move(message)), category(std::move(kind)), position(pos) {}
};
namespace detail {
inline unsigned properties(std::uint32_t c) {
    const auto& ranges=unicode_data::properties;
    auto it=std::upper_bound(std::begin(ranges),std::end(ranges),c,
        [](auto cp,const auto& r){return cp<r.first;});
    return it==std::begin(ranges)?0:(--it)->bits;
}
inline std::uint32_t lower(std::uint32_t c,bool ascii) {
    if(ascii)return c>='A'&&c<='Z'?c+32:c;
    const auto& table=unicode_data::lower;
    auto it=std::lower_bound(std::begin(table),std::end(table),c,
        [](const auto& p,auto cp){return p.from<cp;});
    return it!=std::end(table)&&it->from==c?it->to:c;
}
inline bool extra_equal(std::uint32_t a,std::uint32_t b) {
    if(a==b)return true;
    for(const auto& p:unicode_data::extra_cases)if(p.from==a&&p.to==b)return true;
    return false;
}
enum Flag : unsigned { Ignore=2, Multiline=8, Dotall=16, Unicode=32, Verbose=64, Template=1, Ascii=256 };
enum class Kind { Sequence, Choice, Literal, Dot, Class, Anchor, Group, Repeat, Ref, Assert, Conditional };
struct Item { std::uint32_t first{},last{};char category{}; };
struct Node;
using Ptr=std::shared_ptr<Node>;
struct Node {
    Kind kind;std::vector<Ptr> children;std::vector<Item> items;
    std::uint32_t value{};unsigned add{},remove{};bool negative{},lazy{},behind{};
    std::uint64_t minimum{},maximum{},behind_width{};
    explicit Node(Kind k):kind(k){}
};
inline Ptr node(Kind k){return std::make_shared<Node>(k);}
constexpr std::uint64_t infinity=std::numeric_limits<std::uint64_t>::max();
using Width=std::pair<std::uint64_t,std::uint64_t>;
inline std::uint64_t sum(std::uint64_t a,std::uint64_t b){return b>infinity-a?infinity:a+b;}
inline std::uint64_t multiply(std::uint64_t a,std::uint64_t b){return a&&b>infinity/a?infinity:a*b;}
inline Width width(const Ptr& n,const std::vector<Width>& groups);
inline Width raw_width(const Ptr& n,const std::vector<Width>& groups) {
    switch(n->kind){
    case Kind::Literal:case Kind::Class:case Kind::Dot:return{1,1};
    case Kind::Ref:return groups.at(n->value);
    case Kind::Anchor:case Kind::Assert:return{0,0};
    case Kind::Group:return width(n->children[0],groups);
    case Kind::Repeat:{auto w=width(n->children[0],groups);return{multiply(w.first,n->minimum),multiply(w.second,n->maximum)};}
    case Kind::Choice:case Kind::Conditional:{Width w={infinity,0};for(auto& c:n->children){auto v=width(c,groups);w.first=std::min(w.first,v.first);w.second=std::max(w.second,v.second);}return w;}
    default:{Width w={0,0};for(auto& c:n->children){auto v=width(c,groups);w.first=sum(w.first,v.first);w.second=sum(w.second,v.second);}return w;}
    }
}
inline Width width(const Ptr& n,const std::vector<Width>& groups) {
    // CPython3.8 の SubPattern.getwidth は最小・最大を異なる規
    // 則で飽和させる。その後、SRE 検証器は照合前に符号ビットが立った後読み幅を拒否する。
    const auto w=raw_width(n,groups);
    return {std::min<std::uint64_t>(w.first,0xfffffffeULL),
            std::min<std::uint64_t>(w.second,0xffffffffULL)};
}

class Parser {
    std::vector<std::uint32_t> text;
    std::size_t pos{},depth{};
    std::map<std::vector<std::uint32_t>,std::uint32_t> named;
    std::vector<bool> closed{true};
    std::vector<Width> group_widths{{0,0}};
    std::vector<std::uint32_t> conditions;
    std::optional<std::uint32_t> lookbehind_start;
    [[noreturn]] void error(const String& message,std::size_t at,String kind="error") const {throw CompileError(message,at,std::move(kind));}
    std::uint32_t peek() const {return pos<text.size()?text[pos]:0x110000;}
    std::uint32_t take(){return pos<text.size()?text[pos++]:0x110000;}
    bool accept(std::uint32_t c){if(peek()!=c)return false;++pos;return true;}
    static bool digit(std::uint32_t c){return c>='0'&&c<='9';}
    static bool octal(std::uint32_t c){return c>='0'&&c<='7';}
    static bool letter(std::uint32_t c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z');}
    static bool space(std::uint32_t c){return c==' '||(c>=9&&c<=13);}
    std::vector<std::uint32_t> until(std::uint32_t end,const String& what) {
        const auto start=pos;std::vector<std::uint32_t> result;
        while(peek()!=end&&peek()!=0x110000)result.push_back(take());
        if(!accept(end))error("unterminated "+what,start);
        if(result.empty())error("missing "+what,start);
        return result;
    }
    static bool identifier(const std::vector<std::uint32_t>& n) {
        if(n.empty()||!(properties(n.front())&8))return false;
        return std::all_of(n.begin()+1,n.end(),[](auto c){return(properties(c)&16)!=0;});
    }
    void check_ref(std::uint32_t group,std::size_t at,bool conditional=false) {
        if(!conditional&&(group>=closed.size()||!closed[group]))error("invalid or open group reference",at);
        if(lookbehind_start&&(group>=*lookbehind_start||group>=closed.size()||!closed[group]))error("cannot refer to group defined in the same lookbehind subpattern",at);
    }
    std::uint32_t named_character(std::size_t start) {
        if(!accept('{'))error("missing {",pos);
        auto chars=until('}',"character name");String name;
        for(auto c:chars){if(c>127)error("undefined character name",start);name+=static_cast<char>(c>='a'&&c<='z'?c-32:c);}
        auto& names=unicode_data::names;
        auto it=std::lower_bound(std::begin(names),std::end(names),std::string_view(name),
            [](const auto& item,std::string_view key){return std::string_view(unicode_data::name_chunks[item.offset/60000]+item.offset%60000)<key;});
        if(it==std::end(names)||std::string_view(unicode_data::name_chunks[it->offset/60000]+it->offset%60000)!=name)error("undefined character name",start);
        // unicodedata.lookup は名前付きの文字列を受け入れるが、sre_parse
        // の ord() は複数文字なら re.error ではなく TypeError を投げる。
        if(it->value==0xffffffff)error("ord() expected a character",start,"TypeError");
        return it->value;
    }
    Ptr escape(bool in_class) {
        const auto start=pos-1;auto c=take();if(c==0x110000)error("bad escape (end of pattern)",start);
        auto literal=[&](std::uint32_t cp){auto p=node(Kind::Literal);p->value=cp;return p;};
        if(c=='d'||c=='D'||c=='s'||c=='S'||c=='w'||c=='W'){auto p=node(Kind::Class);p->items.push_back({0,0,static_cast<char>(c)});return p;}
        if(!in_class&&(c=='A'||c=='Z'||c=='b'||c=='B')){auto p=node(Kind::Anchor);p->value=c;return p;}
        switch(c){case'a':return literal(7);case'b':return literal(8);case'f':return literal(12);case'n':return literal(10);case'r':return literal(13);case't':return literal(9);case'v':return literal(11);case'\\':return literal(c);default:break;}
        if(c=='x'||c=='u'||c=='U'){
            unsigned count=c=='x'?2:c=='u'?4:8;std::uint64_t cp=0;
            for(unsigned i=0;i<count;++i){auto d=peek();unsigned value=d>='0'&&d<='9'?d-'0':d>='a'&&d<='f'?d-'a'+10:d>='A'&&d<='F'?d-'A'+10:16;if(value>=16)error("incomplete escape",start);++pos;cp=cp*16+value;}
            if(cp>0x10ffff)error("bad escape",start);return literal(static_cast<std::uint32_t>(cp));
        }
        if(c=='N')return literal(named_character(start));
        if(octal(c)&&(in_class||c=='0'||(octal(peek())&&pos+1<text.size()&&octal(text[pos+1])))){
            unsigned cp=c-'0';for(int i=0;i<2&&octal(peek());++i)cp=cp*8+take()-'0';if(cp>255)error("octal escape outside range 0-0o377",start);return literal(cp);
        }
        if(digit(c)){
            if(in_class)error("bad escape",start);
            unsigned group=c-'0';if(digit(peek()))group=group*10+take()-'0';check_ref(group,start);auto p=node(Kind::Ref);p->value=group;return p;
        }
        if(letter(c))error("bad escape",start);
        return literal(c);
    }
    Ptr character_class(std::size_t start) {
        auto n=node(Kind::Class);n->negative=accept('^');bool any=false;
        for(;;){
            if(peek()==0x110000)error("unterminated character set",start);
            if(peek()==']'&&any){++pos;break;}
            auto c=take();Ptr a;
            if(c=='\\')a=escape(true);else{a=node(Kind::Literal);a->value=c;}
            if(accept('-')){
                if(accept(']')){if(a->kind==Kind::Literal)n->items.push_back({a->value,a->value,0});else n->items.insert(n->items.end(),a->items.begin(),a->items.end());n->items.push_back({'-','-',0});break;}
                if(peek()==0x110000)error("unterminated character set",start);
                c=take();Ptr b;if(c=='\\')b=escape(true);else{b=node(Kind::Literal);b->value=c;}
                if(a->kind!=Kind::Literal||b->kind!=Kind::Literal||a->value>b->value)error("bad character range",pos-3);
                n->items.push_back({a->value,b->value,0});
            }else if(a->kind==Kind::Literal)n->items.push_back({a->value,a->value,0});else n->items.insert(n->items.end(),a->items.begin(),a->items.end());
            any=true;
        }
        return n;
    }
    static unsigned flag(std::uint32_t c) {
        switch(c){case'i':return Ignore;case'm':return Multiline;case's':return Dotall;case'x':return Verbose;case'a':return Ascii;case'u':return Unicode;case't':return Template;default:return 0;}
    }
    Ptr group(bool verbose,std::size_t start) {
        std::uint32_t id=0;unsigned add=0,remove=0;bool capture=true;std::vector<std::uint32_t> name;
        if(accept('?')){
            auto c=take();
            if(c=='P'){
                if(accept('<')){name=until('>',"group name");if(!identifier(name))error("bad character in group name",start+4);}
                else if(accept('=')){name=until(')',"group name");if(!identifier(name))error("bad character in group name",start+4);auto it=named.find(name);if(it==named.end())error("unknown group name",start+4);check_ref(it->second,start+4);auto p=node(Kind::Ref);p->value=it->second;return p;}
                else error("unknown extension ?P",start+1);
            }else if(c==':')capture=false;
            else if(c=='#'){while(peek()!=')'&&peek()!=0x110000)take();if(!accept(')'))error("missing ), unterminated comment",start);return node(Kind::Sequence);}
            else if(c=='='||c=='!'||c=='<'){
                bool behind=c=='<';if(behind){c=take();if(c!='='&&c!='!')error("unknown extension ?<",start+1);}
                auto old=lookbehind_start;if(behind&&!old)lookbehind_start=static_cast<std::uint32_t>(closed.size());
                auto content=expression(verbose);lookbehind_start=old;
                if(!accept(')'))error("missing ), unterminated subpattern",start);
                auto p=node(Kind::Assert);p->negative=c=='!';p->behind=behind;p->children.push_back(content);
                if(behind){auto w=width(content,group_widths);if(w.first!=w.second)error("look-behind requires fixed-width pattern",start);if(w.first>=0x80000000ULL)error("invalid SRE code",start,"RuntimeError");p->behind_width=w.first;}
                return p;
            }else if(c=='('){
                auto condition=until(')',"group name");std::uint32_t ref;
                if(identifier(condition)){auto it=named.find(condition);if(it==named.end())error("unknown group name",start+3);ref=it->second;}
                else{
                    String utf8;for(auto cp:condition){if(cp<128)utf8+=static_cast<char>(cp);else{const auto bits=cp<0x800?2:cp<0x10000?3:4;utf8+=static_cast<char>((bits==2?0xc0:bits==3?0xe0:0xf0)|(cp>>(6*(bits-1))));for(int i=bits-2;i>=0;--i)utf8+=static_cast<char>(0x80|((cp>>(6*i))&63));}}
                    auto number=::cw::lib::parse_python_integer(utf8);if(!number||*number<=0||*number>=1073741823)error("invalid group reference",start+3);ref=static_cast<std::uint32_t>(*number);
                }
                check_ref(ref,start+3,true);conditions.push_back(ref);auto p=node(Kind::Conditional);p->value=ref;p->children.push_back(sequence(verbose));
                if(accept('|'))p->children.push_back(sequence(verbose));else p->children.push_back(node(Kind::Sequence));
                if(peek()=='|')error("conditional backref with more than two branches",pos);
                if(!accept(')'))error("missing ), unterminated subpattern",start);return p;
            }else if(flag(c)||c=='-'||c=='L'){
                while(c!='-'&&c!=':'&&c!=')'){
                    if(c=='L')error("bad inline flags: cannot use 'L' flag with a str pattern",pos);
                    auto f=flag(c);if(!f)error("unknown flag",pos-1);add|=f;if((add&Ascii)&&(add&Unicode))error("bad inline flags: flags 'a', 'u' and 'L' are incompatible",pos);
                    c=take();if(c==0x110000)error("missing -, : or )",pos);
                }
                if(c==')'){global_flags|=add;return node(Kind::Sequence);}
                if(add&Template)error("bad inline flags: cannot turn on global flag",pos-1);
                if(c=='-'){
                    c=take();if(!flag(c)&&c!='L')error("missing flag",pos-1);
                    while(c!=':'){
                        if(c=='a'||c=='u'||c=='L')error("bad inline flags: cannot turn off flags 'a', 'u' and 'L'",pos);
                        auto f=flag(c);if(!f)error("unknown flag",pos-1);remove|=f;c=take();if(c==0x110000)error("missing :",pos);
                    }
                }
                if(remove&Template)error("bad inline flags: cannot turn off global flag",pos-1);
                if(add&remove)error("bad inline flags: flag turned on and off",pos-1);capture=false;
            }else error("unknown extension",start+1);
        }
        if(capture){
            id=static_cast<std::uint32_t>(closed.size());closed.push_back(false);group_widths.emplace_back(0,0);
            if(!name.empty()){if(named.contains(name))error("redefinition of group name",start+4);named[name]=id;}
        }
        auto content=expression(((verbose||(add&Verbose))&&!(remove&Verbose)));
        if(!accept(')'))error("missing ), unterminated subpattern",start);
        if(id){group_widths[id]=width(content,group_widths);closed[id]=true;}
        auto p=node(Kind::Group);p->value=id;p->add=add;p->remove=remove;p->children.push_back(content);return p;
    }
    Ptr sequence(bool verbose) {
        auto result=node(Kind::Sequence);
        while(peek()!=0x110000&&peek()!=')'&&peek()!='|'){
            const auto start=pos;const auto old_global=global_flags;auto c=take();
            if(verbose){
                if(space(c))continue;if(c=='#'){while(peek()!=0x110000&&peek()!='\n')take();continue;}
            }
            if(c=='*'||c=='+'||c=='?'||c=='{'){
                std::uint64_t minimum=0,maximum=infinity;
                if(c=='?')maximum=1;else if(c=='+')minimum=1;
                else if(c=='{'){
                    const auto save=pos;String lo,hi;
                    while(digit(peek()))lo+=static_cast<char>(take());
                    if(accept(',')){while(digit(peek()))hi+=static_cast<char>(take());}else hi=lo;
                    if(!accept('}')||(lo.empty()&&hi.empty()&&pos==save+1)){
                        pos=save;auto p=node(Kind::Literal);p->value='{';result->children.push_back(p);continue;
                    }
                    auto number=[&](const String& s){std::uint64_t v=0;for(auto d:s){v=v*10+d-'0';if(v>=4294967295ULL)error("the repetition number is too large",start,"OverflowError");}return v;};
                    if(!lo.empty())minimum=number(lo);if(!hi.empty())maximum=number(hi);if(maximum<minimum)error("min repeat greater than max repeat",save);
                }
                if(result->children.empty()||result->children.back()->kind==Kind::Anchor)error("nothing to repeat",start);
                if(result->children.back()->kind==Kind::Repeat)error("multiple repeat",start);
                auto p=node(Kind::Repeat);p->minimum=minimum;p->maximum=maximum;p->lazy=accept('?');p->children.push_back(result->children.back());result->children.back()=p;continue;
            }
            Ptr p;
            if(c=='\\')p=escape(false);
            else if(c=='[')p=character_class(start);
            else if(c=='(')p=group(verbose,start);
            else if(c=='.')p=node(Kind::Dot);
            else if(c=='^'||c=='$'){p=node(Kind::Anchor);p->value=c;}
            else{p=node(Kind::Literal);p->value=c;}
            // インラインの全体フラグ・コメントは、繰り返し可能な AST 項目を生成しない。
            if(!(old_global&Verbose)&&(global_flags&Verbose))verbose=true;
            if(p->kind!=Kind::Sequence||!p->children.empty())result->children.push_back(p);
        }
        return result;
    }
    Ptr expression(bool verbose) {
        // このネイティブパーサーは固定の 500 段のスタック制限を使う。Python
        // の再帰制限は呼び出し元のスタックにも依存する。深すぎる・不正
        // なパターンをリテラルや不一致にせず、制限を明示的に報告する。
        if(++depth>500)error("maximum recursion depth exceeded",pos,"RecursionError");
        auto p=node(Kind::Choice);p->children.push_back(sequence(verbose));while(accept('|'))p->children.push_back(sequence(verbose));--depth;
        return p->children.size()==1?p->children[0]:p;
    }
    void validate_template(const Ptr& p) {
        if(p->kind==Kind::Repeat)error("internal: unsupported template operator",0);
        for(const auto& c:p->children)validate_template(c);
    }
public:
    unsigned global_flags{};Ptr root;std::size_t group_count{};
    explicit Parser(const String& pattern):text(::cw::lib::regex_grapheme::decode_utf8(pattern)) {
        root=expression(false);
        if(global_flags&Verbose){
            pos=depth=0;named.clear();closed={true};group_widths={{0,0}};conditions.clear();lookbehind_start.reset();global_flags=Verbose;root=expression(true);
        }
        if(pos!=text.size())error("unbalanced parenthesis",pos);
        if((global_flags&Ascii)&&(global_flags&Unicode))error("ASCII and UNICODE flags are incompatible",0,"ValueError");
        for(auto ref:conditions)if(ref>=closed.size())error("invalid group reference",0);
        if(global_flags&Template)validate_template(root);group_count=closed.size();
    }
};
inline bool category(std::uint32_t c,char code,unsigned flags) {
    bool match=false;const char base=code>='A'&&code<='Z'?code+32:code;
    if(flags&Ascii){if(base=='d')match=c>='0'&&c<='9';else if(base=='s')match=c==' '||(c>=9&&c<=13);else match=(c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';}
    else{const auto bits=properties(c);match=(bits&(base=='d'?2:base=='s'?4:1))!=0;}
    return code==base?match:!match;
}

namespace vm {
using SRE_CODE=std::uint32_t;
using SRE_CHAR=std::uint32_t;
using Py_ssize_t=std::int64_t;
constexpr int SRE_ERROR_ILLEGAL=-1,SRE_ERROR_STATE=-2,SRE_ERROR_MEMORY=-9,SRE_ERROR_INTERRUPTED=-10;
constexpr SRE_CODE SRE_MAXREPEAT=0xffffffff,SRE_CODE_BITS=32,SRE_INFO_PREFIX=1;
struct SRE_REPEAT{Py_ssize_t count;const SRE_CODE* pattern;SRE_REPEAT* prev;void* last_ptr;};
struct SRE_STATE{
    void *beginning{},*start{},*end{},*ptr{};
    void** mark{};Py_ssize_t lastmark=-1,lastindex=-1;
    int match_all=0,must_advance=0;SRE_REPEAT* repeat{};
    char* data_stack{};Py_ssize_t data_stack_size=0,data_stack_base=0;
    std::vector<void*> repeat_allocations;
    void* allocate_repeat(std::size_t size){auto p=std::malloc(size);if(!p)throw std::bad_alloc();try{repeat_allocations.push_back(p);}catch(...){std::free(p);throw;}return p;}
    void free_repeat(void* p){auto it=std::find(repeat_allocations.begin(),repeat_allocations.end(),p);if(it!=repeat_allocations.end())repeat_allocations.erase(it);std::free(p);}
    ~SRE_STATE(){std::free(data_stack);for(auto p:repeat_allocations)std::free(p);}
};
inline int data_stack_grow(SRE_STATE* state,Py_ssize_t size){
    const auto minimum=state->data_stack_base+size;
    if(minimum>state->data_stack_size){const auto capacity=minimum+minimum/4+1024;auto p=std::realloc(state->data_stack,static_cast<std::size_t>(capacity));if(!p)throw std::bad_alloc();state->data_stack=static_cast<char*>(p);state->data_stack_size=capacity;}return 0;
}
inline unsigned sre_lower_ascii(unsigned c){return lower(c,true);}
inline unsigned sre_lower_unicode(unsigned c){return lower(c,false);}
inline unsigned sre_lower_locale(unsigned c){return lower(c,true);}
inline unsigned sre_upper_unicode(unsigned c){const auto& table=unicode_data::upper;auto it=std::lower_bound(std::begin(table),std::end(table),c,[](const auto&p,auto cp){return p.from<cp;});return it!=std::end(table)&&it->from==c?it->to:c;}
inline unsigned sre_upper_locale(unsigned c){return c>='a'&&c<='z'?c-32:c;}
inline bool char_loc_ignore(unsigned a,unsigned b){return a==b||a==sre_lower_locale(b)||a==sre_upper_locale(b);}
#define LOCAL(T) inline T
#define SRE(F) sre_vm_##F
#define SIZEOF_SRE_CHAR 4
#define TRACE(X) do {} while (0)
#define SRE_IS_LINEBREAK(C) ((C)==10)
#define SRE_IS_WORD(C) category((C),'w',Ascii)
#define SRE_LOC_IS_WORD(C) category((C),'w',Ascii)
#define SRE_UNI_IS_WORD(C) category((C),'w',0)
#define PyObject_MALLOC(S) state->allocate_repeat(S)
#define PyObject_FREE(P) state->free_repeat(P)
#define PyErr_NoMemory() throw std::bad_alloc()
// このエンジンに Python 処理系のシグナル登録はない。メモリー確保の失敗は
// RAII で巻き戻し、この純粋な基本処理は OS の取り消しを管理しない。
#define PyErr_CheckSignals() 0
inline constexpr SRE_CODE SRE_OP_FAILURE=0;
inline constexpr SRE_CODE SRE_OP_SUCCESS=1;
inline constexpr SRE_CODE SRE_OP_ANY=2;
inline constexpr SRE_CODE SRE_OP_ANY_ALL=3;
inline constexpr SRE_CODE SRE_OP_ASSERT=4;
inline constexpr SRE_CODE SRE_OP_ASSERT_NOT=5;
inline constexpr SRE_CODE SRE_OP_AT=6;
inline constexpr SRE_CODE SRE_OP_BRANCH=7;
inline constexpr SRE_CODE SRE_OP_CALL=8;
inline constexpr SRE_CODE SRE_OP_CATEGORY=9;
inline constexpr SRE_CODE SRE_OP_CHARSET=10;
inline constexpr SRE_CODE SRE_OP_BIGCHARSET=11;
inline constexpr SRE_CODE SRE_OP_GROUPREF=12;
inline constexpr SRE_CODE SRE_OP_GROUPREF_EXISTS=13;
inline constexpr SRE_CODE SRE_OP_IN=14;
inline constexpr SRE_CODE SRE_OP_INFO=15;
inline constexpr SRE_CODE SRE_OP_JUMP=16;
inline constexpr SRE_CODE SRE_OP_LITERAL=17;
inline constexpr SRE_CODE SRE_OP_MARK=18;
inline constexpr SRE_CODE SRE_OP_MAX_UNTIL=19;
inline constexpr SRE_CODE SRE_OP_MIN_UNTIL=20;
inline constexpr SRE_CODE SRE_OP_NOT_LITERAL=21;
inline constexpr SRE_CODE SRE_OP_NEGATE=22;
inline constexpr SRE_CODE SRE_OP_RANGE=23;
inline constexpr SRE_CODE SRE_OP_REPEAT=24;
inline constexpr SRE_CODE SRE_OP_REPEAT_ONE=25;
inline constexpr SRE_CODE SRE_OP_SUBPATTERN=26;
inline constexpr SRE_CODE SRE_OP_MIN_REPEAT_ONE=27;
inline constexpr SRE_CODE SRE_OP_GROUPREF_IGNORE=28;
inline constexpr SRE_CODE SRE_OP_IN_IGNORE=29;
inline constexpr SRE_CODE SRE_OP_LITERAL_IGNORE=30;
inline constexpr SRE_CODE SRE_OP_NOT_LITERAL_IGNORE=31;
inline constexpr SRE_CODE SRE_OP_GROUPREF_LOC_IGNORE=32;
inline constexpr SRE_CODE SRE_OP_IN_LOC_IGNORE=33;
inline constexpr SRE_CODE SRE_OP_LITERAL_LOC_IGNORE=34;
inline constexpr SRE_CODE SRE_OP_NOT_LITERAL_LOC_IGNORE=35;
inline constexpr SRE_CODE SRE_OP_GROUPREF_UNI_IGNORE=36;
inline constexpr SRE_CODE SRE_OP_IN_UNI_IGNORE=37;
inline constexpr SRE_CODE SRE_OP_LITERAL_UNI_IGNORE=38;
inline constexpr SRE_CODE SRE_OP_NOT_LITERAL_UNI_IGNORE=39;
inline constexpr SRE_CODE SRE_OP_RANGE_UNI_IGNORE=40;
inline constexpr SRE_CODE SRE_AT_BEGINNING=0;
inline constexpr SRE_CODE SRE_AT_BEGINNING_LINE=1;
inline constexpr SRE_CODE SRE_AT_BEGINNING_STRING=2;
inline constexpr SRE_CODE SRE_AT_BOUNDARY=3;
inline constexpr SRE_CODE SRE_AT_NON_BOUNDARY=4;
inline constexpr SRE_CODE SRE_AT_END=5;
inline constexpr SRE_CODE SRE_AT_END_LINE=6;
inline constexpr SRE_CODE SRE_AT_END_STRING=7;
inline constexpr SRE_CODE SRE_AT_LOC_BOUNDARY=8;
inline constexpr SRE_CODE SRE_AT_LOC_NON_BOUNDARY=9;
inline constexpr SRE_CODE SRE_AT_UNI_BOUNDARY=10;
inline constexpr SRE_CODE SRE_AT_UNI_NON_BOUNDARY=11;
inline constexpr SRE_CODE SRE_CATEGORY_DIGIT=0;
inline constexpr SRE_CODE SRE_CATEGORY_NOT_DIGIT=1;
inline constexpr SRE_CODE SRE_CATEGORY_SPACE=2;
inline constexpr SRE_CODE SRE_CATEGORY_NOT_SPACE=3;
inline constexpr SRE_CODE SRE_CATEGORY_WORD=4;
inline constexpr SRE_CODE SRE_CATEGORY_NOT_WORD=5;
inline constexpr SRE_CODE SRE_CATEGORY_LINEBREAK=6;
inline constexpr SRE_CODE SRE_CATEGORY_NOT_LINEBREAK=7;
inline constexpr SRE_CODE SRE_CATEGORY_LOC_WORD=8;
inline constexpr SRE_CODE SRE_CATEGORY_LOC_NOT_WORD=9;
inline constexpr SRE_CODE SRE_CATEGORY_UNI_DIGIT=10;
inline constexpr SRE_CODE SRE_CATEGORY_UNI_NOT_DIGIT=11;
inline constexpr SRE_CODE SRE_CATEGORY_UNI_SPACE=12;
inline constexpr SRE_CODE SRE_CATEGORY_UNI_NOT_SPACE=13;
inline constexpr SRE_CODE SRE_CATEGORY_UNI_WORD=14;
inline constexpr SRE_CODE SRE_CATEGORY_UNI_NOT_WORD=15;
inline constexpr SRE_CODE SRE_CATEGORY_UNI_LINEBREAK=16;
inline constexpr SRE_CODE SRE_CATEGORY_UNI_NOT_LINEBREAK=17;

inline bool sre_category(SRE_CODE code,unsigned c){
    switch(code){
    case SRE_CATEGORY_DIGIT:return category(c,'d',Ascii);case SRE_CATEGORY_NOT_DIGIT:return category(c,'D',Ascii);
    case SRE_CATEGORY_SPACE:return category(c,'s',Ascii);case SRE_CATEGORY_NOT_SPACE:return category(c,'S',Ascii);
    case SRE_CATEGORY_WORD:case SRE_CATEGORY_LOC_WORD:return category(c,'w',Ascii);case SRE_CATEGORY_NOT_WORD:case SRE_CATEGORY_LOC_NOT_WORD:return category(c,'W',Ascii);
    case SRE_CATEGORY_UNI_DIGIT:return category(c,'d',0);case SRE_CATEGORY_UNI_NOT_DIGIT:return category(c,'D',0);
    case SRE_CATEGORY_UNI_SPACE:return category(c,'s',0);case SRE_CATEGORY_UNI_NOT_SPACE:return category(c,'S',0);
    case SRE_CATEGORY_UNI_WORD:return category(c,'w',0);case SRE_CATEGORY_UNI_NOT_WORD:return category(c,'W',0);
    case SRE_CATEGORY_LINEBREAK:return c==10;case SRE_CATEGORY_NOT_LINEBREAK:return c!=10;
    case SRE_CATEGORY_UNI_LINEBREAK:return c==10||c==13||c==11||c==12||(c>=28&&c<=30)||c==0x85||c==0x2028||c==0x2029;
    case SRE_CATEGORY_UNI_NOT_LINEBREAK:return !(c==10||c==13||c==11||c==12||(c>=28&&c<=30)||c==0x85||c==0x2028||c==0x2029);
    default:throw std::logic_error("Invalid compiled SRE category");}
}
/*
 * Secret Labs' Regular Expression Engine
 *
 * regular expression matching engine
 *
 * Copyright (c) 1997-2001 by Secret Labs AB.  All rights reserved.
 *
 * See the _sre.c file for information on usage and redistribution.
 */

/* 文字列の照合エンジン。 */

/* この移植版では32ビット文字型で照合エンジンを実装する。 */

LOCAL(int)
SRE(at)(SRE_STATE* state, SRE_CHAR* ptr, SRE_CODE at)
{
    /* ポインターが指定位置にあるか確認する。 */

    Py_ssize_t thisp, thatp;

    switch (at) {

    case SRE_AT_BEGINNING:
    case SRE_AT_BEGINNING_STRING:
        return ((void*) ptr == state->beginning);

    case SRE_AT_BEGINNING_LINE:
        return ((void*) ptr == state->beginning ||
                SRE_IS_LINEBREAK((int) ptr[-1]));

    case SRE_AT_END:
        return (((SRE_CHAR *)state->end - ptr == 1 &&
                 SRE_IS_LINEBREAK((int) ptr[0])) ||
                ((void*) ptr == state->end));

    case SRE_AT_END_LINE:
        return ((void*) ptr == state->end ||
                SRE_IS_LINEBREAK((int) ptr[0]));

    case SRE_AT_END_STRING:
        return ((void*) ptr == state->end);

    case SRE_AT_BOUNDARY:
        if (state->beginning == state->end)
            return 0;
        thatp = ((void*) ptr > state->beginning) ?
            SRE_IS_WORD((int) ptr[-1]) : 0;
        thisp = ((void*) ptr < state->end) ?
            SRE_IS_WORD((int) ptr[0]) : 0;
        return thisp != thatp;

    case SRE_AT_NON_BOUNDARY:
        if (state->beginning == state->end)
            return 0;
        thatp = ((void*) ptr > state->beginning) ?
            SRE_IS_WORD((int) ptr[-1]) : 0;
        thisp = ((void*) ptr < state->end) ?
            SRE_IS_WORD((int) ptr[0]) : 0;
        return thisp == thatp;

    case SRE_AT_LOC_BOUNDARY:
        if (state->beginning == state->end)
            return 0;
        thatp = ((void*) ptr > state->beginning) ?
            SRE_LOC_IS_WORD((int) ptr[-1]) : 0;
        thisp = ((void*) ptr < state->end) ?
            SRE_LOC_IS_WORD((int) ptr[0]) : 0;
        return thisp != thatp;

    case SRE_AT_LOC_NON_BOUNDARY:
        if (state->beginning == state->end)
            return 0;
        thatp = ((void*) ptr > state->beginning) ?
            SRE_LOC_IS_WORD((int) ptr[-1]) : 0;
        thisp = ((void*) ptr < state->end) ?
            SRE_LOC_IS_WORD((int) ptr[0]) : 0;
        return thisp == thatp;

    case SRE_AT_UNI_BOUNDARY:
        if (state->beginning == state->end)
            return 0;
        thatp = ((void*) ptr > state->beginning) ?
            SRE_UNI_IS_WORD((int) ptr[-1]) : 0;
        thisp = ((void*) ptr < state->end) ?
            SRE_UNI_IS_WORD((int) ptr[0]) : 0;
        return thisp != thatp;

    case SRE_AT_UNI_NON_BOUNDARY:
        if (state->beginning == state->end)
            return 0;
        thatp = ((void*) ptr > state->beginning) ?
            SRE_UNI_IS_WORD((int) ptr[-1]) : 0;
        thisp = ((void*) ptr < state->end) ?
            SRE_UNI_IS_WORD((int) ptr[0]) : 0;
        return thisp == thatp;

    }

    return 0;
}

LOCAL(int)
SRE(charset)(SRE_STATE* state, const SRE_CODE* set, SRE_CODE ch)
{
    /* 文字が指定集合に含まれるか確認する。 */

    int ok = 1;

    for (;;) {
        switch (*set++) {

        case SRE_OP_FAILURE:
            return !ok;

        case SRE_OP_LITERAL:
            /* <LITERAL> <code> */
            if (ch == set[0])
                return ok;
            set++;
            break;

        case SRE_OP_CATEGORY:
            /* <CATEGORY> <code> */
            if (sre_category(set[0], (int) ch))
                return ok;
            set++;
            break;

        case SRE_OP_CHARSET:
            /* <CHARSET> <bitmap> */
            if (ch < 256 &&
                (set[ch/SRE_CODE_BITS] & (1u << (ch & (SRE_CODE_BITS-1)))))
                return ok;
            set += 256/SRE_CODE_BITS;
            break;

        case SRE_OP_RANGE:
            /* <RANGE> <lower> <upper> */
            if (set[0] <= ch && ch <= set[1])
                return ok;
            set += 2;
            break;

        case SRE_OP_RANGE_UNI_IGNORE:
            /* <RANGE_UNI_IGNORE> <lower> <upper> */
        {
            SRE_CODE uch;
            /* ch はすでに小文字化されている。 */
            if (set[0] <= ch && ch <= set[1])
                return ok;
            uch = sre_upper_unicode(ch);
            if (set[0] <= uch && uch <= set[1])
                return ok;
            set += 2;
            break;
        }

        case SRE_OP_NEGATE:
            ok = !ok;
            break;

        case SRE_OP_BIGCHARSET:
            /* <BIGCHARSET> <blockcount> <256 blockindices> <blocks> */
        {
            Py_ssize_t count, block;
            count = *(set++);

            if (ch < 0x10000u)
                block = ((unsigned char*)set)[ch >> 8];
            else
                block = -1;
            set += 256/sizeof(SRE_CODE);
            if (block >=0 &&
                (set[(block * 256 + (ch & 255))/SRE_CODE_BITS] &
                    (1u << (ch & (SRE_CODE_BITS-1)))))
                return ok;
            set += count * (256/SRE_CODE_BITS);
            break;
        }

        default:
            /* 内部エラー。ここでは復旧でき
             * ないため、不一致として扱う。 */
            return 0;
        }
    }
}

LOCAL(int)
SRE(charset_loc_ignore)(SRE_STATE* state, const SRE_CODE* set, SRE_CODE ch)
{
    SRE_CODE lo, up;
    lo = sre_lower_locale(ch);
    if (SRE(charset)(state, set, lo))
       return 1;

    up = sre_upper_locale(ch);
    return up != lo && SRE(charset)(state, set, up);
}

LOCAL(Py_ssize_t) SRE(match)(SRE_STATE* state, const SRE_CODE* pattern, int toplevel);

LOCAL(Py_ssize_t)
SRE(count)(SRE_STATE* state, const SRE_CODE* pattern, Py_ssize_t maxcount)
{
    SRE_CODE chr;
    SRE_CHAR c;
    SRE_CHAR* ptr = (SRE_CHAR *)state->ptr;
    SRE_CHAR* end = (SRE_CHAR *)state->end;
    Py_ssize_t i;

    /* 末尾を調整する。 */
    if (maxcount < end - ptr && maxcount != SRE_MAXREPEAT)
        end = ptr + maxcount;

    switch (pattern[0]) {

    case SRE_OP_IN:
        /* 文字集合の繰り返し。 */
        TRACE(("|%p|%p|COUNT IN\n", pattern, ptr));
        while (ptr < end && SRE(charset)(state, pattern + 2, *ptr))
            ptr++;
        break;

    case SRE_OP_ANY:
        /* ドットのワイルドカードの繰り返し。 */
        TRACE(("|%p|%p|COUNT ANY\n", pattern, ptr));
        while (ptr < end && !SRE_IS_LINEBREAK(*ptr))
            ptr++;
        break;

    case SRE_OP_ANY_ALL:
        /* ドットのワイルドカードの繰り返し。対象文字
         * 列の末尾まで進み、そこから戻り探索する。 */
        TRACE(("|%p|%p|COUNT ANY_ALL\n", pattern, ptr));
        ptr = end;
        break;

    case SRE_OP_LITERAL:
        /* リテラルの繰り返し。 */
        chr = pattern[1];
        TRACE(("|%p|%p|COUNT LITERAL %d\n", pattern, ptr, chr));
        c = (SRE_CHAR) chr;
#if SIZEOF_SRE_CHAR < 4
        if ((SRE_CODE) c != chr)
            ; /* 文字幅に収まらないため、リテラルは一致しない。 */
        else
#endif
        while (ptr < end && *ptr == c)
            ptr++;
        break;

    case SRE_OP_LITERAL_IGNORE:
        /* リテラルの繰り返し。 */
        chr = pattern[1];
        TRACE(("|%p|%p|COUNT LITERAL_IGNORE %d\n", pattern, ptr, chr));
        while (ptr < end && (SRE_CODE) sre_lower_ascii(*ptr) == chr)
            ptr++;
        break;

    case SRE_OP_LITERAL_UNI_IGNORE:
        /* リテラルの繰り返し。 */
        chr = pattern[1];
        TRACE(("|%p|%p|COUNT LITERAL_UNI_IGNORE %d\n", pattern, ptr, chr));
        while (ptr < end && (SRE_CODE) sre_lower_unicode(*ptr) == chr)
            ptr++;
        break;

    case SRE_OP_LITERAL_LOC_IGNORE:
        /* リテラルの繰り返し。 */
        chr = pattern[1];
        TRACE(("|%p|%p|COUNT LITERAL_LOC_IGNORE %d\n", pattern, ptr, chr));
        while (ptr < end && char_loc_ignore(chr, *ptr))
            ptr++;
        break;

    case SRE_OP_NOT_LITERAL:
        /* リテラル以外の繰り返し。 */
        chr = pattern[1];
        TRACE(("|%p|%p|COUNT NOT_LITERAL %d\n", pattern, ptr, chr));
        c = (SRE_CHAR) chr;
#if SIZEOF_SRE_CHAR < 4
        if ((SRE_CODE) c != chr)
            ptr = end; /* 文字幅に収まらないため、リテラルは一致しない。 */
        else
#endif
        while (ptr < end && *ptr != c)
            ptr++;
        break;

    case SRE_OP_NOT_LITERAL_IGNORE:
        /* リテラル以外の繰り返し。 */
        chr = pattern[1];
        TRACE(("|%p|%p|COUNT NOT_LITERAL_IGNORE %d\n", pattern, ptr, chr));
        while (ptr < end && (SRE_CODE) sre_lower_ascii(*ptr) != chr)
            ptr++;
        break;

    case SRE_OP_NOT_LITERAL_UNI_IGNORE:
        /* リテラル以外の繰り返し。 */
        chr = pattern[1];
        TRACE(("|%p|%p|COUNT NOT_LITERAL_UNI_IGNORE %d\n", pattern, ptr, chr));
        while (ptr < end && (SRE_CODE) sre_lower_unicode(*ptr) != chr)
            ptr++;
        break;

    case SRE_OP_NOT_LITERAL_LOC_IGNORE:
        /* リテラル以外の繰り返し。 */
        chr = pattern[1];
        TRACE(("|%p|%p|COUNT NOT_LITERAL_LOC_IGNORE %d\n", pattern, ptr, chr));
        while (ptr < end && !char_loc_ignore(chr, *ptr))
            ptr++;
        break;

    default:
        /* 単一文字パターンの繰り返し。 */
        TRACE(("|%p|%p|COUNT SUBPATTERN\n", pattern, ptr));
        while ((SRE_CHAR*) state->ptr < end) {
            i = SRE(match)(state, pattern, 0);
            if (i < 0)
                return i;
            if (!i)
                break;
        }
        TRACE(("|%p|%p|COUNT %" PY_FORMAT_SIZE_T "d\n", pattern, ptr,
               (SRE_CHAR*) state->ptr - ptr));
        return (SRE_CHAR*) state->ptr - ptr;
    }

    TRACE(("|%p|%p|COUNT %" PY_FORMAT_SIZE_T "d\n", pattern, ptr,
           ptr - (SRE_CHAR*) state->ptr));
    return ptr - (SRE_CHAR*) state->ptr;
}

#if 0 /* この版では使用しない。 */
LOCAL(int)
SRE(info)(SRE_STATE* state, const SRE_CODE* pattern)
{
    /*
     * SRE_OP_INFO ブロックが現在位置で一致するか確認する。成功なら読み飛ばす SRE_CODE オブジェクト数、不一致なら 0 を返す。
     */

    SRE_CHAR* end = (SRE_CHAR*) state->end;
    SRE_CHAR* ptr = (SRE_CHAR*) state->ptr;
    Py_ssize_t i;

    /* 最小長を確認する。 */
    if (pattern[3] && end - ptr < pattern[3])
        return 0;

    /* 既知の接頭辞を確認する。 */
    if (pattern[2] & SRE_INFO_PREFIX && pattern[5] > 1) {
        /* <length> <skip> <prefix data> <overlap data> */
        for (i = 0; i < pattern[5]; i++)
            if ((SRE_CODE) ptr[i] != pattern[7 + i])
                return 0;
        return pattern[0] + 2 * pattern[6];
    }
    return pattern[0];
}
#endif

/*
 * 下のマクロは、再帰的な SRE(match)()
 * が失敗した後に直ちに戻ら
 * ず、戻り探索を行う場合に状
 * 態を保護する。true を
 * 返した場合は ASSERT_NOT
 * などの例外を除いて成功な
 * ので、通常 lastmark
 * を戻す必要はない。false
 * のまま現在の呼び出しも戻
 * る場合、最上位なら照合失敗
 * となり lastmark
 * は不要で、最上位でなければ
 * 呼び出し元が必要に応じて復
 * 元する。false の後に
 * 外側の for を継続する
 * 場合は次の命令が lastmark
 * に依存し得るため、抜け
 * るときに保護する。ローカ
 * ルな for/while
 * 内で再度呼ぶ場合も、再
 * 帰呼び出しが lastmark
 * を変更・参照し得るた
 * め、各反復の間で保護す
 * る。詳細は SF patch
 * #712900
 * の議論を参照。
 */
#define LASTMARK_SAVE()     \
    do { \
        ctx->lastmark = state->lastmark; \
        ctx->lastindex = state->lastindex; \
    } while (0)
#define LASTMARK_RESTORE()  \
    do { \
        state->lastmark = ctx->lastmark; \
        state->lastindex = ctx->lastindex; \
    } while (0)

#define RETURN_ERROR(i) do { return i; } while(0)
#define RETURN_FAILURE do { ret = 0; goto exit; } while(0)
#define RETURN_SUCCESS do { ret = 1; goto exit; } while(0)

#define RETURN_ON_ERROR(i) \
    do { if (i < 0) RETURN_ERROR(i); } while (0)
#define RETURN_ON_SUCCESS(i) \
    do { RETURN_ON_ERROR(i); if (i > 0) RETURN_SUCCESS; } while (0)
#define RETURN_ON_FAILURE(i) \
    do { RETURN_ON_ERROR(i); if (i == 0) RETURN_FAILURE; } while (0)

#define DATA_STACK_ALLOC(state, type, ptr) \
do { \
    alloc_pos = state->data_stack_base; \
    TRACE(("allocating %s in %" PY_FORMAT_SIZE_T "d " \
           "(%" PY_FORMAT_SIZE_T "d)\n", \
           Py_STRINGIFY(type), alloc_pos, sizeof(type))); \
    if (sizeof(type) > state->data_stack_size - alloc_pos) { \
        int j = data_stack_grow(state, sizeof(type)); \
        if (j < 0) return j; \
        if (ctx_pos != -1) \
            DATA_STACK_LOOKUP_AT(state, SRE(match_context), ctx, ctx_pos); \
    } \
    ptr = (type*)(state->data_stack+alloc_pos); \
    state->data_stack_base += sizeof(type); \
} while (0)

#define DATA_STACK_LOOKUP_AT(state, type, ptr, pos) \
do { \
    TRACE(("looking up %s at %" PY_FORMAT_SIZE_T "d\n", Py_STRINGIFY(type), pos)); \
    ptr = (type*)(state->data_stack+pos); \
} while (0)

#define DATA_STACK_PUSH(state, data, size) \
do { \
    TRACE(("copy data in %p to %" PY_FORMAT_SIZE_T "d " \
           "(%" PY_FORMAT_SIZE_T "d)\n", \
           data, state->data_stack_base, size)); \
    if (size > state->data_stack_size - state->data_stack_base) { \
        int j = data_stack_grow(state, size); \
        if (j < 0) return j; \
        if (ctx_pos != -1) \
            DATA_STACK_LOOKUP_AT(state, SRE(match_context), ctx, ctx_pos); \
    } \
    memcpy(state->data_stack+state->data_stack_base, data, size); \
    state->data_stack_base += size; \
} while (0)

#define DATA_STACK_POP(state, data, size, discard) \
do { \
    TRACE(("copy data to %p from %" PY_FORMAT_SIZE_T "d " \
           "(%" PY_FORMAT_SIZE_T "d)\n", \
           data, state->data_stack_base-size, size)); \
    memcpy(data, state->data_stack+state->data_stack_base-size, size); \
    if (discard) \
        state->data_stack_base -= size; \
} while (0)

#define DATA_STACK_POP_DISCARD(state, size) \
do { \
    TRACE(("discard data from %" PY_FORMAT_SIZE_T "d " \
           "(%" PY_FORMAT_SIZE_T "d)\n", \
           state->data_stack_base-size, size)); \
    state->data_stack_base -= size; \
} while(0)

#define DATA_PUSH(x) \
    DATA_STACK_PUSH(state, (x), sizeof(*(x)))
#define DATA_POP(x) \
    DATA_STACK_POP(state, (x), sizeof(*(x)), 1)
#define DATA_POP_DISCARD(x) \
    DATA_STACK_POP_DISCARD(state, sizeof(*(x)))
#define DATA_ALLOC(t,p) \
    DATA_STACK_ALLOC(state, t, p)
#define DATA_LOOKUP_AT(t,p,pos) \
    DATA_STACK_LOOKUP_AT(state,t,p,pos)

#define MARK_PUSH(lastmark) \
    do if (lastmark > 0) { \
        i = lastmark; /* 再確保により ctx->lastmark が変わる場合がある。 */ \
        DATA_STACK_PUSH(state, state->mark, (i+1)*sizeof(void*)); \
    } while (0)
#define MARK_POP(lastmark) \
    do if (lastmark > 0) { \
        DATA_STACK_POP(state, state->mark, (lastmark+1)*sizeof(void*), 1); \
    } while (0)
#define MARK_POP_KEEP(lastmark) \
    do if (lastmark > 0) { \
        DATA_STACK_POP(state, state->mark, (lastmark+1)*sizeof(void*), 0); \
    } while (0)
#define MARK_POP_DISCARD(lastmark) \
    do if (lastmark > 0) { \
        DATA_STACK_POP_DISCARD(state, (lastmark+1)*sizeof(void*)); \
    } while (0)

#define JUMP_NONE            0
#define JUMP_MAX_UNTIL_1     1
#define JUMP_MAX_UNTIL_2     2
#define JUMP_MAX_UNTIL_3     3
#define JUMP_MIN_UNTIL_1     4
#define JUMP_MIN_UNTIL_2     5
#define JUMP_MIN_UNTIL_3     6
#define JUMP_REPEAT          7
#define JUMP_REPEAT_ONE_1    8
#define JUMP_REPEAT_ONE_2    9
#define JUMP_MIN_REPEAT_ONE  10
#define JUMP_BRANCH          11
#define JUMP_ASSERT          12
#define JUMP_ASSERT_NOT      13

#define DO_JUMPX(jumpvalue, jumplabel, nextpattern, toplevel_) \
    DATA_ALLOC(SRE(match_context), nextctx); \
    nextctx->last_ctx_pos = ctx_pos; \
    nextctx->jump = jumpvalue; \
    nextctx->pattern = nextpattern; \
    nextctx->toplevel = toplevel_; \
    ctx_pos = alloc_pos; \
    ctx = nextctx; \
    goto entrance; \
    jumplabel: \
    while (0) /* GCC はスコープ末尾のラベルを受け付けない。 */ \

#define DO_JUMP(jumpvalue, jumplabel, nextpattern) \
    DO_JUMPX(jumpvalue, jumplabel, nextpattern, ctx->toplevel)

#define DO_JUMP0(jumpvalue, jumplabel, nextpattern) \
    DO_JUMPX(jumpvalue, jumplabel, nextpattern, 0)

#pragma pack(push, 1)
// 元のバイトスタックは Wasm で 32 ビットのポインターを保持し得る。この内部文脈
// だけを詰めて配置し、64 ビットのエンジンカウンターを任意のバイト位置で安全に扱う。
typedef struct {
    Py_ssize_t last_ctx_pos;
    Py_ssize_t jump;
    SRE_CHAR* ptr;
    const SRE_CODE* pattern;
    Py_ssize_t count;
    Py_ssize_t lastmark;
    Py_ssize_t lastindex;
    union {
        SRE_CODE chr;
        SRE_REPEAT* rep;
    } u;
    int toplevel;
} SRE(match_context);
#pragma pack(pop)

/* 文字列が指定パターンに一致するか確認する。
 * 負数はエラー、0 は不一致、1 は成功。 */
LOCAL(Py_ssize_t)
SRE(match)(SRE_STATE* state, const SRE_CODE* pattern, int toplevel)
{
    SRE_CHAR* end = (SRE_CHAR *)state->end;
    Py_ssize_t alloc_pos, ctx_pos = -1;
    Py_ssize_t i, ret = 0;
    Py_ssize_t jump;
    unsigned int sigcount=0;

    SRE(match_context)* ctx;
    SRE(match_context)* nextctx;

    TRACE(("|%p|%p|ENTER\n", pattern, state->ptr));

    DATA_ALLOC(SRE(match_context), ctx);
    ctx->last_ctx_pos = -1;
    ctx->jump = JUMP_NONE;
    ctx->pattern = pattern;
    ctx->toplevel = toplevel;
    ctx_pos = alloc_pos;

entrance:

    ctx->ptr = (SRE_CHAR *)state->ptr;

    if (ctx->pattern[0] == SRE_OP_INFO) {
        /* 最適化の情報ブロック。 */
        /* <INFO> <1=skip> <2=flags> <3=min> ... */
        if (ctx->pattern[3] && (uintptr_t)(end - ctx->ptr) < ctx->pattern[3]) {
            TRACE(("reject (got %" PY_FORMAT_SIZE_T "d chars, "
                   "need %" PY_FORMAT_SIZE_T "d)\n",
                   end - ctx->ptr, (Py_ssize_t) ctx->pattern[3]));
            RETURN_FAILURE;
        }
        ctx->pattern += ctx->pattern[1] + 1;
    }

    for (;;) {
        ++sigcount;
        if ((0 == (sigcount & 0xfff)) && PyErr_CheckSignals())
            RETURN_ERROR(SRE_ERROR_INTERRUPTED);

        switch (*ctx->pattern++) {

        case SRE_OP_MARK:
            /* 印を設定する。 */
            /* <MARK> <gid> */
            TRACE(("|%p|%p|MARK %d\n", ctx->pattern,
                   ctx->ptr, ctx->pattern[0]));
            i = ctx->pattern[0];
            if (i & 1)
                state->lastindex = i/2 + 1;
            if (i > state->lastmark) {
                /*
                 * state->lastmark は state->mark 配列の最大の有効インデック
                 * ス。一度に二つ以上増える場合、間の印を NULL にし、まだ通過していないことを示す。
                 */
                Py_ssize_t j = state->lastmark + 1;
                while (j < i)
                    state->mark[j++] = NULL;
                state->lastmark = i;
            }
            state->mark[i] = ctx->ptr;
            ctx->pattern++;
            break;

        case SRE_OP_LITERAL:
            /* リテラル文字列に一致させる。 */
            /* <LITERAL> <code> */
            TRACE(("|%p|%p|LITERAL %d\n", ctx->pattern,
                   ctx->ptr, *ctx->pattern));
            if (ctx->ptr >= end || (SRE_CODE) ctx->ptr[0] != ctx->pattern[0])
                RETURN_FAILURE;
            ctx->pattern++;
            ctx->ptr++;
            break;

        case SRE_OP_NOT_LITERAL:
            /* 指定のリテラル文字以外に一致させる。 */
            /* <NOT_LITERAL> <code> */
            TRACE(("|%p|%p|NOT_LITERAL %d\n", ctx->pattern,
                   ctx->ptr, *ctx->pattern));
            if (ctx->ptr >= end || (SRE_CODE) ctx->ptr[0] == ctx->pattern[0])
                RETURN_FAILURE;
            ctx->pattern++;
            ctx->ptr++;
            break;

        case SRE_OP_SUCCESS:
            /* パターンの終端。 */
            TRACE(("|%p|%p|SUCCESS\n", ctx->pattern, ctx->ptr));
            if (ctx->toplevel &&
                ((state->match_all && ctx->ptr != state->end) ||
                 (state->must_advance && ctx->ptr == state->start)))
            {
                RETURN_FAILURE;
            }
            state->ptr = ctx->ptr;
            RETURN_SUCCESS;

        case SRE_OP_AT:
            /* 指定位置で一致を確認する。 */
            /* <AT> <code> */
            TRACE(("|%p|%p|AT %d\n", ctx->pattern, ctx->ptr, *ctx->pattern));
            if (!SRE(at)(state, ctx->ptr, *ctx->pattern))
                RETURN_FAILURE;
            ctx->pattern++;
            break;

        case SRE_OP_CATEGORY:
            /* 指定分類に一致させる。 */
            /* <CATEGORY> <code> */
            TRACE(("|%p|%p|CATEGORY %d\n", ctx->pattern,
                   ctx->ptr, *ctx->pattern));
            if (ctx->ptr >= end || !sre_category(ctx->pattern[0], ctx->ptr[0]))
                RETURN_FAILURE;
            ctx->pattern++;
            ctx->ptr++;
            break;

        case SRE_OP_ANY:
            /* 改行以外の任意の文字に一致させる。 */
            /* <ANY> */
            TRACE(("|%p|%p|ANY\n", ctx->pattern, ctx->ptr));
            if (ctx->ptr >= end || SRE_IS_LINEBREAK(ctx->ptr[0]))
                RETURN_FAILURE;
            ctx->ptr++;
            break;

        case SRE_OP_ANY_ALL:
            /* 任意の文字に一致させる。 */
            /* <ANY_ALL> */
            TRACE(("|%p|%p|ANY_ALL\n", ctx->pattern, ctx->ptr));
            if (ctx->ptr >= end)
                RETURN_FAILURE;
            ctx->ptr++;
            break;

        case SRE_OP_IN:
            /* 集合の要素、または要素以外に一致させる。 */
            /* <IN> <skip> <set> */
            TRACE(("|%p|%p|IN\n", ctx->pattern, ctx->ptr));
            if (ctx->ptr >= end ||
                !SRE(charset)(state, ctx->pattern + 1, *ctx->ptr))
                RETURN_FAILURE;
            ctx->pattern += ctx->pattern[0];
            ctx->ptr++;
            break;

        case SRE_OP_LITERAL_IGNORE:
            TRACE(("|%p|%p|LITERAL_IGNORE %d\n",
                   ctx->pattern, ctx->ptr, ctx->pattern[0]));
            if (ctx->ptr >= end ||
                sre_lower_ascii(*ctx->ptr) != *ctx->pattern)
                RETURN_FAILURE;
            ctx->pattern++;
            ctx->ptr++;
            break;

        case SRE_OP_LITERAL_UNI_IGNORE:
            TRACE(("|%p|%p|LITERAL_UNI_IGNORE %d\n",
                   ctx->pattern, ctx->ptr, ctx->pattern[0]));
            if (ctx->ptr >= end ||
                sre_lower_unicode(*ctx->ptr) != *ctx->pattern)
                RETURN_FAILURE;
            ctx->pattern++;
            ctx->ptr++;
            break;

        case SRE_OP_LITERAL_LOC_IGNORE:
            TRACE(("|%p|%p|LITERAL_LOC_IGNORE %d\n",
                   ctx->pattern, ctx->ptr, ctx->pattern[0]));
            if (ctx->ptr >= end
                || !char_loc_ignore(*ctx->pattern, *ctx->ptr))
                RETURN_FAILURE;
            ctx->pattern++;
            ctx->ptr++;
            break;

        case SRE_OP_NOT_LITERAL_IGNORE:
            TRACE(("|%p|%p|NOT_LITERAL_IGNORE %d\n",
                   ctx->pattern, ctx->ptr, *ctx->pattern));
            if (ctx->ptr >= end ||
                sre_lower_ascii(*ctx->ptr) == *ctx->pattern)
                RETURN_FAILURE;
            ctx->pattern++;
            ctx->ptr++;
            break;

        case SRE_OP_NOT_LITERAL_UNI_IGNORE:
            TRACE(("|%p|%p|NOT_LITERAL_UNI_IGNORE %d\n",
                   ctx->pattern, ctx->ptr, *ctx->pattern));
            if (ctx->ptr >= end ||
                sre_lower_unicode(*ctx->ptr) == *ctx->pattern)
                RETURN_FAILURE;
            ctx->pattern++;
            ctx->ptr++;
            break;

        case SRE_OP_NOT_LITERAL_LOC_IGNORE:
            TRACE(("|%p|%p|NOT_LITERAL_LOC_IGNORE %d\n",
                   ctx->pattern, ctx->ptr, *ctx->pattern));
            if (ctx->ptr >= end
                || char_loc_ignore(*ctx->pattern, *ctx->ptr))
                RETURN_FAILURE;
            ctx->pattern++;
            ctx->ptr++;
            break;

        case SRE_OP_IN_IGNORE:
            TRACE(("|%p|%p|IN_IGNORE\n", ctx->pattern, ctx->ptr));
            if (ctx->ptr >= end
                || !SRE(charset)(state, ctx->pattern+1,
                                 (SRE_CODE)sre_lower_ascii(*ctx->ptr)))
                RETURN_FAILURE;
            ctx->pattern += ctx->pattern[0];
            ctx->ptr++;
            break;

        case SRE_OP_IN_UNI_IGNORE:
            TRACE(("|%p|%p|IN_UNI_IGNORE\n", ctx->pattern, ctx->ptr));
            if (ctx->ptr >= end
                || !SRE(charset)(state, ctx->pattern+1,
                                 (SRE_CODE)sre_lower_unicode(*ctx->ptr)))
                RETURN_FAILURE;
            ctx->pattern += ctx->pattern[0];
            ctx->ptr++;
            break;

        case SRE_OP_IN_LOC_IGNORE:
            TRACE(("|%p|%p|IN_LOC_IGNORE\n", ctx->pattern, ctx->ptr));
            if (ctx->ptr >= end
                || !SRE(charset_loc_ignore)(state, ctx->pattern+1, *ctx->ptr))
                RETURN_FAILURE;
            ctx->pattern += ctx->pattern[0];
            ctx->ptr++;
            break;

        case SRE_OP_JUMP:
        case SRE_OP_INFO:
            /* 前方へジャンプする。 */
            /* <JUMP> <offset> */
            TRACE(("|%p|%p|JUMP %d\n", ctx->pattern,
                   ctx->ptr, ctx->pattern[0]));
            ctx->pattern += ctx->pattern[0];
            break;

        case SRE_OP_BRANCH:
            /* 選択肢の分岐。 */
            /* <BRANCH> <0=skip> code <JUMP> ... <NULL> */
            TRACE(("|%p|%p|BRANCH\n", ctx->pattern, ctx->ptr));
            LASTMARK_SAVE();
            ctx->u.rep = state->repeat;
            if (ctx->u.rep)
                MARK_PUSH(ctx->lastmark);
            for (; ctx->pattern[0]; ctx->pattern += ctx->pattern[0]) {
                if (ctx->pattern[1] == SRE_OP_LITERAL &&
                    (ctx->ptr >= end ||
                     (SRE_CODE) *ctx->ptr != ctx->pattern[2]))
                    continue;
                if (ctx->pattern[1] == SRE_OP_IN &&
                    (ctx->ptr >= end ||
                     !SRE(charset)(state, ctx->pattern + 3,
                                   (SRE_CODE) *ctx->ptr)))
                    continue;
                state->ptr = ctx->ptr;
                DO_JUMP(JUMP_BRANCH, jump_branch, ctx->pattern+1);
                if (ret) {
                    if (ctx->u.rep)
                        MARK_POP_DISCARD(ctx->lastmark);
                    RETURN_ON_ERROR(ret);
                    RETURN_SUCCESS;
                }
                if (ctx->u.rep)
                    MARK_POP_KEEP(ctx->lastmark);
                LASTMARK_RESTORE();
            }
            if (ctx->u.rep)
                MARK_POP_DISCARD(ctx->lastmark);
            RETURN_FAILURE;

        case SRE_OP_REPEAT_ONE:
            /* 最長一致で繰り返し列に一致させる。 */

            /*
             * 繰り返す項目がちょうど一文字で、戻り探索の位置をまだ集めていない
             * 場合だけ使える演算子。それ以外は MAX_REPEAT を使う。
             */

            /* <REPEAT_ONE> <skip> <1=min> <2=max> item <SUCCESS> tail */

            TRACE(("|%p|%p|REPEAT_ONE %d %d\n", ctx->pattern, ctx->ptr,
                   ctx->pattern[1], ctx->pattern[2]));

            if ((Py_ssize_t) ctx->pattern[1] > end - ctx->ptr)
                RETURN_FAILURE; /* 一致できない。 */

            state->ptr = ctx->ptr;

            ret = SRE(count)(state, ctx->pattern+3, ctx->pattern[2]);
            RETURN_ON_ERROR(ret);
            DATA_LOOKUP_AT(SRE(match_context), ctx, ctx_pos);
            ctx->count = ret;
            ctx->ptr += ctx->count;

            /*
             * ここでは count が一致数、ctx->ptr が対象文字列の残りの
             * 先頭を示す。残りのパターンが一致するか確認し、しなければ戻り探索する。
             */

            if (ctx->count < (Py_ssize_t) ctx->pattern[1])
                RETURN_FAILURE;

            if (ctx->pattern[ctx->pattern[0]] == SRE_OP_SUCCESS &&
                ctx->ptr == state->end &&
                !(ctx->toplevel && state->must_advance && ctx->ptr == state->start))
            {
                /* 残りが空なので完了。 */
                state->ptr = ctx->ptr;
                RETURN_SUCCESS;
            }

            LASTMARK_SAVE();

            if (ctx->pattern[ctx->pattern[0]] == SRE_OP_LITERAL) {
                /* 残りはリテラルで始まる。残りのパター
                 * ンが一致し得ない位置を読み飛ばす。 */
                ctx->u.chr = ctx->pattern[ctx->pattern[0]+1];
                for (;;) {
                    while (ctx->count >= (Py_ssize_t) ctx->pattern[1] &&
                           (ctx->ptr >= end || *ctx->ptr != ctx->u.chr)) {
                        ctx->ptr--;
                        ctx->count--;
                    }
                    if (ctx->count < (Py_ssize_t) ctx->pattern[1])
                        break;
                    state->ptr = ctx->ptr;
                    DO_JUMP(JUMP_REPEAT_ONE_1, jump_repeat_one_1,
                            ctx->pattern+ctx->pattern[0]);
                    if (ret) {
                        RETURN_ON_ERROR(ret);
                        RETURN_SUCCESS;
                    }

                    LASTMARK_RESTORE();

                    ctx->ptr--;
                    ctx->count--;
                }

            } else {
                /* 一般の場合。 */
                while (ctx->count >= (Py_ssize_t) ctx->pattern[1]) {
                    state->ptr = ctx->ptr;
                    DO_JUMP(JUMP_REPEAT_ONE_2, jump_repeat_one_2,
                            ctx->pattern+ctx->pattern[0]);
                    if (ret) {
                        RETURN_ON_ERROR(ret);
                        RETURN_SUCCESS;
                    }
                    ctx->ptr--;
                    ctx->count--;
                    LASTMARK_RESTORE();
                }
            }
            RETURN_FAILURE;

        case SRE_OP_MIN_REPEAT_ONE:
            /* 最短一致で繰り返し列に一致させる。 */

            /*
             * 繰り返す項目がちょうど一文字で、戻り探索の位置をまだ集めていない
             * 場合だけ使える演算子。それ以外は MIN_REPEAT を使う。
             */

            /* <MIN_REPEAT_ONE> <skip> <1=min> <2=max> item <SUCCESS> tail */

            TRACE(("|%p|%p|MIN_REPEAT_ONE %d %d\n", ctx->pattern, ctx->ptr,
                   ctx->pattern[1], ctx->pattern[2]));

            if ((Py_ssize_t) ctx->pattern[1] > end - ctx->ptr)
                RETURN_FAILURE; /* 一致できない。 */

            state->ptr = ctx->ptr;

            if (ctx->pattern[1] == 0)
                ctx->count = 0;
            else {
                /* パターンの最小回数を上限にして数える。 */
                ret = SRE(count)(state, ctx->pattern+3, ctx->pattern[1]);
                RETURN_ON_ERROR(ret);
                DATA_LOOKUP_AT(SRE(match_context), ctx, ctx_pos);
                if (ret < (Py_ssize_t) ctx->pattern[1])
                    /* 最小回数の一致に届かなかった。 */
                    RETURN_FAILURE;
                /* 繰り返しの最小一致分だけ進める。 */
                ctx->count = ret;
                ctx->ptr += ctx->count;
            }

            if (ctx->pattern[ctx->pattern[0]] == SRE_OP_SUCCESS &&
                !(ctx->toplevel &&
                  ((state->match_all && ctx->ptr != state->end) ||
                   (state->must_advance && ctx->ptr == state->start))))
            {
                /* 残りが空なので完了。 */
                state->ptr = ctx->ptr;
                RETURN_SUCCESS;

            } else {
                /* 一般の場合。 */
                LASTMARK_SAVE();
                while ((Py_ssize_t)ctx->pattern[2] == SRE_MAXREPEAT
                       || ctx->count <= (Py_ssize_t)ctx->pattern[2]) {
                    state->ptr = ctx->ptr;
                    DO_JUMP(JUMP_MIN_REPEAT_ONE,jump_min_repeat_one,
                            ctx->pattern+ctx->pattern[0]);
                    if (ret) {
                        RETURN_ON_ERROR(ret);
                        RETURN_SUCCESS;
                    }
                    state->ptr = ctx->ptr;
                    ret = SRE(count)(state, ctx->pattern+3, 1);
                    RETURN_ON_ERROR(ret);
                    DATA_LOOKUP_AT(SRE(match_context), ctx, ctx_pos);
                    if (ret == 0)
                        break;
                    assert(ret == 1);
                    ctx->ptr++;
                    ctx->count++;
                    LASTMARK_RESTORE();
                }
            }
            RETURN_FAILURE;

        case SRE_OP_REPEAT:
            /* 繰り返しの文脈を作る。実際の処理は UNTIL 演算
             * 子（MAX_UNTIL、MIN_UNTIL）が行う。 */
            /* <REPEAT> <skip> <1=min> <2=max> item <UNTIL> tail */
            TRACE(("|%p|%p|REPEAT %d %d\n", ctx->pattern, ctx->ptr,
                   ctx->pattern[1], ctx->pattern[2]));

            /* 新しい繰り返しの文脈を設定する。 */
            ctx->u.rep = (SRE_REPEAT*) PyObject_MALLOC(sizeof(*ctx->u.rep));
            if (!ctx->u.rep) {
                PyErr_NoMemory();
                RETURN_FAILURE;
            }
            ctx->u.rep->count = -1;
            ctx->u.rep->pattern = ctx->pattern;
            ctx->u.rep->prev = state->repeat;
            ctx->u.rep->last_ptr = NULL;
            state->repeat = ctx->u.rep;

            state->ptr = ctx->ptr;
            DO_JUMP(JUMP_REPEAT, jump_repeat, ctx->pattern+ctx->pattern[0]);
            state->repeat = ctx->u.rep->prev;
            PyObject_FREE(ctx->u.rep);

            if (ret) {
                RETURN_ON_ERROR(ret);
                RETURN_SUCCESS;
            }
            RETURN_FAILURE;

        case SRE_OP_MAX_UNTIL:
            /* 最長一致の繰り返し。 */
            /* <REPEAT> <skip> <1=min> <2=max> item <MAX_UNTIL> tail */

            /* FIXME: ここで長さゼロの一
             * 致への対応が必要かもしれない。 */

            ctx->u.rep = state->repeat;
            if (!ctx->u.rep)
                RETURN_ERROR(SRE_ERROR_STATE);

            state->ptr = ctx->ptr;

            ctx->count = ctx->u.rep->count+1;

            TRACE(("|%p|%p|MAX_UNTIL %" PY_FORMAT_SIZE_T "d\n", ctx->pattern,
                   ctx->ptr, ctx->count));

            if (ctx->count < (Py_ssize_t) ctx->u.rep->pattern[1]) {
                /* 一致回数が足りない。 */
                ctx->u.rep->count = ctx->count;
                DO_JUMP(JUMP_MAX_UNTIL_1, jump_max_until_1,
                        ctx->u.rep->pattern+3);
                if (ret) {
                    RETURN_ON_ERROR(ret);
                    RETURN_SUCCESS;
                }
                ctx->u.rep->count = ctx->count-1;
                state->ptr = ctx->ptr;
                RETURN_FAILURE;
            }

            if ((ctx->count < (Py_ssize_t) ctx->u.rep->pattern[2] ||
                ctx->u.rep->pattern[2] == SRE_MAXREPEAT) &&
                state->ptr != ctx->u.rep->last_ptr) {
                /* 必要な回数に達していても、さら
                 * に一項目一致できるなら続ける。 */
                ctx->u.rep->count = ctx->count;
                LASTMARK_SAVE();
                MARK_PUSH(ctx->lastmark);
                /* 長さゼロの一致で文字位置が進まないまま繰り返すのを防ぐ。 */
                DATA_PUSH(&ctx->u.rep->last_ptr);
                ctx->u.rep->last_ptr = state->ptr;
                DO_JUMP(JUMP_MAX_UNTIL_2, jump_max_until_2,
                        ctx->u.rep->pattern+3);
                DATA_POP(&ctx->u.rep->last_ptr);
                if (ret) {
                    MARK_POP_DISCARD(ctx->lastmark);
                    RETURN_ON_ERROR(ret);
                    RETURN_SUCCESS;
                }
                MARK_POP(ctx->lastmark);
                LASTMARK_RESTORE();
                ctx->u.rep->count = ctx->count-1;
                state->ptr = ctx->ptr;
            }

            /* これ以上繰り返し項目が一致しない
             * ため、残りが一致するか確認する。 */
            state->repeat = ctx->u.rep->prev;
            DO_JUMP(JUMP_MAX_UNTIL_3, jump_max_until_3, ctx->pattern);
            RETURN_ON_SUCCESS(ret);
            state->repeat = ctx->u.rep;
            state->ptr = ctx->ptr;
            RETURN_FAILURE;

        case SRE_OP_MIN_UNTIL:
            /* 最短一致の繰り返し。 */
            /* <REPEAT> <skip> <1=min> <2=max> item <MIN_UNTIL> tail */

            ctx->u.rep = state->repeat;
            if (!ctx->u.rep)
                RETURN_ERROR(SRE_ERROR_STATE);

            state->ptr = ctx->ptr;

            ctx->count = ctx->u.rep->count+1;

            TRACE(("|%p|%p|MIN_UNTIL %" PY_FORMAT_SIZE_T "d %p\n", ctx->pattern,
                   ctx->ptr, ctx->count, ctx->u.rep->pattern));

            if (ctx->count < (Py_ssize_t) ctx->u.rep->pattern[1]) {
                /* 一致回数が足りない。 */
                ctx->u.rep->count = ctx->count;
                DO_JUMP(JUMP_MIN_UNTIL_1, jump_min_until_1,
                        ctx->u.rep->pattern+3);
                if (ret) {
                    RETURN_ON_ERROR(ret);
                    RETURN_SUCCESS;
                }
                ctx->u.rep->count = ctx->count-1;
                state->ptr = ctx->ptr;
                RETURN_FAILURE;
            }

            LASTMARK_SAVE();

            /* 残りが一致するか確認する。 */
            state->repeat = ctx->u.rep->prev;
            DO_JUMP(JUMP_MIN_UNTIL_2, jump_min_until_2, ctx->pattern);
            if (ret) {
                RETURN_ON_ERROR(ret);
                RETURN_SUCCESS;
            }

            state->repeat = ctx->u.rep;
            state->ptr = ctx->ptr;

            LASTMARK_RESTORE();

            if ((ctx->count >= (Py_ssize_t) ctx->u.rep->pattern[2]
                && ctx->u.rep->pattern[2] != SRE_MAXREPEAT) ||
                state->ptr == ctx->u.rep->last_ptr)
                RETURN_FAILURE;

            ctx->u.rep->count = ctx->count;
            /* 長さゼロの一致で文字位置が進まないまま繰り返すのを防ぐ。 */
            DATA_PUSH(&ctx->u.rep->last_ptr);
            ctx->u.rep->last_ptr = state->ptr;
            DO_JUMP(JUMP_MIN_UNTIL_3,jump_min_until_3,
                    ctx->u.rep->pattern+3);
            DATA_POP(&ctx->u.rep->last_ptr);
            if (ret) {
                RETURN_ON_ERROR(ret);
                RETURN_SUCCESS;
            }
            ctx->u.rep->count = ctx->count-1;
            state->ptr = ctx->ptr;
            RETURN_FAILURE;

        case SRE_OP_GROUPREF:
            /* 後方参照との照合。 */
            TRACE(("|%p|%p|GROUPREF %d\n", ctx->pattern,
                   ctx->ptr, ctx->pattern[0]));
            i = ctx->pattern[0];
            {
                Py_ssize_t groupref = i+i;
                if (groupref >= state->lastmark) {
                    RETURN_FAILURE;
                } else {
                    SRE_CHAR* p = (SRE_CHAR*) state->mark[groupref];
                    SRE_CHAR* e = (SRE_CHAR*) state->mark[groupref+1];
                    if (!p || !e || e < p)
                        RETURN_FAILURE;
                    while (p < e) {
                        if (ctx->ptr >= end || *ctx->ptr != *p)
                            RETURN_FAILURE;
                        p++;
                        ctx->ptr++;
                    }
                }
            }
            ctx->pattern++;
            break;

        case SRE_OP_GROUPREF_IGNORE:
            /* 後方参照との照合。 */
            TRACE(("|%p|%p|GROUPREF_IGNORE %d\n", ctx->pattern,
                   ctx->ptr, ctx->pattern[0]));
            i = ctx->pattern[0];
            {
                Py_ssize_t groupref = i+i;
                if (groupref >= state->lastmark) {
                    RETURN_FAILURE;
                } else {
                    SRE_CHAR* p = (SRE_CHAR*) state->mark[groupref];
                    SRE_CHAR* e = (SRE_CHAR*) state->mark[groupref+1];
                    if (!p || !e || e < p)
                        RETURN_FAILURE;
                    while (p < e) {
                        if (ctx->ptr >= end ||
                            sre_lower_ascii(*ctx->ptr) != sre_lower_ascii(*p))
                            RETURN_FAILURE;
                        p++;
                        ctx->ptr++;
                    }
                }
            }
            ctx->pattern++;
            break;

        case SRE_OP_GROUPREF_UNI_IGNORE:
            /* 後方参照との照合。 */
            TRACE(("|%p|%p|GROUPREF_UNI_IGNORE %d\n", ctx->pattern,
                   ctx->ptr, ctx->pattern[0]));
            i = ctx->pattern[0];
            {
                Py_ssize_t groupref = i+i;
                if (groupref >= state->lastmark) {
                    RETURN_FAILURE;
                } else {
                    SRE_CHAR* p = (SRE_CHAR*) state->mark[groupref];
                    SRE_CHAR* e = (SRE_CHAR*) state->mark[groupref+1];
                    if (!p || !e || e < p)
                        RETURN_FAILURE;
                    while (p < e) {
                        if (ctx->ptr >= end ||
                            sre_lower_unicode(*ctx->ptr) != sre_lower_unicode(*p))
                            RETURN_FAILURE;
                        p++;
                        ctx->ptr++;
                    }
                }
            }
            ctx->pattern++;
            break;

        case SRE_OP_GROUPREF_LOC_IGNORE:
            /* 後方参照との照合。 */
            TRACE(("|%p|%p|GROUPREF_LOC_IGNORE %d\n", ctx->pattern,
                   ctx->ptr, ctx->pattern[0]));
            i = ctx->pattern[0];
            {
                Py_ssize_t groupref = i+i;
                if (groupref >= state->lastmark) {
                    RETURN_FAILURE;
                } else {
                    SRE_CHAR* p = (SRE_CHAR*) state->mark[groupref];
                    SRE_CHAR* e = (SRE_CHAR*) state->mark[groupref+1];
                    if (!p || !e || e < p)
                        RETURN_FAILURE;
                    while (p < e) {
                        if (ctx->ptr >= end ||
                            sre_lower_locale(*ctx->ptr) != sre_lower_locale(*p))
                            RETURN_FAILURE;
                        p++;
                        ctx->ptr++;
                    }
                }
            }
            ctx->pattern++;
            break;

        case SRE_OP_GROUPREF_EXISTS:
            TRACE(("|%p|%p|GROUPREF_EXISTS %d\n", ctx->pattern,
                   ctx->ptr, ctx->pattern[0]));
            /* <GROUPREF_EXISTS> <group> <skip> codeyes <JUMP> codeno ... */
            i = ctx->pattern[0];
            {
                Py_ssize_t groupref = i+i;
                if (groupref >= state->lastmark) {
                    ctx->pattern += ctx->pattern[1];
                    break;
                } else {
                    SRE_CHAR* p = (SRE_CHAR*) state->mark[groupref];
                    SRE_CHAR* e = (SRE_CHAR*) state->mark[groupref+1];
                    if (!p || !e || e < p) {
                        ctx->pattern += ctx->pattern[1];
                        break;
                    }
                }
            }
            ctx->pattern += 2;
            break;

        case SRE_OP_ASSERT:
            /* 部分パターンの一致を確認する。 */
            /* <ASSERT> <skip> <back> <pattern> */
            TRACE(("|%p|%p|ASSERT %d\n", ctx->pattern,
                   ctx->ptr, ctx->pattern[1]));
            if (ctx->ptr - (SRE_CHAR *)state->beginning < (Py_ssize_t)ctx->pattern[1])
                RETURN_FAILURE;
            state->ptr = ctx->ptr - ctx->pattern[1];
            DO_JUMP0(JUMP_ASSERT, jump_assert, ctx->pattern+2);
            RETURN_ON_FAILURE(ret);
            ctx->pattern += ctx->pattern[0];
            break;

        case SRE_OP_ASSERT_NOT:
            /* 部分パターンの不一致を確認する。 */
            /* <ASSERT_NOT> <skip> <back> <pattern> */
            TRACE(("|%p|%p|ASSERT_NOT %d\n", ctx->pattern,
                   ctx->ptr, ctx->pattern[1]));
            if (ctx->ptr - (SRE_CHAR *)state->beginning >= (Py_ssize_t)ctx->pattern[1]) {
                state->ptr = ctx->ptr - ctx->pattern[1];
                DO_JUMP0(JUMP_ASSERT_NOT, jump_assert_not, ctx->pattern+2);
                if (ret) {
                    RETURN_ON_ERROR(ret);
                    RETURN_FAILURE;
                }
            }
            ctx->pattern += ctx->pattern[0];
            break;

        case SRE_OP_FAILURE:
            /* 直ちに失敗する。 */
            TRACE(("|%p|%p|FAILURE\n", ctx->pattern, ctx->ptr));
            RETURN_FAILURE;

        default:
            TRACE(("|%p|%p|UNKNOWN %d\n", ctx->pattern, ctx->ptr,
                   ctx->pattern[-1]));
            RETURN_ERROR(SRE_ERROR_ILLEGAL);
        }
    }

exit:
    ctx_pos = ctx->last_ctx_pos;
    jump = ctx->jump;
    DATA_POP_DISCARD(ctx);
    if (ctx_pos == -1)
        return ret;
    DATA_LOOKUP_AT(SRE(match_context), ctx, ctx_pos);

    switch (jump) {
        case JUMP_MAX_UNTIL_2:
            TRACE(("|%p|%p|JUMP_MAX_UNTIL_2\n", ctx->pattern, ctx->ptr));
            goto jump_max_until_2;
        case JUMP_MAX_UNTIL_3:
            TRACE(("|%p|%p|JUMP_MAX_UNTIL_3\n", ctx->pattern, ctx->ptr));
            goto jump_max_until_3;
        case JUMP_MIN_UNTIL_2:
            TRACE(("|%p|%p|JUMP_MIN_UNTIL_2\n", ctx->pattern, ctx->ptr));
            goto jump_min_until_2;
        case JUMP_MIN_UNTIL_3:
            TRACE(("|%p|%p|JUMP_MIN_UNTIL_3\n", ctx->pattern, ctx->ptr));
            goto jump_min_until_3;
        case JUMP_BRANCH:
            TRACE(("|%p|%p|JUMP_BRANCH\n", ctx->pattern, ctx->ptr));
            goto jump_branch;
        case JUMP_MAX_UNTIL_1:
            TRACE(("|%p|%p|JUMP_MAX_UNTIL_1\n", ctx->pattern, ctx->ptr));
            goto jump_max_until_1;
        case JUMP_MIN_UNTIL_1:
            TRACE(("|%p|%p|JUMP_MIN_UNTIL_1\n", ctx->pattern, ctx->ptr));
            goto jump_min_until_1;
        case JUMP_REPEAT:
            TRACE(("|%p|%p|JUMP_REPEAT\n", ctx->pattern, ctx->ptr));
            goto jump_repeat;
        case JUMP_REPEAT_ONE_1:
            TRACE(("|%p|%p|JUMP_REPEAT_ONE_1\n", ctx->pattern, ctx->ptr));
            goto jump_repeat_one_1;
        case JUMP_REPEAT_ONE_2:
            TRACE(("|%p|%p|JUMP_REPEAT_ONE_2\n", ctx->pattern, ctx->ptr));
            goto jump_repeat_one_2;
        case JUMP_MIN_REPEAT_ONE:
            TRACE(("|%p|%p|JUMP_MIN_REPEAT_ONE\n", ctx->pattern, ctx->ptr));
            goto jump_min_repeat_one;
        case JUMP_ASSERT:
            TRACE(("|%p|%p|JUMP_ASSERT\n", ctx->pattern, ctx->ptr));
            goto jump_assert;
        case JUMP_ASSERT_NOT:
            TRACE(("|%p|%p|JUMP_ASSERT_NOT\n", ctx->pattern, ctx->ptr));
            goto jump_assert_not;
        case JUMP_NONE:
            TRACE(("|%p|%p|RETURN %" PY_FORMAT_SIZE_T "d\n", ctx->pattern,
                   ctx->ptr, ret));
            break;
    }

    return ret; /* ここには到達しないはず。 */
}

/* ループ内で SRE(match) を再度呼ぶ前に、捕捉グループを初期化する必要がある。 */
#define RESET_CAPTURE_GROUP() \
    do { state->lastmark = state->lastindex = -1; } while (0)


#undef DATA_ALLOC
#undef DATA_LOOKUP_AT
#undef DATA_POP
#undef DATA_POP_DISCARD
#undef DATA_PUSH
#undef DATA_STACK_ALLOC
#undef DATA_STACK_LOOKUP_AT
#undef DATA_STACK_POP
#undef DATA_STACK_POP_DISCARD
#undef DATA_STACK_PUSH
#undef DO_JUMP
#undef DO_JUMP0
#undef DO_JUMPX
#undef JUMP_ASSERT
#undef JUMP_ASSERT_NOT
#undef JUMP_BRANCH
#undef JUMP_MAX_UNTIL_1
#undef JUMP_MAX_UNTIL_2
#undef JUMP_MAX_UNTIL_3
#undef JUMP_MIN_REPEAT_ONE
#undef JUMP_MIN_UNTIL_1
#undef JUMP_MIN_UNTIL_2
#undef JUMP_MIN_UNTIL_3
#undef JUMP_NONE
#undef JUMP_REPEAT
#undef JUMP_REPEAT_ONE_1
#undef JUMP_REPEAT_ONE_2
#undef LASTMARK_RESTORE
#undef LASTMARK_SAVE
#undef LOCAL
#undef MARK_POP
#undef MARK_POP_DISCARD
#undef MARK_POP_KEEP
#undef MARK_PUSH
#undef PyErr_CheckSignals
#undef PyErr_NoMemory
#undef PyObject_FREE
#undef PyObject_MALLOC
#undef RESET_CAPTURE_GROUP
#undef RETURN_ERROR
#undef RETURN_FAILURE
#undef RETURN_ON_ERROR
#undef RETURN_ON_FAILURE
#undef RETURN_ON_SUCCESS
#undef RETURN_SUCCESS
#undef SIZEOF_SRE_CHAR
#undef SRE
#undef SRE_IS_LINEBREAK
#undef SRE_IS_WORD
#undef SRE_LOC_IS_WORD
#undef SRE_UNI_IS_WORD
#undef TRACE

// AST のコンパイルは sre_compile._compile に従う。同等の
// 基本文字集合を使い、CPython の任意のビットマップ・接頭辞最適化は省く。
// 照合 VM、ジャンプ配置、印のスタック、単純な繰り返しの選択は元の方針を保つ。
class Compiler{
    std::vector<SRE_CODE> result;
    void emit(SRE_CODE value){result.push_back(value);}
    SRE_CODE category_code(char c,unsigned flags){
        const char base=c>='A'&&c<='Z'?c+32:c;const bool negative=c!=base;
        SRE_CODE value=base=='d'?SRE_CATEGORY_DIGIT:base=='s'?SRE_CATEGORY_SPACE:SRE_CATEGORY_WORD;
        if(!(flags&Ascii))value=base=='d'?SRE_CATEGORY_UNI_DIGIT:base=='s'?SRE_CATEGORY_UNI_SPACE:SRE_CATEGORY_UNI_WORD;
        return value+negative;
    }
    void charset(const std::vector<Item>& items,bool negative,unsigned flags){
        emit((flags&Ignore)?((flags&Ascii)?SRE_OP_IN_IGNORE:SRE_OP_IN_UNI_IGNORE):SRE_OP_IN);
        const auto skip=result.size();emit(0);if(negative)emit(SRE_OP_NEGATE);
        for(auto item:items){
            if(item.category){emit(SRE_OP_CATEGORY);emit(category_code(item.category,flags));continue;}
            if(!(flags&Ignore)){if(item.first==item.last){emit(SRE_OP_LITERAL);emit(item.first);}else{emit(SRE_OP_RANGE);emit(item.first);emit(item.last);}continue;}
            if(item.first==item.last){const auto cp=lower(item.first,flags&Ascii);emit(SRE_OP_LITERAL);emit(cp);if(!(flags&Ascii))for(auto pair:unicode_data::extra_cases)if(pair.from==cp){emit(SRE_OP_LITERAL);emit(pair.to);}continue;}
            emit(SRE_OP_RANGE);emit(item.first);emit(item.last);
            if(flags&Ascii){for(unsigned cp='A';cp<='Z';++cp)if(item.first<=cp&&cp<=item.last){emit(SRE_OP_LITERAL);emit(cp+32);}}
            else{
                for(auto pair:unicode_data::lower)if(item.first<=pair.from&&pair.from<=item.last){emit(SRE_OP_LITERAL);emit(pair.to);for(auto fix:unicode_data::extra_cases)if(fix.from==pair.to){emit(SRE_OP_LITERAL);emit(fix.to);}}
                for(auto fix:unicode_data::extra_cases)if(item.first<=fix.from&&fix.from<=item.last){emit(SRE_OP_LITERAL);emit(fix.to);}
            }
        }
        emit(SRE_OP_FAILURE);result[skip]=static_cast<SRE_CODE>(result.size()-skip);
    }
    static bool simple(Ptr p){
        while((p->kind==Kind::Group&&p->value==0&&!p->add&&!p->remove)||(p->kind==Kind::Sequence&&p->children.size()==1))p=p->children[0];
        return p->kind==Kind::Literal||p->kind==Kind::Dot||p->kind==Kind::Class;
    }
    void compile(const Ptr& p,unsigned flags){
        switch(p->kind){
        case Kind::Sequence:for(auto&child:p->children)compile(child,flags);break;
        case Kind::Choice:{emit(SRE_OP_BRANCH);std::vector<std::size_t> tails;for(auto& child:p->children){auto skip=result.size();emit(0);compile(child,flags);emit(SRE_OP_JUMP);tails.push_back(result.size());emit(0);result[skip]=static_cast<SRE_CODE>(result.size()-skip);}emit(SRE_OP_FAILURE);for(auto at:tails)result[at]=static_cast<SRE_CODE>(result.size()-at);break;}
        case Kind::Literal:{
            const auto cp=lower(p->value,flags&Ascii);bool extra=false;if((flags&Ignore)&&!(flags&Ascii))for(auto pair:unicode_data::extra_cases)if(pair.from==cp)extra=true;
            if(extra)charset({{p->value,p->value,0}},false,flags);
            else{emit(!(flags&Ignore)?SRE_OP_LITERAL:(flags&Ascii)?SRE_OP_LITERAL_IGNORE:SRE_OP_LITERAL_UNI_IGNORE);emit((flags&Ignore)?cp:p->value);}break;}
        case Kind::Class:charset(p->items,p->negative,flags);break;
        case Kind::Dot:emit((flags&Dotall)?SRE_OP_ANY_ALL:SRE_OP_ANY);break;
        case Kind::Group:{auto local=(flags|p->add)&~p->remove;if(p->add&Ascii)local&=~Unicode;if(p->add&Unicode)local&=~Ascii;if(p->value){emit(SRE_OP_MARK);emit((p->value-1)*2);}compile(p->children[0],local);if(p->value){emit(SRE_OP_MARK);emit((p->value-1)*2+1);}break;}
        case Kind::Repeat:{const bool one=simple(p->children[0]);emit(one?(p->lazy?SRE_OP_MIN_REPEAT_ONE:SRE_OP_REPEAT_ONE):SRE_OP_REPEAT);auto skip=result.size();emit(0);emit(static_cast<SRE_CODE>(p->minimum));emit(p->maximum==infinity?SRE_MAXREPEAT:static_cast<SRE_CODE>(p->maximum));compile(p->children[0],flags);if(one)emit(SRE_OP_SUCCESS);result[skip]=static_cast<SRE_CODE>(result.size()-skip);if(!one)emit(p->lazy?SRE_OP_MIN_UNTIL:SRE_OP_MAX_UNTIL);break;}
        case Kind::Ref:emit(!(flags&Ignore)?SRE_OP_GROUPREF:(flags&Ascii)?SRE_OP_GROUPREF_IGNORE:SRE_OP_GROUPREF_UNI_IGNORE);emit(p->value-1);break;
        case Kind::Assert:{emit(p->negative?SRE_OP_ASSERT_NOT:SRE_OP_ASSERT);auto skip=result.size();emit(0);emit(p->behind?static_cast<SRE_CODE>(p->behind_width):0);compile(p->children[0],flags);emit(SRE_OP_SUCCESS);result[skip]=static_cast<SRE_CODE>(result.size()-skip);break;}
        case Kind::Conditional:{emit(SRE_OP_GROUPREF_EXISTS);emit(p->value-1);auto yes=result.size();emit(0);compile(p->children[0],flags);emit(SRE_OP_JUMP);auto no=result.size();emit(0);result[yes]=static_cast<SRE_CODE>(result.size()-yes+1);compile(p->children[1],flags);result[no]=static_cast<SRE_CODE>(result.size()-no);break;}
        case Kind::Anchor:{SRE_CODE at=0;switch(p->value){case'A':at=SRE_AT_BEGINNING_STRING;break;case'Z':at=SRE_AT_END_STRING;break;case'^':at=(flags&Multiline)?SRE_AT_BEGINNING_LINE:SRE_AT_BEGINNING;break;case'$':at=(flags&Multiline)?SRE_AT_END_LINE:SRE_AT_END;break;case'b':at=(flags&Ascii)?SRE_AT_BOUNDARY:SRE_AT_UNI_BOUNDARY;break;case'B':at=(flags&Ascii)?SRE_AT_NON_BOUNDARY:SRE_AT_UNI_NON_BOUNDARY;break;}emit(SRE_OP_AT);emit(at);break;}
        }
    }
public:
    std::vector<SRE_CODE> operator()(const Parser& parser){compile(parser.root,parser.global_flags);emit(SRE_OP_SUCCESS);return std::move(result);}
};
inline bool prefix_match(const std::vector<SRE_CODE>& code,std::size_t groups,const String& subject){
    auto text=::cw::lib::regex_grapheme::decode_utf8(subject);const auto size=text.size();text.push_back(0);
    std::vector<void*> marks(groups*2,nullptr);SRE_STATE state;state.beginning=state.start=state.ptr=text.data();state.end=text.data()+size;state.mark=marks.data();
    const auto status=sre_vm_match(&state,code.data(),1);
    if(status<0)throw std::runtime_error("SRE matching state failure");return status!=0;
}
} // namespace vm
} // namespace detail
class Pattern {
    std::size_t group_count{};
    std::vector<detail::vm::SRE_CODE> code;
public:
    explicit Pattern(const String& pattern){
        detail::Parser parsed(pattern);group_count=parsed.group_count;
        code=detail::vm::Compiler{}(parsed);
    }
    [[nodiscard]] bool match(const String& subject) const {
        return detail::vm::prefix_match(code,group_count,subject);
    }
};
} // namespace cw::lib::python_re
